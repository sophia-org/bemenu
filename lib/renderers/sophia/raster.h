#ifndef BM_SOPHIA_RASTER_H
#define BM_SOPHIA_RASTER_H

#include <stdbool.h>
#include <stdint.h>

struct bm_menu;
struct bm_sophia_raster;
struct bm_item;

struct bm_sophia_painted_row {
    /* Borrowed only until a menu mutation; copy catalog identity immediately. */
    const struct bm_item *item;
    uint32_t x, y, width, height;
};

/* Owned CPU pixels only. This object has no display, socket or input authority.
 * Dimensions are physical pixels; scale is logical-to-physical. Call serially,
 * like the upstream Cairo painter. The borrowed bytes expire at paint/free. */
struct bm_sophia_pixels {
    const unsigned char *data;
    uint32_t width, height, stride, content_height, displayed;
    /* Physical clipped background rectangles observed during the actual paint,
     * not reconstructed from row count. Later painter decorations may overlay
     * these backgrounds; this alone is not a native target authorization. */
    uint32_t row_count;
    struct bm_sophia_painted_row rows[32];
};

struct bm_sophia_raster *bm_sophia_raster_new(uint32_t width, uint32_t height, double scale);
bool bm_sophia_raster_paint(struct bm_sophia_raster *raster, struct bm_menu *menu,
                           struct bm_sophia_pixels *pixels);
void bm_sophia_raster_free(struct bm_sophia_raster *raster);

#endif
