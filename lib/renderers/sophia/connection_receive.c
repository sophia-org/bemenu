#include "connection_internal.h"
#include <string.h>

static int initialize_native(struct bm_sophia_connection *c)
{
    if (c->native || !c->content || !c->model.generation)
        return SOPHIA_SHELL_OK;
    struct sophia_shell_frame limits = {161,0,c->limits_payload,sizeof(c->limits_payload)};
    return sophia_shell_native_lifecycle_new(&limits, c->model.generation, &c->outbox, &c->native);
}

static int install_catalog(struct bm_sophia_connection *c)
{
    uint64_t generation; size_t count;
    if (!sophia_shell_catalog_entries(&c->catalog, &count, &generation))
        return SOPHIA_SHELL_INVALID;
    if (c->model.generation != generation && !bm_sophia_catalog_install(&c->model, c->menu, &c->catalog))
        return SOPHIA_SHELL_BUSY;
    if (c->native) {
        int r = sophia_shell_native_lifecycle_catalog(c->native, generation);
        if (r != SOPHIA_SHELL_OK) return r;
    } else {
        int r = initialize_native(c);
        if (r != SOPHIA_SHELL_OK) return r;
    }
    c->catalog_pending = false;
    return SOPHIA_SHELL_OK;
}

int
bm_sophia_connection_receive(struct bm_sophia_connection *c, const struct sophia_shell_frame *f)
{
    if (!c->welcomed) {
        int r = sophia_shell_welcome_decode(f, (struct sophia_shell_hello){7,7,0x9a0}, &c->welcome);
        if (r != SOPHIA_SHELL_OK) return r;
        r = sophia_shell_catalog_init(&c->catalog, &c->welcome, c->catalog_a, c->catalog_b, 4096);
        if (r == SOPHIA_SHELL_OK) c->welcomed = true;
        return r;
    }
    if (c->content && f->payload_bytes > c->limits.max_frame_payload)
        return SOPHIA_SHELL_INVALID;
    if (c->catalog_pending)
        return install_catalog(c); /* Original End remains the wire's borrowed frame. */
    if (f->kind >= 114 && f->kind <= 116) {
        int r = sophia_shell_catalog_accept(&c->catalog, f);
        if (r < 0) return r;
        if (r == 1) {
            c->catalog_pending = true;
            return install_catalog(c);
        }
        return r;
    }
    if (f->kind == 161) {
        if (c->content) return SOPHIA_SHELL_INVALID;
        struct sophia_shell_content_limits limits;
        int r = sophia_shell_content_limits_decode(f, &limits);
        if (r != SOPHIA_SHELL_OK) return r;
        if (limits.grant.connection_epoch != c->welcome.connection_epoch ||
            limits.max_control_records < 6 || limits.max_input_queue_bytes < 4096 ||
            c->outbox.bytes > limits.max_input_queue_bytes || c->outbox.count > limits.max_control_records)
            return SOPHIA_SHELL_INVALID;
        /* Hello is the only earlier outbound frame. Retain it if partially
         * written; resize the existing budget, never replace a nonempty FIFO. */
        c->outbox.max_bytes = limits.max_input_queue_bytes;
        /* Ring modulus cannot change while any cell is owned. The handshake
         * has one cell at index zero, so defer processing until it drains. */
        if (c->outbox.count) return SOPHIA_SHELL_BUSY;
        c->outbox.head = 0; c->outbox.max_records = limits.max_control_records;
        if (!c->upload) {
            r = sophia_shell_upload_new(f, &c->upload);
            if (r != SOPHIA_SHELL_OK) return r;
        }
        c->limits = limits;
        memcpy(c->limits_payload, f->payload, sizeof(c->limits_payload));
        c->content = true;
        r = initialize_native(c);
        if (r != SOPHIA_SHELL_OK) {c->content = false; return r;}
        return SOPHIA_SHELL_OK;
    }
    if (!c->content) return SOPHIA_SHELL_INVALID;
    if (f->kind == 162) {
        struct sophia_shell_content_feedback feedback;
        int r = sophia_shell_content_feedback_decode(f, &feedback);
        if (r != SOPHIA_SHELL_OK) return r;
        if (feedback.grant.connection_epoch != c->limits.grant.connection_epoch ||
            feedback.grant.content_grant_epoch != c->limits.grant.content_grant_epoch ||
            feedback.value.facts.generation <= c->facts.generation ||
            feedback.value.facts.count > c->limits.max_outputs)
            return SOPHIA_SHELL_INVALID;
        c->facts = feedback.value.facts;
        return SOPHIA_SHELL_OK;
    }
    if (f->kind == 166 || f->kind == 171)
        return sophia_shell_upload_reply(c->upload, f);
    if (f->kind == 164)
        return bm_sophia_allocation_receive(c, f);
    if (f->kind == 177)
        return bm_sophia_permit_receive(c, f);
    if (!c->native) return SOPHIA_SHELL_INVALID;
    if (f->kind == 175)
        return bm_sophia_candidate_receive(c, f);
    return sophia_shell_native_lifecycle_receive(c->native, f, &c->next_transaction,
                                                bm_sophia_view_edit, c->menu);
}
