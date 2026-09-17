#include "internal.h"
#include "renderers/sophia/raster.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct bm_menu *
fixture(void)
{
    static struct bm_renderer renderer;
    struct bm_menu *menu = calloc(1, sizeof(*menu));
    assert(menu);
    menu->renderer = &renderer;
    menu->filter_item = bm_item_new(NULL);
    assert(menu->filter_item && bm_menu_set_font(menu, "DejaVu Sans 14"));
    for (unsigned i = 0; i < BM_COLOR_LAST; ++i)
        assert(bm_menu_set_color(menu, i, NULL));
    bm_menu_set_lines(menu, 4);
    const char *items[] = {"Terminal", "Browser", "Résumé — 日本語", "Files"};
    for (unsigned i = 0; i < sizeof(items) / sizeof(*items); ++i) {
        struct bm_item *item = bm_item_new(items[i]);
        assert(item && bm_menu_add_item(menu, item));
    }
    return menu;
}

static unsigned char *
copy_pixels(const struct bm_sophia_pixels *pixels)
{
    size_t bytes = (size_t)pixels->stride * pixels->height;
    unsigned char *copy = malloc(bytes);
    assert(copy);
    memcpy(copy, pixels->data, bytes);
    return copy;
}

static void
menu_behavior(void)
{
    struct bm_menu *menu = fixture();
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1);
    struct bm_sophia_pixels pixels;
    assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
    assert(pixels.width == 640 && pixels.height == 320 && pixels.stride == 2560);
    assert(pixels.content_height > 0 && pixels.content_height <= pixels.height);
    unsigned char *before = copy_pixels(&pixels);
    size_t bytes = (size_t)pixels.stride * pixels.height;
    assert(bm_sophia_raster_paint(raster, menu, &pixels));
    assert(!memcmp(before, pixels.data, bytes));

    bm_menu_run_with_key(menu, BM_KEY_DOWN, 0);
    assert(!strcmp(bm_item_get_text(bm_menu_get_highlighted_item(menu)), "Browser"));
    assert(bm_sophia_raster_paint(raster, menu, &pixels));
    assert(memcmp(before, pixels.data, bytes));

    bm_menu_set_filter(menu, "Résumé");
    bm_menu_filter(menu);
    uint32_t count = 0;
    struct bm_item **items = bm_menu_get_filtered_items(menu, &count);
    assert(count == 1 && !strcmp(bm_item_get_text(items[0]), "Résumé — 日本語"));
    assert(bm_sophia_raster_paint(raster, menu, &pixels));
    memcpy(before, pixels.data, bytes);
    assert(bm_menu_set_color(menu, BM_COLOR_FILTER_BG, "#123456"));
    assert(bm_sophia_raster_paint(raster, menu, &pixels));
    assert(memcmp(before, pixels.data, bytes));
    free(before);
    bm_sophia_raster_free(raster);
    bm_menu_free(menu);
}

static void
scale_and_bounds(void)
{
    assert(!bm_sophia_raster_new(0, 320, 1));
    assert(!bm_sophia_raster_new(8192, 8192, 1));
    assert(!bm_sophia_raster_new(4096, 2048, 1));
    assert(!bm_sophia_raster_new(640, 320, NAN));
    assert(!bm_sophia_raster_new(640, 320, INFINITY));
    assert(!bm_sophia_raster_new(640, 320, 0));
    struct bm_menu *menu = fixture();
    uint32_t base = 0;
    for (unsigned scale = 1; scale <= 2; ++scale) {
        struct bm_sophia_raster *raster = bm_sophia_raster_new(640 * scale, 320 * scale, scale);
        struct bm_sophia_pixels pixels;
        assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
        if (scale == 1)
            base = pixels.content_height;
        else
            assert(pixels.content_height == base * scale);
        bm_sophia_raster_free(raster);
    }
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1.25);
    struct bm_sophia_pixels pixels;
    assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
    assert(pixels.content_height > base && pixels.content_height <= 320);
    assert(bm_menu_set_font(menu, "DejaVu Sans 2048"));
    assert(!bm_sophia_raster_paint(raster, menu, &pixels));
    assert(!pixels.data);
    bm_sophia_raster_free(raster);
    assert(bm_menu_set_items(menu, NULL, 0));
    bm_menu_free(menu);
}

int
main(void)
{
    assert(bm_init());
    /* Explicit selection must refuse until real Sophia admission is available. */
    uint32_t count = 0;
    const struct bm_renderer **renderers = bm_get_renderers(&count);
    assert(count == 1 && !strcmp(bm_renderer_get_name(renderers[0]), "sophia"));
    assert(!bm_menu_new("sophia"));
    menu_behavior();
    scale_and_bounds();
    return 0;
}
