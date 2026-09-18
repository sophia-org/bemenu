#define _GNU_SOURCE
#include "fonts.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct bm_sophia_fonts {
    FcConfig *config;
    int directory;
    char cache[64];
};

bool bm_sophia_fonts_free(struct bm_sophia_fonts *fonts)
{
    if (!fonts) return true;
    if (fonts->config) FcConfigDestroy(fonts->config);
    bool complete = true;
    if (fonts->directory >= 0) {
        DIR *entries = fdopendir(fonts->directory);
        if (!entries) {close(fonts->directory); complete = false;}
        else {
            /* Fontconfig caches are flat files. Never traverse a child link or
             * directory during cleanup; bound work even on an unexpected tree. */
            unsigned visited = 0;
            errno = 0;
            struct dirent *entry;
            while ((entry = readdir(entries))) {
                if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
                if (++visited > 4096) {complete = false; break;}
                if (unlinkat(dirfd(entries),entry->d_name,0)) complete = false;
                errno = 0;
            }
            if (errno) complete = false;
            if (closedir(entries)) complete = false;
        }
    }
    if (fonts->cache[0] && rmdir(fonts->cache)) complete = false;
    free(fonts);
    return complete;
}

struct bm_sophia_fonts *bm_sophia_fonts_new(const char *const *directories, size_t count)
{
    if (!directories || !count || count > 8) return NULL;
    struct bm_sophia_fonts *fonts = calloc(1,sizeof(*fonts));
    if (!fonts) return NULL;
    fonts->directory = -1;
    char template[] = "/tmp/bemenu-fonts-XXXXXX";
    if (!mkdtemp(template)) goto failed;
    memcpy(fonts->cache,template,sizeof(template));
    fonts->directory = open(template,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (fonts->directory < 0) goto failed;
    fonts->config = FcConfigCreate();
    if (!fonts->config) goto failed;
    /* No default /etc configuration or home lookup. Preserve the generic
     * monospace request without naming a distribution-specific font family. */
    char xml[512];
    int length = snprintf(xml,sizeof(xml),
        "<fontconfig><cachedir>%s</cachedir>"
        "<match target=\"pattern\"><test name=\"family\" compare=\"eq\">"
        "<string>monospace</string></test><edit name=\"spacing\" mode=\"assign\">"
        "<const>mono</const></edit></match></fontconfig>",fonts->cache);
    if (length < 0 || (size_t)length >= sizeof(xml) ||
        !FcConfigParseAndLoadFromMemory(fonts->config,(const FcChar8 *)xml,FcTrue) ||
        !FcConfigSetRescanInterval(fonts->config,0)) goto failed;
    for (size_t i = 0; i < count; ++i) {
        struct stat value;
        if (!directories[i] || directories[i][0] != '/') goto failed;
        if (stat(directories[i],&value)) {
            if (errno == ENOENT) continue;
            goto failed;
        }
        if (!S_ISDIR(value.st_mode) ||
            !FcConfigAppFontAddDir(fonts->config,(const FcChar8 *)directories[i])) goto failed;
    }
    FcFontSet *available = FcConfigGetFonts(fonts->config,FcSetApplication);
    if (!available || !available->nfont || !FcConfigSetCurrent(fonts->config)) goto failed;
    return fonts;
failed:
    if (!bm_sophia_fonts_free(fonts))
        fputs("bemenu_native status=failed stage=font_cache_cleanup\n",stderr);
    return NULL;
}
