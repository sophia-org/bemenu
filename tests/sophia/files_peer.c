/* Scripted stand-in for the public sophia_ss API, for UNIT tests of the Bemenu
 * file adapter only. It defines every sophia_ss_* symbol the adapter and the
 * SDK's native session use, so no shell_session member is linked; the real
 * sophia_ns and the real file codec run on top of it. Every scripted event and
 * object goes through sophia_sf_encode/decode. These are SUPPLIED peer
 * outcomes: no 9P transport, Session, compositor or display is involved, and
 * nothing here is live, physical or production evidence. */
#include "../../vendor/sophia-desktop-sdk/source/src/sophia_shell_session.h"
#include "../../vendor/sophia-desktop-sdk/source/src/sophia_shell_image.h"
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

int sophia_ss_open_attached(struct sophia_ss *s, struct sophia_9p_client *wire,
                            uint32_t root, const struct sophia_ss_config *config,
                            void *storage, size_t bytes)
{
    (void)s; (void)wire; (void)root; (void)config; (void)storage; (void)bytes;
    assert(!"supplied session fixture cannot authenticate");
    return SOPHIA_9P_INVALID;
}

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
    struct sophia_ss_image *image;
    uint64_t image_offer;
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
    if (r.header.kind == SOPHIA_SF_RESOURCE_STATUS && P.image) {
        const struct sophia_sf_resource_status *v = &r.value.resource_status;
        enum sophia_ip_status status = v->status == 2 ? SOPHIA_IPS_ACCEPTED :
                                       v->status == 3 ? SOPHIA_IPS_REJECTED :
                                       v->status == 4 ? SOPHIA_IPS_CANCELLED : 0;
        if (status)
            assert(!sophia_ip_resource_status(&P.image->producer, v->resource_id,
                                              v->resource_generation, status, 0, P.image->producer.last_now));
    }
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
    assert(c->offer.minimum_revision == 9 && c->offer.maximum_revision == 9 &&
           c->offer.required_capabilities == 0x29a0 && c->object_storage &&
           c->object_capacity == SOPHIA_SF_MAX_RECORD);
    P.fd = fd;
    s->files.profile = SOPHIA_SF_LAUNCHER;
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
    *out = (struct sophia_ss_reservation){s, P.epoch, ++P.serial, slots, bytes};
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

/* Supplied channel readiness and Source delivery for adapter unit tests.
 * Real producer admission validates the sealed fd; the executable fixture
 * separately exercises actual 9P discovery and SEQPACKET transport. */
int sophia_ss_image_init(struct sophia_ss_image *image, const struct sophia_ss_image_config *config)
{
    memset(image, 0, sizeof(*image));
    image->config = *config;
    return 0;
}
int sophia_ss_image_attach(struct sophia_ss *s, struct sophia_ss_image *image)
{
    struct sophia_ip_config config = image->config.producer;
    config.connection_epoch = P.epoch;
    config.role = SOPHIA_IC_ROLE_SHELL;
    config.revision = 9;
    config.granted_kinds = SOPHIA_IC_KIND_BIT_SEALED;
    assert(!sophia_ip_init(&image->producer, &config, 0));
    image->session = s;
    s->image = P.image = image;
    image->started = 1;
    image->producer.channel = SOPHIA_IPC_READY;
    image->producer.max_offers = SOPHIA_IP_MAX_OFFERS;
    image->producer.expect = (struct sophia_ic_expect){
        .role = SOPHIA_IC_ROLE_SHELL, .connection_epoch = P.epoch,
        .channel_generation = 1, .source_kinds = SOPHIA_IC_KIND_BIT_SEALED,
        .max_width = config.bounds.max_width, .max_height = config.bounds.max_height,
        .max_resource_bytes = config.bounds.max_resource_bytes,
    };
    return 0;
}
int sophia_ss_image_poll_fd(const struct sophia_ss_image *image) {(void)image; return -1;}
short sophia_ss_image_poll_events(const struct sophia_ss_image *image) {(void)image; return 0;}
int64_t sophia_ss_image_timeout(const struct sophia_ss_image *image, uint64_t now)
{(void)image; (void)now; return -1;}
int sophia_ss_image_dispatch(struct sophia_ss_image *image, short events, uint64_t now)
{(void)image; (void)events; (void)now; return 0;}
int sophia_ss_image_begin(struct sophia_ss_image *image, struct sophia_ic_metadata *m,
                          int fd, uint64_t now, uint64_t *offer, uint64_t *ticket)
{
    uint8_t body[SOPHIA_IC_METADATA_BYTES];
    if (P.submit_busy || P.reserved)
        return SOPHIA_IPR_BUSY;
    assert(m->admission_epoch == P.limits.grant_content_epoch);
    int result = sophia_ip_begin(&image->producer, m, fd, now, body, offer);
    if (result)
        return result;
    P.image_offer = *offer;
    assert(m->logical_bytes <= PEER_UPLOAD);
    assert(pread(fd, P.uploaded, m->logical_bytes, 0) == (ssize_t)m->logical_bytes);
    P.received = m->logical_bytes;
    for (unsigned i = 0; i < SOPHIA_IP_MAX_OFFERS; ++i)
        if (image->producer.slots[i].metadata.offer == *offer)
            image->producer.slots[i].state = SOPHIA_IPO_SENT;
    /* A test-local geometry summary, not a byte-upload wire record. */
    struct sophia_sf_record summary = {0};
    summary.header.kind = SOPHIA_IC_IMAGE_BEGIN;
    summary.value.resource_begin = (struct sophia_sf_resource_begin){
        .transaction = *offer, .resource_id = m->resource_id, .resource_generation = m->resource_generation,
        .width_px = m->width, .height_px = m->height, .total_bytes = m->logical_bytes,
        .pixel_format = 1, .rendered_scale_numerator = 1, .rendered_scale_denominator = 1,
    };
    *ticket = peer_log_record(&summary);
    return 0;
}
uint64_t peer_image_offer(void) {return P.image_offer;}
void peer_image_release(void)
{
    struct sophia_ip_offer_info info;
    struct sophia_ic_record record = {0};
    assert(!sophia_ip_offer(&P.image->producer, P.image_offer, &info));
    record.header.kind = SOPHIA_IC_SOURCE_RELEASED;
    record.header.epoch = P.epoch;
    record.value.released.offer = info.offer;
    record.value.released.resource_id = info.resource_id;
    record.value.released.resource_generation = info.resource_generation;
    record.value.released.disposition = SOPHIA_IC_RETAINED;
    assert(!sophia_ip_record(&P.image->producer, &record, P.image->producer.last_now));
}
