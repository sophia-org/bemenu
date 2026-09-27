#define _GNU_SOURCE
#include "internal.h"
#include "connection.h"
#include "files.h"
#include "fonts.h"
#include "../../../vendor/sophia-desktop-sdk/source/src/sophia_desktop_connection.h"
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

static int connect_endpoint(const char *path)
{
    struct sophia_desktop_connection connection = {.fd = -1};
    uint64_t start, now;
    if (!monotonic(&start)) return -1;
    int state = sophia_desktop_connection_begin(&connection,path);
    while (!stopping && state >= 0) {
        if (state == SOPHIA_DESKTOP_CONNECTED)
            return sophia_desktop_connection_take(&connection);
        if (!monotonic(&now) || now < start || now-start >= BM_SOPHIA_FAILURE_TIMEOUT_MS) break;
        if (state == SOPHIA_DESKTOP_CONNECT_RETRY) {
            if (poll(NULL,0,SOPHIA_DESKTOP_CONNECT_RETRY_MS) < 0 && errno != EINTR) break;
            state = sophia_desktop_connection_begin(&connection,path);
        } else {
            struct pollfd poller = {connection.fd,sophia_desktop_connection_events(&connection),0};
            int ready = poll(&poller,1,50);
            if (ready < 0 && errno != EINTR) break;
            if (ready > 0) state = sophia_desktop_connection_finish(&connection,poller.revents);
        }
    }
    sophia_desktop_connection_close(&connection);
    return -1;
}

struct native_client {
    struct bm_sophia_connection *ipc;
    struct bm_sophia_files *files;
};
static uint32_t displayed(const struct bm_menu *menu)
{
    const struct native_client *client = menu->userdata;
    if (!client) return 0;
    if (client->files) {
        struct bm_sophia_files_snapshot state;
        return bm_sophia_files_inspect(client->files,&state) ? state.displayed : 0;
    }
    struct bm_sophia_connection_snapshot state;
    return bm_sophia_connection_inspect(client->ipc,&state) ? state.displayed : 0;
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
        puts("Usage: bemenu-sophia --serve\nRequires exactly one of Session's SOPHIA_SHELL_9P_SOCKET or SOPHIA_SHELL_SOCKET; persistent native launcher.");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1],"--serve")) {
        fputs("Usage: bemenu-sophia --serve\n",stderr); return 2;
    }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM,&action,NULL) || sigaction(SIGINT,&action,NULL)) return 1;
    struct sophia_desktop_endpoint endpoint;
    if (sophia_desktop_shell_environment(&endpoint) != 0) {
        fputs("bemenu_native status=failed stage=connect\n",stderr); return 1;
    }
    int fd = endpoint.wire == SOPHIA_DESKTOP_IPC ? connect_endpoint(endpoint.path) : -1;
    if (endpoint.wire == SOPHIA_DESKTOP_IPC && fd < 0) {
        fputs("bemenu_native status=failed stage=connect\n",stderr); return 1;
    }
    const char *const font_directories[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
    struct bm_sophia_fonts *fonts = bm_sophia_fonts_new(font_directories,2);
    if (!fonts) {if (fd >= 0) close(fd); fputs("bemenu_native status=failed stage=fonts\n",stderr); return 1;}
    struct bm_menu *menu = native_menu();
    if (endpoint.wire == SOPHIA_DESKTOP_FILES) {
        int result = menu ? serve_files(menu,endpoint.path) : 1;
        if (menu) bm_menu_free(menu);
        if (!bm_sophia_fonts_free(fonts)) result = 1;
        fprintf(stderr,"bemenu_native status=stopped result=%d\n",result);
        return result;
    }
    struct bm_sophia_connection *connection = NULL;
    if (!menu || bm_sophia_connection_new(menu,fd,&connection) != SOPHIA_SHELL_OK) {
        close(fd); if (menu) bm_menu_free(menu);
        if (!bm_sophia_fonts_free(fonts)) fputs("bemenu_native status=failed stage=font_cache_cleanup\n",stderr);
        fputs("bemenu_native status=failed stage=initialize\n",stderr); return 1;
    }
    struct native_client client = {.ipc = connection};
    menu->userdata = &client;
    bool announced = false;
    int result = 0;
    while (!stopping) {
        int step = bm_sophia_connection_service(connection);
        struct bm_sophia_connection_snapshot state;
        if (!bm_sophia_connection_inspect(connection,&state)) {result = 1; break;}
        if (step != SOPHIA_SHELL_AGAIN && step != SOPHIA_SHELL_BUSY && step != SOPHIA_SHELL_OK) {
            fprintf(stderr,"bemenu_native status=failed stage=service code=%d timeout=%u\n",step,(unsigned)state.timeout);
            result = 1; break;
        }
        if (state.lifecycle && !announced) {
            fprintf(stderr,"bemenu_native status=negotiated revision=7 epoch=%llu\n",(unsigned long long)state.connection_epoch);
            announced = true;
        }
        if (step == SOPHIA_SHELL_BUSY) {
            /* A retained borrowed frame may leave more readable peer data.
             * Do not spin on that readiness while its admission is refused. */
            if (poll(NULL,0,10) < 0 && errno != EINTR) {result = 1; break;}
            continue;
        }
        struct pollfd poller = {fd,POLLIN,0};
        if (state.queued_records && step != SOPHIA_SHELL_BUSY) poller.events |= POLLOUT;
        if (poll(&poller,1,20) < 0 && errno != EINTR) {result = 1; break;}
    }
    close(fd); /* End the connection before freeing local owners. */
    menu->userdata = NULL;
    bm_sophia_connection_dispose(connection); bm_menu_free(menu);
    if (!bm_sophia_fonts_free(fonts)) {
        fputs("bemenu_native status=failed stage=font_cache_cleanup\n",stderr); result = 1;
    }
    fprintf(stderr,"bemenu_native status=stopped result=%d\n",result);
    return result;
}
