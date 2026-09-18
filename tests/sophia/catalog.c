#include "internal.h"
#include "renderers/sophia/catalog.h"
#include "../../vendor/sophia-shell/shell_wire/fields.h"
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

struct wire_fixture {
    struct sophia_shell_catalog catalog;
    struct sophia_shell_catalog_entry a[4], b[4];
};
static void init(struct wire_fixture *f, uint64_t epoch)
{
    struct sophia_shell_welcome welcome = {7,epoch,SOPHIA_SHELL_CAP_APPLICATION_CATALOG,0,0,0};
    assert(sophia_shell_catalog_init(&f->catalog, &welcome, f->a, f->b, 4) == 0);
}
static int control(struct wire_fixture *f, uint16_t kind, uint64_t gen, uint16_t count)
{
    uint8_t p[20] = {0};
    shell_put64(p, f->catalog.connection_epoch); shell_put64(p+8, gen); shell_put16(p+16, count);
    struct sophia_shell_frame frame = {kind,1,p,kind == 114 ? 20 : 16};
    return sophia_shell_catalog_accept(&f->catalog, &frame);
}
static void entry(struct wire_fixture *f, uint64_t gen, uint16_t slot,
                  const char *label, const char *keywords, bool available)
{
    uint8_t p[408] = {0};
    size_t n = strlen(label), k = strlen(keywords);
    assert(n <= 128 && k <= 256);
    shell_put64(p, f->catalog.connection_epoch); shell_put64(p+8, gen);
    shell_put16(p+16, slot); shell_put16(p+18, available); shell_put16(p+20, (uint16_t)n);
    memcpy(p+22, label, n); shell_put16(p+22+n, (uint16_t)k); memcpy(p+24+n, keywords, k);
    struct sophia_shell_frame frame = {115,1,p,24+n+k};
    assert(sophia_shell_catalog_accept(&f->catalog, &frame) == 0);
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
    assert(!bm_sophia_catalog_install(&model, menu, &wire.catalog));
    assert(control(&wire, 114, 1, 3) == 0);
    entry(&wire, 1, 3, "Browser", "internet web", true);
    entry(&wire, 1, 7, "Editor", "code", true);
    entry(&wire, 1, 12, "Unavailable", "web", false);
    assert(control(&wire, 116, 1, 0) == 1);
    assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
    assert(model.count == 2 && model.generation == 1);
    assert(!bm_sophia_catalog_install(&model, menu, &wire.catalog));
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
    /* A replacement never exposes its partial transfer, or reuses old identity. */
    assert(control(&wire, 114, 2, 1) == 0);
    entry(&wire, 2, 4096, "Browser Two", "web", true);
    assert(!bm_sophia_catalog_install(&model, menu, &wire.catalog));
    assert(only(menu, "Browser") == browser);
    assert(control(&wire, 116, 2, 0) == 1);
    assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
    struct bm_item *next = only(menu, "Browser Two");
    assert(bm_sophia_catalog_identity(&model, next, &epoch, &generation, &slot));
    assert(epoch == 5 && generation == 2 && slot == 4096);
    /* Reject another connection even when it has a numerically newer catalog. */
    struct wire_fixture other; init(&other, 6);
    assert(control(&other, 114, 3, 0) == 0 && control(&other, 116, 3, 0) == 1);
    assert(!bm_sophia_catalog_install(&model, menu, &other.catalog));
    assert(only(menu, "Browser Two") == next);
    assert(control(&wire, 114, 3, 0) == 0 && control(&wire, 116, 3, 0) == 1);
    assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
    uint32_t count; bm_menu_get_filtered_items(menu, &count); assert(count == 0);
    assert(model.count == 0 && model.generation == 3);
    assert(bm_sophia_catalog_clear(&model)); assert(bm_sophia_catalog_clear(&model));
    bm_menu_free(menu);
}
static void foreign_menu(void)
{
    struct wire_fixture wire; init(&wire, 5);
    assert(control(&wire, 114, 1, 1) == 0);
    entry(&wire, 1, 1, "A", "", true); assert(control(&wire, 116, 1, 0) == 1);
    struct bm_menu *menu = menu_new(); struct bm_sophia_catalog_model model = {0};
    struct bm_item *foreign = bm_item_new("foreign"); assert(foreign && bm_menu_add_item(menu, foreign));
    assert(!bm_sophia_catalog_install(&model, menu, &wire.catalog));
    assert(only(menu, "foreign") == foreign);
    bm_menu_free(menu);
}
static void refused_allocation(void)
{
    for (int failure=0; failure<4; ++failure) {
        struct wire_fixture wire; init(&wire, 5);
        struct bm_menu *menu = menu_new(); struct bm_sophia_catalog_model model = {0};
        assert(control(&wire, 114, 1, 1) == 0);
        entry(&wire, 1, 1, "Old", "", true); assert(control(&wire, 116, 1, 0) == 1);
        assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
        struct bm_item *old = only(menu, "Old");
        assert(control(&wire, 114, 2, 2) == 0);
        entry(&wire, 2, 2, "New", "web", true);
        entry(&wire, 2, 3, "Second", "code", true); assert(control(&wire, 116, 2, 0) == 1);
        fail_after = failure; allocation_refused = false;
        assert(!bm_sophia_catalog_install(&model, menu, &wire.catalog));
        fail_after = -1;
        assert(allocation_refused && model.generation == 1 && only(menu, "Old") == old);
        assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
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
        assert(control(&wire, 114, gen, populated ? 1 : 0) == 0);
        if (populated) entry(&wire, gen, (uint16_t)gen, "Browser", "web", true);
        assert(control(&wire, 116, gen, 0) == 1);
        assert(bm_sophia_catalog_install(&model, menu, &wire.catalog));
        if (populated) {
            uint64_t epoch, generation; uint16_t slot;
            assert(bm_sophia_catalog_identity(&model, only(menu, "Browser"), &epoch, &generation, &slot));
            assert(epoch == 5 && generation == gen && slot == gen);
        }
    }
    assert(bm_sophia_catalog_clear(&model)); bm_menu_free(menu);
}
int main(void)
{
    catalog_menu(); foreign_menu(); refused_allocation(); repeated_replacement();
    return 0;
}
