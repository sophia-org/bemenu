#include "internal.h"
#include "files_internal.h"
#include <stdlib.h>
#include <string.h>

struct bm_sophia_files_view *
bm_sophia_files_view_find(struct bm_sophia_files *f, uint64_t id)
{
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i)
        if (f->views[i].state != BM_SOPHIA_FILES_VIEW_FREE && f->views[i].id == id)
            return &f->views[i];
    return NULL;
}

/* Chunk storage is borrowed until the session is ready again or no longer
 * uploading; only then does the owned copy go. */
void
bm_sophia_files_release_copy(struct bm_sophia_files *f)
{
    if (!f->copy || (sophia_ss_upload_pending(&f->ss) && !sophia_ss_upload_ready(&f->ss)))
        return;
    free(f->copy);
    f->copy = NULL;
    f->copy_bytes = f->copy_sent = f->chunk_bytes = 0;
}

static bool
refused(const struct bm_sophia_files *f, uint64_t ticket)
{
    enum sophia_ss_outcome o;
    if (!ticket || sophia_ss_outcome(&f->ss, ticket, &o, NULL))
        return false;
    return o == SOPHIA_SS_REFUSED || o == SOPHIA_SS_DROPPED_UNSENT;
}

/* A refused submission of ours is never a wait: nothing was journaled, and
 * this client's bookkeeping no longer matches the lifecycle. */
int
bm_sophia_files_settle(struct bm_sophia_files *f)
{
    if (refused(f, f->allocation_ticket))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_ALLOCATION, SOPHIA_9P_INVALID);
    if ((f->demand && refused(f, f->demand_ticket)) || (f->pending >= 0 && refused(f, f->present_ticket)))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_FRAME, SOPHIA_9P_INVALID);
    if (f->activation && refused(f, f->activation_ticket))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_INPUT, SOPHIA_9P_INVALID);
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i) {
        const struct bm_sophia_files_view *v = &f->views[i];
        if ((v->state == BM_SOPHIA_FILES_VIEW_BEGUN || v->state == BM_SOPHIA_FILES_VIEW_ENDED ||
             v->state == BM_SOPHIA_FILES_VIEW_RETIRING) && refused(f, v->ticket))
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, SOPHIA_9P_INVALID);
    }
    return 0;
}

static const struct sophia_sf_content_output_facts_entry *
output_fact(const struct bm_sophia_files *f, uint64_t id, uint64_t generation)
{
    for (unsigned i = 0; i < f->outputs.output_count && i < 16; ++i)
        if (f->outputs.outputs[i].output_id == id && f->outputs.outputs[i].output_generation == generation)
            return &f->outputs.outputs[i];
    return NULL;
}

static uint32_t smaller(uint32_t a, uint32_t b) {return a < b ? a : b;}

/* Bemenu's existing popout policy: at most 640x320 logical, within the output,
 * content coverage, the resource byte budget and fractional endpoint slack. */
static int
allocation_size(const struct sophia_sf_limits *l, const struct sophia_sf_content_output_facts_entry *fact,
                struct sophia_ns_allocation_params *out)
{
    if (!fact->scale_denominator || fact->scale_numerator < fact->scale_denominator ||
        fact->scale_numerator > 4 * fact->scale_denominator)
        return SOPHIA_9P_INVALID;
    uint32_t max_width = smaller(l->max_width_px, l->max_popout_extent_px);
    uint32_t max_height = smaller(l->max_height_px, l->max_popout_extent_px);
    unsigned slack = fact->scale_denominator > 1;
    if (max_width <= slack || max_height <= slack)
        return SOPHIA_9P_INVALID;
    max_width -= slack;
    max_height -= slack;
    uint32_t width = smaller(640, smaller(fact->local_width, (uint64_t)max_width * fact->scale_denominator / fact->scale_numerator));
    uint32_t height = smaller(320, smaller(fact->local_height, (uint64_t)max_height * fact->scale_denominator / fact->scale_numerator));
    if (!width)
        return SOPHIA_9P_INVALID;
    uint64_t pixel_width = ((uint64_t)width * fact->scale_numerator + fact->scale_denominator - 1) / fact->scale_denominator + slack;
    uint64_t byte_height = l->max_resource_bytes / (4 * pixel_width);
    if (byte_height <= slack)
        return SOPHIA_9P_INVALID;
    byte_height -= slack;
    if (height > byte_height * fact->scale_denominator / fact->scale_numerator)
        height = byte_height * fact->scale_denominator / fact->scale_numerator;
    uint64_t area = (uint64_t)fact->local_width * fact->local_height;
    area = (area / 100) * l->max_content_coverage_percent + (area % 100) * l->max_content_coverage_percent / 100;
    if (height > area / width)
        height = area / width;
    if (height < 64)
        return SOPHIA_9P_INVALID;
    *out = (struct sophia_ns_allocation_params){1, width, height, 0, 0, 0, 0};
    return 0;
}

static int
allocate(struct bm_sophia_files *f, const struct sophia_ns_obligations *o)
{
    const struct sophia_sf_native_opening *open = sophia_ns_opening(&f->ns);
    if (!open || sophia_ns_allocation(&f->ns) || o->allocation_due_ms || o->awaiting_invalidation ||
        o->needs_facts || !f->outputs.facts_generation ||
        (f->allocation_opening == open->opening && f->allocation_facts == f->outputs.facts_generation))
        return 0;
    const struct sophia_sf_content_output_facts_entry *fact = output_fact(f, open->output_id, open->output_generation);
    if (!fact)
        return 0; /* Awaiting facts that name the opening's output. */
    struct sophia_ns_allocation_params params;
    uint64_t ticket;
    int r = allocation_size(sophia_ss_limits(&f->ss), fact, &params);
    if (!r)
        r = sophia_ns_allocate(&f->ns, &params, f->now, &ticket);
    if (r == SOPHIA_9P_BUSY)
        return 0;
    if (r)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_ALLOCATION, r);
    /* One request per (opening, facts): a rejection is not retried blindly. */
    f->allocation_opening = open->opening;
    f->allocation_facts = f->outputs.facts_generation;
    f->allocation_ticket = ticket;
    f->allocation_fact = *fact;
    return 0;
}

static bool
geometry(const struct bm_sophia_files *f, const struct sophia_sf_allocation_result *a)
{
    const struct sophia_sf_content_output_facts_entry *fact = &f->allocation_fact;
    const struct sophia_sf_limits *l = sophia_ss_limits(&f->ss);
    if (!l || a->output_id != fact->output_id || a->output_generation != fact->output_generation ||
        a->parent_id || a->parent_generation || a->allowed_reservation_extent ||
        a->scale_generation != fact->scale_generation || a->scale_numerator != fact->scale_numerator ||
        a->scale_denominator != fact->scale_denominator || a->logical_x < 0 || a->logical_y < 0 ||
        (uint64_t)a->logical_x + a->logical_width > fact->local_width ||
        (uint64_t)a->logical_y + a->logical_height > fact->local_height || a->logical_height < 64 ||
        a->pixel_width > l->max_width_px || a->pixel_height > l->max_height_px ||
        a->pixel_width > l->max_popout_extent_px || a->pixel_height > l->max_popout_extent_px ||
        (uint64_t)a->pixel_width * a->pixel_height * 4 > l->max_resource_bytes)
        return false;
    if (a->acknowledged_anchor_x || a->acknowledged_anchor_y || a->acknowledged_anchor_width ||
        a->acknowledged_anchor_height || a->margin_top || a->margin_right || a->margin_bottom || a->margin_left)
        return false;
    uint64_t n = fact->scale_numerator, d = fact->scale_denominator;
    uint64_t x = (uint64_t)a->logical_x * n / d, y = (uint64_t)a->logical_y * n / d;
    uint64_t right = (((uint64_t)a->logical_x + a->logical_width) * n + d - 1) / d;
    uint64_t bottom = (((uint64_t)a->logical_y + a->logical_height) * n + d - 1) / d;
    return a->pixel_x >= 0 && a->pixel_y >= 0 && (uint64_t)a->pixel_x == x && (uint64_t)a->pixel_y == y &&
        a->pixel_width == right - x && a->pixel_height == bottom - y;
}

int
bm_sophia_files_granted(struct bm_sophia_files *f, const struct sophia_sf_allocation_result *a)
{
    const struct sophia_sf_allocation_result *g = sophia_ns_allocation(&f->ns);
    /* A grant for a closed opening is never used; it awaits invalidation. */
    if (!g || g->allocation_id != a->allocation_id || g->allocation_generation != a->allocation_generation)
        return 0;
    if (!geometry(f, a))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_ALLOCATION, SOPHIA_9P_INVALID);
    struct bm_sophia_raster *raster = bm_sophia_raster_new(a->pixel_width, a->pixel_height,
        (double)a->scale_numerator / a->scale_denominator);
    if (!raster)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_RENDER, SOPHIA_9P_INVALID);
    bm_sophia_raster_free(f->raster);
    f->raster = raster;
    f->raster_id = a->allocation_id;
    f->raster_generation = a->allocation_generation;
    f->raster_numerator = a->scale_numerator;
    f->raster_denominator = a->scale_denominator;
    return 0;
}

static bool
current(const struct bm_sophia_files *f, const struct bm_sophia_files_view *v)
{
    const struct sophia_sf_native_opening *o = sophia_ns_opening(&f->ns);
    const struct sophia_sf_allocation_result *a = sophia_ns_allocation(&f->ns);
    return o && a && v->opening == o->opening && v->revision == sophia_ns_revision(&f->ns) &&
        v->catalog_generation == o->catalog_generation && v->catalog_generation == f->model.generation &&
        v->facts_generation == f->outputs.facts_generation && v->allocation_id == a->allocation_id &&
        v->allocation_generation == a->allocation_generation;
}

static uint32_t
divisor(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Paint the current menu once through the unchanged view/raster path and
 * keep one immutable copy: later paints never alter bytes being uploaded. */
static int
stage(struct bm_sophia_files *f, const struct sophia_ns_obligations *o)
{
    const struct sophia_sf_native_opening *open = sophia_ns_opening(&f->ns);
    const struct sophia_sf_allocation_result *a = sophia_ns_allocation(&f->ns);
    const struct sophia_sf_limits *l = sophia_ss_limits(&f->ss);
    if (f->upload >= 0 || f->held || !o->needs_candidate || !open || !a || !l || !f->raster ||
        f->raster_id != a->allocation_id || f->raster_generation != a->allocation_generation ||
        f->model.generation != open->catalog_generation)
        return 0;
    bm_sophia_files_release_copy(f);
    if (f->copy || sophia_ss_upload_pending(&f->ss))
        return 0;
    int slot = -1;
    unsigned live = 0;
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i) {
        const struct bm_sophia_files_view *v = &f->views[i];
        if (v->state == BM_SOPHIA_FILES_VIEW_FREE) {
            if (slot < 0)
                slot = (int)i;
            continue;
        }
        ++live;
        if (v->state != BM_SOPHIA_FILES_VIEW_RETIRING && current(f, v))
            return 0;
    }
    if (slot < 0 || live >= l->max_live_resources || f->next_resource == UINT64_MAX)
        return 0;
    struct bm_sophia_view view;
    if (!bm_sophia_view_capture(f->raster, &f->model, &view))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_RENDER, SOPHIA_9P_INVALID);
    size_t row = (size_t)view.width * 4, bytes = row * view.height;
    uint32_t payload = l->max_frame_payload > 48 ? l->max_frame_payload - 48 : 0;
    size_t rows = smaller(payload, l->max_chunk_bytes) / row;
    if (view.stride != row || view.row_count > l->max_candidate_targets || view.row_count > SOPHIA_NS_ROWS ||
        bytes > l->max_resource_bytes || !rows)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_RENDER, SOPHIA_9P_INVALID);
    f->copy = malloc(bytes);
    if (!f->copy)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_RENDER, SOPHIA_9P_BUSY);
    memcpy(f->copy, view.data, bytes);
    view.data = NULL;
    f->copy_bytes = bytes;
    f->copy_sent = 0;
    f->chunk_bytes = rows * row;
    f->displayed = view.row_count + 1; /* Upstream count includes the filter row. */
    uint32_t common = divisor(f->raster_numerator, f->raster_denominator);
    f->views[slot] = (struct bm_sophia_files_view){
        BM_SOPHIA_FILES_VIEW_STAGED, f->next_resource++, 0, open->opening, sophia_ns_revision(&f->ns),
        f->model.generation, f->outputs.facts_generation, a->allocation_id, a->allocation_generation,
        f->raster_numerator / common, f->raster_denominator / common, view,
    };
    f->upload = slot;
    return 0;
}

/* Begin, canonical chunks, End. A chunk's short writes are the session's:
 * it advances by each positive Rwrite count and is ready again only once the
 * whole chunk is written. A failed transfer ends in the resource's status. */
static int
upload(struct bm_sophia_files *f)
{
    if (f->upload < 0)
        return 0;
    struct bm_sophia_files_view *v = &f->views[f->upload];
    uint64_t ticket;
    int r;
    if (v->state == BM_SOPHIA_FILES_VIEW_STAGED) {
        struct sophia_sf_resource_begin begin = {
            .resource_id = v->id, .resource_generation = 1, .width_px = v->view.width,
            .height_px = v->view.height, .rendered_scale_numerator = v->scale_numerator,
            .rendered_scale_denominator = v->scale_denominator, .pixel_format = 1,
            .chunk_count = (uint32_t)((f->copy_bytes + f->chunk_bytes - 1) / f->chunk_bytes),
            .total_bytes = f->copy_bytes,
        };
        r = sophia_ns_upload_begin(&f->ns, begin, &ticket);
        if (r == SOPHIA_9P_BUSY)
            return 0;
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, r);
        v->ticket = ticket;
        v->state = BM_SOPHIA_FILES_VIEW_BEGUN;
        return 0;
    }
    if (v->state != BM_SOPHIA_FILES_VIEW_SENDING || !sophia_ss_upload_ready(&f->ss))
        return 0;
    if (f->copy_sent < f->copy_bytes) {
        size_t n = f->copy_bytes - f->copy_sent;
        if (n > f->chunk_bytes)
            n = f->chunk_bytes;
        r = sophia_ss_upload_chunk(&f->ss, f->copy + f->copy_sent, n);
        if (r == SOPHIA_9P_BUSY)
            return 0;
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, r);
        f->copy_sent += n;
        return 0;
    }
    /* Ready with every byte written: no chunk is borrowed any more. */
    bm_sophia_files_release_copy(f);
    r = sophia_ns_upload_end(&f->ns, &ticket);
    if (r == SOPHIA_9P_BUSY)
        return 0;
    if (r)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, r);
    v->ticket = ticket;
    v->state = BM_SOPHIA_FILES_VIEW_ENDED;
    return 0;
}

/* One retirement per pass: accepted, neither outstanding nor still shown,
 * and no longer the current scene. Released frees the local entry. */
static int
retire(struct bm_sophia_files *f)
{
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i) {
        struct bm_sophia_files_view *v = &f->views[i];
        if (v->state != BM_SOPHIA_FILES_VIEW_ACCEPTED || (int)i == f->pending ||
            ((int)i == f->shown && sophia_ns_presented(&f->ns)) || current(f, v))
            continue;
        uint64_t ticket;
        int r = sophia_ns_retire(&f->ns, v->id, 1, &ticket);
        if (r == SOPHIA_9P_BUSY)
            return 0;
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, r);
        v->ticket = ticket;
        v->state = BM_SOPHIA_FILES_VIEW_RETIRING;
        if ((int)i == f->shown)
            f->shown = -1;
        return 0;
    }
    return 0;
}

/* Demand only when no demand, permit or candidate of ours is outstanding;
 * present only inside the permit's advisory bound. An INVALID from either is
 * then bad local state, not a wait. */
static int
frame(struct bm_sophia_files *f, const struct sophia_ns_obligations *o)
{
    int view = -1;
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS && view < 0; ++i)
        if (f->views[i].state == BM_SOPHIA_FILES_VIEW_ACCEPTED && current(f, &f->views[i]))
            view = (int)i;
    if (!o->needs_candidate || view < 0 || f->pending >= 0)
        return 0;
    uint64_t ticket;
    int r;
    if (!f->demand) {
        r = sophia_ns_demand(&f->ns, 1, f->now, &ticket);
        if (r == SOPHIA_9P_BUSY)
            return 0;
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_FRAME, r);
        f->demand = true;
        f->permit = false;
        f->demand_ticket = ticket;
        return 0;
    }
    uint64_t expires = o->permit_expires_ms;
    if (!f->permit || !expires || f->now >= expires || expires - f->now <= BM_SOPHIA_FILES_MARGIN_MS)
        return 0; /* Awaiting its grant, or the server's end of a skipped permit. */
    const struct bm_sophia_files_view *v = &f->views[view];
    if (f->target_generation == UINT64_MAX)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_FRAME, SOPHIA_9P_INVALID);
    struct sophia_ns_scene scene;
    memset(&scene, 0, sizeof(scene));
    scene.placement_count = 1;
    scene.placements[0] = (struct sophia_sf_content_placement){v->id, 1, 0, 0, 0};
    scene.row_count = (uint16_t)v->view.row_count;
    scene.selected = v->view.selected;
    for (unsigned i = 0; i < v->view.row_count; ++i) {
        const struct bm_sophia_view_row *row = &v->view.rows[i];
        scene.rows[i] = row->slot;
        scene.targets[i] = (struct sophia_sf_content_target){0, 0, i + 1, f->target_generation + 1, 0,
            (int32_t)row->x, (int32_t)row->y, row->width, row->height};
    }
    r = sophia_ns_present(&f->ns, &scene, f->now, &ticket);
    if (r == SOPHIA_9P_BUSY)
        return 0;
    if (r)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_FRAME, r);
    ++f->target_generation;
    f->pending = view;
    f->present_ticket = ticket;
    f->demand = f->permit = false; /* The candidate consumed the permit. */
    return 0;
}

int
bm_sophia_files_schedule(struct bm_sophia_files *f)
{
    struct sophia_ns_obligations o;
    if (sophia_ns_obligations(&f->ns, &o))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, SOPHIA_9P_INVALID);
    const struct sophia_sf_allocation_result *a = sophia_ns_allocation(&f->ns);
    if (f->raster && (!a || a->allocation_id != f->raster_id || a->allocation_generation != f->raster_generation)) {
        /* Stop using an allocation at close, before its invalidation. */
        bm_sophia_raster_free(f->raster);
        f->raster = NULL;
        f->raster_id = f->raster_generation = 0;
    }
    int r;
    if ((r = retire(f)) || (r = allocate(f, &o)) || (r = stage(f, &o)) || (r = upload(f)) ||
        (r = frame(f, &o)))
        return r;
    return f->terminal;
}
