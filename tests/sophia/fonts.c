#define _GNU_SOURCE
#include "../../lib/renderers/sophia/fonts.h"
#include <assert.h>
#include <dirent.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned caches(void)
{
    DIR *directory = opendir("/tmp"); assert(directory);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (!strncmp(entry->d_name,"bemenu-fonts-",13)) ++count;
    assert(!closedir(directory));
    return count;
}

static char *cache_path(void)
{
    FcStrList *paths = FcConfigGetCacheDirs(FcConfigGetCurrent()); assert(paths);
    const FcChar8 *first = FcStrListNext(paths); assert(first);
    char *path = strdup((const char *)first); assert(path);
    assert(!FcStrListNext(paths)); FcStrListDone(paths);
    assert(!strncmp(path,"/tmp/bemenu-fonts-",17));
    struct stat state;
    assert(!lstat(path,&state) && S_ISDIR(state.st_mode) && (state.st_mode & 0777) == 0700);
    return path;
}

int main(void)
{
    unsigned before = caches();
    assert(!bm_sophia_fonts_new(NULL,0));
    char empty[] = "/tmp/bemenu-no-fonts-XXXXXX"; assert(mkdtemp(empty));
    const char *missing[] = {empty};
    assert(!bm_sophia_fonts_new(missing,1));
    assert(caches() == before); assert(!rmdir(empty));
    const char *relative[] = {"relative"};
    assert(!bm_sophia_fonts_new(relative,1)); assert(caches() == before);
    const char *system[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
    struct bm_sophia_fonts *fonts = bm_sophia_fonts_new(system,2); assert(fonts);
    char *cache = cache_path(); assert(caches() == before+1);
    FcFontSet *available = FcConfigGetFonts(FcConfigGetCurrent(),FcSetApplication);
    assert(available && available->nfont > 0 && FcInit());
    FcPattern *pattern = FcNameParse((const FcChar8 *)"monospace"); assert(pattern);
    assert(FcConfigSubstitute(NULL,pattern,FcMatchPattern)); FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern *matched = FcFontMatch(NULL,pattern,&result); assert(matched);
    int spacing;
    assert(FcPatternGetInteger(matched,FC_SPACING,0,&spacing) == FcResultMatch && spacing == FC_MONO);
    FcPatternDestroy(matched); FcPatternDestroy(pattern);
    // Cleanup unlinks a child symlink, never the file it names.
    char target[] = "/tmp/bemenu-font-target-XXXXXX"; int fd = mkstemp(target); assert(fd >= 0); close(fd);
    char link[256]; assert(snprintf(link,sizeof(link),"%s/link",cache) > 0);
    assert(!symlink(target,link));
    assert(bm_sophia_fonts_free(fonts)); assert(access(cache,F_OK));
    assert(!access(target,F_OK)); assert(!unlink(target)); free(cache);
    assert(caches() == before);
    fonts = bm_sophia_fonts_new(system,2); assert(fonts); cache = cache_path();
    assert(snprintf(link,sizeof(link),"%s/unexpected-directory",cache) > 0);
    assert(!mkdir(link,0700));
    assert(!bm_sophia_fonts_free(fonts)); // do not claim recursive/complete cleanup
    assert(!rmdir(link)); assert(!rmdir(cache)); free(cache);
    assert(caches() == before);
    puts("bemenu_fonts configured=pass monospace=pass owned_cache=pass no_fonts=refused native=false");
    return 0;
}
