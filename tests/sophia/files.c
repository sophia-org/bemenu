/* UNIT tests of the Bemenu native launcher file adapter. The adapter, the
 * SDK's real sophia_ns, the real file codec and Bemenu's unchanged menu,
 * filter, input and Cairo raster run together; tests/sophia/files_peer.c
 * supplies every sophia_ss_* outcome. SUPPLIED PEER OUTCOMES ARE NOT LIVE,
 * PHYSICAL OR PRODUCTION EVIDENCE: no 9P export, Session, compositor, display,
 * input device or application launch is involved. A real executable against a
 * production export fixture is separate, later evidence. */
#include "internal.h"
#include "renderers/sophia/files.h"
#include "renderers/sophia/raster.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* files_peer.c: the scripted stand-in's control surface. */
void peer_reset(const struct sophia_sf_limits *limits);
void peer_ready(void);
void peer_end(enum sophia_ss_state state);
void peer_busy(int submit, int reserve);
void peer_outcome(uint64_t ticket, enum sophia_ss_outcome o);
void peer_push(struct sophia_sf_record r);
void peer_object(const struct sophia_sf_record *r);
int peer_fd(void);
unsigned peer_fetches(uint16_t kind);
unsigned peer_logged(void);
const struct sophia_sf_record *peer_log(unsigned i);
const struct sophia_sf_record *peer_last(uint16_t kind);
unsigned peer_count(uint16_t kind);
unsigned peer_chunks(void);
size_t peer_chunk_size(unsigned i);
uint64_t peer_ticket(uint16_t kind);
const uint8_t *peer_uploaded(size_t *bytes);

#define EPOCH 17u
#define CONTENT 3u
#define OUTPUT 11u
#define OUTPUT_GEN 12u
#define FACTS 3u
#define SCALE_GEN 4u
#define OPENING 7u
#define ALLOC 21u
#define ALLOC_GEN 22u
#define EPOCH_P 9u

struct ctx {
    struct bm_menu *menu;
    struct bm_sophia_files *f;
    uint64_t now;
    char dir[32], path[64];
    int listener;
};

static struct sophia_sf_limits
limits(void)
{
    struct sophia_sf_limits l;
    memset(&l, 0, sizeof(l));
    l.grant_connection_epoch = EPOCH;
    l.grant_content_epoch = CONTENT;
    l.limits_generation = 1;
    l.max_resource_bytes = 4194304;
    l.max_staging_bytes = 8388608;
    l.max_resident_bytes = l.max_retiring_bytes = 16777216;
    l.max_session_retiring_bytes = 67108864;
    l.pixel_format_mask = 1;
    l.max_frame_payload = 65536;
    l.max_chunk_bytes = 65488;
    l.max_width_px = 8192;
    l.max_height_px = 4096;
    l.max_live_resources = 8;
    l.max_resource_ids = 4096;
    l.max_open_transfers = 4;
    l.max_outputs = 16;
    l.max_popout_extent_px = 1024;
    l.max_candidate_targets = 64;
    l.max_candidate_bytes = 8192;
    l.max_content_coverage_percent = 50;
    l.max_pending_actions = 16;
    l.max_frames_per_service_tick = 16;
    l.max_scale_numerator = 32;
    l.max_scale_denominator = 4;
    l.allocation_timeout_ms = l.candidate_timeout_ms = l.preparation_timeout_ms = 1000;
    l.transfer_timeout_ms = l.presentation_timeout_ms = l.peer_write_timeout_ms = 2000;
    l.transfer_idle_timeout_ms = 500;
    l.action_ack_timeout_ms = 1000;
    l.permit_timeout_ms = 250;
    return l;
}

struct row {
    uint16_t slot, available;
    const char *label;
};

static void
catalog_object(uint64_t generation, const struct row *rows, unsigned count)
{
    static uint8_t encoded[8][656];
    struct sophia_sf_record r;
    assert(count <= 8);
    for (unsigned i = 0; i < count; ++i) {
        struct sophia_sf_catalog_entry e;
        memset(&e, 0, sizeof(e));
        e.slot = rows[i].slot;
        e.available = rows[i].available;
        e.label.data = (const uint8_t *)rows[i].label;
        e.label.size = (uint16_t)strlen(rows[i].label);
        assert(!sophia_sf_catalog_entry_encode(encoded[i], &e));
    }
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_CATALOG;
    r.value.catalog = (struct sophia_sf_catalog){1, EPOCH, generation, (uint16_t)count, 0,
                                                 encoded[0], (size_t)count * 656};
    peer_object(&r);
}

static void
outputs_object(uint64_t generation)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_OUTPUTS;
    r.value.outputs.transaction = 1;
    r.value.outputs.grant_connection_epoch = EPOCH;
    r.value.outputs.grant_content_epoch = CONTENT;
    r.value.outputs.facts_generation = generation;
    r.value.outputs.output_count = 1;
    r.value.outputs.outputs[0] = (struct sophia_sf_content_output_facts_entry){OUTPUT, OUTPUT_GEN, 1280, 800, 1, 1, SCALE_GEN};
    peer_object(&r);
}

static void
published(uint16_t kind, uint64_t generation)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_OBJECT_PUBLISHED;
    r.value.object_published = (struct sophia_sf_object_published){kind, generation, 0x40 + generation};
    peer_push(r);
}

static void
opening(uint64_t id, uint64_t catalog)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_NATIVE_OPENING;
    r.value.native_opening = (struct sophia_sf_native_opening){60, EPOCH, CONTENT, id, OUTPUT, OUTPUT_GEN, catalog, 1};
    peer_push(r);
}

static void
allocation_result(uint64_t request, uint32_t scale_generation)
{
    struct sophia_sf_record r;
    struct sophia_sf_allocation_result *v = &r.value.allocation_result;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_ALLOCATION_RESULT;
    v->transaction = 61;
    v->grant_connection_epoch = EPOCH;
    v->grant_content_epoch = CONTENT;
    v->allocation_request_id = request;
    v->status = 1;
    v->output_id = OUTPUT;
    v->output_generation = OUTPUT_GEN;
    v->allocation_id = ALLOC;
    v->allocation_generation = ALLOC_GEN;
    v->scale_generation = scale_generation;
    v->logical_x = v->pixel_x = 320;
    v->logical_y = v->pixel_y = 240;
    v->logical_width = v->pixel_width = 640;
    v->logical_height = v->pixel_height = 320;
    v->scale_numerator = v->scale_denominator = 1;
    peer_push(r);
}

static void
invalidated(void)
{
    struct sophia_sf_record r;
    struct sophia_sf_allocation_result *v = &r.value.allocation_result;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_ALLOCATION_RESULT;
    v->transaction = 68;
    v->grant_connection_epoch = EPOCH;
    v->grant_content_epoch = CONTENT;
    v->status = 4;
    v->reason = 12;
    v->output_id = OUTPUT;
    v->output_generation = OUTPUT_GEN;
    v->allocation_id = ALLOC;
    v->allocation_generation = ALLOC_GEN;
    peer_push(r);
}

static void
resource_status(uint64_t id, uint16_t status)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_RESOURCE_STATUS;
    r.value.resource_status = (struct sophia_sf_resource_status){62, EPOCH, CONTENT, id, 1, status,
                                                                 (uint16_t)(status > 2 ? 3 : 0), 0, 0};
    peer_push(r);
}

static void
released(uint64_t id)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_RESOURCE_RELEASED;
    r.value.resource_released = (struct sophia_sf_resource_released){69, EPOCH, CONTENT, id, 1, 0};
    peer_push(r);
}

static void
permit(uint64_t demand, uint16_t state, uint64_t id, uint32_t ttl)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_FRAME_PERMIT;
    r.value.frame_permit = (struct sophia_sf_frame_permit){63, EPOCH, CONTENT, OUTPUT, OUTPUT_GEN, demand, id, state,
        (uint16_t)(state == 1 ? 0 : 6), state == 1 ? ttl : 0, state == 1 ? 8192u : 0};
    peer_push(r);
}

static void
outcome(const struct sophia_sf_record *candidate, uint16_t kind)
{
    const struct sophia_sf_candidate *c = &candidate->value.role_candidate.content;
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_CANDIDATE_OUTCOME;
    r.value.candidate_outcome = (struct sophia_sf_candidate_outcome){c->transaction, EPOCH, CONTENT,
        c->candidate_generation, OUTPUT, OUTPUT_GEN, kind, (uint16_t)(kind > 2 ? 1 : 0),
        kind == 2 ? EPOCH_P : 0, 0, 0};
    peer_push(r);
}

static struct sophia_sf_native_binding
binding(uint64_t candidate, uint64_t revision, uint64_t lease)
{
    return (struct sophia_sf_native_binding){64, EPOCH, CONTENT, OPENING, OUTPUT, OUTPUT_GEN, ALLOC, ALLOC_GEN,
                                             1, candidate, EPOCH_P, 1, revision, lease};
}

static void
focus(struct sophia_sf_native_binding b)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_NATIVE_FOCUS;
    r.value.native_focus = b;
    peer_push(r);
}

static void
input(struct sophia_sf_native_binding b, uint64_t event, uint64_t revision, uint16_t kind, const char *text)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_NATIVE_INPUT;
    r.value.native_input.binding = b;
    r.value.native_input.event_id = event;
    r.value.native_input.state_revision = revision;
    r.value.native_input.issued_mono_usec = 5000000;
    r.value.native_input.kind = kind;
    r.value.native_input.text.data = (const uint8_t *)text;
    r.value.native_input.text.size = (uint16_t)strlen(text);
    peer_push(r);
}

static void
action(uint64_t generation, uint64_t event, uint16_t kind)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_ACTION;
    r.value.action = (struct sophia_sf_action){67, EPOCH, CONTENT, OUTPUT, OUTPUT_GEN, generation, EPOCH_P, 1,
        ALLOC, ALLOC_GEN, 0, 0, 0, event, kind, (uint16_t)(kind == 3 ? 1 : 0)};
    peer_push(r);
}

static void
closed(uint64_t id, uint16_t reason)
{
    struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_NATIVE_CLOSED;
    r.value.native_closed = (struct sophia_sf_native_closed){65, EPOCH, CONTENT, id, reason};
    peer_push(r);
}

static struct bm_menu *
fixture(void)
{
    static struct bm_renderer renderer;
    struct bm_menu *menu = calloc(1, sizeof(*menu));
    assert(menu);
    menu->renderer = &renderer;
    menu->key_binding = BM_KEY_BINDING_DEFAULT;
    menu->vim_mode = 'i';
    menu->filter_item = bm_item_new(NULL);
    assert(menu->filter_item && bm_menu_set_font(menu, "DejaVu Sans 14"));
    for (unsigned i = 0; i < BM_COLOR_LAST; ++i)
        assert(bm_menu_set_color(menu, i, NULL));
    bm_menu_set_lines(menu, 4);
    return menu;
}

static const struct row first_rows[] = {
    {1, 1, "Terminal"}, {2, 1, "Browser"}, {3, 1, "Files"}, {4, 0, "Hidden"}, {5, 1, "Editor"},
};

static void
pass(struct ctx *c, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        int r = bm_sophia_files_progress(c->f, POLLIN, ++c->now);
        if (r) {
            struct bm_sophia_files_snapshot s;
            assert(bm_sophia_files_inspect(c->f, &s));
            fprintf(stderr, "files test: progress=%d stage=%d\n", r, (int)s.failed);
        }
        assert(!r);
    }
}

/* A real same-user listening Unix socket: the SDK connector really connects;
 * the supplied session then stands in for everything above the fd. */
static void
start(struct ctx *c)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct sophia_sf_limits l = limits();
    memset(c, 0, sizeof(*c));
    c->now = 1000;
    strcpy(c->dir, "/tmp/bm-files-XXXXXX");
    assert(mkdtemp(c->dir));
    snprintf(c->path, sizeof(c->path), "%s/shell", c->dir);
    strcpy(address.sun_path, c->path);
    c->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(c->listener >= 0 && !bind(c->listener, (struct sockaddr *)&address, sizeof(address)));
    assert(!listen(c->listener, 4));
    peer_reset(&l);
    c->menu = fixture();
    assert(!bm_sophia_files_new(c->menu, c->path, c->now, &c->f));
    struct bm_sophia_files_snapshot s;
    for (unsigned i = 0; i < 50 && peer_fd() < 0; ++i)
        pass(c, 1);
    assert(peer_fd() >= 0 && bm_sophia_files_inspect(c->f, &s) && s.connected && !s.native);
    peer_ready();
    pass(c, 1);
    assert(bm_sophia_files_inspect(c->f, &s) && s.ready && s.native);
}

static void
stop(struct ctx *c)
{
    bm_sophia_files_dispose(c->f);
    uint32_t count;
    bm_menu_get_items(c->menu, &count);
    assert(!count); /* The adapter took back exactly the catalog it installed. */
    bm_menu_free(c->menu);
    close(c->listener);
    unlink(c->path);
    rmdir(c->dir);
}

static void
published_catalog(struct ctx *c)
{
    outputs_object(FACTS);
    catalog_object(1, first_rows, 5);
    published(SOPHIA_SF_OUTPUTS, FACTS);
    published(SOPHIA_SF_CATALOG, 1);
    pass(c, 6);
    uint32_t count;
    bm_menu_get_items(c->menu, &count);
    assert(count == 4 && peer_fetches(SOPHIA_SF_CATALOG) == 1 && peer_fetches(SOPHIA_SF_OUTPUTS) == 1);
}

/* Upload the next view: returns its begin record, after Accepted. */
static struct sophia_sf_resource_begin
uploaded(struct ctx *c)
{
    unsigned ends = peer_count(SOPHIA_SF_RESOURCE_END);
    const struct sophia_sf_record *begin = peer_last(SOPHIA_SF_RESOURCE_BEGIN);
    assert(begin);
    struct sophia_sf_resource_begin v = begin->value.resource_begin;
    resource_status(v.resource_id, 1);
    for (unsigned i = 0; i < 400 && peer_count(SOPHIA_SF_RESOURCE_END) == ends; ++i)
        pass(c, 1);
    assert(peer_count(SOPHIA_SF_RESOURCE_END) == ends + 1);
    /* Canonical chunks: whole row groups, the last one the remainder. */
    size_t rows = 65488 / ((size_t)v.width_px * 4), chunk = rows * v.width_px * 4, bytes;
    assert(peer_chunks() == v.chunk_count && v.chunk_count == (v.height_px + rows - 1) / rows);
    for (unsigned i = 0; i + 1 < peer_chunks(); ++i)
        assert(peer_chunk_size(i) == chunk);
    (void)peer_uploaded(&bytes);
    assert(bytes == v.total_bytes);
    resource_status(v.resource_id, 2);
    pass(c, 2);
    return v;
}

static void
same_pixels(struct bm_menu *menu, const struct sophia_sf_resource_begin *v, bool expect)
{
    struct bm_sophia_raster *raster = bm_sophia_raster_new(v->width_px, v->height_px, 1.0);
    struct bm_sophia_pixels pixels;
    size_t bytes;
    const uint8_t *sent = peer_uploaded(&bytes);
    assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
    assert(bytes == (size_t)pixels.stride * pixels.height);
    assert(!memcmp(pixels.data, sent, bytes) == expect);
    bm_sophia_raster_free(raster);
}

/* Demand, permit and one candidate; returns it once Presented and focused. */
/* In these flows every revision needs exactly one demand. */
static const struct sophia_sf_record *
presented(struct ctx *c, uint64_t revision, uint64_t lease)
{
    unsigned demands = (unsigned)revision;
    pass(c, 1);
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == demands);
    uint64_t demand = peer_last(SOPHIA_SF_FRAME_DEMAND)->value.frame_demand.demand_id;
    permit(demand, 1, 70 + demand, 200);
    pass(c, 1);
    const struct sophia_sf_record *candidate = peer_last(SOPHIA_SF_NATIVE_CANDIDATE);
    assert(candidate && candidate->value.role_candidate.content.pacing_permit == 70 + demand);
    assert(candidate->value.role_candidate.state_revision == revision);
    assert(!bm_sophia_files_progress(c->f, POLLIN, ++c->now));
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == demands); /* one candidate at a time */
    outcome(candidate, 1);
    pass(c, 1);
    struct bm_sophia_files_snapshot s;
    assert(bm_sophia_files_inspect(c->f, &s) && (revision == 1 ? !s.presented : true));
    outcome(candidate, 2);
    focus(binding(candidate->value.role_candidate.content.candidate_generation, revision, lease));
    pass(c, 2);
    assert(bm_sophia_files_inspect(c->f, &s) && s.presented && s.focused);
    return candidate;
}

/* Bootstrap, catalog, opening, allocation, the first upload and Presented. */
static const struct sophia_sf_record *
launcher(struct ctx *c, struct sophia_sf_resource_begin *first)
{
    start(c);
    published_catalog(c);
    opening(OPENING, 1);
    pass(c, 2);
    const struct sophia_sf_record *request = peer_last(SOPHIA_SF_NATIVE_ALLOCATION_REQUEST);
    assert(request && request->value.native_allocation_request.operation == 1 &&
           request->value.native_allocation_request.edge == 1 &&
           request->value.native_allocation_request.desired_width == 640 &&
           request->value.native_allocation_request.desired_height == 320);
    allocation_result(request->value.native_allocation_request.request_id, SCALE_GEN);
    pass(c, 2);
    const struct sophia_sf_record *begin = peer_last(SOPHIA_SF_RESOURCE_BEGIN);
    assert(begin && begin->value.resource_begin.width_px == 640 && begin->value.resource_begin.height_px == 320 &&
           begin->value.resource_begin.pixel_format == 1 && begin->value.resource_begin.total_bytes == 640u * 320u * 4u &&
           begin->value.resource_begin.rendered_scale_numerator == 1 &&
           begin->value.resource_begin.rendered_scale_denominator == 1);
    *first = uploaded(c);
    return presented(c, 1, 1);
}

static void
test_lifecycle(void)
{
    struct ctx c;
    struct sophia_sf_resource_begin first, second;
    struct bm_sophia_files_snapshot s;
    const struct sophia_sf_record *candidate = launcher(&c, &first);
    /* The upload carried Bemenu's own painted pixels of the Session catalog. */
    same_pixels(c.menu, &first, true);
    const struct expected_rows { uint16_t count, selected, rows[4]; } expect = {4, 1, {1, 2, 3, 5}};
    assert(candidate->value.role_candidate.row_count == expect.count &&
           candidate->value.role_candidate.selected == expect.selected &&
           !memcmp(candidate->value.role_candidate.rows, expect.rows, sizeof(expect.rows)));
    assert(candidate->value.role_candidate.content.placements[0].resource_id == first.resource_id);
    assert(candidate->value.role_candidate.content.interaction_generation == 1);
    for (unsigned i = 0; i < 4; ++i) {
        const struct sophia_sf_content_target *t = &candidate->value.role_candidate.content.targets[i];
        assert(t->action_kind == 2 && t->action_id == expect.rows[i] && t->bounds_width && t->bounds_height &&
               t->bounds_x >= 0 && t->bounds_y >= 0 && (uint32_t)t->bounds_x + t->bounds_width <= 640u &&
               (uint32_t)t->bounds_y + t->bounds_height <= 320u);
    }
    assert(bm_sophia_files_inspect(c.f, &s) && s.displayed == 5 && s.revision == 1);
    uint64_t g1 = candidate->value.role_candidate.content.candidate_generation;

    /* Input: no UI edit without reserved ack room; then exactly one edit and
     * one Consumed ack, after which the view is re-rendered and re-uploaded. */
    input(binding(g1, 1, 1), 10, 2, 1, "Fil");
    peer_busy(0, 1);
    pass(&c, 2);
    assert(bm_sophia_files_inspect(c.f, &s) && s.edits == 0 && !peer_count(SOPHIA_SF_NATIVE_INPUT_ACK));
    assert(!bm_menu_get_filter(c.menu) || !*bm_menu_get_filter(c.menu));
    peer_busy(0, 0);
    pass(&c, 2);
    const struct sophia_sf_record *ack = peer_last(SOPHIA_SF_NATIVE_INPUT_ACK);
    assert(bm_sophia_files_inspect(c.f, &s) && s.edits == 1 && s.revision == 2);
    assert(ack && ack->value.native_input_ack.event_id == 10 && ack->value.native_input_ack.disposition == 1);
    assert(!strcmp(bm_menu_get_filter(c.menu), "Fil"));
    assert(peer_last(SOPHIA_SF_RESOURCE_BEGIN)->value.resource_begin.resource_id == first.resource_id + 1);
    second = uploaded(&c);
    same_pixels(c.menu, &second, true);
    candidate = presented(&c, 2, 2);
    assert(candidate->value.role_candidate.row_count == 1 && candidate->value.role_candidate.rows[0] == 3 &&
           candidate->value.role_candidate.selected == 3);
    uint64_t g2 = candidate->value.role_candidate.content.candidate_generation;
    assert(g2 > g1 && bm_sophia_files_inspect(c.f, &s) && s.displayed == 2 && s.edits == 1);
    /* The superseded resource is retired once no longer shown, then released. */
    pass(&c, 1);
    assert(peer_last(SOPHIA_SF_RESOURCE_RETIRE)->value.resource_retire.resource_id == first.resource_id);
    released(first.resource_id);
    pass(&c, 1);
    assert(bm_sophia_files_inspect(c.f, &s) && s.views == 1);

    /* Actions: a cancellation owes nothing; a dismissal is acknowledged. */
    action(g2, 30, 3);
    pass(&c, 1);
    assert(!peer_count(SOPHIA_SF_ACTION_ACK));
    action(g2, 31, 2);
    pass(&c, 1);
    assert(peer_count(SOPHIA_SF_ACTION_ACK) == 1 &&
           peer_last(SOPHIA_SF_ACTION_ACK)->value.action_ack.disposition == 1 && !peer_count(SOPHIA_SF_NATIVE_ACTIVATE));

    /* Keyboard Accept: the activation precedes its ack in one commit. */
    unsigned before = peer_logged();
    input(binding(g2, 2, 2), 11, 2, 17, "");
    pass(&c, 1);
    assert(peer_logged() == before + 2);
    const struct sophia_sf_record *activate = peer_log(before);
    assert(activate->header.kind == SOPHIA_SF_NATIVE_ACTIVATE && activate->value.native_activate.cause == 1 &&
           activate->value.native_activate.slot == 3 && activate->value.native_activate.event_id == 11);
    assert(peer_log(before + 1)->header.kind == SOPHIA_SF_NATIVE_INPUT_ACK);
    /* The record union includes whole candidates. Keep this serial fixture's
     * storage off the stack, which also holds the by-value peer_push copy. */
    static struct sophia_sf_record r;
    memset(&r, 0, sizeof(r));
    r.header.kind = SOPHIA_SF_NATIVE_ACTIVATION_OUTCOME;
    r.value.native_activation_outcome.activation = activate->value.native_activate;
    r.value.native_activation_outcome.status = 1;
    peer_push(r);
    pass(&c, 1);
    assert(bm_sophia_files_inspect(c.f, &s) && !s.focused && s.edits == 1); /* Admitted disarms focus */
    /* A matching focus after Admitted stays disarmed. */
    focus(binding(g2, 2, 3));
    pass(&c, 1);
    assert(bm_sophia_files_inspect(c.f, &s) && !s.focused);

    /* Close: nothing presented; the allocation is not used again; the last
     * resource retires and releases; invalidation arrives later. */
    closed(OPENING, 11);
    pass(&c, 2);
    assert(bm_sophia_files_inspect(c.f, &s) && !s.open && !s.presented);
    assert(peer_last(SOPHIA_SF_RESOURCE_RETIRE)->value.resource_retire.resource_id == second.resource_id);
    invalidated();
    released(second.resource_id);
    pass(&c, 2);
    assert(bm_sophia_files_inspect(c.f, &s) && s.views == 0 && s.lifecycle == SOPHIA_NS_LIVE);
    unsigned logged = peer_logged();
    pass(&c, 3);
    assert(peer_logged() == logged); /* Idle: no demand, upload or allocation. */

    /* A final session ends the adapter cleanly; the result latches. */
    peer_end(SOPHIA_SS_CLOSED);
    assert(bm_sophia_files_progress(c.f, POLLIN, ++c.now) == SOPHIA_9P_CLOSED);
    assert(bm_sophia_files_progress(c.f, POLLIN, ++c.now) == SOPHIA_9P_CLOSED);
    struct pollfd p;
    int timeout;
    assert(bm_sophia_files_poll(c.f, c.now, &p, &timeout) == SOPHIA_9P_CLOSED && p.fd == -1);
    stop(&c);
}

/* A newer catalog during an opening is fetched at once (ack hold cleared) but
 * adopted only after that opening closes. */
static void
test_catalog_deferred(void)
{
    struct ctx c;
    static const struct row second_rows[] = {{1, 1, "Terminal"}, {6, 1, "Mail"}};
    start(&c);
    published_catalog(&c);
    opening(OPENING, 1);
    pass(&c, 2);
    catalog_object(2, second_rows, 2);
    published(SOPHIA_SF_CATALOG, 2);
    pass(&c, 3);
    uint32_t count;
    struct bm_sophia_files_snapshot s;
    bm_menu_get_items(c.menu, &count);
    assert(peer_fetches(SOPHIA_SF_CATALOG) == 2 && count == 4);
    assert(bm_sophia_files_inspect(c.f, &s) && s.catalog_generation == 1);
    closed(OPENING, 11);
    pass(&c, 4);
    bm_menu_get_items(c.menu, &count);
    assert(peer_fetches(SOPHIA_SF_CATALOG) == 3 && count == 2);
    assert(bm_sophia_files_inspect(c.f, &s) && s.catalog_generation == 2);
    stop(&c);
}

/* A permit judged expired locally allows no action: poll waits a bounded,
 * nonzero time (no spin, no demand) until the server's end re-arms demand. */
static void
test_permit_expiry(void)
{
    struct ctx c;
    struct bm_sophia_files_snapshot s;
    start(&c);
    published_catalog(&c);
    opening(OPENING, 1);
    pass(&c, 2);
    allocation_result(peer_last(SOPHIA_SF_NATIVE_ALLOCATION_REQUEST)->value.native_allocation_request.request_id,
                      SCALE_GEN);
    pass(&c, 2);
    (void)uploaded(&c);
    pass(&c, 1);
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == 1);
    /* An unfocused input still advances the revision: the view is stale. */
    input(binding(1, 1, 1), 10, 2, 1, "x");
    pass(&c, 1);
    assert(bm_sophia_files_inspect(c.f, &s) && s.revision == 2 && s.edits == 0);
    assert(peer_last(SOPHIA_SF_NATIVE_INPUT_ACK)->value.native_input_ack.disposition == 2);
    permit(1, 1, 71, 30);
    pass(&c, 1);
    assert(!peer_count(SOPHIA_SF_NATIVE_CANDIDATE));
    c.now += 40;
    pass(&c, 1);
    for (unsigned i = 0; i < 3; ++i) {
        struct pollfd p;
        int timeout;
        assert(!bm_sophia_files_poll(c.f, c.now, &p, &timeout));
        assert(timeout > 0 && timeout <= 50);
        pass(&c, 1);
    }
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == 1 && !peer_count(SOPHIA_SF_NATIVE_CANDIDATE));
    /* The revision-2 view uploads meanwhile; still no second demand. */
    (void)uploaded(&c);
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == 1);
    permit(1, 2, 71, 0);
    pass(&c, 2);
    assert(peer_count(SOPHIA_SF_FRAME_DEMAND) == 2);
    stop(&c);
}

/* One edit, its ack refused at submit: terminal, never re-edited. */
static void
test_lost_ack(void)
{
    struct ctx c;
    struct sophia_sf_resource_begin first;
    struct bm_sophia_files_snapshot s;
    const struct sophia_sf_record *candidate = launcher(&c, &first);
    input(binding(candidate->value.role_candidate.content.candidate_generation, 1, 1), 10, 2, 1, "F");
    pass(&c, 1);
    assert(bm_sophia_files_inspect(c.f, &s) && s.edits == 1 && peer_count(SOPHIA_SF_NATIVE_INPUT_ACK) == 1);
    peer_outcome(peer_ticket(SOPHIA_SF_NATIVE_INPUT_ACK), SOPHIA_SS_REFUSED);
    assert(bm_sophia_files_progress(c.f, POLLIN, ++c.now) == SOPHIA_9P_INVALID);
    assert(bm_sophia_files_progress(c.f, POLLIN, ++c.now) == SOPHIA_9P_INVALID);
    assert(bm_sophia_files_inspect(c.f, &s) && s.failed == BM_SOPHIA_FILES_STAGE_INPUT && s.edits == 1);
    assert(peer_count(SOPHIA_SF_NATIVE_INPUT_ACK) == 1);
    stop(&c);
}

/* Local failure guards: an unanswered allocation, a clock regression, and a
 * grant whose geometry does not match the requested facts. */
static void
test_guards(void)
{
    struct ctx c;
    struct bm_sophia_files_snapshot s;
    start(&c);
    published_catalog(&c);
    opening(OPENING, 1);
    pass(&c, 2);
    assert(peer_count(SOPHIA_SF_NATIVE_ALLOCATION_REQUEST) == 1);
    c.now += 4990;
    pass(&c, 1);
    c.now += 10;
    assert(bm_sophia_files_progress(c.f, POLLIN, c.now) == SOPHIA_9P_IO);
    assert(bm_sophia_files_inspect(c.f, &s) && s.failed == BM_SOPHIA_FILES_STAGE_TIMEOUT);
    stop(&c);

    start(&c);
    assert(bm_sophia_files_progress(c.f, POLLIN, c.now - 1) == SOPHIA_9P_INVALID);
    assert(bm_sophia_files_inspect(c.f, &s) && s.failed == BM_SOPHIA_FILES_STAGE_CLOCK);
    stop(&c);

    start(&c);
    published_catalog(&c);
    opening(OPENING, 1);
    pass(&c, 2);
    allocation_result(peer_last(SOPHIA_SF_NATIVE_ALLOCATION_REQUEST)->value.native_allocation_request.request_id,
                      SCALE_GEN + 1);
    assert(bm_sophia_files_progress(c.f, POLLIN, ++c.now) == SOPHIA_9P_INVALID);
    assert(bm_sophia_files_inspect(c.f, &s) && s.failed == BM_SOPHIA_FILES_STAGE_ALLOCATION);
    assert(!peer_count(SOPHIA_SF_RESOURCE_BEGIN));
    stop(&c);
}

int
main(void)
{
    test_lifecycle();
    test_catalog_deferred();
    test_permit_expiry();
    test_lost_ack();
    test_guards();
    puts("sophia files adapter unit: ok (supplied peer outcomes; not live or physical evidence)");
    return 0;
}
