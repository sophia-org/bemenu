#include "internal.h"
#include "files_internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The native launcher role: revision 7, bits 5, 7, 8 and 11 exactly. */
static const struct sophia_ss_config session_config = {
    {7, 7, 0x9a0}, SOPHIA_SF_LAUNCHER, 8192, 8, 65536, NULL, 0,
};
static const struct sophia_ns_config native_config = {1, BM_SOPHIA_FILES_MARGIN_MS};

/* Deadlines saturate: the whole uint64 millisecond range is accepted. */
static uint64_t
later(uint64_t now, uint64_t delay)
{
    return now > UINT64_MAX - delay ? UINT64_MAX : now + delay;
}

int
bm_sophia_files_fail(struct bm_sophia_files *f, enum bm_sophia_files_stage stage, int code)
{
    if (!f->terminal) {
        f->terminal = code < 0 ? code : SOPHIA_9P_INVALID;
        f->failed = stage;
    }
    return f->terminal;
}

static int
open_session(struct bm_sophia_files *f)
{
    int fd = sophia_desktop_connection_take(&f->connection);
    if (fd < 0)
        return SOPHIA_9P_IO;
    f->fd = fd;
    f->connecting = false;
    struct sophia_ss_config config = session_config;
    config.object_storage = f->objects;
    config.object_capacity = SOPHIA_SF_MAX_RECORD;
    int r = sophia_ss_open_fd(&f->ss, fd, &config, f->storage, f->storage_bytes);
    if (r)
        return r;
    f->session = true;
    return 0;
}

/* Nonblocking connect to the one selected endpoint; RETRY only after its wait. */
static int
begin_connect(struct bm_sophia_files *f)
{
    int r = sophia_desktop_connection_begin(&f->connection, f->endpoint);
    if (r == SOPHIA_DESKTOP_CONNECTED)
        return open_session(f);
    if (r == SOPHIA_DESKTOP_CONNECTING) {
        f->connecting = true;
        return 0;
    }
    if (r == SOPHIA_DESKTOP_CONNECT_RETRY) {
        f->connecting = true;
        f->retry_at = later(f->now, SOPHIA_DESKTOP_CONNECT_RETRY_MS);
        return 0;
    }
    return SOPHIA_9P_IO;
}

static int
drive_connect(struct bm_sophia_files *f, short revents)
{
    if (f->connection.fd < 0)
        return f->now >= f->retry_at ? begin_connect(f) : 0;
    int r = sophia_desktop_connection_finish(&f->connection, revents);
    if (r == SOPHIA_DESKTOP_CONNECTED)
        return open_session(f);
    if (r == SOPHIA_DESKTOP_CONNECTING)
        return 0;
    if (r == SOPHIA_DESKTOP_CONNECT_RETRY) {
        f->retry_at = later(f->now, SOPHIA_DESKTOP_CONNECT_RETRY_MS);
        return 0;
    }
    return SOPHIA_9P_IO;
}

int
bm_sophia_files_new(struct bm_menu *menu, const char *endpoint, uint64_t now_ms,
                    struct bm_sophia_files **out)
{
    if (!menu || !endpoint || !out)
        return SOPHIA_9P_ARGUMENT;
    uint32_t count;
    bm_menu_get_items(menu, &count);
    if (count || endpoint[0] != '/' || strlen(endpoint) >= sizeof(((struct bm_sophia_files *)0)->endpoint))
        return SOPHIA_9P_ARGUMENT;
    size_t storage = sophia_ss_storage_bytes(session_config.msize, session_config.queue_bytes);
    if (!storage)
        return SOPHIA_9P_ARGUMENT;
    struct bm_sophia_files *f = calloc(1, sizeof(*f));
    if (!f)
        return SOPHIA_9P_BUSY;
    f->menu = menu;
    f->fd = -1;
    f->connection.fd = -1;
    f->upload = f->pending = f->shown = -1;
    f->next_resource = 1;
    f->now = now_ms;
    f->clock_seen = true;
    memcpy(f->endpoint, endpoint, strlen(endpoint) + 1);
    f->storage = malloc(storage);
    f->storage_bytes = storage;
    /* A launcher catalog object is up to 4 MiB; its rows borrow this scratch
     * only until the next fetch, so installation copies them at once. */
    f->objects = malloc(SOPHIA_SF_MAX_RECORD);
    int r = f->storage && f->objects ? begin_connect(f) : SOPHIA_9P_BUSY;
    if (r) {
        bm_sophia_files_dispose(f);
        return r;
    }
    *out = f;
    return 0;
}

static bool
guard(struct bm_sophia_files *f, enum bm_sophia_files_guard_kind kind, bool active, uint64_t key)
{
    struct bm_sophia_files_guard *g = &f->guards[kind];
    if (!active) {
        g->active = false;
        return false;
    }
    if (!g->active || g->key != key) {
        *g = (struct bm_sophia_files_guard){true, key, f->now};
        return false;
    }
    return f->now >= later(g->started, BM_SOPHIA_FILES_GUARD_MS);
}

static uint64_t
retiring_key(const struct bm_sophia_files *f)
{
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i)
        if (f->views[i].state == BM_SOPHIA_FILES_VIEW_RETIRING)
            return f->views[i].id;
    return 0;
}

/* Pending local obligations only. Invalidation after close has no bound. */
static int
guards(struct bm_sophia_files *f)
{
    struct sophia_ns_obligations o = {0};
    if (f->native && sophia_ns_obligations(&f->ns, &o))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, SOPHIA_9P_INVALID);
    /* An owed input or action ack refused at submit is lost for good: the edit
     * already happened and nothing is replayed or re-edited. End here. */
    if (o.lost_acks)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_INPUT, SOPHIA_9P_INVALID);
    const struct bm_sophia_files_view *up = f->upload >= 0 ? &f->views[f->upload] : NULL;
    uint64_t retiring = retiring_key(f);
    uint64_t frame = f->pending >= 0 ? f->present_ticket : f->demand_ticket;
    if (guard(f, BM_SOPHIA_FILES_GUARD_BOOT, !f->native, 0) ||
        guard(f, BM_SOPHIA_FILES_GUARD_OBJECT, f->fetching, f->fetch_kind ^ (f->fetch_generation << 3)) ||
        guard(f, BM_SOPHIA_FILES_GUARD_ALLOCATION, o.allocation_due_ms != 0, f->allocation_ticket) ||
        guard(f, BM_SOPHIA_FILES_GUARD_UPLOAD, up != NULL, up ? up->id : 0) ||
        guard(f, BM_SOPHIA_FILES_GUARD_FRAME, f->demand || f->pending >= 0, frame) ||
        guard(f, BM_SOPHIA_FILES_GUARD_ACTIVATION, f->activation, f->activation_ticket) ||
        guard(f, BM_SOPHIA_FILES_GUARD_RETIRE, retiring != 0, retiring))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_TIMEOUT, SOPHIA_9P_IO);
    return 0;
}

static int
bootstrap(struct bm_sophia_files *f)
{
    if (f->native)
        return 0;
    if (sophia_ss_state(&f->ss) != SOPHIA_SS_READY || !sophia_ss_limits(&f->ss))
        return 0;
    int r = sophia_ns_init(&f->ns, &f->ss, &native_config);
    if (r == SOPHIA_9P_BUSY)
        return 0;
    if (r)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, r);
    f->native = true;
    return 0;
}

int
bm_sophia_files_progress(struct bm_sophia_files *f, short revents, uint64_t now_ms)
{
    if (!f)
        return SOPHIA_9P_ARGUMENT;
    if (f->terminal)
        return f->terminal;
    if (f->clock_seen && now_ms < f->now)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_CLOCK, SOPHIA_9P_INVALID);
    f->now = now_ms;
    f->clock_seen = true;
    int r;
    if (f->connecting) {
        r = drive_connect(f, revents);
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_CONNECT, r);
        return guards(f);
    }
    r = sophia_ss_dispatch(&f->ss, revents, BM_SOPHIA_FILES_BYTES, now_ms);
    if (r)
        return bm_sophia_files_fail(f, f->native ? BM_SOPHIA_FILES_STAGE_NATIVE :
                                    BM_SOPHIA_FILES_STAGE_NEGOTIATE, r);
    if ((r = bootstrap(f)))
        return r;
    if (f->native) {
        r = sophia_ns_service(&f->ns, now_ms);
        if (r)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, r);
        if ((r = bm_sophia_files_events(f)) || (r = bm_sophia_files_settle(f)) ||
            (r = bm_sophia_files_objects(f)) || (r = bm_sophia_files_schedule(f)))
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, r);
    }
    /* Consumed, fetched events are acknowledged every pass: journal progress. */
    r = sophia_ss_ack(&f->ss);
    if (r < 0)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, r);
    return guards(f);
}

static int
smaller(int a, uint64_t b)
{
    return b < (uint64_t)a ? (int)b : a;
}

int
bm_sophia_files_poll(const struct bm_sophia_files *f, uint64_t now_ms, struct pollfd *poller,
                     int *timeout_ms)
{
    if (!f || !poller || !timeout_ms)
        return SOPHIA_9P_ARGUMENT;
    *poller = (struct pollfd){-1, 0, 0};
    *timeout_ms = 50;
    if (f->terminal)
        return f->terminal;
    if (f->connecting) {
        if (f->connection.fd >= 0) {
            poller->fd = f->connection.fd;
            poller->events = sophia_desktop_connection_events(&f->connection);
        } else {
            *timeout_ms = smaller(*timeout_ms, f->retry_at > now_ms ? f->retry_at - now_ms : 0);
        }
        return 0;
    }
    poller->fd = sophia_ss_poll_fd(&f->ss);
    poller->events = sophia_ss_poll_events(&f->ss);
    if (!poller->events)
        poller->fd = -1;
    int retry = sophia_ss_timeout(&f->ss, now_ms);
    if (retry >= 0)
        *timeout_ms = smaller(*timeout_ms, (uint64_t)retry);
    /* Only FUTURE lifecycle deadlines wake the loop. A past advisory permit
     * bound, like any past informational deadline, allows no local action:
     * the server's own record ends it, so it must not become a zero wait. */
    struct sophia_ns_obligations o;
    if (f->native && !sophia_ns_obligations(&f->ns, &o)) {
        const uint64_t due[] = {o.input_due_ms, o.action_due_ms, o.permit_expires_ms,
                                o.allocation_due_ms, o.candidate_due_ms};
        for (unsigned i = 0; i < sizeof(due) / sizeof(due[0]); ++i)
            if (due[i] > now_ms)
                *timeout_ms = smaller(*timeout_ms, due[i] - now_ms);
    }
    /* A past local guard is actionable: the next progress call fails. */
    for (unsigned i = 0; i < BM_SOPHIA_FILES_GUARD_COUNT; ++i) {
        const struct bm_sophia_files_guard *g = &f->guards[i];
        if (!g->active)
            continue;
        uint64_t due = later(g->started, BM_SOPHIA_FILES_GUARD_MS);
        *timeout_ms = smaller(*timeout_ms, due > now_ms ? due - now_ms : 0);
    }
    return 0;
}

bool
bm_sophia_files_inspect(const struct bm_sophia_files *f, struct bm_sophia_files_snapshot *out)
{
    if (!f || !out)
        return false;
    struct bm_sophia_files_snapshot v = {
        .connected = f->session, .ready = f->session && sophia_ss_state(&f->ss) == SOPHIA_SS_READY,
        .native = f->native, .session = f->session ? sophia_ss_state(&f->ss) : SOPHIA_SS_NEGOTIATING,
        .lifecycle = f->native ? sophia_ns_state(&f->ns) : SOPHIA_NS_LIVE, .failed = f->failed,
        .connection_epoch = f->session ? sophia_ss_epoch(&f->ss) : 0,
        .catalog_generation = f->model.generation, .facts_generation = f->outputs.facts_generation,
        .displayed = f->displayed, .edits = f->edits,
    };
    if (f->native) {
        const struct sophia_sf_native_opening *opening = sophia_ns_opening(&f->ns);
        v.open = opening != NULL;
        v.opening = opening ? opening->opening : 0;
        v.revision = sophia_ns_revision(&f->ns);
        v.presented = sophia_ns_presented(&f->ns) != NULL;
        v.focused = sophia_ns_focus(&f->ns) != NULL;
    }
    for (unsigned i = 0; i < BM_SOPHIA_FILES_VIEWS; ++i)
        v.views += f->views[i].state != BM_SOPHIA_FILES_VIEW_FREE;
    *out = v;
    return true;
}

void
bm_sophia_files_dispose(struct bm_sophia_files *f)
{
    if (!f)
        return;
    /* Local end first: queued records become DROPPED_UNSENT; no replay. */
    if (f->session)
        sophia_ss_close(&f->ss);
    if (f->fd >= 0)
        close(f->fd);
    sophia_desktop_connection_close(&f->connection);
    bm_sophia_raster_free(f->raster);
    /* No replacement of an externally modified menu; never free foreign items. */
    bm_sophia_catalog_clear(&f->model);
    free(f->copy);
    free(f->storage);
    free(f->objects);
    free(f);
}
