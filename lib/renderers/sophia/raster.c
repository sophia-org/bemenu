#include "raster.h"
#include "../cairo_renderer.h"

#include <stdlib.h>
#include <fontconfig/fontconfig.h>

/* A local raster allocation ceiling, not a negotiated content/GPU budget. */
#define MAX_RASTER_BYTES (16u * 1024u * 1024u)
#define MAX_DIMENSION 8192u

struct bm_sophia_raster {
    struct cairo painter;
    uint32_t width, height, stride;
};

struct bm_sophia_raster *
bm_sophia_raster_new(uint32_t width, uint32_t height, double scale)
{
    if (!width || !height || width > MAX_DIMENSION || height > MAX_DIMENSION ||
        !isfinite(scale) || scale < 1.0 || scale > 4.0 || height / scale < 64)
        return NULL;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    if (stride <= 0 || (uint64_t)stride * height > MAX_RASTER_BYTES)
        return NULL;
    if (!FcInit())
        return NULL;

    struct bm_sophia_raster *raster = calloc(1, sizeof(*raster));
    if (!raster)
        return NULL;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        free(raster);
        return NULL;
    }
    raster->painter.scale = scale;
    raster->painter.antialiasing = true;
    if (!bm_cairo_create_for_surface(&raster->painter, surface)) {
        cairo_surface_destroy(surface);
        free(raster);
        return NULL;
    }
    if (cairo_status(raster->painter.cr) != CAIRO_STATUS_SUCCESS) {
        bm_sophia_raster_free(raster);
        return NULL;
    }
    raster->width = width;
    raster->height = height;
    raster->stride = stride;
    return raster;
}

bool
bm_sophia_raster_paint(struct bm_sophia_raster *raster, struct bm_menu *menu,
                      struct bm_sophia_pixels *pixels)
{
    if (!pixels)
        return false;
    *pixels = (struct bm_sophia_pixels){0};
    if (!raster || !menu || !menu->font.name || !menu->filter_item ||
        !isfinite(menu->border_size) || menu->border_size < 0 ||
        menu->border_size >= raster->width || menu->lines > 32)
        return false;

    /* The shared painter assumes a nonzero text line fitting the allocation. */
    struct cairo_paint measure = {.font = menu->font.name};
    struct cairo_result metrics = {0};
    bm_pango_get_text_extents(&raster->painter, &measure, &metrics, "Ag");
    if (!metrics.height || metrics.height > raster->height / raster->painter.scale)
        return false;

    struct cairo_paint_result result;
    bm_cairo_paint(&raster->painter, raster->width, raster->height, menu, &result);
    cairo_surface_flush(raster->painter.surface);
    if (cairo_status(raster->painter.cr) != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status(raster->painter.surface) != CAIRO_STATUS_SUCCESS)
        return false;
    *pixels = (struct bm_sophia_pixels){
        .data = cairo_image_surface_get_data(raster->painter.surface),
        .width = raster->width, .height = raster->height, .stride = raster->stride,
        .content_height = result.height, .displayed = result.displayed,
    };
    return true;
}

void
bm_sophia_raster_free(struct bm_sophia_raster *raster)
{
    if (raster) {
        bm_cairo_destroy(&raster->painter);
        free(raster);
    }
}
