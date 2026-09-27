/* Scripted stand-in for the public sophia_ss API, for UNIT tests of the Bemenu
 * file adapter only. It defines every sophia_ss_* symbol the adapter and the
 * SDK's native session use, so no shell_session member is linked; the real
 * sophia_ns and the real file codec run on top of it. Every scripted event and
 * object goes through sophia_sf_encode/decode. These are SUPPLIED peer
 * outcomes: no 9P transport, Session, compositor or display is involved, and
 * nothing here is live, physical or production evidence. */
#include "../../vendor/sophia-desktop-sdk/source/src/sophia_shell_session.h"
#include <assert.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#define PEER_EVENTS 96u
#define PEER_LOG 128u
#define PEER_UPLOAD (4u * 1024u * 1024u)

struct peer_object {
    uint8_t bytes[8192];
    struct sophia_sf_record record;
    int present;
};
static struct {
    enum sophia_ss_state state;
    int final_status, have_limits, ready_on_dispatch, submit_busy, reserve_busy, reserved;
    int fd, object_requested;
    struct sophia_sf_limits limits;
    uint64_t epoch;
    struct sophia_sf_record events[PEER_EVENTS];
    uint8_t bytes[PEER_EVENTS][1024];
    unsigned pushed, head;
    uint64_t sequence, next_ticket, serial;
    uint16_t reserved_slots;
    size_t reserved_bytes;
    enum sophia_ss_outcome outcomes[512];
    struct sophia_sf_record log[PEER_LOG];
    unsigned logged, acks, fetches[5], chunks;
    struct peer_object objects[5];
    uint16_t request_kind;
    uint64_t request_generation;
    /* Upload: begin record, whether upload/N is writable, chunk in flight. */
    struct sophia_sf_resource_begin begin;
    int upload_pending, upload_ready;
    const uint8_t *chunk;
    size_t chunk_size, chunk_sent, write_step, received, chunk_sizes[64];
    uint8_t *uploaded;
} P;

static size_t
peer_encode(const struct sophia_sf_record *in)
{
    static uint8_t b[8192];
    struct sophia_sf_record r = *in;
    size_t n;
    r.header.epoch = P.epoch;
    r.header.submission = 1;
    r.header.sequence = 0;
    return sophia_sf_encode(b, sizeof(b), &r, &n) ? 0 : n;
}

static uint64_t
peer_log_record(const struct sophia_sf_record *r)
{
    assert(P.logged < PEER_LOG && P.next_ticket < 512);
    P.log[P.logged++] = *r;
    P.outcomes[P.next_ticket] = SOPHIA_SS_ADMITTED_LOCAL;
    return P.next_ticket++;
}

/* Test control, declared again in tests/sophia/files.c. */
void
peer_reset(const struct sophia_sf_limits *limits)
{
    free(P.uploaded);
    memset(&P, 0, sizeof(P));
    P.limits = *limits;
    P.epoch = limits->grant_connection_epoch;
    P.state = SOPHIA_SS_NEGOTIATING;
    P.final_status = SOPHIA_9P_CLOSED;
    P.next_ticket = 1;
    P.fd = -1;
    P.write_step = 7000;
    P.uploaded = malloc(PEER_UPLOAD);
    assert(P.uploaded);
}
void peer_ready(void) {P.ready_on_dispatch = 1;}
void peer_end(enum sophia_ss_state state) {P.state = state;}
void peer_busy(int submit, int reserve) {P.submit_busy = submit; P.reserve_busy = reserve;}
void peer_outcome(uint64_t ticket, enum sophia_ss_outcome o) {assert(ticket < 512); P.outcomes[ticket] = o;}
int peer_fd(void) {return P.fd;}
unsigned peer_fetches(uint16_t kind) {return kind <= 4 ? P.fetches[kind] : 0;}
unsigned peer_logged(void) {return P.logged;}
const struct sophia_sf_record *peer_log(unsigned i) {return i < P.logged ? &P.log[i] : NULL;}
unsigned peer_chunks(void) {return P.chunks;}
size_t peer_chunk_size(unsigned i) {return i < 64 ? P.chunk_sizes[i] : 0;}
/* Every logged record took the next ticket, from 1: log[i] is ticket i + 1. */
uint64_t
peer_ticket(uint16_t kind)
{
    for (unsigned i = P.logged; i--;)
        if (P.log[i].header.kind == kind)
            return i + 1;
    return 0;
}

const struct sophia_sf_record *
peer_last(uint16_t kind)
{
    for (unsigned i = P.logged; i--;)
        if (P.log[i].header.kind == kind)
            return &P.log[i];
    return NULL;
}

unsigned
peer_count(uint16_t kind)
{
    unsigned count = 0;
    for (unsigned i = 0; i < P.logged; ++i)
        count += P.log[i].header.kind == kind;
    return count;
}

/* The bytes received for the latest resource, once complete. */
const uint8_t *
peer_uploaded(size_t *bytes)
{
    *bytes = P.received;
    return P.uploaded;
}

/* Journal one event: encoded and decoded by the library; text borrows its slot.
 * Resource status drives the stand-in upload slot like the session's writer. */
void
peer_push(struct sophia_sf_record r)
{
    size_t n;
    assert(P.pushed < PEER_EVENTS);
    r.header.epoch = P.epoch;
    r.header.submission = 0;
    r.header.sequence = ++P.sequence;
    assert(!sophia_sf_encode(P.bytes[P.pushed], sizeof(P.bytes[0]), &r, &n));
    assert(!sophia_sf_decode(P.bytes[P.pushed], n, &P.events[P.pushed]));
    P.pushed++;
    if (r.header.kind == SOPHIA_SF_RESOURCE_STATUS && P.upload_pending &&
        r.value.resource_status.resource_id == P.begin.resource_id) {
        if (r.value.resource_status.status == 1) {
            P.upload_ready = 1;
        } else {
            P.upload_pending = P.upload_ready = 0;
            P.chunk = NULL;
        }
    }
}

/* A fetchable snapshot object, validated through the codec. */
void
peer_object(const struct sophia_sf_record *in)
{
    struct peer_object *o = &P.objects[in->header.kind];
    struct sophia_sf_record r = *in;
    size_t n;
    assert(in->header.kind >= 1 && in->header.kind <= 4);
    r.header.epoch = P.epoch;
    r.header.submission = r.header.sequence = 0;
    assert(!sophia_sf_encode(o->bytes, sizeof(o->bytes), &r, &n));
    assert(!sophia_sf_decode(o->bytes, n, &o->record));
    o->present = 1;
}

size_t
sophia_ss_storage_bytes(uint32_t msize, size_t queue_bytes)
{
    return msize && queue_bytes ? 64 : 0;
}

int
sophia_ss_open_fd(struct sophia_ss *s, int fd, const struct sophia_ss_config *c, void *storage, size_t bytes)
{
    (void)s;
    assert(fd >= 0 && storage && bytes == 64 && c->profile == SOPHIA_SF_LAUNCHER);
    assert(c->offer.minimum_revision == 7 && c->offer.maximum_revision == 7 &&
           c->offer.required_capabilities == 0x9a0 && c->object_storage &&
           c->object_capacity == SOPHIA_SF_MAX_RECORD);
    P.fd = fd;
    P.state = SOPHIA_SS_NEGOTIATING;
    return 0;
}

int sophia_ss_poll_fd(const struct sophia_ss *s) {(void)s; return P.fd;}
short sophia_ss_poll_events(const struct sophia_ss *s) {(void)s; return P.state > SOPHIA_SS_READY ? 0 : POLLIN;}
int sophia_ss_timeout(const struct sophia_ss *s, uint64_t now) {(void)s; (void)now; return -1;}
enum sophia_ss_state sophia_ss_state(const struct sophia_ss *s) {(void)s; return P.state;}
uint64_t sophia_ss_epoch(const struct sophia_ss *s) {(void)s; return P.state == SOPHIA_SS_READY ? P.epoch : 0;}

const struct sophia_sf_limits *
sophia_ss_limits(const struct sophia_ss *s)
{
    (void)s;
    return P.have_limits ? &P.limits : NULL;
}

/* Short writes: at most write_step bytes of the borrowed chunk per pass. */
int
sophia_ss_dispatch(struct sophia_ss *s, short revents, size_t budget, uint64_t now)
{
    (void)s;
    (void)revents;
    (void)now;
    assert(budget == 65536);
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (P.ready_on_dispatch && P.state == SOPHIA_SS_NEGOTIATING) {
        P.state = SOPHIA_SS_READY;
        P.have_limits = 1;
    }
    if (P.chunk) {
        size_t n = P.chunk_size - P.chunk_sent;
        if (n > P.write_step)
            n = P.write_step;
        assert(P.received + n <= PEER_UPLOAD);
        memcpy(P.uploaded + P.received, P.chunk + P.chunk_sent, n);
        P.received += n;
        P.chunk_sent += n;
        if (P.chunk_sent == P.chunk_size) {
            P.chunk = NULL;
            P.upload_ready = 1;
        }
    }
    return 0;
}

int sophia_ss_ack(struct sophia_ss *s) {(void)s; P.acks++; return P.state > SOPHIA_SS_READY ? P.final_status : 0;}
void sophia_ss_close(struct sophia_ss *s) {(void)s; if (P.state <= SOPHIA_SS_READY) P.state = SOPHIA_SS_CLOSED;}
size_t sophia_ss_record_bytes(const struct sophia_sf_record *r) {return r ? peer_encode(r) : 0;}

int
sophia_ss_submit(struct sophia_ss *s, const struct sophia_sf_record *records, size_t count, uint64_t *first)
{
    (void)s;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (P.submit_busy || P.reserved)
        return SOPHIA_9P_BUSY;
    for (size_t i = 0; i < count; ++i)
        if (!peer_encode(&records[i]))
            return SOPHIA_9P_INVALID;
    *first = P.next_ticket;
    for (size_t i = 0; i < count; ++i)
        (void)peer_log_record(&records[i]);
    return 0;
}

int
sophia_ss_reserve(struct sophia_ss *s, uint16_t slots, size_t bytes, struct sophia_ss_reservation *out)
{
    (void)s;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (P.reserve_busy || P.reserved)
        return SOPHIA_9P_BUSY;
    P.reserved = 1;
    P.reserved_slots = slots;
    P.reserved_bytes = bytes;
    *out = (struct sophia_ss_reservation){++P.serial, slots, bytes};
    return 0;
}

int
sophia_ss_commit(struct sophia_ss *s, struct sophia_ss_reservation *v, const struct sophia_sf_record *records,
                 size_t count, uint64_t *first)
{
    size_t bytes = 0;
    (void)s;
    assert(P.reserved && v->serial == P.serial);
    if (P.state > SOPHIA_SS_READY) {
        P.reserved = 0;
        return P.final_status;
    }
    for (size_t i = 0; i < count; ++i) {
        size_t n = peer_encode(&records[i]);
        if (!n)
            return SOPHIA_9P_INVALID;
        bytes += n;
    }
    if (count > P.reserved_slots || bytes > P.reserved_bytes)
        return SOPHIA_9P_ARGUMENT;
    *first = P.next_ticket;
    for (size_t i = 0; i < count; ++i)
        (void)peer_log_record(&records[i]);
    P.reserved = 0;
    memset(v, 0, sizeof(*v));
    return 0;
}

int
sophia_ss_cancel(struct sophia_ss *s, struct sophia_ss_reservation *v)
{
    (void)s;
    if (!P.reserved || v->serial != P.serial)
        return SOPHIA_9P_ARGUMENT;
    P.reserved = 0;
    return 0;
}

int
sophia_ss_outcome(const struct sophia_ss *s, uint64_t ticket, enum sophia_ss_outcome *out, uint32_t *error)
{
    (void)s;
    if (!ticket || ticket >= P.next_ticket)
        return SOPHIA_9P_ARGUMENT;
    *out = P.outcomes[ticket];
    if (error)
        *error = 0;
    return 0;
}

int
sophia_ss_event(struct sophia_ss *s, const struct sophia_sf_record **out)
{
    (void)s;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (P.head == P.pushed)
        return SOPHIA_9P_AGAIN;
    *out = &P.events[P.head];
    return 0;
}

int
sophia_ss_consume(struct sophia_ss *s)
{
    (void)s;
    assert(P.head < P.pushed);
    P.head++;
    return 0;
}

int
sophia_ss_object(struct sophia_ss *s, uint16_t kind, uint64_t generation, uint64_t qid)
{
    (void)s;
    (void)qid;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    assert(!P.object_requested && kind >= 1 && kind <= 4);
    P.object_requested = 1;
    P.request_kind = kind;
    P.request_generation = generation;
    P.fetches[kind]++;
    return 0;
}

static uint64_t
object_generation(const struct sophia_sf_record *r)
{
    switch (r->header.kind) {
    case SOPHIA_SF_OUTPUTS:
        return r->value.outputs.facts_generation;
    case SOPHIA_SF_CATALOG:
        return r->value.catalog.generation;
    default:
        return r->value.limits.limits_generation;
    }
}

/* The object currently published for that kind; an older request is superseded. */
int
sophia_ss_object_result(struct sophia_ss *s, const struct sophia_sf_record **out)
{
    (void)s;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (!P.object_requested)
        return SOPHIA_9P_ARGUMENT;
    P.object_requested = 0;
    const struct peer_object *o = &P.objects[P.request_kind];
    if (!o->present || object_generation(&o->record) != P.request_generation)
        return SOPHIA_9P_AGAIN;
    *out = &o->record;
    return 0;
}

static int
upload_record(uint16_t kind, uint64_t *ticket)
{
    struct sophia_sf_record r;
    if (P.state > SOPHIA_SS_READY)
        return P.final_status;
    if (P.reserved || P.submit_busy)
        return SOPHIA_9P_BUSY;
    memset(&r, 0, sizeof(r));
    r.header.kind = kind;
    r.value.resource_begin = P.begin;
    *ticket = peer_log_record(&r);
    return 0;
}

int
sophia_ss_upload_begin(struct sophia_ss *s, struct sophia_sf_resource_begin v, uint64_t *ticket)
{
    (void)s;
    if (P.upload_pending)
        return SOPHIA_9P_BUSY;
    assert(v.transaction && v.grant_connection_epoch == P.limits.grant_connection_epoch &&
           v.grant_content_epoch == P.limits.grant_content_epoch && v.slot < P.limits.max_open_transfers);
    P.begin = v;
    int r = upload_record(SOPHIA_SF_RESOURCE_BEGIN, ticket);
    if (!r) {
        P.upload_pending = 1;
        P.upload_ready = 0;
        P.received = 0;
        P.chunks = 0;
    }
    return r;
}

int
sophia_ss_upload_end(struct sophia_ss *s, uint64_t transaction, uint64_t *ticket)
{
    (void)s;
    assert(transaction && P.upload_ready && P.received == P.begin.total_bytes);
    P.upload_ready = 0;
    return upload_record(SOPHIA_SF_RESOURCE_END, ticket);
}

int
sophia_ss_upload_cancel(struct sophia_ss *s, uint64_t transaction, uint64_t *ticket)
{
    (void)s;
    assert(transaction && P.upload_ready);
    P.upload_ready = 0;
    return upload_record(SOPHIA_SF_RESOURCE_CANCEL, ticket);
}

int
sophia_ss_upload_chunk(struct sophia_ss *s, const void *bytes, size_t count)
{
    (void)s;
    if (!bytes || !count)
        return SOPHIA_9P_ARGUMENT;
    if (!P.upload_ready)
        return SOPHIA_9P_BUSY;
    if (count > P.begin.total_bytes - P.received)
        return SOPHIA_9P_ARGUMENT;
    if (P.chunks < 64)
        P.chunk_sizes[P.chunks] = count;
    P.chunks++;
    P.chunk = bytes;
    P.chunk_size = count;
    P.chunk_sent = 0;
    P.upload_ready = 0;
    return 0;
}

int sophia_ss_upload_ready(const struct sophia_ss *s) {(void)s; return P.state <= SOPHIA_SS_READY && P.upload_ready;}
int sophia_ss_upload_pending(const struct sophia_ss *s) {(void)s; return P.state <= SOPHIA_SS_READY && P.upload_pending;}
