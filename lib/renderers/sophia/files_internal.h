#ifndef BM_SOPHIA_FILES_INTERNAL_H
#define BM_SOPHIA_FILES_INTERNAL_H
#include "files.h"
#include "view.h"
#include "../../../vendor/sophia-desktop-sdk/source/src/sophia_desktop_connection.h"
#include <sys/un.h>

#define BM_SOPHIA_FILES_GUARD_MS UINT64_C(5000)
#define BM_SOPHIA_FILES_BYTES 65536u
#define BM_SOPHIA_FILES_EVENTS 16u
#define BM_SOPHIA_FILES_VIEWS 4u
#define BM_SOPHIA_FILES_MARGIN_MS 20u

/* Local bookkeeping of one immutable upload, mirrored only from this client's
 * own calls, delivered events and ticket outcomes. The SDK keeps authority. */
enum bm_sophia_files_view_state {
    BM_SOPHIA_FILES_VIEW_FREE, BM_SOPHIA_FILES_VIEW_STAGED, BM_SOPHIA_FILES_VIEW_BEGUN,
    BM_SOPHIA_FILES_VIEW_SENDING, BM_SOPHIA_FILES_VIEW_ENDED, BM_SOPHIA_FILES_VIEW_ACCEPTED,
    BM_SOPHIA_FILES_VIEW_RETIRING
};
struct bm_sophia_files_view {
    enum bm_sophia_files_view_state state;
    uint64_t id, ticket;
    uint64_t opening, revision, catalog_generation, facts_generation;
    uint64_t allocation_id, allocation_generation;
    uint32_t scale_numerator, scale_denominator;
    /* Copied scene only: its pixel pointer is cleared once copied. */
    struct bm_sophia_view view;
};
enum bm_sophia_files_guard_kind {
    BM_SOPHIA_FILES_GUARD_BOOT, BM_SOPHIA_FILES_GUARD_OBJECT, BM_SOPHIA_FILES_GUARD_ALLOCATION,
    BM_SOPHIA_FILES_GUARD_UPLOAD, BM_SOPHIA_FILES_GUARD_FRAME, BM_SOPHIA_FILES_GUARD_ACTIVATION,
    BM_SOPHIA_FILES_GUARD_RETIRE, BM_SOPHIA_FILES_GUARD_COUNT
};
struct bm_sophia_files_guard {
    bool active;
    uint64_t key, started;
};
struct bm_sophia_files_announcement {
    bool owed;
    uint64_t generation, qid;
};

struct bm_sophia_files {
    struct bm_menu *menu;
    char endpoint[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct sophia_desktop_connection connection;
    int fd, terminal;
    enum bm_sophia_files_stage failed;
    bool connecting, session, native, clock_seen;
    uint64_t now, retry_at;
    struct sophia_ss ss;
    struct sophia_ns ns;
    void *storage, *objects;
    size_t storage_bytes;
    /* Objects: announcements are consumed at once; one fetch at a time. */
    struct bm_sophia_files_announcement announced[4], deferred;
    bool fetching, refetch;
    uint16_t fetch_kind;
    uint64_t fetch_generation, fetch_qid;
    struct sophia_sf_outputs outputs;
    struct bm_sophia_catalog_model model;
    /* Allocation policy: one request per (opening, facts); its fact snapshot. */
    uint64_t allocation_opening, allocation_facts, allocation_ticket;
    struct sophia_sf_content_output_facts_entry allocation_fact;
    struct bm_sophia_raster *raster;
    uint64_t raster_id, raster_generation;
    uint32_t raster_numerator, raster_denominator;
    /* Uploads: one owned immutable copy, canonical chunks handed to the ss. */
    struct bm_sophia_files_view views[BM_SOPHIA_FILES_VIEWS];
    unsigned char *copy;
    size_t copy_bytes, copy_sent, chunk_bytes;
    int upload, pending, shown;
    uint64_t next_resource, target_generation;
    /* Frame: known waits from delivered events, never inferred from INVALID. */
    bool demand, permit, activation;
    /* An input head the SDK already applied (its revision is current) whose
     * menu edit is still waiting for ack room: never paint that revision. */
    bool held;
    uint64_t demand_ticket, present_ticket, activation_ticket;
    unsigned displayed, edits;
    struct bm_sophia_files_guard guards[BM_SOPHIA_FILES_GUARD_COUNT];
};

int bm_sophia_files_fail(struct bm_sophia_files *f, enum bm_sophia_files_stage stage, int code);
/* Up to 16 ns events; BUSY input/action heads stay for a later pass. */
int bm_sophia_files_events(struct bm_sophia_files *f);
/* One object fetch step: announced, deferred-catalog refetch or result. */
int bm_sophia_files_objects(struct bm_sophia_files *f);
/* Tickets this client owns: a refused submission is a local failure. */
int bm_sophia_files_settle(struct bm_sophia_files *f);
/* Allocation, render/upload, retirement, demand and present. */
int bm_sophia_files_schedule(struct bm_sophia_files *f);
/* Allocation grant: local geometry check and raster for its pixels. */
int bm_sophia_files_granted(struct bm_sophia_files *f, const struct sophia_sf_allocation_result *a);
struct bm_sophia_files_view *bm_sophia_files_view_find(struct bm_sophia_files *f, uint64_t id);
void bm_sophia_files_release_copy(struct bm_sophia_files *f);
#endif
