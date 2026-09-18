#include "internal.h"
#include "catalog.h"
#include <stdlib.h>
#include <string.h>

static bool
owns_menu(const struct bm_sophia_catalog_model *model, const struct bm_menu *menu)
{
    uint32_t count;
    struct bm_item **items = bm_menu_get_items(menu, &count);
    if ((!model->menu && count) || (model->menu && model->menu != menu) || count != model->count)
        return false;
    for (size_t i=0; i<model->count; ++i)
        if (items[i] != model->rows[i].item || bm_item_get_userdata(items[i]) != &model->rows[i])
            return false;
    return true;
}

static void
free_rows(struct bm_sophia_catalog_row *rows, size_t count, bool items)
{
    if (items)
        for (size_t i=0; i<count; ++i)
            if (rows[i].item) bm_item_free(rows[i].item);
    free(rows);
}

static struct bm_item *
make_item(const struct sophia_shell_catalog_entry *entry)
{
    struct bm_item *item = bm_item_new(NULL);
    if (!item) return NULL;
    if (!bm_item_set_text(item, entry->label)) goto fail;
    if (entry->keywords_bytes) {
        size_t bytes = (size_t)entry->label_bytes + 1 + entry->keywords_bytes;
        item->search_text = malloc(bytes + 1);
        if (!item->search_text) goto fail;
        memcpy(item->search_text, entry->label, entry->label_bytes);
        item->search_text[entry->label_bytes] = ' ';
        memcpy(item->search_text + entry->label_bytes + 1, entry->keywords, entry->keywords_bytes);
        item->search_text[bytes] = 0;
    }
    return item;
fail:
    bm_item_free(item);
    return NULL;
}

bool
bm_sophia_catalog_install(struct bm_sophia_catalog_model *model, struct bm_menu *menu,
                         const struct sophia_shell_catalog *catalog)
{
    if (!model || !menu || !catalog || !owns_menu(model, menu)) return false;
    size_t count = 0;
    uint64_t generation = 0;
    const struct sophia_shell_catalog_entry *entries = sophia_shell_catalog_entries(catalog, &count, &generation);
    if (!entries || catalog->failed || !catalog->connection_epoch ||
        count > SOPHIA_SHELL_CATALOG_MAX_ENTRIES ||
        (model->menu && (model->connection_epoch != catalog->connection_epoch || generation <= model->generation)))
        return false;
    struct bm_sophia_catalog_row *rows = count ? calloc(count, sizeof(*rows)) : NULL;
    const struct bm_item **items = count ? calloc(count, sizeof(*items)) : NULL;
    if (count && (!rows || !items)) { free(rows); free(items); return false; }
    size_t used = 0;
    for (size_t i=0; i<count; ++i) {
        if (!entries[i].available) continue;
        struct bm_item *item = make_item(&entries[i]);
        if (!item) goto fail;
        rows[used] = (struct bm_sophia_catalog_row){item, entries[i].slot};
        bm_item_set_userdata(item, &rows[used]);
        items[used++] = item;
    }
    if (!bm_menu_set_items(menu, items, (uint32_t)used)) goto fail;
    /* Upstream nonempty replacement frees the pointer list but not its items;
     * empty replacement frees both. Retire exactly the former ownership here. */
    free_rows(model->rows, model->count, used != 0);
    free(items);
    *model = (struct bm_sophia_catalog_model){menu, rows, used, catalog->connection_epoch, generation};
    free(menu->old_filter);
    menu->old_filter = NULL;
    menu->dirty = true;
    bm_menu_filter(menu);
    bm_menu_set_highlighted_index(menu, 0);
    return true;
fail:
    free_rows(rows, used, true);
    free(items);
    return false;
}

bool
bm_sophia_catalog_clear(struct bm_sophia_catalog_model *model)
{
    if (!model) return false;
    if (model->menu) {
        if (!owns_menu(model, model->menu)) return false;
        if (!bm_menu_set_items(model->menu, NULL, 0)) return false;
        free(model->menu->old_filter);
        model->menu->old_filter = NULL;
        model->menu->dirty = true;
    }
    free_rows(model->rows, model->count, false);
    *model = (struct bm_sophia_catalog_model){0};
    return true;
}

bool
bm_sophia_catalog_identity(const struct bm_sophia_catalog_model *model,
                          const struct bm_item *item, uint64_t *epoch,
                          uint64_t *generation, uint16_t *slot)
{
    if (!model || !model->menu || !item || !epoch || !generation || !slot) return false;
    for (size_t i=0; i<model->count; ++i) {
        if (model->rows[i].item != item || item->userdata != &model->rows[i]) continue;
        *epoch = model->connection_epoch;
        *generation = model->generation;
        *slot = model->rows[i].slot;
        return true;
    }
    return false;
}
