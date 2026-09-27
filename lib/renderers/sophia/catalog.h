#ifndef BM_SOPHIA_CATALOG_H
#define BM_SOPHIA_CATALOG_H
#include "bemenu.h"
#include "../../../vendor/sophia-desktop-sdk/source/src/sophia_shell_catalog.h"
#include "../../../vendor/sophia-desktop-sdk/source/src/sophia_shell_files_roles.h"

struct bm_sophia_catalog_row {
    struct bm_item *item;
    uint16_t slot;
};
/* Caller-owned, zero-initialized, bound to one otherwise-empty menu. Clear before
 * freeing that menu. Items belong to the menu; this owns their identity storage.
 * Retained presentation models must copy identities, never retain these items. */
struct bm_sophia_catalog_model {
    struct bm_menu *menu;
    struct bm_sophia_catalog_row *rows;
    size_t count;
    uint64_t connection_epoch, generation;
};
/* Only a committed, validated wire catalog; no executables or local discovery.
 * Failure before installation preserves the current menu/identities. Existing
 * libbemenu filtering runs after commit and can yield no rows on allocation
 * failure. A menu owned/modified elsewhere refuses, rather than stealing items. */
bool bm_sophia_catalog_install(struct bm_sophia_catalog_model *model,
                              struct bm_menu *menu,
                              const struct sophia_shell_catalog *catalog);
/* A decoded, pinned file catalog. Rows are copied into the same menu model;
 * the object storage may be released after success. No IPC assembly is used. */
bool bm_sophia_catalog_install_files(struct bm_sophia_catalog_model *model,
                                    struct bm_menu *menu,
                                    const struct sophia_sf_catalog *catalog);
bool bm_sophia_catalog_clear(struct bm_sophia_catalog_model *model);
/* Local display reference only, not authorization. False leaves outputs alone. */
bool bm_sophia_catalog_identity(const struct bm_sophia_catalog_model *model,
                               const struct bm_item *item, uint64_t *epoch,
                               uint64_t *generation, uint16_t *slot);
#endif
