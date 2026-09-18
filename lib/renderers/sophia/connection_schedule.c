#include "connection_internal.h"

static int stage_view(struct bm_sophia_connection *c, const struct sophia_shell_native_lifecycle_snapshot *state)
{
    if (!bm_sophia_allocation_current(c,state) || state->opening.catalog_generation != c->model.generation)
        return SOPHIA_SHELL_AGAIN;
    bool vacant = false;
    for (unsigned i = 0; i < SOPHIA_SHELL_UPLOAD_SLOTS; ++i) {
        struct sophia_shell_upload_snapshot slot;
        int r = sophia_shell_upload_inspect(c->upload,i,&slot);
        if (r != SOPHIA_SHELL_OK) return r;
        if (slot.state == SOPHIA_UPLOAD_EMPTY) {c->views[i].valid = false; vacant = true;}
        else if (!c->retire_slot[i] && bm_sophia_view_current(c,i,state))
            return SOPHIA_SHELL_AGAIN;
    }
    if (!vacant) return SOPHIA_SHELL_BUSY;
    struct bm_sophia_view view;
    if (!bm_sophia_view_capture(c->raster,&c->model,&view)) return SOPHIA_SHELL_INVALID;
    if (view.stride != view.width*4 || view.row_count > c->limits.max_candidate_targets)
        return SOPHIA_SHELL_INVALID;
    unsigned slot;
    int r = sophia_shell_upload_stage(c->upload,view.width,view.height,
        c->allocation.scale_numerator,c->allocation.scale_denominator,
        view.data,(size_t)view.stride*view.height,&slot);
    if (r != SOPHIA_SHELL_OK) return r;
    view.data = NULL; /* The upload owner now holds the immutable copy. */
    c->views[slot] = (struct bm_sophia_uploaded_view){true,state->opening.opening,state->state_revision,
        c->facts.generation,c->allocation.allocation,view};
    return SOPHIA_SHELL_OK;
}

static int retire_views(struct bm_sophia_connection *c, const struct sophia_shell_native_lifecycle_snapshot *state)
{
    if ((!state->open || state->closing) && c->shown_valid) {
        c->retire_slot[c->shown_slot] = true; c->shown_valid = false;
    }
    for (unsigned i = 0; i < SOPHIA_SHELL_UPLOAD_SLOTS; ++i) {
        if ((c->shown_valid && c->shown_slot == i) || (c->candidate_active && c->candidate_slot == i)) continue;
        if (c->views[i].valid && !bm_sophia_view_current(c,i,state)) c->retire_slot[i] = true;
        if (!c->retire_slot[i]) continue;
        struct sophia_shell_upload_snapshot resource;
        int r = sophia_shell_upload_inspect(c->upload,i,&resource);
        if (r != SOPHIA_SHELL_OK) return r;
        if (resource.state == SOPHIA_UPLOAD_EMPTY) {
            c->retire_slot[i] = false; c->views[i].valid = false;
        } else if (resource.state == SOPHIA_UPLOAD_RESIDENT) {
            r = sophia_shell_upload_retire(c->upload,i);
        } else if (resource.state == SOPHIA_UPLOAD_STAGED || resource.state == SOPHIA_UPLOAD_CHUNKS ||
                   resource.state == SOPHIA_UPLOAD_BEGIN_PENDING || resource.state == SOPHIA_UPLOAD_END_PENDING) {
            r = sophia_shell_upload_cancel(c->upload,i);
        }
        if (r < 0) return r;
    }
    return SOPHIA_SHELL_OK;
}

int bm_sophia_connection_schedule(struct bm_sophia_connection *c)
{
    if (!c->native || !c->content) return SOPHIA_SHELL_AGAIN;
    struct sophia_shell_native_lifecycle_snapshot state;
    int r = sophia_shell_native_lifecycle_inspect(c->native,&state);
    if (r != SOPHIA_SHELL_OK) return r;
    r = retire_views(c,&state);
    if (r < 0) return r;
    r = bm_sophia_allocation_service(c,&state);
    if (r < 0) return r;
    r = stage_view(c,&state);
    if (r < 0) return r;
    r = bm_sophia_frame_service(c,&state);
    if (r < 0) return r;
    /* One resource record per visit, including exact retained cleanup. */
    r = sophia_shell_upload_pump(c->upload,&c->outbox,&c->next_transaction);
    return r < 0 ? r : SOPHIA_SHELL_AGAIN;
}
