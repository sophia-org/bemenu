#include "connection_internal.h"
#include "../../../vendor/sophia-shell/shell_wire/fields.h"

static bool expired(struct bm_sophia_connection *c, enum bm_sophia_timeout kind,
                    bool active, uint64_t first, uint64_t second, unsigned phase)
{
    struct bm_sophia_deadline *d = &c->deadlines[kind];
    if (!active) {d->active = false; return false;}
    if (!d->active || d->first != first || d->second != second || d->phase != phase) {
        *d = (struct bm_sophia_deadline){true,first,second,c->now_msec,phase};
        return false;
    }
    if (c->now_msec-d->started < BM_SOPHIA_FAILURE_TIMEOUT_MS) return false;
    c->timeout = kind;
    return true;
}

int bm_sophia_connection_deadlines(struct bm_sophia_connection *c)
{
    /* Expiry is terminal without changing any wire/resource owner. The caller
     * closes the stream before disposal; no timeout fabricates a release. */
    if (expired(c,BM_SOPHIA_TIMEOUT_STARTUP,!c->native,0,0,0) ||
        expired(c,BM_SOPHIA_TIMEOUT_ALLOCATION,c->allocation_pending,c->allocation_transaction,0,0) ||
        expired(c,BM_SOPHIA_TIMEOUT_PERMIT,c->demand_pending,c->demand_transaction,0,0) ||
        expired(c,BM_SOPHIA_TIMEOUT_CANDIDATE,c->candidate_active,c->candidate_transaction,0,0))
        return SOPHIA_SHELL_IO_ERROR;
    if (c->upload) for (unsigned i = 0; i < SOPHIA_SHELL_UPLOAD_SLOTS; ++i) {
        struct sophia_shell_upload_snapshot s;
        int r = sophia_shell_upload_inspect(c->upload,i,&s);
        if (r != SOPHIA_SHELL_OK) return r;
        bool active = s.state != SOPHIA_UPLOAD_EMPTY && s.state != SOPHIA_UPLOAD_RESIDENT;
        /* One upload deadline spans all chunks. Retirement is a distinct wait.
         * Unrelated input/other-slot progress cannot renew either obligation. */
        unsigned phase = s.state >= SOPHIA_UPLOAD_RETIRE_READY ? 2 : 1;
        if (expired(c,(enum bm_sophia_timeout)(BM_SOPHIA_TIMEOUT_UPLOAD_FIRST+i),
                    active,s.key.resource.id,s.key.resource.generation,phase)) return SOPHIA_SHELL_IO_ERROR;
    }
    if (c->native) {
        struct sophia_shell_native_lifecycle_snapshot s;
        int r = sophia_shell_native_lifecycle_inspect(c->native,&s);
        if (r != SOPHIA_SHELL_OK) return r;
        if (expired(c,BM_SOPHIA_TIMEOUT_ACTIVATION,s.activation_pending,0,0,0) ||
            expired(c,BM_SOPHIA_TIMEOUT_REVOKE,c->allocation_valid &&
                !bm_sophia_allocation_current(c,&s),
                c->allocation.allocation.id,c->allocation.allocation.generation,0))
            return SOPHIA_SHELL_IO_ERROR;
    }
    if (expired(c,BM_SOPHIA_TIMEOUT_WRITE,c->outbox.count != 0,c->write_progress,0,0))
        return SOPHIA_SHELL_IO_ERROR;
    uint64_t tx = c->wire.rx_used >= 16 ? shell_get64(c->wire.rx+8) : 0;
    unsigned kind = c->wire.rx_used >= 8 ? shell_get16(c->wire.rx+6) : 0;
    if (expired(c,BM_SOPHIA_TIMEOUT_RECEIVE,c->wire.rx_used != 0,tx,0,kind)) return SOPHIA_SHELL_IO_ERROR;
    return SOPHIA_SHELL_OK;
}
