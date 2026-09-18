#include "view.h"
#include "input.h"

bool
bm_sophia_view_capture(struct bm_sophia_raster *raster,
                      const struct bm_sophia_catalog_model *catalog,
                      struct bm_sophia_view *view)
{
    if (!view)
        return false;
    *view = (struct bm_sophia_view){0};
    if (!catalog || !catalog->menu || !catalog->connection_epoch || !catalog->generation)
        return false;
    struct bm_sophia_pixels pixels;
    if (!bm_sophia_raster_paint(raster, catalog->menu, &pixels))
        return false;
    struct bm_sophia_view next = {
        .data = pixels.data, .width = pixels.width, .height = pixels.height,
        .stride = pixels.stride, .content_height = pixels.content_height,
        .connection_epoch = catalog->connection_epoch, .catalog_generation = catalog->generation,
        .row_count = pixels.row_count,
    };
    const struct bm_item *selected = bm_menu_get_highlighted_item(catalog->menu);
    for (unsigned i = 0; i < pixels.row_count; ++i) {
        const struct bm_sophia_painted_row *row = &pixels.rows[i];
        uint64_t epoch, generation;
        uint16_t slot;
        if (!bm_sophia_catalog_identity(catalog, row->item, &epoch, &generation, &slot) ||
            epoch != next.connection_epoch || generation != next.catalog_generation)
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (next.rows[j].slot == slot)
                return false;
        next.rows[i] = (struct bm_sophia_view_row){slot, row->x, row->y, row->width, row->height};
        if (row->item == selected)
            next.selected = slot;
    }
    *view = next;
    return true;
}

int
bm_sophia_view_edit(void *menu, uint16_t kind, const uint8_t *text, size_t bytes)
{
    return bm_sophia_menu_input(menu, kind, text, bytes) == BM_SOPHIA_INPUT_APPLIED;
}
