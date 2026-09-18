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
        else if (c->views[i].valid && c->views[i].opening == state->opening.opening &&
            c->views[i].revision == state->state_revision && c->views[i].view.catalog_generation == c->model.generation &&
            c->views[i].allocation.id == c->allocation.allocation.id &&
            c->views[i].allocation.generation == c->allocation.allocation.generation)
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

int bm_sophia_connection_schedule(struct bm_sophia_connection *c)
{
    if (!c->native || !c->content) return SOPHIA_SHELL_AGAIN;
    struct sophia_shell_native_lifecycle_snapshot state;
    int r = sophia_shell_native_lifecycle_inspect(c->native,&state);
    if (r != SOPHIA_SHELL_OK) return r;
    if (!state.open || state.closing) return SOPHIA_SHELL_AGAIN;
    r = bm_sophia_allocation_service(c,&state);
    if (r < 0) return r;
    r = stage_view(c,&state);
    if (r < 0) return r;
    /* One resource record per visit; response control capacity remains reserved.
     * Candidate/permit scheduling will consume the retained views separately. */
    r = sophia_shell_upload_pump(c->upload,&c->outbox,&c->next_transaction);
    return r < 0 ? r : SOPHIA_SHELL_AGAIN;
}
