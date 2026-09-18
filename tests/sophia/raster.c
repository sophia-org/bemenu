#include "internal.h"
#include "renderers/sophia/raster.h"
#include "renderers/sophia/input.h"
#include "renderers/cairo_renderer.h"

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
painted_rows(void)
{
    struct bm_menu *menu = fixture();
    assert(bm_menu_set_color(menu, BM_COLOR_ITEM_BG, "#113355"));
    assert(bm_menu_set_color(menu, BM_COLOR_ALTERNATE_BG, "#224466"));
    assert(bm_menu_set_color(menu, BM_COLOR_HIGHLIGHTED_BG, "#335577"));
    for (unsigned step = 0; step < 3; ++step) {
        double scale = step == 0 ? 1 : step == 1 ? 1.25 : 2;
        struct bm_sophia_raster *raster = bm_sophia_raster_new(640 * scale, 320 * scale, scale);
        struct bm_sophia_pixels pixels;
        assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
        assert(pixels.row_count == 4);
        uint32_t count;
        struct bm_item **items = bm_menu_get_filtered_items(menu, &count);
        assert(count == 4);
        for (unsigned i = 0; i < count; ++i) {
            const struct bm_sophia_painted_row *row = &pixels.rows[i];
            assert(row->item == items[i] && row->height && row->width);
            assert(row->x + row->width <= pixels.width && row->y + row->height <= pixels.height);
            unsigned x = row->x + row->width - 10, y = row->y + row->height / 2;
            const unsigned char *pixel = pixels.data + y * pixels.stride + 4 * x;
            enum bm_color color = i == 0 ? BM_COLOR_HIGHLIGHTED_BG : i % 2 ? BM_COLOR_ALTERNATE_BG : BM_COLOR_ITEM_BG;
            /* Upstream stores parsed green in b, blue in g, and swaps them
             * back at cairo_set_source_rgba. Check the actual BGRA bytes. */
            assert(pixel[0] == menu->colors[color].g && pixel[1] == menu->colors[color].b &&
                   pixel[2] == menu->colors[color].r && pixel[3] == 255);
        }
        /* Observing item geometry must not change the upstream painter pixels. */
        struct cairo ordinary = {.scale = scale, .antialiasing = true};
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixels.width, pixels.height);
        assert(bm_cairo_create_for_surface(&ordinary, surface));
        struct cairo_paint_result result;
        bm_cairo_paint(&ordinary, pixels.width, pixels.height, menu, &result);
        cairo_surface_flush(surface);
        assert(!memcmp(pixels.data, cairo_image_surface_get_data(surface), (size_t)pixels.stride * pixels.height));
        bm_cairo_destroy(&ordinary);
        bm_sophia_raster_free(raster);
    }
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1);
    struct bm_sophia_pixels pixels;
    bm_menu_set_lines(menu, 2);
    bm_menu_run_with_key(menu, BM_KEY_DOWN, 0);
    bm_menu_run_with_key(menu, BM_KEY_DOWN, 0);
    assert(bm_sophia_raster_paint(raster, menu, &pixels) && pixels.row_count == 2);
    assert(!strcmp(bm_item_get_text(pixels.rows[0].item), "Résumé — 日本語"));
    assert(!strcmp(bm_item_get_text(pixels.rows[1].item), "Files"));
    bm_menu_set_lines_mode(menu, BM_LINES_UP);
    assert(bm_sophia_raster_paint(raster, menu, &pixels) && pixels.row_count == 2);
    assert(!strcmp(bm_item_get_text(pixels.rows[0].item), "Files"));
    assert(!strcmp(bm_item_get_text(pixels.rows[1].item), "Résumé — 日本語"));
    bm_menu_set_lines_mode(menu, BM_LINES_DOWN);
    bm_menu_set_filter(menu, "Files"); bm_menu_filter(menu);
    assert(bm_sophia_raster_paint(raster, menu, &pixels) && pixels.row_count == 1);
    assert(!strcmp(bm_item_get_text(pixels.rows[0].item), "Files"));
    bm_menu_set_filter(menu, "no matches"); bm_menu_filter(menu);
    assert(bm_sophia_raster_paint(raster, menu, &pixels) && pixels.row_count == 0);
    bm_menu_set_filter(menu, ""); bm_menu_filter(menu); bm_menu_set_lines(menu, 0);
    assert(bm_sophia_raster_paint(raster, menu, &pixels) && pixels.row_count > 0);
    for (unsigned i = 1; i < pixels.row_count; ++i)
        assert(pixels.rows[i].x > pixels.rows[i - 1].x);
    bm_sophia_raster_free(raster); bm_menu_free(menu);

    menu = fixture(); bm_menu_set_lines(menu, 0);
    for (unsigned i = 0; i < 40; ++i) {
        struct bm_item *item = bm_item_new("A");
        assert(item && bm_menu_add_item(menu, item));
    }
    raster = bm_sophia_raster_new(8192, 128, 1);
    assert(raster && !bm_sophia_raster_paint(raster, menu, &pixels));
    assert(!pixels.data && !pixels.row_count); /* No silently truncated target set. */
    bm_sophia_raster_free(raster); bm_menu_free(menu);
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

static void
semantic_input(void)
{
    struct bm_menu *menu = fixture();
    const uint8_t text[] = "caf\xc3\xa9";
    assert(bm_sophia_menu_input(menu, 1, text, sizeof(text)-1) == BM_SOPHIA_INPUT_APPLIED);
    assert(!strcmp(menu->filter, "caf\xc3\xa9"));
    assert(bm_sophia_menu_input(menu, 6, NULL, 0) == BM_SOPHIA_INPUT_APPLIED);
    assert(!strcmp(menu->filter, "caf"));
    const uint8_t invalid[] = {'a', 0xff};
    assert(bm_sophia_menu_input(menu, 1, invalid, sizeof(invalid)) == BM_SOPHIA_INPUT_REFUSED);
    assert(!strcmp(menu->filter, "caf"));
    assert(bm_sophia_menu_input(menu, 17, NULL, 0) == BM_SOPHIA_INPUT_ACCEPT);
    assert(!bm_menu_get_selected_items(menu, NULL));
    assert(bm_sophia_menu_input(menu, 18, NULL, 0) == BM_SOPHIA_INPUT_REFUSED);
    assert(bm_sophia_menu_input(menu, 2, text, 1) == BM_SOPHIA_INPUT_REFUSED);
    bm_menu_set_filter(menu, "");
    bm_menu_filter(menu);
    assert(bm_sophia_menu_input(menu, 13, NULL, 0) == BM_SOPHIA_INPUT_APPLIED);
    assert(!strcmp(bm_item_get_text(bm_menu_get_highlighted_item(menu)), "Files"));
    assert(bm_sophia_menu_input(menu, 12, NULL, 0) == BM_SOPHIA_INPUT_APPLIED);
    assert(!strcmp(bm_item_get_text(bm_menu_get_highlighted_item(menu)), "Terminal"));
    /* Accepted semantic edits reach the actual raster, not just menu metadata. */
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1);
    struct bm_sophia_pixels pixels;
    assert(raster && bm_sophia_raster_paint(raster, menu, &pixels));
    unsigned char *before = copy_pixels(&pixels);
    size_t pixel_bytes = (size_t)pixels.stride * pixels.height;
    assert(bm_sophia_menu_input(menu, 9, NULL, 0) == BM_SOPHIA_INPUT_APPLIED);
    assert(!strcmp(bm_item_get_text(bm_menu_get_highlighted_item(menu)), "Browser"));
    assert(bm_sophia_raster_paint(raster, menu, &pixels));
    assert(memcmp(before, pixels.data, pixel_bytes));
    free(before);
    bm_sophia_raster_free(raster);

    /* Reject the complete event before applying even its valid prefix. */
    const uint8_t control[] = {'x', '\n'};
    const uint8_t bidi[] = {'x', 0xe2, 0x80, 0xae};
    assert(bm_sophia_menu_input(menu, 1, control, sizeof(control)) == BM_SOPHIA_INPUT_REFUSED);
    assert(bm_sophia_menu_input(menu, 1, bidi, sizeof(bidi)) == BM_SOPHIA_INPUT_REFUSED);
    assert(!menu->filter || !menu->filter[0]);
    uint8_t oversized[257];
    memset(oversized, 'x', sizeof(oversized));
    assert(bm_sophia_menu_input(menu, 1, oversized, sizeof(oversized)) == BM_SOPHIA_INPUT_REFUSED);
    char full[257];
    memset(full, 'x', sizeof(full)-1);
    full[sizeof(full)-1] = 0;
    bm_menu_set_filter(menu, full);
    assert(bm_sophia_menu_input(menu, 1, text, 1) == BM_SOPHIA_INPUT_REFUSED);
    assert(!strcmp(menu->filter, full));
    full[255] = 0;
    bm_menu_set_filter(menu, full);
    assert(bm_sophia_menu_input(menu, 1, text, 1) == BM_SOPHIA_INPUT_APPLIED);
    assert(strlen(menu->filter) == 256);
    assert(bm_sophia_menu_input(menu, 1, text, 1) == BM_SOPHIA_INPUT_REFUSED);
    assert(strlen(menu->filter) == 256);
    menu->key_binding = BM_KEY_BINDING_VIM;
    assert(bm_sophia_menu_input(menu, 17, NULL, 0) == BM_SOPHIA_INPUT_REFUSED);
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
    semantic_input();
    painted_rows();
    menu_behavior();
    scale_and_bounds();
    return 0;
}
