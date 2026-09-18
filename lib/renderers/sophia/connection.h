#ifndef BM_SOPHIA_CONNECTION_H
#define BM_SOPHIA_CONNECTION_H
#include <stdbool.h>
#include <stdint.h>
#include "../../../vendor/sophia-shell/sophia_shell_native_lifecycle.h"

struct bm_menu;
struct bm_sophia_connection;
/* Failure guard, not an ACK/presentation latency target. */
#define BM_SOPHIA_FAILURE_TIMEOUT_MS UINT64_C(5000)
enum bm_sophia_timeout {
    BM_SOPHIA_TIMEOUT_NONE, BM_SOPHIA_TIMEOUT_STARTUP, BM_SOPHIA_TIMEOUT_ALLOCATION,
    BM_SOPHIA_TIMEOUT_PERMIT, BM_SOPHIA_TIMEOUT_CANDIDATE, BM_SOPHIA_TIMEOUT_UPLOAD_FIRST,
    BM_SOPHIA_TIMEOUT_UPLOAD_SECOND, BM_SOPHIA_TIMEOUT_ACTIVATION, BM_SOPHIA_TIMEOUT_REVOKE,
    BM_SOPHIA_TIMEOUT_RECEIVE, BM_SOPHIA_TIMEOUT_WRITE, BM_SOPHIA_TIMEOUT_COUNT
};
struct bm_sophia_connection_snapshot {
    enum bm_sophia_timeout timeout;
    bool welcomed, content, catalog, lifecycle;
    uint64_t connection_epoch, catalog_generation, facts_generation;
    unsigned queued_records;
    size_t queued_bytes;
    struct sophia_shell_native_lifecycle_snapshot native;
};
/* Owns one connection's C protocol/menu state, but not fd or menu lifetime.
 * The caller supplies an authenticated connected stream and an otherwise empty
 * configured menu. It must close the connection before dispose, and dispose
 * before freeing/mutating that menu elsewhere. No connect, process execution,
 * fallback display or background thread. No native-readiness claim. */
int bm_sophia_connection_new(struct bm_menu *menu, int fd,
                            struct bm_sophia_connection **out);
/* One serialized visit: at most 16 complete records, 64 KiB read and 64 KiB
 * written, each underlying I/O call also syscall-bounded. May capture one bounded
 * CPU raster and enqueue one resource record after input dispatch. Retains a BUSY input
 * frame in place; subsequent frames cannot pass it. Caller owns polling
 * and termination on a terminal result. Pending obligations have a
 * five-second failure guard; healthy idle time has no deadline. */
int bm_sophia_connection_service(struct bm_sophia_connection *connection);
/* Same visit with caller-supplied monotonic milliseconds, for an event loop or
 * deterministic private host. Clock regression is terminal. */
int bm_sophia_connection_service_at(struct bm_sophia_connection *connection, uint64_t now_msec);
bool bm_sophia_connection_inspect(const struct bm_sophia_connection *connection,
                                 struct bm_sophia_connection_snapshot *snapshot);
void bm_sophia_connection_dispose(struct bm_sophia_connection *connection);
#endif
