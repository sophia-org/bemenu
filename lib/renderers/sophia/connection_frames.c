#include "connection_internal.h"

bool bm_sophia_view_current(const struct bm_sophia_connection *c, unsigned slot,
                            const struct sophia_shell_native_lifecycle_snapshot *s)
{
    const struct bm_sophia_uploaded_view *v = &c->views[slot];
    return v->valid && bm_sophia_allocation_current(c,s) && v->opening == s->opening.opening &&
        v->revision == s->state_revision && v->view.catalog_generation == c->model.generation &&
        v->facts_generation == c->facts.generation && v->allocation.id == c->allocation.allocation.id &&
        v->allocation.generation == c->allocation.allocation.generation;
}

int bm_sophia_permit_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *f)
{
    struct sophia_shell_content_feedback feedback;
    int r = sophia_shell_content_feedback_decode(f,&feedback);
    if (r != SOPHIA_SHELL_OK) return r;
    const struct sophia_shell_frame_permit *p = &feedback.value.permit;
    if (!c->demand_pending || f->transaction != c->demand_transaction ||
        feedback.grant.connection_epoch != c->limits.grant.connection_epoch ||
        feedback.grant.content_grant_epoch != c->limits.grant.content_grant_epoch ||
        p->output.id != c->demand.output.id || p->output.generation != c->demand.output.generation ||
        p->demand_id != c->demand.demand_id) return SOPHIA_SHELL_INVALID;
    if (p->state == 1) {
        if (c->permit_ready || p->permit_id <= c->last_permit || p->ttl_ms > c->limits.permit_timeout_ms ||
            p->max_candidate_bytes > c->limits.max_candidate_bytes ||
            c->demand_started > UINT64_MAX-p->ttl_ms) return SOPHIA_SHELL_INVALID;
        c->permit = *p; c->permit_ready = true; c->last_permit = p->permit_id;
    } else {
        if (p->state != 2 || !c->permit_ready || p->permit_id != c->permit.permit_id)
            return SOPHIA_SHELL_INVALID;
        c->demand_pending = false; c->permit_ready = false;
    }
    return SOPHIA_SHELL_OK;
}

static int demand_frame(struct bm_sophia_connection *c)
{
    if (!c->next_transaction || c->next_transaction == UINT64_MAX || c->demand_counter == UINT64_MAX)
        return SOPHIA_SHELL_INVALID;
    struct sophia_shell_frame_demand demand = {
        c->limits.grant,c->allocation.output,c->allocation.allocation,c->demand_counter+1,1,
    };
    uint8_t bytes[82]; size_t length;
    int r = sophia_shell_frame_demand_encode(bytes,sizeof(bytes),c->next_transaction,&demand,&length);
    if (r != SOPHIA_SHELL_OK) return r;
    struct sophia_shell_outbound_frame frame = {bytes,length,SOPHIA_SHELL_OUTBOUND_CONTROL};
    r = sophia_shell_outbox_push(&c->outbox,&frame,1);
    if (r != SOPHIA_SHELL_OK) return r;
    c->demand = demand; ++c->demand_counter; c->demand_transaction = c->next_transaction++;
    c->demand_started = c->now_msec; c->demand_pending = true;
    return SOPHIA_SHELL_OK;
}

static int offer(struct bm_sophia_connection *c, unsigned slot, const struct sophia_shell_upload_snapshot *resource)
{
    const struct bm_sophia_uploaded_view *v = &c->views[slot];
    if (c->demand.output.id != c->allocation.output.id ||
        c->demand.output.generation != c->allocation.output.generation ||
        c->demand.allocation.id != c->allocation.allocation.id ||
        c->demand.allocation.generation != c->allocation.allocation.generation)
        return SOPHIA_SHELL_INVALID;
    if (c->interaction_counter == UINT64_MAX ||
        244u+50u*v->view.row_count > c->permit.max_candidate_bytes) return SOPHIA_SHELL_INVALID;
    struct sophia_shell_native_candidate begin = {
        .grant = {c->limits.grant.connection_epoch,c->limits.grant.content_grant_epoch},
        .output = {c->allocation.output.id,c->allocation.output.generation},
        .facts_generation = v->facts_generation, .pacing_permit = c->permit.permit_id,
        .interaction_generation = c->interaction_counter+1, .placement_count = 1,
        .opening = v->opening, .catalog_generation = v->view.catalog_generation, .state_revision = v->revision,
        .selected = v->view.selected, .row_count = v->view.row_count,
    };
    struct sophia_shell_native_chunk chunk = {
        .grant = begin.grant, .surface_count = 1, .placement_count = 1, .target_count = v->view.row_count,
        .surface = {{v->allocation.id,v->allocation.generation},c->allocation.scale_generation,1,{0}},
        .placements = {{{resource->key.resource.id,resource->key.resource.generation},0,0}},
    };
    for (unsigned i = 0; i < v->view.row_count; ++i) {
        const struct bm_sophia_view_row *row = &v->view.rows[i];
        begin.rows[i] = row->slot;
        chunk.targets[i] = (struct sophia_shell_native_target){i+1,begin.interaction_generation,row->slot,
            row->x,row->y,row->width,row->height};
    }
    uint64_t transaction = c->next_transaction;
    int r = sophia_shell_native_lifecycle_offer(c->native,&begin,&chunk,&c->next_transaction);
    if (r != SOPHIA_SHELL_OK) return r;
    ++c->interaction_counter; c->candidate_slot = slot; c->candidate_transaction = transaction;
    c->candidate_active = true; c->demand_pending = false; c->permit_ready = false;
    return SOPHIA_SHELL_OK;
}

int bm_sophia_frame_service(struct bm_sophia_connection *c,
                            const struct sophia_shell_native_lifecycle_snapshot *s)
{
    if (s->candidate_pending)
        return sophia_shell_native_lifecycle_pump(c->native,&c->next_transaction);
    if (!s->open || s->closing || !bm_sophia_allocation_current(c,s)) return SOPHIA_SHELL_AGAIN;
    if (c->demand_pending) {
        /* TTL starts no earlier than our enqueue. Using receipt+TTL would
         * incorrectly add inbound queue delay to the server's lifetime. */
        if (!c->permit_ready || c->now_msec-c->demand_started >= c->permit.ttl_ms ||
            c->demand.output.id != c->allocation.output.id ||
            c->demand.output.generation != c->allocation.output.generation ||
            c->demand.allocation.id != c->allocation.allocation.id ||
            c->demand.allocation.generation != c->allocation.allocation.generation)
            return SOPHIA_SHELL_AGAIN;
    }
    for (unsigned i = 0; i < SOPHIA_SHELL_UPLOAD_SLOTS; ++i) {
        struct sophia_shell_upload_snapshot resource;
        int r = sophia_shell_upload_inspect(c->upload,i,&resource);
        if (r != SOPHIA_SHELL_OK) return r;
        if (resource.state != SOPHIA_UPLOAD_RESIDENT || c->retire_slot[i] ||
            (c->shown_valid && c->shown_slot == i) || !bm_sophia_view_current(c,i,s)) continue;
        return c->demand_pending ? offer(c,i,&resource) : demand_frame(c);
    }
    return SOPHIA_SHELL_AGAIN;
}

int bm_sophia_candidate_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *f)
{
    struct sophia_shell_content_feedback feedback;
    int r = sophia_shell_content_feedback_decode(f,&feedback);
    if (r != SOPHIA_SHELL_OK) return r;
    if (c->candidate_active && f->transaction != c->candidate_transaction) return SOPHIA_SHELL_INVALID;
    r = sophia_shell_native_lifecycle_receive(c->native,f,&c->next_transaction,bm_sophia_view_edit,c->menu);
    if (r != SOPHIA_SHELL_OK || !c->candidate_active || feedback.value.candidate.kind == 1) return r;
    struct sophia_shell_native_lifecycle_snapshot state;
    if (sophia_shell_native_lifecycle_inspect(c->native,&state) != SOPHIA_SHELL_OK) return SOPHIA_SHELL_INVALID;
    unsigned slot = c->candidate_slot;
    if (feedback.value.candidate.kind == 2 && state.presented &&
        state.candidate_generation == feedback.value.candidate.generation) {
        if (c->shown_valid && c->shown_slot != slot) c->retire_slot[c->shown_slot] = true;
        c->shown_slot = slot; c->shown_valid = true;
    } else c->retire_slot[slot] = true;
    c->candidate_active = false;
    return SOPHIA_SHELL_OK;
}
