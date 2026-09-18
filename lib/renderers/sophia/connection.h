#ifndef BM_SOPHIA_CONNECTION_H
#define BM_SOPHIA_CONNECTION_H
#include <stdbool.h>
#include <stdint.h>
#include "../../../vendor/sophia-shell/sophia_shell_native_lifecycle.h"

struct bm_menu;
struct bm_sophia_connection;
struct bm_sophia_connection_snapshot {
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
 * frame in place; subsequent frames cannot pass it. Caller owns polling,
 * monotonic deadlines and termination on a terminal result. */
int bm_sophia_connection_service(struct bm_sophia_connection *connection);
bool bm_sophia_connection_inspect(const struct bm_sophia_connection *connection,
                                 struct bm_sophia_connection_snapshot *snapshot);
void bm_sophia_connection_dispose(struct bm_sophia_connection *connection);
#endif
