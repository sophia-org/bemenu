#ifndef BM_SOPHIA_CONNECTION_INTERNAL_H
#define BM_SOPHIA_CONNECTION_INTERNAL_H
#include "connection.h"
#include "view.h"
#include "../../../vendor/sophia-shell/sophia_shell_upload.h"

/* One serialized owner; neither raw fd nor transport owner escapes publicly. */
struct bm_sophia_connection {
    struct bm_menu *menu;
    int fd, terminal;
    struct sophia_shell_wire wire;
    uint8_t rx[SOPHIA_SHELL_MAX_FRAME_BYTES], unused_tx[24], limits_payload[264];
    struct sophia_shell_outbox outbox;
    struct sophia_shell_welcome welcome;
    struct sophia_shell_content_limits limits;
    struct sophia_shell_catalog catalog;
    struct sophia_shell_catalog_entry catalog_a[4096], catalog_b[4096];
    struct bm_sophia_catalog_model model;
    struct sophia_shell_upload *upload;
    struct sophia_shell_native_lifecycle *native;
    struct sophia_shell_output_facts facts;
    uint64_t next_transaction;
    bool welcomed, content, catalog_pending;
};
int bm_sophia_connection_receive(struct bm_sophia_connection *c,
                                const struct sophia_shell_frame *frame);
#endif
