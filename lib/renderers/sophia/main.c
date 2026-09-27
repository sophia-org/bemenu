#define _GNU_SOURCE
#include "internal.h"
#include "files.h"
#include "fonts.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int number) {(void)number; stopping = 1;}
static bool monotonic(uint64_t *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec < 0 ||
        (uint64_t)now.tv_sec > (UINT64_MAX-999)/1000) return false;
    *out = (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
    return true;
}

struct native_client {
    struct bm_sophia_files *files;
};
static uint32_t displayed(const struct bm_menu *menu)
{
    const struct native_client *client = menu->userdata;
    if (!client) return 0;
    struct bm_sophia_files_snapshot state;
    return bm_sophia_files_inspect(client->files,&state) ? state.displayed : 0;
}
static struct bm_menu *native_menu(void)
{
    /* Private native entry: no dynamic renderer discovery or stdin catalog.
     * Menu/filtering/Cairo behavior remains the existing libbemenu implementation. */
    static struct bm_renderer renderer = {.api = {.get_displayed_count = displayed}};
    struct bm_menu *menu = calloc(1,sizeof(*menu));
    if (!menu) return NULL;
    menu->renderer = &renderer; menu->dirty = true;
    menu->key_binding = BM_KEY_BINDING_DEFAULT; menu->vim_mode = 'i';
    menu->filter_item = bm_item_new(NULL);
    if (!menu->filter_item || !bm_menu_set_font(menu,NULL)) goto failed;
    for (unsigned i = 0; i < BM_COLOR_LAST; ++i)
        if (!bm_menu_set_color(menu,i,NULL)) goto failed;
    bm_menu_set_lines(menu,4);
    return menu;
failed:
    bm_menu_free(menu); return NULL;
}

static int serve_files(struct bm_menu *menu, const char *path)
{
    struct native_client client = {0};
    uint64_t now;
    if (!monotonic(&now) || bm_sophia_files_new(menu,path,now,&client.files) < 0) {
        fputs("bemenu_native status=failed stage=initialize wire=9p\n",stderr);
        return 1;
    }
    menu->userdata = &client;
    bool announced = false;
    int result = 0;
    short revents = 0;
    while (!stopping) {
        if (!monotonic(&now)) {result = 1; break;}
        int step = bm_sophia_files_progress(client.files,revents,now);
        struct bm_sophia_files_snapshot state;
        if (!bm_sophia_files_inspect(client.files,&state)) {result = 1; break;}
        if (step < 0) {
            fprintf(stderr,"bemenu_native status=failed stage=service wire=9p code=%d phase=%u\n",
                    step,(unsigned)state.failed);
            result = 1; break;
        }
        if (state.native && !announced) {
            fprintf(stderr,"bemenu_native status=negotiated revision=7 epoch=%llu wire=9p\n",
                    (unsigned long long)state.connection_epoch);
            announced = true;
        }
        struct pollfd poller;
        int timeout;
        if (bm_sophia_files_poll(client.files,now,&poller,&timeout) < 0) {result = 1; break;}
        int ready = poll(&poller,1,timeout);
        if (ready < 0 && errno != EINTR) {result = 1; break;}
        revents = ready > 0 ? poller.revents : 0;
    }
    bm_sophia_files_dispose(client.files);
    menu->userdata = NULL;
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1],"--help")) {
        puts("Usage: bemenu-sophia --serve\nRequires Session's SOPHIA_SHELL_9P_SOCKET; persistent native launcher.");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1],"--serve")) {
        fputs("Usage: bemenu-sophia --serve\n",stderr); return 2;
    }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM,&action,NULL) || sigaction(SIGINT,&action,NULL)) return 1;
    const char *path = getenv("SOPHIA_SHELL_9P_SOCKET");
    if (getenv("SOPHIA_SHELL_SOCKET") || !path || !*path) {
        fputs("bemenu_native status=failed stage=connect transport=9p_required\n",stderr);
        return 1;
    }
    const char *const font_directories[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
    struct bm_sophia_fonts *fonts = bm_sophia_fonts_new(font_directories,2);
    if (!fonts) {fputs("bemenu_native status=failed stage=fonts\n",stderr); return 1;}
    struct bm_menu *menu = native_menu();
    int result = menu ? serve_files(menu,path) : 1;
    if (menu) bm_menu_free(menu);
    if (!bm_sophia_fonts_free(fonts)) result = 1;
    fprintf(stderr,"bemenu_native status=stopped result=%d\n",result);
    return result;
}
