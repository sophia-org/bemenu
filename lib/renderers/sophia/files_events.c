#include "internal.h"
#include "files_internal.h"
#include <string.h>

static int
done(struct bm_sophia_files *f)
{
    return sophia_ns_done(&f->ns);
}

/* The existing Bemenu semantic edit, run by the SDK after it reserved the ack. */
static int
edit(void *user, const struct sophia_sf_native_input *input)
{
    struct bm_sophia_files *f = user;
    int applied = bm_sophia_view_edit(f->menu, input->kind, input->text.data, input->text.size);
    f->edits += applied != 0;
    return applied;
}

static int
input(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    /* Keyboard activation goes with its own Accept only when the SDK armed it. */
    int activate = e->record->value.native_input.kind == 17 && e->can_activate;
    uint64_t first;
    int r = sophia_ns_input_apply(&f->ns, activate, edit, f, &first);
    if (r)
        return r;
    if (activate) {
        f->activation = true;
        f->activation_ticket = first;
    }
    return 0;
}

/* Kind 3 is a cancellation and owes nothing. Kinds 1-2 keep old Bemenu
 * semantics: a row invocation activates when armed; dismissal is consumed. */
static int
action(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    if (!e->ack_owed)
        return done(f);
    uint16_t kind = e->record->value.action.kind;
    uint16_t disposition = e->can_activate || kind == 2 ? 1 : 2;
    uint64_t first;
    int r = sophia_ns_action_ack(&f->ns, disposition, e->can_activate, &first);
    if (r)
        return r;
    if (e->can_activate) {
        f->activation = true;
        f->activation_ticket = first;
    }
    return 0;
}

static int
allocation(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    const struct sophia_sf_allocation_result *a = &e->record->value.allocation_result;
    if (e->current && a->status == 1) {
        int r = bm_sophia_files_granted(f, a);
        if (r)
            return r;
    }
    if (e->current && a->status == 4 && a->allocation_id == f->raster_id &&
        a->allocation_generation == f->raster_generation) {
        bm_sophia_raster_free(f->raster);
        f->raster = NULL;
        f->raster_id = f->raster_generation = 0;
    }
    return done(f);
}

static int
resource(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    const struct sophia_sf_resource_status *v = &e->record->value.resource_status;
    struct bm_sophia_files_view *view = bm_sophia_files_view_find(f, v->resource_id);
    if (e->current && view && v->resource_generation == 1) {
        if (v->status == 1 && view->state == BM_SOPHIA_FILES_VIEW_BEGUN) {
            view->state = BM_SOPHIA_FILES_VIEW_SENDING;
        } else if (v->status == 2 && view->state == BM_SOPHIA_FILES_VIEW_ENDED) {
            view->state = BM_SOPHIA_FILES_VIEW_ACCEPTED;
            f->upload = -1;
        } else if (v->status != 1 && v->status != 2) {
            /* Rejected or expired: the SDK freed it; so is our copy once the
             * session no longer borrows a chunk of it. */
            if (f->upload == view - f->views)
                f->upload = -1;
            view->state = BM_SOPHIA_FILES_VIEW_FREE;
            bm_sophia_files_release_copy(f);
        } else {
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, SOPHIA_9P_INVALID);
        }
    }
    return done(f);
}

static int
released(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    const struct sophia_sf_resource_released *v = &e->record->value.resource_released;
    struct bm_sophia_files_view *view = bm_sophia_files_view_find(f, v->resource_id);
    if (e->current && view && v->resource_generation == 1) {
        if (f->pending == view - f->views || f->upload == view - f->views)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, SOPHIA_9P_INVALID);
        if (f->shown == view - f->views)
            f->shown = -1;
        view->state = BM_SOPHIA_FILES_VIEW_FREE;
    }
    return done(f);
}

static int
permit(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    if (e->current) {
        /* Grant: usable until consumed or its advisory bound. Any other
         * state is the server's end of this demand. */
        f->permit = e->record->value.frame_permit.state == 1;
        f->demand = f->permit;
    }
    return done(f);
}

static int
candidate(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    uint16_t kind = e->record->value.candidate_outcome.kind;
    if (f->pending < 0)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_FRAME, SOPHIA_9P_INVALID);
    if (kind != 1) {
        if (kind == 2 && e->current && sophia_ns_presented(&f->ns))
            f->shown = f->pending;
        f->pending = -1;
        f->present_ticket = 0;
    }
    return done(f);
}

static void
announce(struct bm_sophia_files *f, const struct sophia_sf_record *record)
{
    if (record->header.kind != SOPHIA_SF_OBJECT_PUBLISHED)
        return;
    const struct sophia_sf_object_published *v = &record->value.object_published;
    if (v->object_kind >= SOPHIA_SF_LIMITS && v->object_kind <= SOPHIA_SF_INDICATORS)
        f->announced[v->object_kind - 1] = (struct bm_sophia_files_announcement){true, v->generation, v->qid};
}

static int
opening(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    if (e->current) {
        /* A fresh validated opening starts a fresh query. These empty-filter
         * operations allocate nothing; retained immutable uploads stay owned. */
        bm_menu_set_filter(f->menu, NULL);
        bm_menu_filter(f->menu);
        bm_menu_set_highlighted_index(f->menu, 0);
    }
    return done(f);
}

static int
dispatch(struct bm_sophia_files *f, const struct sophia_ns_event *e)
{
    switch (e->kind) {
    case SOPHIA_NS_EV_OTHER:
        announce(f, e->record);
        return done(f);
    case SOPHIA_NS_EV_OPENING:
        return opening(f, e);
    case SOPHIA_NS_EV_ALLOCATION:
        return allocation(f, e);
    case SOPHIA_NS_EV_RESOURCE:
        return resource(f, e);
    case SOPHIA_NS_EV_RESOURCE_RELEASED:
        return released(f, e);
    case SOPHIA_NS_EV_PERMIT:
        return permit(f, e);
    case SOPHIA_NS_EV_CANDIDATE:
        return candidate(f, e);
    case SOPHIA_NS_EV_INPUT:
        return input(f, e);
    case SOPHIA_NS_EV_ACTION:
        return action(f, e);
    case SOPHIA_NS_EV_ACTIVATION:
        f->activation = false;
        f->activation_ticket = 0;
        return done(f);
    default:
        /* Closed, focus and revocation live in the SDK's state. */
        return done(f);
    }
}

int
bm_sophia_files_events(struct bm_sophia_files *f)
{
    f->held = false;
    for (unsigned i = 0; i < BM_SOPHIA_FILES_EVENTS && !f->terminal; ++i) {
        struct sophia_ns_event e;
        int r = sophia_ns_next(&f->ns, f->now, &e);
        if (r == SOPHIA_9P_AGAIN)
            return 0;
        if (r)
            return r;
        r = dispatch(f, &e);
        /* BUSY keeps the head: no edit happened and nothing was sent. */
        if (r == SOPHIA_9P_BUSY) {
            f->held = e.kind == SOPHIA_NS_EV_INPUT;
            return 0;
        }
        if (r)
            return bm_sophia_files_fail(f, e.kind == SOPHIA_NS_EV_INPUT ? BM_SOPHIA_FILES_STAGE_INPUT :
                                        e.kind == SOPHIA_NS_EV_ACTION ? BM_SOPHIA_FILES_STAGE_ACTION :
                                        BM_SOPHIA_FILES_STAGE_NATIVE, r);
    }
    return f->terminal;
}

/* A catalog is adopted between openings, or as the current opening's own
 * generation. A newer one is still fetched at once (clearing its ack hold),
 * then refetched and installed after that opening closes. */
static int
catalog(struct bm_sophia_files *f, const struct sophia_sf_catalog *v)
{
    const struct sophia_sf_native_opening *open = sophia_ns_opening(&f->ns);
    if (v->generation < f->model.generation)
        return 0;
    if (open && open->catalog_generation != v->generation) {
        if (v->generation > f->model.generation)
            f->deferred = (struct bm_sophia_files_announcement){true, v->generation, f->fetch_qid};
        return 0;
    }
    if (v->generation != f->model.generation &&
        !bm_sophia_catalog_install_files(&f->model, f->menu, v))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_CATALOG, SOPHIA_9P_INVALID);
    if (f->deferred.owed && f->deferred.generation <= v->generation)
        f->deferred.owed = false;
    return sophia_ns_catalog(&f->ns, v->generation);
}

static int
install(struct bm_sophia_files *f, const struct sophia_sf_record *object)
{
    switch (object->header.kind) {
    case SOPHIA_SF_OUTPUTS:
        if (object->value.outputs.facts_generation < f->outputs.facts_generation ||
            object->value.outputs.output_count > 16)
            return 0;
        f->outputs = object->value.outputs;
        return sophia_ns_facts(&f->ns, f->outputs.facts_generation);
    case SOPHIA_SF_CATALOG:
        return catalog(f, &object->value.catalog);
    default:
        /* Limits stay the ones the lifecycle was initialized with; a
         * launcher has no indicator feed. Fetched only to clear ack holds. */
        return 0;
    }
}

static bool
choose(struct bm_sophia_files *f)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (!f->announced[i].owed)
            continue;
        f->fetch_kind = (uint16_t)(i + 1);
        f->fetch_generation = f->announced[i].generation;
        f->fetch_qid = f->announced[i].qid;
        f->refetch = false;
        return true;
    }
    const struct sophia_sf_native_opening *open = sophia_ns_opening(&f->ns);
    if (!f->deferred.owed || (open && open->catalog_generation != f->deferred.generation))
        return false;
    f->fetch_kind = SOPHIA_SF_CATALOG;
    f->fetch_generation = f->deferred.generation;
    f->fetch_qid = f->deferred.qid;
    f->refetch = true;
    return true;
}

int
bm_sophia_files_objects(struct bm_sophia_files *f)
{
    const struct sophia_sf_record *object;
    if (!f->fetching) {
        if (!choose(f))
            return 0;
        int r = sophia_ss_object(&f->ss, f->fetch_kind, f->fetch_generation, f->fetch_qid);
        if (r == SOPHIA_9P_BUSY)
            return 0;
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_OBJECT, r);
        f->fetching = true;
        if (f->refetch)
            f->deferred.owed = false;
        else if (f->announced[f->fetch_kind - 1].generation == f->fetch_generation)
            f->announced[f->fetch_kind - 1].owed = false;
    }
    int r = sophia_ss_object_result(&f->ss, &object);
    if (r == SOPHIA_9P_BUSY)
        return 0;
    f->fetching = false;
    if (r == SOPHIA_9P_AGAIN)
        return 0; /* Superseded: its newer announcement is fetched instead. */
    if (r || object->header.kind != f->fetch_kind)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_OBJECT, r ? r : SOPHIA_9P_INVALID);
    r = install(f, object);
    return r ? bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_CATALOG, r) : 0;
}
