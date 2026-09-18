#include "connection_internal.h"
#include <stdlib.h>
#include <time.h>

int
bm_sophia_connection_new(struct bm_menu *menu, int fd, struct bm_sophia_connection **out)
{
    if (!menu || fd < 0 || !out)
        return SOPHIA_SHELL_ARGUMENT;
    uint32_t count;
    bm_menu_get_items(menu, &count);
    if (count)
        return SOPHIA_SHELL_INVALID;
    struct bm_sophia_connection *c = calloc(1, sizeof(*c));
    if (!c)
        return SOPHIA_SHELL_BUSY;
    c->menu = menu; c->fd = fd; c->next_transaction = 1;
    int result = sophia_shell_wire_init(&c->wire, fd, c->rx, sizeof(c->rx), c->unused_tx, sizeof(c->unused_tx));
    if (result == SOPHIA_SHELL_OK)
        result = sophia_shell_outbox_init(&c->outbox, 131072, 64, 1024, 4);
    uint8_t bytes[36]; size_t length;
    if (result == SOPHIA_SHELL_OK)
        result = sophia_shell_hello_encode(bytes, sizeof(bytes), (struct sophia_shell_hello){7,7,0x9a0}, &length);
    if (result == SOPHIA_SHELL_OK) {
        struct sophia_shell_outbound_frame hello = {bytes, length, SOPHIA_SHELL_OUTBOUND_CONTROL};
        result = sophia_shell_outbox_push(&c->outbox, &hello, 1);
    }
    if (result != SOPHIA_SHELL_OK) {
        sophia_shell_outbox_dispose(&c->outbox); free(c); return result;
    }
    *out = c;
    return SOPHIA_SHELL_OK;
}

static int terminal(struct bm_sophia_connection *c, int result)
{
    c->terminal = result;
    return result;
}

int
bm_sophia_connection_service(struct bm_sophia_connection *c)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        (uint64_t)now.tv_sec > (UINT64_MAX-999)/1000)
        return SOPHIA_SHELL_IO_ERROR;
    return bm_sophia_connection_service_at(c, (uint64_t)now.tv_sec*1000 + now.tv_nsec/1000000);
}

int
bm_sophia_connection_service_at(struct bm_sophia_connection *c, uint64_t now)
{
    if (!c)
        return SOPHIA_SHELL_ARGUMENT;
    if (c->terminal)
        return c->terminal;
    if (c->clock_seen && now < c->now_msec)
        return terminal(c, SOPHIA_SHELL_INVALID);
    c->clock_seen = true; c->now_msec = now;
    int checked = bm_sophia_connection_deadlines(c);
    if (checked < 0) return terminal(c, checked);
    /* One flush owner and one receive FIFO. Never use wire_queue/flush here. */
    unsigned count_before = c->outbox.count;
    size_t sent_before = count_before ? c->outbox.records[c->outbox.head].sent : 0;
    int r = sophia_shell_outbox_flush(&c->outbox, c->fd, 64 * 1024);
    if (count_before != c->outbox.count || (c->outbox.count &&
        sent_before != c->outbox.records[c->outbox.head].sent)) {
        if (c->write_progress == UINT64_MAX) return terminal(c,SOPHIA_SHELL_INVALID);
        ++c->write_progress;
    }
    if (r < 0 || r == SOPHIA_SHELL_CLOSED)
        return terminal(c, r);
    size_t remaining = 64 * 1024;
    unsigned frames = c->content ? c->limits.max_frames_per_service_tick : 16;
    if (frames > 16) frames = 16;
    for (unsigned i = 0; i < frames; ++i) {
        struct sophia_shell_frame frame;
        size_t before = c->wire.rx_used;
        r = sophia_shell_wire_receive(&c->wire, remaining, &frame);
        remaining -= c->wire.rx_used - before;
        if (r == SOPHIA_SHELL_AGAIN)
            break;
        if (r != SOPHIA_SHELL_FRAME)
            return terminal(c, r);
        r = bm_sophia_connection_receive(c, &frame);
        if (r == SOPHIA_SHELL_BUSY)
            return r;
        if (r != SOPHIA_SHELL_OK)
            return terminal(c, r);
        r = sophia_shell_wire_consume(&c->wire);
        if (r != SOPHIA_SHELL_OK)
            return terminal(c, r);
        /* A limits record may reduce the current visit's allowance. */
        if (c->content && c->limits.max_frames_per_service_tick < frames)
            frames = c->limits.max_frames_per_service_tick;
        if (!remaining)
            break;
    }
    r = bm_sophia_connection_schedule(c);
    if (r < 0) return terminal(c, r);
    checked = bm_sophia_connection_deadlines(c);
    return checked < 0 ? terminal(c, checked) : r;
}

bool
bm_sophia_connection_inspect(const struct bm_sophia_connection *c,
                             struct bm_sophia_connection_snapshot *out)
{
    if (!c || !out) return false;
    struct bm_sophia_connection_snapshot v = {
        .timeout = c->timeout, .welcomed = c->welcomed, .content = c->content, .catalog = c->model.generation != 0,
        .lifecycle = c->native != NULL, .connection_epoch = c->welcome.connection_epoch,
        .catalog_generation = c->model.generation, .facts_generation = c->facts.generation,
        .queued_records = c->outbox.count, .queued_bytes = c->outbox.bytes,
    };
    if (c->native && sophia_shell_native_lifecycle_inspect(c->native, &v.native) != SOPHIA_SHELL_OK)
        return false;
    *out = v; return true;
}

void
bm_sophia_connection_dispose(struct bm_sophia_connection *c)
{
    if (!c) return;
    sophia_shell_native_lifecycle_dispose(c->native);
    sophia_shell_upload_dispose(c->upload);
    sophia_shell_outbox_dispose(&c->outbox);
    bm_sophia_raster_free(c->raster);
    /* No replacement of externally modified menu ownership. Such modification
     * violates this connection's borrowing contract; never free foreign items. */
    bm_sophia_catalog_clear(&c->model);
    free(c);
}
