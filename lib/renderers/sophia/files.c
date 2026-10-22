#include "internal.h"
#include "files_internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

/* Native launcher authority plus the required owned-ring grant (r10). */
static const struct sophia_ss_config session_config = {
    {SOPHIA_SF_REVISION_OWNED_RING, SOPHIA_SF_REVISION_OWNED_RING, 0x89a0},
    SOPHIA_SF_LAUNCHER, 8192, 8, 65536, NULL, 0,
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
open_role(struct bm_sophia_files *f)
{
    struct sophia_ss_config config = session_config;
    config.object_storage = f->objects;
    config.object_capacity = SOPHIA_SF_MAX_RECORD;
    int r = f->relay_fd >= 0 ?
        sophia_ss_open_attached(&f->ss, &f->role, sophia_la_root(&f->auth),
                                &config, f->storage, f->storage_bytes) :
        sophia_ss_open_fd(&f->ss, f->fd, &config, f->storage, f->storage_bytes);
    if (r)
        return r;
    f->session = true;
    return 0;
}

static int
open_session(struct bm_sophia_files *f)
{
    f->fd = sophia_desktop_connection_take(&f->connection);
    if (f->fd < 0)
        return SOPHIA_9P_IO;
    f->connecting = false;
    if (f->relay_fd < 0)
        return open_role(f);
    size_t bytes = sophia_9p_storage_bytes(session_config.msize, 8);
    f->role_storage = malloc(bytes);
    f->relay_storage = malloc(bytes);
    if (!f->role_storage || !f->relay_storage)
        return SOPHIA_9P_BUSY;
    int r = sophia_9p_init(&f->role, f->fd, session_config.msize, 8, 32, f->role_storage, bytes);
    if (!r)
        r = sophia_9p_init(&f->relay, f->relay_fd, session_config.msize, 8, 32, f->relay_storage, bytes);
    if (!r)
        r = sophia_la_init(&f->auth, &f->role, &f->relay, "shell", "launcher", f->now);
    f->authenticating = !r;
    return r;
}

/* Adopt before any connection can reuse an invalid inherited descriptor. */
int
bm_sophia_files_prepare_launch(int *relay_fd)
{
    if (!relay_fd)
        return SOPHIA_9P_ARGUMENT;
    *relay_fd = -1;
    const char *value = getenv("SOPHIA_LAUNCH_AUTH_FD");
    if (!value)
        return 0;
    if (!*value)
        return SOPHIA_9P_ARGUMENT;
    unsigned fd = 0;
    for (; *value; ++value) {
        if (*value < '0' || *value > '9' || fd > (INT_MAX - (unsigned)(*value - '0')) / 10)
            return SOPHIA_9P_ARGUMENT;
        fd = fd * 10 + (unsigned)(*value - '0');
    }
    if (fd < 3)
        return SOPHIA_9P_ARGUMENT;
    int flags = fcntl((int)fd, F_GETFD);
    if (flags < 0)
        return SOPHIA_9P_IO;
    if (fcntl((int)fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return SOPHIA_9P_IO;
    *relay_fd = (int)fd;
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
    f->image_dir = -1;
    f->relay_fd = -1;
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
    int r = bm_sophia_files_prepare_launch(&f->relay_fd);
    if (!r)
        r = f->storage && f->objects ? begin_connect(f) : SOPHIA_9P_BUSY;
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
    bool image_ready = f->image_enabled && sophia_ip_channel(&f->image.producer) == SOPHIA_IPC_READY;
    if (guard(f, BM_SOPHIA_FILES_GUARD_BOOT, !f->native || !image_ready, 0) ||
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
    char directory[sizeof(f->endpoint)];
    memcpy(directory, f->endpoint, sizeof(directory));
    char *slash = strrchr(directory, '/');
    slash[slash == directory ? 1 : 0] = 0;
    f->image_dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (f->image_dir < 0)
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, SOPHIA_9P_IO);
    const struct sophia_sf_limits *l = sophia_ss_limits(&f->ss);
    struct sophia_ss_image_config image = {0};
    image.endpoint_dir = f->image_dir;
    image.formats = f->image_formats;
    image.formats_capacity = sizeof(f->image_formats);
    image.producer.bounds.max_resource_bytes = l->max_resource_bytes;
    image.producer.bounds.max_width = l->max_width_px;
    image.producer.bounds.max_height = l->max_height_px;
    image.producer.bounds.max_outputs = l->max_outputs;
    image.producer.bounds.transfer_slots = l->max_open_transfers;
    image.producer.transfer_timeout_ms = l->transfer_timeout_ms;
    image.producer.startup_deadline_ms = later(f->now, BM_SOPHIA_FILES_GUARD_MS);
    if (sophia_ss_image_init(&f->image, &image) || sophia_ss_image_attach(&f->ss, &f->image))
        return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NATIVE, SOPHIA_9P_INVALID);
    f->image_enabled = true;
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
    if (f->authenticating) {
        r = sophia_la_service(&f->auth, now_ms, BM_SOPHIA_FILES_BYTES);
        if (!r && sophia_la_ready(&f->auth)) {
            r = open_role(f);
            f->authenticating = false;
        }
        return r ? bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_NEGOTIATE, r) : guards(f);
    }
    r = sophia_ss_dispatch(&f->ss, revents, BM_SOPHIA_FILES_BYTES, now_ms);
    if (r)
        return bm_sophia_files_fail(f, f->native ? BM_SOPHIA_FILES_STAGE_NATIVE :
                                    BM_SOPHIA_FILES_STAGE_NEGOTIATE, r);
    if ((r = bootstrap(f)))
        return r;
    if (f->image_enabled) {
        struct pollfd image;
        bm_sophia_files_image_poll(f, &image);
        if (image.fd >= 0 && poll(&image, 1, 0) > 0)
            (void)sophia_ss_image_dispatch(&f->image, image.revents, now_ms);
        if (f->image.started && sophia_ip_path(&f->image.producer) == SOPHIA_IPP_ERROR)
            return bm_sophia_files_fail(f, BM_SOPHIA_FILES_STAGE_UPLOAD, SOPHIA_9P_IO);
    }
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
    if (f->authenticating) {
        poller->fd = f->fd;
        poller->events = POLLIN | (sophia_9p_wants_write(&f->role) ? POLLOUT : 0);
        uint64_t due = sophia_la_next_wake(&f->auth);
        *timeout_ms = smaller(*timeout_ms, due > now_ms ? due - now_ms : 0);
        return 0;
    }
    poller->fd = sophia_ss_poll_fd(&f->ss);
    poller->events = sophia_ss_poll_events(&f->ss);
    if (!poller->events)
        poller->fd = -1;
    int retry = sophia_ss_timeout(&f->ss, now_ms);
    if (retry >= 0)
        *timeout_ms = smaller(*timeout_ms, (uint64_t)retry);
    if (f->image_enabled) {
        int64_t due = sophia_ss_image_timeout(&f->image, now_ms);
        if (due >= 0)
            *timeout_ms = smaller(*timeout_ms, (uint64_t)due);
    }
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

void
bm_sophia_files_image_poll(const struct bm_sophia_files *f, struct pollfd *poller)
{
    *poller = (struct pollfd){-1, 0, 0};
    if (f && f->image_enabled && !f->terminal)
        *poller = (struct pollfd){sophia_ss_image_poll_fd(&f->image),
                                 sophia_ss_image_poll_events(&f->image), 0};
}

void
bm_sophia_files_relay_poll(const struct bm_sophia_files *f, struct pollfd *poller)
{
    *poller = (struct pollfd){-1, 0, 0};
    if (f && f->authenticating && !f->terminal)
        *poller = (struct pollfd){f->relay_fd, POLLIN | (sophia_9p_wants_write(&f->relay) ? POLLOUT : 0), 0};
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
    if (f->authenticating)
        sophia_la_cancel(&f->auth);
    if (f->relay_fd >= 0)
        close(f->relay_fd);
    free(f->role_storage);
    free(f->relay_storage);
    if (f->fd >= 0)
        close(f->fd);
    sophia_desktop_connection_close(&f->connection);
    bm_sophia_raster_free(f->raster);
    /* No replacement of an externally modified menu; never free foreign items. */
    bm_sophia_catalog_clear(&f->model);
    if (f->image_enabled && f->image.started) {
        unsigned cursor = 0;
        struct sophia_ss_ring_offer_info offer;
        while (sophia_ss_image_ring_next(&f->image, &cursor, &offer))
            (void)sophia_ss_image_ring_forget(&f->image, offer.offer.offer);
        sophia_ss_image_close(&f->image);
    }
    if (f->image_dir >= 0)
        close(f->image_dir);
    free(f->storage);
    free(f->objects);
    free(f);
}
