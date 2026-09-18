#ifndef BEMENU_SOPHIA_FONTS_H
#define BEMENU_SOPHIA_FONTS_H
#include <stdbool.h>
#include <stddef.h>

struct bm_sophia_fonts;
/* Standalone native process only. Install before creating any Pango objects.
 * Paths refer to already-visible system font directories, never new mounts. */
struct bm_sophia_fonts *bm_sophia_fonts_new(const char *const *directories, size_t count);
/* Call after disposing the native menu/raster. Reports incomplete cache cleanup. */
bool bm_sophia_fonts_free(struct bm_sophia_fonts *fonts);
#endif
