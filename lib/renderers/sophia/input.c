#include "internal.h"
#include "input.h"
#include <glib.h>
#include <string.h>

#define MAX_EVENT_TEXT 256u
#define MAX_FILTER_BYTES 4096u

enum bm_sophia_input_result
bm_sophia_menu_input(struct bm_menu *menu, uint16_t kind, const uint8_t *text, size_t bytes)
{
    if (!menu || menu->key_binding != BM_KEY_BINDING_DEFAULT || kind < 1 || kind > 17)
        return BM_SOPHIA_INPUT_REFUSED;
    if (kind != 1 && bytes)
        return BM_SOPHIA_INPUT_REFUSED;
    if (kind == 17)
        return BM_SOPHIA_INPUT_ACCEPT;
    if (kind == 1) {
        if (!text || !bytes || bytes > MAX_EVENT_TEXT ||
            (menu->filter && strlen(menu->filter) > MAX_FILTER_BYTES - bytes) ||
            !g_utf8_validate((const char *)text, (gssize)bytes, NULL))
            return BM_SOPHIA_INPUT_REFUSED;
        /* Validate the entire event before changing the actual menu. */
        const char *end = (const char *)text + bytes;
        for (const char *p = (const char *)text; p < end; p = g_utf8_next_char(p)) {
            gunichar c = g_utf8_get_char(p);
            if (g_unichar_iscntrl(c) || (c >= 0x202a && c <= 0x202e) ||
                (c >= 0x2066 && c <= 0x2069))
                return BM_SOPHIA_INPUT_REFUSED;
        }
        for (const char *p = (const char *)text; p < end; p = g_utf8_next_char(p))
            bm_menu_run_with_key(menu, BM_KEY_UNICODE, g_utf8_get_char(p));
        return BM_SOPHIA_INPUT_APPLIED;
    }
    if (kind == 12 || kind == 13) {
        uint32_t count;
        bm_menu_get_filtered_items(menu, &count);
        if (count)
            bm_menu_set_highlighted_index(menu, kind == 12 ? 0 : count - 1);
        return BM_SOPHIA_INPUT_APPLIED;
    }
    static const enum bm_key keys[17] = {
        BM_KEY_NONE, BM_KEY_NONE, BM_KEY_LEFT, BM_KEY_RIGHT, BM_KEY_HOME,
        BM_KEY_END, BM_KEY_BACKSPACE, BM_KEY_DELETE, BM_KEY_UP, BM_KEY_DOWN,
        BM_KEY_PAGE_UP, BM_KEY_PAGE_DOWN, BM_KEY_NONE, BM_KEY_NONE,
        BM_KEY_LINE_DELETE_LEFT, BM_KEY_LINE_DELETE_RIGHT, BM_KEY_WORD_DELETE
    };
    bm_menu_run_with_key(menu, keys[kind], 0);
    return BM_SOPHIA_INPUT_APPLIED;
}
