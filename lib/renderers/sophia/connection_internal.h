#ifndef BM_SOPHIA_CONNECTION_INTERNAL_H
#define BM_SOPHIA_CONNECTION_INTERNAL_H
#include "connection.h"
#include "view.h"
#include "../../../vendor/sophia-shell/sophia_shell_upload.h"
#include "../../../vendor/sophia-shell/sophia_shell_content_control.h"

struct bm_sophia_uploaded_view {
    bool valid;
    uint64_t opening, revision, facts_generation;
    struct sophia_shell_content_id allocation;
    struct bm_sophia_view view;
};

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
    uint64_t allocation_counter, allocation_transaction, allocation_opening;
    uint64_t allocation_attempt_opening, allocation_attempt_facts;
    struct sophia_shell_native_allocation allocation_request;
    struct sophia_shell_output_fact allocation_fact;
    struct sophia_shell_allocation_result allocation;
    bool allocation_pending, allocation_valid;
    struct bm_sophia_raster *raster;
    struct bm_sophia_uploaded_view views[SOPHIA_SHELL_UPLOAD_SLOTS];
    uint64_t now_msec, demand_counter, demand_transaction, demand_started, last_permit, interaction_counter;
    bool clock_seen, demand_pending, permit_ready, candidate_active, shown_valid;
    struct sophia_shell_frame_demand demand;
    struct sophia_shell_frame_permit permit;
    unsigned candidate_slot, shown_slot;
    uint64_t candidate_transaction;
    bool retire_slot[SOPHIA_SHELL_UPLOAD_SLOTS];
};
int bm_sophia_connection_receive(struct bm_sophia_connection *c,
                                const struct sophia_shell_frame *frame);
int bm_sophia_connection_schedule(struct bm_sophia_connection *c);
int bm_sophia_allocation_service(struct bm_sophia_connection *c,
                                const struct sophia_shell_native_lifecycle_snapshot *state);
int bm_sophia_allocation_receive(struct bm_sophia_connection *c,
                                const struct sophia_shell_frame *frame);
bool bm_sophia_allocation_current(const struct bm_sophia_connection *c,
                                 const struct sophia_shell_native_lifecycle_snapshot *state);
int bm_sophia_frame_service(struct bm_sophia_connection *c,
                           const struct sophia_shell_native_lifecycle_snapshot *state);
int bm_sophia_permit_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *frame);
int bm_sophia_candidate_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *frame);
bool bm_sophia_view_current(const struct bm_sophia_connection *c, unsigned slot,
                           const struct sophia_shell_native_lifecycle_snapshot *state);
#endif
