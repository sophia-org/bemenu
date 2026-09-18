#ifndef BM_SOPHIA_VIEW_H
#define BM_SOPHIA_VIEW_H
#include "raster.h"
#include "catalog.h"

struct bm_sophia_view_row {
    uint16_t slot;
    uint32_t x, y, width, height;
};
/* Copyable scene description with no borrowed menu item. Pixels still belong
 * to the raster and must be copied by the resource owner before another paint.
 * These are observed background bounds, not yet authorized protocol targets. */
struct bm_sophia_view {
    const unsigned char *data;
    uint32_t width, height, stride, content_height, row_count;
    uint64_t connection_epoch, catalog_generation;
    uint16_t selected;
    struct bm_sophia_view_row rows[32];
};
bool bm_sophia_view_capture(struct bm_sophia_raster *raster,
                           const struct bm_sophia_catalog_model *catalog,
                           struct bm_sophia_view *view);
/* Callback for the protocol lifecycle, after it validates authority and
 * reserves its response. Does not handle Accept, ACK or application launch. */
int bm_sophia_view_edit(void *menu, uint16_t kind, const uint8_t *text, size_t bytes);
#endif
