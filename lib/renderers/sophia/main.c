#define _GNU_SOURCE
#include "internal.h"
#include "connection.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct stat before;
    if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path) ||
        lstat(path,&before) || !S_ISSOCK(before.st_mode) || before.st_uid != geteuid()) return -1;
    memcpy(address.sun_path,path,strlen(path)+1);
    int fd = socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if (fd < 0) return -1;
    if (connect(fd,(struct sockaddr *)&address,sizeof(address)) < 0) {
        if (errno != EINPROGRESS) {close(fd); return -1;}
        uint64_t start, now;
        if (!monotonic(&start)) {close(fd); return -1;}
        while (!stopping) {
            if (!monotonic(&now) || now < start || now-start >= BM_SOPHIA_FAILURE_TIMEOUT_MS) break;
            struct pollfd poller = {fd,POLLOUT,0};
            int ready = poll(&poller,1,50);
            if (ready < 0 && errno != EINTR) break;
            if (ready > 0) {
                int error = 0; socklen_t bytes = sizeof(error);
                if (!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&bytes) && !error) goto connected;
                break;
            }
        }
        close(fd); return -1;
    }
connected:;
    struct ucred peer; socklen_t bytes = sizeof(peer);
    if (getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&bytes) || bytes != sizeof(peer) || peer.uid != geteuid()) {
        close(fd); return -1;
    }
    /* Same-user endpoint authentication is not evidence of protected admission;
     * Session authenticates/confines this child and the wire grants authority. */
    return fd;
}

static uint32_t displayed(const struct bm_menu *menu)
{
    struct bm_sophia_connection_snapshot state;
    return bm_sophia_connection_inspect(menu->userdata,&state) ? state.displayed : 0;
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

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1],"--help")) {
        puts("Usage: bemenu-sophia --serve\nRequires Session's SOPHIA_SHELL_SOCKET; persistent native launcher.");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1],"--serve")) {
        fputs("Usage: bemenu-sophia --serve\n",stderr); return 2;
    }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM,&action,NULL) || sigaction(SIGINT,&action,NULL)) return 1;
    int fd = connect_endpoint(getenv("SOPHIA_SHELL_SOCKET"));
    if (fd < 0) {fputs("bemenu_native status=failed stage=connect\n",stderr); return 1;}
    struct bm_menu *menu = native_menu();
    struct bm_sophia_connection *connection = NULL;
    if (!menu || bm_sophia_connection_new(menu,fd,&connection) != SOPHIA_SHELL_OK) {
        close(fd); if (menu) bm_menu_free(menu);
        fputs("bemenu_native status=failed stage=initialize\n",stderr); return 1;
    }
    menu->userdata = connection;
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
    fprintf(stderr,"bemenu_native status=stopped result=%d\n",result);
    return result;
}
