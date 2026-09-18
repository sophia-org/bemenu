#include "connection_internal.h"

static bool same_id(struct sophia_shell_content_id a, struct sophia_shell_content_id b)
{
    return a.id == b.id && a.generation == b.generation;
}
static const struct sophia_shell_output_fact *output_fact(const struct bm_sophia_connection *c,
                                                        struct sophia_shell_content_id output)
{
    for (unsigned i = 0; i < c->facts.count; ++i)
        if (same_id(c->facts.outputs[i].output, output)) return &c->facts.outputs[i];
    return NULL;
}
static bool same_fact(const struct sophia_shell_output_fact *a, const struct sophia_shell_output_fact *b)
{
    return a && b && same_id(a->output,b->output) && a->local_width == b->local_width &&
        a->local_height == b->local_height && a->scale_numerator == b->scale_numerator &&
        a->scale_denominator == b->scale_denominator && a->scale_generation == b->scale_generation;
}
bool bm_sophia_allocation_current(const struct bm_sophia_connection *c,
                                  const struct sophia_shell_native_lifecycle_snapshot *s)
{
    return c->allocation_valid && s->open && !s->closing && c->allocation_opening == s->opening.opening &&
        same_fact(&c->allocation_fact, output_fact(c, c->allocation.output));
}
static uint32_t smaller(uint32_t a, uint32_t b) {return a < b ? a : b;}

int bm_sophia_allocation_service(struct bm_sophia_connection *c,
                                 const struct sophia_shell_native_lifecycle_snapshot *s)
{
    if (!s->open || s->closing || !c->facts.generation || c->allocation_pending || c->allocation_valid)
        return SOPHIA_SHELL_AGAIN;
    struct sophia_shell_content_id output = {s->opening.output.id,s->opening.output.generation};
    const struct sophia_shell_output_fact *f = output_fact(c,output);
    if (!f || (c->allocation_attempt_opening == s->opening.opening && c->allocation_attempt_facts == c->facts.generation))
        return SOPHIA_SHELL_AGAIN;
    if (!c->next_transaction || c->next_transaction == UINT64_MAX || c->allocation_counter == UINT64_MAX ||
        f->scale_numerator < f->scale_denominator || f->scale_numerator > 4*f->scale_denominator)
        return SOPHIA_SHELL_INVALID;
    uint32_t max_width = smaller(c->limits.max_width_px,c->limits.max_popout_extent_px);
    uint32_t max_height = smaller(c->limits.max_height_px,c->limits.max_popout_extent_px);
    /* A fractional allocation origin can add one pixel beyond ceil(extent).
     * Reserve that endpoint slack before requesting geometry or budgeting bytes. */
    unsigned slack = f->scale_denominator > 1;
    if (max_width <= slack || max_height <= slack) return SOPHIA_SHELL_INVALID;
    max_width -= slack; max_height -= slack;
    uint32_t width = smaller(640, smaller(f->local_width,max_width*f->scale_denominator/f->scale_numerator));
    uint32_t height = smaller(320, smaller(f->local_height,max_height*f->scale_denominator/f->scale_numerator));
    if (!width) return SOPHIA_SHELL_INVALID;
    uint64_t pixel_width = ((uint64_t)width*f->scale_numerator+f->scale_denominator-1)/f->scale_denominator + slack;
    uint64_t byte_height = c->limits.max_resource_bytes/(4*pixel_width);
    if (byte_height <= slack) return SOPHIA_SHELL_INVALID;
    byte_height -= slack;
    if (height > byte_height*f->scale_denominator/f->scale_numerator)
        height = byte_height*f->scale_denominator/f->scale_numerator;
    uint64_t area = (uint64_t)f->local_width*f->local_height;
    area = (area/100)*c->limits.max_content_coverage_percent +
        (area%100)*c->limits.max_content_coverage_percent/100;
    if (height > area/width) height = area/width;
    if (height < 64) return SOPHIA_SHELL_INVALID;
    struct sophia_shell_native_allocation request = {
        .grant = {c->limits.grant.connection_epoch,c->limits.grant.content_grant_epoch},
        .opening = s->opening.opening, .output = s->opening.output,
        .request_id = c->allocation_counter+1, .operation = 1, .edge = 1,
        .desired_width = width, .desired_height = height,
    };
    uint8_t bytes[108]; size_t length;
    int r = sophia_shell_native_allocation_encode(bytes,sizeof(bytes),c->next_transaction,&request,&length);
    if (r != SOPHIA_SHELL_OK) return r;
    struct sophia_shell_outbound_frame frame = {bytes,length,SOPHIA_SHELL_OUTBOUND_CONTROL};
    r = sophia_shell_outbox_push(&c->outbox,&frame,1);
    if (r != SOPHIA_SHELL_OK) return r;
    c->allocation_request = request; c->allocation_fact = *f;
    c->allocation_transaction = c->next_transaction++; ++c->allocation_counter;
    c->allocation_attempt_opening = s->opening.opening; c->allocation_attempt_facts = c->facts.generation;
    c->allocation_pending = true;
    return SOPHIA_SHELL_OK;
}

static bool geometry(const struct bm_sophia_connection *c, const struct sophia_shell_allocation_result *a)
{
    const struct sophia_shell_output_fact *f = &c->allocation_fact;
    if (a->parent.id || a->parent.generation || a->allowed_reservation_extent ||
        a->scale_generation != f->scale_generation || a->scale_numerator != f->scale_numerator ||
        a->scale_denominator != f->scale_denominator || a->logical.x < 0 || a->logical.y < 0 ||
        (uint64_t)a->logical.x+a->logical.width > f->local_width ||
        (uint64_t)a->logical.y+a->logical.height > f->local_height || a->logical.height < 64 ||
        a->pixel.width > c->limits.max_width_px || a->pixel.height > c->limits.max_height_px ||
        a->pixel.width > c->limits.max_popout_extent_px || a->pixel.height > c->limits.max_popout_extent_px ||
        (uint64_t)a->pixel.width*a->pixel.height*4 > c->limits.max_resource_bytes)
        return false;
    if (a->acknowledged_anchor.x || a->acknowledged_anchor.y || a->acknowledged_anchor.width || a->acknowledged_anchor.height)
        return false;
    for (unsigned i = 0; i < 4; ++i) if (a->margins[i]) return false;
    uint64_t x = (uint64_t)a->logical.x*f->scale_numerator/f->scale_denominator;
    uint64_t y = (uint64_t)a->logical.y*f->scale_numerator/f->scale_denominator;
    uint64_t right = (((uint64_t)a->logical.x+a->logical.width)*f->scale_numerator+f->scale_denominator-1)/f->scale_denominator;
    uint64_t bottom = (((uint64_t)a->logical.y+a->logical.height)*f->scale_numerator+f->scale_denominator-1)/f->scale_denominator;
    return a->pixel.x >= 0 && a->pixel.y >= 0 && (uint64_t)a->pixel.x == x && (uint64_t)a->pixel.y == y &&
        a->pixel.width == right-x && a->pixel.height == bottom-y;
}
int bm_sophia_allocation_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *f)
{
    struct sophia_shell_content_feedback feedback;
    int r = sophia_shell_content_feedback_decode(f,&feedback);
    if (r != SOPHIA_SHELL_OK) return r;
    const struct sophia_shell_allocation_result *a = &feedback.value.allocation;
    if (feedback.grant.connection_epoch != c->limits.grant.connection_epoch ||
        feedback.grant.content_grant_epoch != c->limits.grant.content_grant_epoch) return SOPHIA_SHELL_INVALID;
    if (a->status == 4) {
        if (!c->allocation_valid || !same_id(a->allocation,c->allocation.allocation) || !same_id(a->output,c->allocation.output))
            return SOPHIA_SHELL_INVALID;
        c->allocation_valid = false;
        bm_sophia_raster_free(c->raster); c->raster = NULL;
        return SOPHIA_SHELL_OK;
    }
    if (!c->allocation_pending || f->transaction != c->allocation_transaction ||
        a->request_id != c->allocation_request.request_id ||
        a->output.id != c->allocation_request.output.id || a->output.generation != c->allocation_request.output.generation ||
        (a->status != 1 && a->status != 2)) return SOPHIA_SHELL_INVALID;
    if (a->status == 1) {
        if (!geometry(c,a)) return SOPHIA_SHELL_INVALID;
        struct bm_sophia_raster *raster = bm_sophia_raster_new(a->pixel.width,a->pixel.height,
            (double)a->scale_numerator/a->scale_denominator);
        if (!raster) return SOPHIA_SHELL_BUSY;
        c->raster = raster; c->allocation = *a;
        c->allocation_opening = c->allocation_request.opening; c->allocation_valid = true;
    }
    c->allocation_pending = false;
    return SOPHIA_SHELL_OK;
}
