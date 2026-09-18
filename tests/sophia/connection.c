#include "internal.h"
#include "renderers/sophia/connection_internal.h"
#include "../../vendor/sophia-shell/shell_wire/fields.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint8_t limits_frame[288], record[2048], reply[8192];
static int peers[2];
static struct bm_sophia_connection *connection;
static struct bm_menu *menu;
static unsigned refuse_lifecycle;
static unsigned refuse_commit;
static uint64_t now_msec;
int __real_sophia_shell_native_lifecycle_new(const struct sophia_shell_frame *, uint64_t,
    struct sophia_shell_outbox *, struct sophia_shell_native_lifecycle **);
int __wrap_sophia_shell_native_lifecycle_new(const struct sophia_shell_frame *f, uint64_t g,
    struct sophia_shell_outbox *o, struct sophia_shell_native_lifecycle **out)
{
    if (refuse_lifecycle) {--refuse_lifecycle; return SOPHIA_SHELL_BUSY;}
    return __real_sophia_shell_native_lifecycle_new(f, g, o, out);
}
int __real_sophia_shell_outbox_commit(struct sophia_shell_outbox *, struct sophia_shell_outbox_reservation,
    const struct sophia_shell_outbound_frame *, unsigned);
int __wrap_sophia_shell_outbox_commit(struct sophia_shell_outbox *o, struct sophia_shell_outbox_reservation t,
    const struct sophia_shell_outbound_frame *f, unsigned n)
{
    if (refuse_commit) {--refuse_commit; return SOPHIA_SHELL_BUSY;}
    return __real_sophia_shell_outbox_commit(o, t, f, n);
}

static void send_frame(uint16_t kind, uint64_t tx, const uint8_t *p, size_t n)
{
    size_t length;
    assert(sophia_shell_frame_encode(record, sizeof(record), kind, tx, p, n, &length) == 0);
    assert(send(peers[1], record, length, MSG_NOSIGNAL) == (ssize_t)length);
}
static struct bm_sophia_connection_snapshot inspect(void)
{
    struct bm_sophia_connection_snapshot v;
    assert(bm_sophia_connection_inspect(connection, &v)); return v;
}
static void tick(void)
{
    int r = bm_sophia_connection_service_at(connection,now_msec++);
    if (r != SOPHIA_SHELL_AGAIN && r != SOPHIA_SHELL_BUSY)
        fprintf(stderr,"connection tick result=%d kind=%u clock=%llu\n",r,connection->wire.rx_used >= 8 ? shell_get16(connection->wire.rx+6) : 0,(unsigned long long)now_msec);
    assert(r == SOPHIA_SHELL_AGAIN || r == SOPHIA_SHELL_BUSY);
}
static void drain(void)
{
    while (recv(peers[1], reply, sizeof(reply), MSG_DONTWAIT) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
}
static void setup(void)
{
    now_msec = 1000;
    static struct bm_renderer renderer;
    menu = calloc(1, sizeof(*menu)); assert(menu); menu->renderer = &renderer;
    menu->filter_item = bm_item_new(NULL); assert(menu->filter_item);
    assert(bm_menu_set_font(menu, "DejaVu Sans 14"));
    for (unsigned i = 0; i < BM_COLOR_LAST; ++i) assert(bm_menu_set_color(menu, i, NULL));
    bm_menu_set_lines(menu, 4);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peers) == 0);
    assert(bm_sophia_connection_new(menu, peers[0], &connection) == 0);
    tick();
    assert(recv(peers[1], reply, sizeof(reply), MSG_DONTWAIT) == 36);
    struct sophia_shell_frame hello;
    assert(sophia_shell_frame_decode(reply, 36, &hello) == 0 && hello.kind == 96);
    assert(shell_get16(hello.payload) == 7 && shell_get64(hello.payload + 4) == 0x9a0);
}
static void welcome(void)
{
    uint8_t p[28] = {0}; shell_put16(p, 7); shell_put64(p+4, 11); shell_put64(p+12, 0x9a0);
    shell_put16(p+20, 16); shell_put16(p+22, 128); shell_put16(p+24, 16);
    send_frame(97, 0, p, sizeof(p)); tick(); assert(inspect().welcomed);
}
static void limits(void)
{
    assert(send(peers[1], limits_frame, sizeof(limits_frame), MSG_NOSIGNAL) == sizeof(limits_frame));
    tick(); assert(inspect().content);
}
static void catalog_begin(uint64_t gen, uint16_t count)
{
    uint8_t p[20] = {0}; shell_put64(p, 11); shell_put64(p+8, gen); shell_put16(p+16, count);
    send_frame(114, gen, p, sizeof(p));
}
static void catalog_entry(uint64_t gen, uint16_t slot, const char *label, const char *keywords)
{
    uint8_t p[408] = {0}; size_t n = strlen(label), k = strlen(keywords);
    shell_put64(p, 11); shell_put64(p+8, gen); shell_put16(p+16, slot); shell_put16(p+18, 1);
    shell_put16(p+20, n); memcpy(p+22, label, n); shell_put16(p+22+n, k); memcpy(p+24+n, keywords, k);
    send_frame(115, gen, p, 24+n+k);
}
static void catalog_end(uint64_t gen)
{
    uint8_t p[16] = {0}; shell_put64(p, 11); shell_put64(p+8, gen); send_frame(116, gen, p, sizeof(p));
}
static void catalog(void)
{
    catalog_begin(9, 2); catalog_entry(9, 7, "Browser", "web"); catalog_entry(9, 3, "Editor", "code");
    catalog_end(9); tick(); assert(inspect().catalog_generation == 9);
}
static void teardown(void)
{
    close(peers[0]); close(peers[1]); bm_sophia_connection_dispose(connection);
    uint32_t count; bm_menu_get_items(menu, &count); assert(!count); bm_menu_free(menu);
}
static void grant(uint8_t *p) {shell_put64(p, 11); shell_put64(p+8, 3);}
static void opening(void)
{
    uint8_t p[56] = {0}; grant(p); shell_put64(p+16, 1); shell_put64(p+24, 2);
    shell_put64(p+32, 7); shell_put64(p+40, 9); shell_put64(p+48, 1);
    send_frame(187, 50, p, sizeof(p)); tick(); assert(inspect().native.open);
}
static void supplied_presentation(void)
{
    /* Exercise connection FIFO/menu delegation, not the future frame scheduler:
     * allocation, resident resource, permit and native outcomes are supplied. */
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1);
    struct bm_sophia_view view; assert(raster && bm_sophia_view_capture(raster, &connection->model, &view));
    assert(view.row_count == 2 && view.selected == 7);
    struct sophia_shell_native_candidate begin = {
        .grant = {11,3}, .output = {2,7}, .facts_generation = 1, .pacing_permit = 2,
        .interaction_generation = 1, .placement_count = 1, .opening = 1,
        .catalog_generation = view.catalog_generation, .state_revision = 1,
        .selected = view.selected, .row_count = view.row_count,
    };
    struct sophia_shell_native_chunk chunk = {
        .grant = {11,3}, .surface_count = 1, .placement_count = 1, .target_count = view.row_count,
        .surface = {{1,2},1,1,{0}}, .placements = {{{9,1},0,0}},
    };
    for (unsigned i = 0; i < view.row_count; ++i) {
        const struct bm_sophia_view_row *row = &view.rows[i]; begin.rows[i] = row->slot;
        chunk.targets[i] = (struct sophia_shell_native_target){i+1,1,row->slot,row->x,row->y,row->width,row->height};
    }
    assert(sophia_shell_native_lifecycle_offer(connection->native, &begin, &chunk, &connection->next_transaction) == 0);
    assert(sophia_shell_native_lifecycle_pump(connection->native, &connection->next_transaction) == 0);
    tick(); drain(); bm_sophia_raster_free(raster);
    uint8_t p[68] = {0}; grant(p); shell_put64(p+16, 1); shell_put64(p+24, 2); shell_put64(p+32, 7);
    shell_put16(p+40, 1); send_frame(175, 1, p, sizeof(p));
    shell_put16(p+40, 2); shell_put64(p+44, 10); send_frame(175, 1, p, sizeof(p)); tick();
    assert(inspect().native.presented && !inspect().native.focused);
}
static void binding(uint8_t *p)
{
    uint64_t values[] = {11,3,1,2,7,1,2,9,1,10,1,1,1};
    for (unsigned i = 0; i < 13; ++i) shell_put64(p+8*i, values[i]);
}
static void real_menu_input(void)
{
    setup(); welcome(); limits(); catalog(); opening(); supplied_presentation();
    uint8_t focus[104]; binding(focus); send_frame(191, 60, focus, sizeof(focus));
    uint8_t input[135] = {0}; binding(input); shell_put64(input+104, 1); shell_put64(input+112, 2);
    shell_put64(input+120, 10); shell_put16(input+128, 1); shell_put16(input+130, 3); memcpy(input+132, "web", 3);
    send_frame(193, 61, input, sizeof(input)); refuse_commit = 1; tick();
    assert(inspect().native.state_revision == 2 && menu->filter && !strcmp(menu->filter, "web"));
    assert(connection->wire.rx_used == sizeof(input)+24 && connection->outbox.reservation_count == 1);
    tick(); /* Original input retries its response, not its completed edit. */
    assert(!strcmp(menu->filter, "web") && !connection->wire.rx_used && !connection->outbox.reservation_count);
    uint32_t count; struct bm_item **items = bm_menu_get_filtered_items(menu, &count);
    assert(count == 1 && !strcmp(bm_item_get_text(items[0]), "Browser"));
    tick(); ssize_t n = recv(peers[1], reply, sizeof(reply), MSG_DONTWAIT);
    assert(n == 148); struct sophia_shell_frame ack;
    assert(sophia_shell_frame_decode(reply, n, &ack) == 0 && ack.kind == 194 && shell_get16(ack.payload+120) == 1);
    send_frame(193, 61, input, sizeof(input));
    assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID);
    assert(!strcmp(menu->filter, "web")); teardown();
}
static void bounded_catalog_and_order(void)
{
    setup(); welcome(); catalog(); assert(!inspect().lifecycle); limits(); assert(inspect().lifecycle);
    catalog_begin(10, 20);
    for (unsigned i = 0; i < 20; ++i) catalog_entry(10, i+1, "Entry", "");
    catalog_end(10); tick();
    assert(inspect().catalog_generation == 9); /* Incomplete after the bounded visit. */
    tick(); assert(inspect().catalog_generation == 10);
    teardown();
    setup();
    uint8_t p[28] = {0}; shell_put16(p, 6); shell_put64(p+4, 11); shell_put64(p+12, 0x603);
    shell_put16(p+20, 16); shell_put16(p+22, 128); shell_put16(p+24, 16); send_frame(97, 0, p, sizeof(p));
    assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID && !inspect().welcomed);
    teardown();
}
static void retained_catalog_end(void)
{
    setup(); welcome(); limits();
    catalog_begin(9, 2); catalog_entry(9, 7, "Browser", "web"); catalog_entry(9, 3, "Editor", "code");
    catalog_end(9);
    uint8_t p[56] = {0}; grant(p); shell_put64(p+16, 1); shell_put64(p+24, 2);
    shell_put64(p+32, 7); shell_put64(p+40, 9); shell_put64(p+48, 1);
    send_frame(187, 50, p, sizeof(p));
    refuse_lifecycle = 1;
    assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_BUSY);
    assert(inspect().catalog_generation == 9 && !inspect().lifecycle);
    assert(connection->catalog_pending && connection->wire.rx_used == 40);
    assert(shell_get16(connection->wire.rx+6) == 116);
    uint32_t count; struct bm_item **items = bm_menu_get_items(menu, &count);
    assert(count == 2); struct bm_item *first = items[0];
    tick(); assert(inspect().lifecycle && inspect().native.open && !connection->catalog_pending);
    items = bm_menu_get_items(menu, &count); assert(count == 2 && items[0] == first);
    teardown();
    setup(); welcome();
    uint8_t bad[288]; memcpy(bad, limits_frame, sizeof(bad)); shell_put64(bad+24, 12);
    assert(send(peers[1], bad, sizeof(bad), MSG_NOSIGNAL) == sizeof(bad));
    assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID && !inspect().content);
    teardown();
}
static struct sophia_shell_frame client_record(void)
{
    static uint8_t bytes[SOPHIA_SHELL_MAX_FRAME_BYTES];
    size_t used = 0, need = 24;
    for (unsigned turn = 0; turn < 200; ++turn) {
        tick();
        ssize_t n = recv(peers[1], bytes+used, need-used, MSG_DONTWAIT);
        if (n < 0) {assert(errno == EAGAIN || errno == EWOULDBLOCK); continue;}
        assert(n > 0); used += n;
        if (used == 24) {need = 24 + shell_get32(bytes+16); assert(need <= sizeof(bytes));}
        if (used == need) {
            struct sophia_shell_frame f;
            assert(sophia_shell_frame_decode(bytes,used,&f) == 0); return f;
        }
    }
    abort();
}
static void facts_scaled(unsigned n, unsigned d)
{
    uint8_t p[72] = {0}; grant(p); shell_put64(p+16,1); shell_put32(p+24,1);
    shell_put64(p+32,2); shell_put64(p+40,7); shell_put32(p+48,1280); shell_put32(p+52,720);
    shell_put32(p+56,n); shell_put32(p+60,d); shell_put64(p+64,1); send_frame(162,70,p,sizeof(p)); tick();
}
static void facts(void) {facts_scaled(1,1);}
static void allocation_payload(uint8_t *p)
{
    memset(p,0,160); grant(p); shell_put64(p+16,1); shell_put16(p+24,1);
    shell_put64(p+32,2); shell_put64(p+40,7); shell_put64(p+48,4); shell_put64(p+56,1);
    shell_put64(p+80,1); shell_put32(p+96,640); shell_put32(p+100,320);
    shell_put32(p+112,640); shell_put32(p+116,320); shell_put32(p+120,1); shell_put32(p+124,1);
}
static void automatic_allocation_and_upload(unsigned mode)
{
    setup(); welcome(); limits(); catalog(); facts(); opening();
    struct sophia_shell_frame request = client_record();
    assert(request.kind == 188 && request.transaction == 1 && connection->allocation_pending);
    assert(shell_get64(request.payload+16) == 1 && shell_get64(request.payload+24) == 2 &&
           shell_get64(request.payload+32) == 7 && shell_get32(request.payload+68) == 640 && shell_get32(request.payload+72) == 320);
    uint8_t p[160]; allocation_payload(p);
    send_frame(164,1,p,sizeof(p)); tick();
    assert(connection->allocation_valid && !connection->allocation_pending && connection->views[0].valid);
    assert(!connection->views[0].view.data && connection->views[0].revision == 1 && connection->views[0].view.selected == 7);
    struct bm_sophia_view view; assert(bm_sophia_view_capture(connection->raster,&connection->model,&view));
    size_t bytes = (size_t)view.stride*view.height;
    uint8_t *expected = malloc(bytes); assert(expected); memcpy(expected,view.data,bytes);
    /* Repainting the same mutable Cairo image cannot change the staged upload. */
    assert(bm_menu_set_color(menu,BM_COLOR_FILTER_BG,"#AABBCC"));
    assert(bm_sophia_view_capture(connection->raster,&connection->model,&view));
    assert(memcmp(expected,view.data,bytes));
    struct sophia_shell_frame begin = client_record();
    assert(begin.kind == 165 && begin.transaction == 2);
    assert(shell_get32(begin.payload+32) == 640 && shell_get32(begin.payload+36) == 320);
    uint8_t key[32]; memcpy(key,begin.payload,sizeof(key));
    uint32_t chunks = shell_get32(begin.payload+52);
    assert(shell_get64(begin.payload+56) == bytes);
    uint8_t status[48] = {0}; memcpy(status,key,sizeof(key)); shell_put16(status+32,1); shell_put64(status+40,bytes);
    send_frame(166,begin.transaction,status,sizeof(status)); tick();
    size_t copied = 0;
    for (unsigned ordinal = 0; ordinal < chunks; ++ordinal) {
        struct sophia_shell_frame chunk = client_record();
        assert(chunk.kind == 167 && !memcmp(chunk.payload,key,sizeof(key)) && shell_get32(chunk.payload+32) == ordinal);
        size_t size = shell_get32(chunk.payload+36);
        assert(shell_get64(chunk.payload+40) == copied && copied+size <= bytes);
        assert(!memcmp(chunk.payload+48,expected+copied,size)); copied += size;
    }
    struct sophia_shell_frame end = client_record();
    assert(end.kind == 168 && copied == bytes && shell_get64(end.payload+32) == bytes);
    shell_put16(status+32,2); send_frame(166,end.transaction,status,sizeof(status)); tick();
    struct sophia_shell_upload_snapshot upload;
    assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RESIDENT);
    struct sophia_shell_frame demand = client_record();
    assert(demand.kind == 176 && shell_get64(demand.payload+48) == 1);
    uint64_t demand_tx = demand.transaction;
    assert(connection->demand_pending && !connection->candidate_active && !inspect().native.presented);
    uint8_t permit[64] = {0}; grant(permit); shell_put64(permit+16,2); shell_put64(permit+24,7);
    shell_put64(permit+32,1); shell_put64(permit+40,1); shell_put16(permit+48,1);
    shell_put32(permit+52,connection->limits.permit_timeout_ms);
    shell_put32(permit+56,connection->limits.max_candidate_bytes);
    if (mode == 1) {
        send_frame(177,demand_tx+1,permit,sizeof(permit));
        assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID);
        assert(!connection->candidate_active && !inspect().native.presented);
        free(expected); teardown(); return;
    }
    if (mode == 2) now_msec += connection->limits.permit_timeout_ms;
    send_frame(177,demand_tx,permit,sizeof(permit)); tick();
    if (mode == 2) {
        assert(connection->permit_ready && !connection->candidate_active && !connection->outbox.count);
        shell_put16(permit+48,2); shell_put16(permit+50,6);
        shell_put32(permit+52,0); shell_put32(permit+56,0);
        send_frame(177,demand_tx,permit,sizeof(permit)); tick();
        struct sophia_shell_frame renewed = client_record();
        assert(renewed.kind == 176 && shell_get64(renewed.payload+48) == 2);
        assert(!connection->candidate_active && !inspect().native.presented);
        free(expected); teardown(); return;
    }
    struct sophia_shell_frame candidate = client_record();
    assert(candidate.kind == 189 && connection->candidate_active);
    uint64_t candidate_tx = candidate.transaction;
    struct sophia_shell_frame targets = client_record();
    assert(targets.kind == 190);
    struct sophia_shell_frame finish = client_record();
    assert(finish.kind == 174 && !inspect().native.presented);
    uint8_t outcome[68] = {0}; grant(outcome); shell_put64(outcome+16,1);
    shell_put64(outcome+24,2); shell_put64(outcome+32,7); shell_put16(outcome+40,1);
    if (mode == 4) {
        send_frame(175,candidate_tx+1,outcome,sizeof(outcome));
        assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID);
        assert(connection->candidate_active && !connection->shown_valid && !inspect().native.presented);
        free(expected); teardown(); return;
    }
    if (mode == 5) {
        uint8_t closed[28] = {0}; grant(closed); shell_put64(closed+16,1); shell_put16(closed+24,11);
        send_frame(197,90,closed,sizeof(closed));
        uint8_t next[56] = {0}; grant(next); shell_put64(next+16,2); shell_put64(next+24,2);
        shell_put64(next+32,7); shell_put64(next+40,9); shell_put64(next+48,1);
        send_frame(187,91,next,sizeof(next)); tick();
        assert(inspect().native.opening.opening == 2 && connection->candidate_active);
    }
    send_frame(175,candidate_tx,outcome,sizeof(outcome)); tick();
    assert(connection->candidate_active && !connection->shown_valid && !inspect().native.presented);
    shell_put16(outcome+40,2); shell_put64(outcome+44,10);
    send_frame(175,candidate_tx,outcome,sizeof(outcome)); tick();
    if (mode == 5) {
        assert(!connection->candidate_active && !connection->shown_valid && !inspect().native.presented);
        assert(inspect().native.opening.opening == 2 && !inspect().native.focused);
        struct sophia_shell_frame retired = client_record();
        assert(retired.kind == 170 && !memcmp(retired.payload,key,sizeof(key)));
        assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RELEASE_PENDING);
        free(expected); teardown(); return;
    }
    assert(!connection->candidate_active && connection->shown_valid && connection->shown_slot == 0);
    assert(inspect().native.presented && !inspect().native.focused);
    assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RESIDENT);
    tick(); assert(!connection->outbox.count); /* No duplicate candidate for this revision. */
    uint8_t closed[28] = {0}; grant(closed); shell_put64(closed+16,1); shell_put16(closed+24,11);
    send_frame(197,90,closed,sizeof(closed)); tick();
    struct sophia_shell_frame retire = client_record();
    assert(retire.kind == 170 && !memcmp(retire.payload,key,sizeof(key)));
    assert(!connection->shown_valid && !inspect().native.presented);
    assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RELEASE_PENDING);
    for (unsigned i = 0; i < 3; ++i) tick();
    assert(connection->views[0].valid && connection->retire_slot[0]);
    assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RELEASE_PENDING);
    uint8_t released[34] = {0}; memcpy(released,key,sizeof(key));
    if (mode == 0) {
        /* Closed disarms input but is not an allocation/resource release. A
         * new opening waits for the old allocation's exact revocation. */
        uint8_t next[56] = {0}; grant(next); shell_put64(next+16,2); shell_put64(next+24,2);
        shell_put64(next+32,7); shell_put64(next+40,9); shell_put64(next+48,1);
        send_frame(187,91,next,sizeof(next)); tick();
        assert(inspect().native.open && inspect().native.opening.opening == 2);
        assert(connection->allocation_valid && connection->allocation_opening == 1 && !connection->outbox.count);
        assert(!connection->views[1].valid && !inspect().native.presented);
        uint8_t revoked[160]; allocation_payload(revoked);
        shell_put64(revoked+16,0); shell_put16(revoked+24,4); shell_put16(revoked+26,9);
        send_frame(164,92,revoked,sizeof(revoked)); tick();
        struct sophia_shell_frame fresh = client_record();
        assert(fresh.kind == 188 && shell_get64(fresh.payload+16) == 2 && shell_get64(fresh.payload+40) == 2);
        uint64_t allocation_tx = fresh.transaction;
        allocation_payload(revoked); shell_put64(revoked+16,2); shell_put64(revoked+48,5);
        send_frame(164,allocation_tx,revoked,sizeof(revoked)); tick();
        assert(connection->allocation_valid && connection->allocation_opening == 2);
        assert(connection->views[1].valid && connection->views[1].opening == 2);
        assert(connection->views[1].allocation.id == 5 && connection->views[0].allocation.id == 4);
        struct sophia_shell_frame second = client_record();
        assert(second.kind == 165 && shell_get64(second.payload+16) != shell_get64(key+16));
        assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RELEASE_PENDING);
        assert(!inspect().native.presented); /* An old displayed image cannot rearm the new opening. */
    }
    send_frame(171,retire.transaction+(mode == 3),released,sizeof(released));
    if (mode == 3) {
        assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID);
        assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_RELEASE_PENDING);
    } else {
        tick();
        assert(sophia_shell_upload_inspect(connection->upload,0,&upload) == 0 && upload.state == SOPHIA_UPLOAD_EMPTY);
        assert(!connection->views[0].valid && !connection->retire_slot[0]);
    }
    free(expected); teardown();
}
static void allocation_refusal_and_identity(void)
{
    for (unsigned bad = 0; bad < 7; ++bad) {
        setup(); welcome(); limits(); catalog(); facts(); opening();
        assert(client_record().kind == 188);
        uint8_t p[160]; allocation_payload(p); uint64_t tx = 1;
        unsigned words[] = {0,16,40,80};
        if (bad < 4) shell_put64(p+words[bad],shell_get64(p+words[bad])+1);
        else if (bad == 4) shell_put32(p+112,641);
        else if (bad == 5) shell_put32(p+128,1);
        else tx = 2;
        send_frame(164,tx,p,sizeof(p));
        assert(bm_sophia_connection_service_at(connection,now_msec++) == SOPHIA_SHELL_INVALID);
        assert(connection->allocation_pending && !connection->allocation_valid && !connection->raster);
        assert(connection->allocation_counter == 1 && !connection->views[0].valid && !connection->outbox.count);
        teardown();
    }
    setup(); welcome(); limits(); catalog(); facts(); opening(); assert(client_record().kind == 188);
    uint8_t p[160] = {0}; grant(p); shell_put64(p+16,1); shell_put16(p+24,2); shell_put16(p+26,2);
    shell_put64(p+32,2); shell_put64(p+40,7); send_frame(164,1,p,sizeof(p)); tick();
    for (unsigned i = 0; i < 3; ++i) tick();
    assert(!connection->allocation_pending && !connection->allocation_valid && !connection->outbox.count);
    assert(connection->allocation_counter == 1); /* No retry storm for unchanged opening/facts. */
    teardown();
}
static void fractional_allocation_origin(void)
{
    setup(); welcome(); limits(); catalog(); facts_scaled(7,4); opening();
    struct sophia_shell_frame request = client_record();
    assert(request.kind == 188 && shell_get32(request.payload+68) == 584 && shell_get32(request.payload+72) == 320);
    uint8_t p[160]; allocation_payload(p);
    shell_put32(p+88,1); shell_put32(p+92,1); shell_put32(p+96,584);
    shell_put32(p+104,1); shell_put32(p+108,1); shell_put32(p+112,1023); shell_put32(p+116,561);
    shell_put32(p+120,7); shell_put32(p+124,4); send_frame(164,1,p,sizeof(p)); tick();
    assert(connection->allocation_valid && connection->views[0].valid);
    struct sophia_shell_frame begin = client_record();
    assert(begin.kind == 165 && shell_get32(begin.payload+32) == 1023 && shell_get32(begin.payload+36) == 561);
    assert(shell_get64(begin.payload+56) == UINT64_C(1023)*561*4);
    teardown();
}
static unsigned nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    abort();
}
int main(int argc, char **argv)
{
    assert(argc == 2); FILE *f = fopen(argv[1], "r"); assert(f); static char line[4096]; bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "content-161 ", 12)) continue;
        assert(strcspn(line+12, "\r\n") == sizeof(limits_frame)*2);
        for (unsigned i = 0; i < sizeof(limits_frame); ++i)
            limits_frame[i] = 16*nibble(line[12+2*i]) + nibble(line[13+2*i]);
        found = true; break;
    }
    fclose(f); assert(found);
    real_menu_input(); bounded_catalog_and_order(); retained_catalog_end();
    for (unsigned mode = 0; mode < 6; ++mode) automatic_allocation_and_upload(mode);
    allocation_refusal_and_identity();
    fractional_allocation_origin();
    puts("bemenu_connection fifo=pass real_menu=pass supplied_presentation=true native=false");
    return 0;
}
