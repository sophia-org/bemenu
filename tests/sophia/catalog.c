#include "internal.h"
#include "renderers/sophia/catalog.h"
#include "renderers/sophia/view.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Link wrapping affects this executable's direct adapter allocations, not
 * allocations internal to the separately linked libbemenu. */
static int fail_after = -1;
static bool allocation_refused;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__wrap_malloc(size_t n)
{
    if (fail_after == 0) { allocation_refused = true; return NULL; }
    if (fail_after > 0) --fail_after;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t size)
{
    if (fail_after == 0) { allocation_refused = true; return NULL; }
    if (fail_after > 0) --fail_after;
    return __real_calloc(n, size);
}

/* Build complete typed file objects; there is no incremental wire catalog. */
struct wire_fixture {
    struct sophia_sf_catalog catalog;
    uint8_t rows[4 * 656];
    uint64_t generation;
    uint16_t count, used;
};
static void init(struct wire_fixture *f, uint64_t epoch)
{
    memset(f,0,sizeof(*f)); f->catalog.connection_epoch = epoch;
}
static int begin(struct wire_fixture *f, uint64_t gen, uint16_t count)
{
    assert(count <= 4); f->generation = gen; f->count = count; f->used = 0;
    return 0;
}
static int finish(struct wire_fixture *f)
{
    assert(f->used == f->count);
    f->catalog = (struct sophia_sf_catalog){1,f->catalog.connection_epoch,
        f->generation,f->count,0,f->rows,(size_t)f->count*656};
    return 1;
}
static void entry(struct wire_fixture *f, uint64_t gen, uint16_t slot,
                  const char *label, const char *keywords, bool available)
{
    assert(gen == f->generation && f->used < f->count);
    struct sophia_sf_catalog_entry row = {slot,available,
        {(const uint8_t *)label,strlen(label)},
        {(const uint8_t *)keywords,strlen(keywords)}, {NULL,0}};
    assert(sophia_sf_catalog_entry_encode(f->rows + 656*f->used,&row) == 0);
    ++f->used;
}
static struct bm_menu *menu_new(void)
{
    static struct bm_renderer renderer;
    struct bm_menu *menu = calloc(1, sizeof(*menu));
    assert(menu); menu->renderer = &renderer; menu->filter_item = bm_item_new(NULL);
    assert(menu->filter_item); return menu;
}
static struct bm_item *only(struct bm_menu *menu, const char *label)
{
    uint32_t count = 999;
    struct bm_item **items = bm_menu_get_filtered_items(menu, &count);
    assert(count == 1 && !strcmp(bm_item_get_text(items[0]), label));
    return items[0];
}
static void catalog_menu(void)
{
    struct wire_fixture wire; init(&wire, 5);
    struct bm_menu *menu = menu_new();
    struct bm_sophia_catalog_model model = {0};
    assert(!bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    assert(begin(&wire, 1, 3) == 0);
    entry(&wire, 1, 3, "Browser", "internet web", true);
    entry(&wire, 1, 7, "Editor", "code", true);
    entry(&wire, 1, 12, "Unavailable", "web", false);
    assert(finish(&wire) == 1);
    assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    assert(model.count == 2 && model.generation == 1);
    assert(!bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    bm_menu_set_filter(menu, "web"); bm_menu_filter(menu);
    struct bm_item *browser = only(menu, "Browser");
    uint64_t epoch = 0, generation = 0; uint16_t slot = 0;
    assert(bm_sophia_catalog_identity(&model, browser, &epoch, &generation, &slot));
    assert(epoch == 5 && generation == 1 && slot == 3);
    /* Case-insensitive matching still uses the upstream matching algorithm. */
    bm_menu_set_filter_mode(menu, BM_FILTER_MODE_DMENU_CASE_INSENSITIVE);
    bm_menu_set_filter(menu, "WEB"); bm_menu_filter(menu); assert(only(menu, "Browser") == browser);
    struct bm_item *foreign = bm_item_new("Browser"); assert(foreign);
    bm_item_set_userdata(foreign, browser->userdata);
    assert(!bm_sophia_catalog_identity(&model, foreign, &epoch, &generation, &slot));
    assert(epoch == 5 && generation == 1 && slot == 3); bm_item_free(foreign);
    /* A replacement never installs its unfinished object, or reuses old identity. */
    assert(begin(&wire, 2, 1) == 0);
    entry(&wire, 2, 4096, "Browser Two", "web", true);
    assert(!bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    assert(only(menu, "Browser") == browser);
    assert(finish(&wire) == 1);
    assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    struct bm_item *next = only(menu, "Browser Two");
    assert(bm_sophia_catalog_identity(&model, next, &epoch, &generation, &slot));
    assert(epoch == 5 && generation == 2 && slot == 4096);
    /* Reject another connection even when it has a numerically newer catalog. */
    struct wire_fixture other; init(&other, 6);
    assert(begin(&other, 3, 0) == 0 && finish(&other) == 1);
    assert(!bm_sophia_catalog_install_files(&model, menu, &other.catalog));
    assert(only(menu, "Browser Two") == next);
    assert(begin(&wire, 3, 0) == 0 && finish(&wire) == 1);
    assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    uint32_t count; bm_menu_get_filtered_items(menu, &count); assert(count == 0);
    assert(model.count == 0 && model.generation == 3);
    assert(bm_sophia_catalog_clear(&model)); assert(bm_sophia_catalog_clear(&model));
    bm_menu_free(menu);
}
static void foreign_menu(void)
{
    struct wire_fixture wire; init(&wire, 5);
    assert(begin(&wire, 1, 1) == 0);
    entry(&wire, 1, 1, "A", "", true); assert(finish(&wire) == 1);
    struct bm_menu *menu = menu_new(); struct bm_sophia_catalog_model model = {0};
    struct bm_item *foreign = bm_item_new("foreign"); assert(foreign && bm_menu_add_item(menu, foreign));
    assert(!bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    assert(only(menu, "foreign") == foreign);
    bm_menu_free(menu);
}
static void refused_allocation(void)
{
    for (int failure=0; failure<4; ++failure) {
        struct wire_fixture wire; init(&wire, 5);
        struct bm_menu *menu = menu_new(); struct bm_sophia_catalog_model model = {0};
        assert(begin(&wire, 1, 1) == 0);
        entry(&wire, 1, 1, "Old", "", true); assert(finish(&wire) == 1);
        assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
        struct bm_item *old = only(menu, "Old");
        assert(begin(&wire, 2, 2) == 0);
        entry(&wire, 2, 2, "New", "web", true);
        entry(&wire, 2, 3, "Second", "code", true); assert(finish(&wire) == 1);
        fail_after = failure; allocation_refused = false;
        assert(!bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
        fail_after = -1;
        assert(allocation_refused && model.generation == 1 && only(menu, "Old") == old);
        assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
        assert(model.generation == 2 && model.count == 2);
        assert(bm_sophia_catalog_clear(&model)); bm_menu_free(menu);
    }
}
static void repeated_replacement(void)
{
    struct wire_fixture wire; init(&wire, 5);
    struct bm_menu *menu = menu_new(); struct bm_sophia_catalog_model model = {0};
    bm_menu_set_filter(menu, "web");
    for (uint64_t gen=1; gen<=1000; ++gen) {
        bool populated = gen%3 != 0;
        assert(begin(&wire, gen, populated ? 1 : 0) == 0);
        if (populated) entry(&wire, gen, (uint16_t)gen, "Browser", "web", true);
        assert(finish(&wire) == 1);
        assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
        if (populated) {
            uint64_t epoch, generation; uint16_t slot;
            assert(bm_sophia_catalog_identity(&model, only(menu, "Browser"), &epoch, &generation, &slot));
            assert(epoch == 5 && generation == gen && slot == gen);
        }
    }
    assert(bm_sophia_catalog_clear(&model)); bm_menu_free(menu);
}
static void painted_catalog_identity(void)
{
    struct wire_fixture wire; init(&wire, 5);
    struct bm_menu *menu = menu_new();
    assert(bm_menu_set_font(menu, "DejaVu Sans 14"));
    for (unsigned i = 0; i < BM_COLOR_LAST; ++i)
        assert(bm_menu_set_color(menu, i, NULL));
    bm_menu_set_lines(menu, 4);
    struct bm_sophia_catalog_model model = {0};
    assert(begin(&wire, 1, 2) == 0);
    entry(&wire, 1, 7, "Browser", "web", true);
    entry(&wire, 1, 3, "Editor", "code", true);
    assert(finish(&wire) == 1);
    assert(bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    struct bm_sophia_raster *raster = bm_sophia_raster_new(640, 320, 1);
    struct bm_sophia_view view;
    assert(raster && bm_sophia_view_capture(raster, &model, &view));
    assert(view.connection_epoch == 5 && view.catalog_generation == 1);
    assert(view.row_count == 2 && view.rows[0].slot == 7 && view.rows[1].slot == 3 && view.selected == 7);
    size_t bytes = (size_t)view.stride * view.height;
    unsigned char *old_pixels = malloc(bytes); assert(old_pixels); memcpy(old_pixels, view.data, bytes);
    assert(bm_sophia_view_edit(menu, 9, NULL, 0));
    assert(bm_sophia_view_capture(raster, &model, &view) && view.selected == 3);
    assert(memcmp(old_pixels, view.data, bytes));
    struct bm_sophia_view old = view;
    assert(bm_sophia_view_edit(menu, 1, (const uint8_t *)"web", 3));
    assert(bm_sophia_view_capture(raster, &model, &view));
    assert(view.row_count == 1 && view.rows[0].slot == 7 && view.selected == 7);
    assert(!bm_sophia_view_edit(menu, 17, NULL, 0)); /* Accept is protocol-owned. */
    assert(begin(&wire, 2, 1) == 0);
    entry(&wire, 2, 4096, "New Browser", "web", true);
    assert(finish(&wire) == 1 && bm_sophia_catalog_install_files(&model, menu, &wire.catalog));
    assert(bm_sophia_view_capture(raster, &model, &view));
    assert(view.catalog_generation == 2 && view.row_count == 1 && view.selected == 4096);
    assert(old.catalog_generation == 1 && old.rows[0].slot == 7 && old.rows[1].slot == 3 && old.selected == 3);
    /* Row identity is copied, not a dangling menu pointer after replacement. */
    assert(bm_sophia_catalog_clear(&model));
    assert(!bm_sophia_view_capture(raster, &model, &view) && !view.data && !view.row_count);
    free(old_pixels); bm_sophia_raster_free(raster); bm_menu_free(menu);
}
static void file_catalog_menu(void)
{
    uint8_t rows[2 * 656];
    struct sophia_sf_catalog_entry entry = {7, 1,
        {(const uint8_t *)"Browser", 7}, {(const uint8_t *)"web", 3}, {NULL, 0}};
    assert(sophia_sf_catalog_entry_encode(rows, &entry) == 0);
    entry.slot = 9; entry.available = 0;
    assert(sophia_sf_catalog_entry_encode(rows + 656, &entry) == 0);
    struct sophia_sf_catalog catalog = {1, 5, 1, 2, 0, rows, sizeof(rows)};
    struct bm_menu *menu = menu_new();
    struct bm_sophia_catalog_model model = {0};
    assert(bm_sophia_catalog_install_files(&model, menu, &catalog));
    bm_menu_set_filter(menu, "web"); bm_menu_filter(menu);
    struct bm_item *browser = only(menu, "Browser");
    uint64_t epoch, generation; uint16_t slot;
    assert(bm_sophia_catalog_identity(&model, browser, &epoch, &generation, &slot));
    assert(epoch == 5 && generation == 1 && slot == 7 && model.count == 1);
    assert(!bm_sophia_catalog_install_files(&model, menu, &catalog));
    catalog.generation = 2;
    fail_after = 0; allocation_refused = false;
    assert(!bm_sophia_catalog_install_files(&model, menu, &catalog));
    fail_after = -1;
    assert(allocation_refused && only(menu, "Browser") == browser && model.generation == 1);
    assert(bm_sophia_catalog_install_files(&model, menu, &catalog));
    /* The object can go away immediately: menu strings and identities are owned. */
    memset(rows, 0, sizeof(rows));
    assert(only(menu, "Browser"));
    assert(bm_sophia_catalog_clear(&model)); bm_menu_free(menu);
}

int main(void)
{
    catalog_menu(); foreign_menu(); refused_allocation(); repeated_replacement(); painted_catalog_identity(); file_catalog_menu();
    return 0;
}
