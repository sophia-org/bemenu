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

struct row_capture {
    struct bm_sophia_pixels *pixels;
    double scale;
    bool overflow;
};

/* Keep one rectangular target per catalog row. If decoration splits a row,
 * retain its largest uncovered rectangle rather than authorizing covered pixels. */
static void
capture_cover(void *data, const struct cairo_result *box)
{
    struct row_capture *capture = data;
    struct bm_sophia_pixels *pixels = capture->pixels;
    double x = fmax(0, floor(box->x * capture->scale));
    double y = fmax(0, floor(box->y * capture->scale));
    double right = fmin(pixels->width, ceil((box->x + box->width) * capture->scale));
    double bottom = fmin(pixels->height, ceil((box->y + box->box_height) * capture->scale));
    if (right <= x || bottom <= y) return;
    for (unsigned i = 0; i < pixels->row_count; ++i) {
        struct bm_sophia_painted_row *row = &pixels->rows[i];
        uint32_t rx = row->x + row->width, by = row->y + row->height;
        uint32_t left = fmax(row->x, x), top = fmax(row->y, y);
        uint32_t end = fmin(rx, right), foot = fmin(by, bottom);
        if (end <= left || foot <= top) continue;
        struct bm_sophia_painted_row choices[4] = {
            {row->item,row->x,row->y,left-row->x,row->height},
            {row->item,end,row->y,rx-end,row->height},
            {row->item,row->x,row->y,row->width,top-row->y},
            {row->item,row->x,foot,row->width,by-foot},
        };
        unsigned best = 0;
        for (unsigned j = 1; j < 4; ++j)
            if ((uint64_t)choices[j].width*choices[j].height > (uint64_t)choices[best].width*choices[best].height)
                best = j;
        *row = choices[best];
    }
}

static void
capture_clip(void *data, const struct cairo_result *box)
{
    struct row_capture *capture = data;
    struct bm_sophia_pixels *pixels = capture->pixels;
    double x = fmax(0, ceil(box->x * capture->scale));
    double y = fmax(0, ceil(box->y * capture->scale));
    double right = fmin(pixels->width, floor((box->x + box->width) * capture->scale));
    double bottom = fmin(pixels->height, floor((box->y + box->box_height) * capture->scale));
    unsigned count = 0;
    for (unsigned i = 0; i < pixels->row_count; ++i) {
        struct bm_sophia_painted_row row = pixels->rows[i];
        double left = fmax(row.x,x), top = fmax(row.y,y);
        double end = fmin(row.x+row.width,right), foot = fmin(row.y+row.height,bottom);
        if (end <= left || foot <= top) continue;
        pixels->rows[count++] = (struct bm_sophia_painted_row){row.item,left,top,end-left,foot-top};
    }
    pixels->row_count = count;
}

static void
capture_row(void *data, const struct bm_item *item, const struct cairo_result *box)
{
    struct row_capture *capture = data;
    struct bm_sophia_pixels *pixels = capture->pixels;
    /* Later row backgrounds occlude older horizontal/vertical targets too. */
    capture_cover(data, box);
    /* Only fully covered physical pixels authorize input; round inward. */
    double x = fmax(0, ceil(box->x * capture->scale));
    double y = fmax(0, ceil(box->y * capture->scale));
    double right = fmin(pixels->width, floor((box->x + box->width) * capture->scale));
    double bottom = fmin(pixels->height, floor((box->y + box->box_height) * capture->scale));
    if (right <= x || bottom <= y)
        return;
    if (pixels->row_count == 32) {
        capture->overflow = true;
        return;
    }
    pixels->rows[pixels->row_count++] = (struct bm_sophia_painted_row){item, x, y, right - x, bottom - y};
}

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
        !isfinite(menu->border_radius) || menu->border_radius < 0 ||
        menu->border_size >= raster->width || menu->lines > 32)
        return false;

    /* The shared painter assumes a nonzero text line fitting the allocation. */
    struct cairo_paint measure = {.font = menu->font.name};
    struct cairo_result metrics = {0};
    bm_pango_get_text_extents(&raster->painter, &measure, &metrics, "Ag");
    if (!metrics.height || metrics.height > raster->height / raster->painter.scale)
        return false;

    struct bm_sophia_pixels next = {.width = raster->width, .height = raster->height};
    struct row_capture capture = {&next, raster->painter.scale, false};
    struct cairo_row_observer observer = {capture_row, &capture, capture_cover, capture_clip};
    struct cairo_paint_result result;
    bm_cairo_paint_observed(&raster->painter, raster->width, raster->height, menu, &result, &observer);
    cairo_surface_flush(raster->painter.surface);
    if (cairo_status(raster->painter.cr) != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status(raster->painter.surface) != CAIRO_STATUS_SUCCESS || capture.overflow)
        return false;
    next.data = cairo_image_surface_get_data(raster->painter.surface);
    next.stride = raster->stride;
    next.content_height = result.height;
    next.displayed = result.displayed;
    *pixels = next;
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
