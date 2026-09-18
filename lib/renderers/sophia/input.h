#ifndef BM_SOPHIA_INPUT_H
#define BM_SOPHIA_INPUT_H
#include <stddef.h>
#include <stdint.h>
struct bm_menu;
enum bm_sophia_input_result {
    BM_SOPHIA_INPUT_REFUSED,
    BM_SOPHIA_INPUT_APPLIED,
    BM_SOPHIA_INPUT_ACCEPT
};
/* Menu semantics only, after caller validates the current native event. This
 * does not ACK, advance protocol revision, select an executable or run a helper.
 * Accept is returned to the protocol owner without completing the local menu.
 * Native semantic commands use the default key mode; Vim raw-key mode refuses. */
enum bm_sophia_input_result bm_sophia_menu_input(struct bm_menu *menu,
        uint16_t kind, const uint8_t *text, size_t bytes);
#endif
