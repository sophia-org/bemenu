#include "internal.h"

/* The raster seam is usable headlessly; transport admission is not implemented
 * yet. Never turn selecting this backend into an ambient X/Wayland fallback,
 * an unresponsive fake menu, or authorization inferred from an environment fd. */
static bool
constructor(struct bm_menu *menu)
{
    (void)menu;
    return false;
}

BM_PUBLIC extern const char *
register_renderer(struct render_api *api)
{
    api->constructor = constructor;
    api->priorty = BM_PRIO_GUI;
    api->version = BM_PLUGIN_VERSION;
    return "sophia";
}
