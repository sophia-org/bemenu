#ifndef BM_SOPHIA_FILES_H
#define BM_SOPHIA_FILES_H
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../../vendor/sophia-desktop-sdk/source/src/sophia_shell_native_session.h"

struct bm_menu;
struct bm_sophia_files;

/* The first local step that ended the adapter, for diagnostics only.
 * INPUT also covers a lost acknowledgement for a content Action. */
enum bm_sophia_files_stage {
    BM_SOPHIA_FILES_STAGE_NONE, BM_SOPHIA_FILES_STAGE_CONNECT, BM_SOPHIA_FILES_STAGE_NEGOTIATE,
    BM_SOPHIA_FILES_STAGE_NATIVE, BM_SOPHIA_FILES_STAGE_OBJECT, BM_SOPHIA_FILES_STAGE_CATALOG,
    BM_SOPHIA_FILES_STAGE_ALLOCATION, BM_SOPHIA_FILES_STAGE_RENDER, BM_SOPHIA_FILES_STAGE_UPLOAD,
    BM_SOPHIA_FILES_STAGE_FRAME, BM_SOPHIA_FILES_STAGE_INPUT, BM_SOPHIA_FILES_STAGE_ACTION,
    BM_SOPHIA_FILES_STAGE_TIMEOUT, BM_SOPHIA_FILES_STAGE_CLOCK
};
struct bm_sophia_files_snapshot {
    bool connected, ready, native, open, presented, focused;
    enum sophia_ss_state session;
    enum sophia_ns_state lifecycle;
    enum bm_sophia_files_stage failed;
    uint64_t connection_epoch, catalog_generation, facts_generation, opening, revision;
    /* displayed: rows of the last captured view plus the filter row, as the
     * upstream menu counts them. views: resources not yet released. */
    unsigned displayed, views, edits;
};
/* Native launcher over the shell file export at the caller's explicit 9P
 * socket path. No discovery,
 * fallback or reconnect. The menu is borrowed: empty, configured, default key
 * mode; this adapter installs only the Session catalog into it. The adapter
 * owns the connected fd and every SDK owner. Returns sophia_9p_result codes. */
int bm_sophia_files_new(struct bm_menu *menu, const char *endpoint, uint64_t now_ms,
                        struct bm_sophia_files **out);
/* What to wait for before the next progress call. A pollfd with fd -1 means
 * only the timeout, which is at most 50 ms; the loop never spins on it. */
int bm_sophia_files_poll(const struct bm_sophia_files *files, uint64_t now_ms,
                         struct pollfd *poller, int *timeout_ms);
/* One bounded serialized pass with CLOCK_MONOTONIC milliseconds: at most 64 KiB
 * each way, 16 events and one resource or frame step of each kind. 0 while
 * live; a terminal result latches and every later call returns it. Timeouts are
 * local failure guards, never a fabricated outcome or resource release. */
int bm_sophia_files_progress(struct bm_sophia_files *files, short revents, uint64_t now_ms);
bool bm_sophia_files_inspect(const struct bm_sophia_files *files,
                            struct bm_sophia_files_snapshot *snapshot);
/* Ends the session locally, closes the fd and clears the catalog from the
 * borrowed menu. Call before freeing that menu. Nothing is replayed. */
void bm_sophia_files_dispose(struct bm_sophia_files *files);
#endif
