#include "app.h"

struct ext_background_effect_manager_v1;
struct ext_background_effect_surface_v1;
struct org_kde_kwin_blur_manager;
struct org_kde_kwin_blur;

extern const struct wl_interface ext_background_effect_manager_v1_interface;
extern const struct wl_interface ext_background_effect_surface_v1_interface;
extern const struct wl_interface org_kde_kwin_blur_manager_interface;
extern const struct wl_interface org_kde_kwin_blur_interface;

static const struct wl_interface *blur_types[] = {
    NULL,
    &ext_background_effect_surface_v1_interface,
    &wl_surface_interface,
    &wl_region_interface,
    &org_kde_kwin_blur_interface,
};

static const struct wl_message ext_bg_mgr_requests[] = {
    { "destroy", "", blur_types + 0 },
    { "get_background_effect", "no", blur_types + 1 },
};

static const struct wl_message ext_bg_mgr_events[] = {
    { "capabilities", "u", blur_types + 0 },
};

const struct wl_interface ext_background_effect_manager_v1_interface = {
    "ext_background_effect_manager_v1", 1,
    2, ext_bg_mgr_requests,
    1, ext_bg_mgr_events,
};

static const struct wl_message ext_bg_surf_requests[] = {
    { "destroy", "", blur_types + 0 },
    { "set_blur_region", "?o", blur_types + 3 },
};

const struct wl_interface ext_background_effect_surface_v1_interface = {
    "ext_background_effect_surface_v1", 1,
    2, ext_bg_surf_requests,
    0, NULL,
};

static const struct wl_message kde_blur_mgr_requests[] = {
    { "create", "no", blur_types + 4 },
    { "unset", "o", blur_types + 2 },
};

const struct wl_interface org_kde_kwin_blur_manager_interface = {
    "org_kde_kwin_blur_manager", 1,
    2, kde_blur_mgr_requests,
    0, NULL,
};

static const struct wl_message kde_blur_requests[] = {
    { "commit", "", blur_types + 0 },
    { "set_region", "?o", blur_types + 3 },
    { "release", "", blur_types + 0 },
};

const struct wl_interface org_kde_kwin_blur_interface = {
    "org_kde_kwin_blur", 1,
    3, kde_blur_requests,
    0, NULL,
};

struct ext_background_effect_manager_v1_listener {
    void (*capabilities)(void *data, struct ext_background_effect_manager_v1 *mgr, uint32_t caps);
};

static void ext_bg_capabilities(void *d, struct ext_background_effect_manager_v1 *m, uint32_t caps)
{
    (void)d; (void)m; (void)caps;
}

static const struct ext_background_effect_manager_v1_listener ext_bg_listener = {
    ext_bg_capabilities
};

static inline struct ext_background_effect_surface_v1 *
ext_background_effect_manager_v1_get_background_effect(struct ext_background_effect_manager_v1 *mgr, struct wl_surface *surface)
{
    struct wl_proxy *id = wl_proxy_marshal_flags(
        (struct wl_proxy *)mgr, 1, &ext_background_effect_surface_v1_interface,
        wl_proxy_get_version((struct wl_proxy *)mgr), 0, NULL, surface);
    return (struct ext_background_effect_surface_v1 *)id;
}

static inline void
ext_background_effect_surface_v1_set_blur_region(struct ext_background_effect_surface_v1 *s, struct wl_region *region)
{
    wl_proxy_marshal_flags((struct wl_proxy *)s, 1, NULL,
        wl_proxy_get_version((struct wl_proxy *)s), 0, region);
}

static inline void
ext_background_effect_surface_v1_destroy(struct ext_background_effect_surface_v1 *s)
{
    wl_proxy_marshal_flags((struct wl_proxy *)s, 0, NULL,
        wl_proxy_get_version((struct wl_proxy *)s), WL_MARSHAL_FLAG_DESTROY);
}

static inline void
ext_background_effect_manager_v1_destroy(struct ext_background_effect_manager_v1 *mgr)
{
    wl_proxy_marshal_flags((struct wl_proxy *)mgr, 0, NULL,
        wl_proxy_get_version((struct wl_proxy *)mgr), WL_MARSHAL_FLAG_DESTROY);
}

static inline struct org_kde_kwin_blur *
org_kde_kwin_blur_manager_create(struct org_kde_kwin_blur_manager *mgr, struct wl_surface *surface)
{
    struct wl_proxy *id = wl_proxy_marshal_flags(
        (struct wl_proxy *)mgr, 0, &org_kde_kwin_blur_interface,
        wl_proxy_get_version((struct wl_proxy *)mgr), 0, NULL, surface);
    return (struct org_kde_kwin_blur *)id;
}

static inline void
org_kde_kwin_blur_manager_unset(struct org_kde_kwin_blur_manager *mgr, struct wl_surface *surface)
{
    wl_proxy_marshal_flags((struct wl_proxy *)mgr, 1, NULL,
        wl_proxy_get_version((struct wl_proxy *)mgr), 0, surface);
}

static inline void
org_kde_kwin_blur_manager_destroy(struct org_kde_kwin_blur_manager *mgr)
{
    wl_proxy_destroy((struct wl_proxy *)mgr);
}

static inline void
org_kde_kwin_blur_set_region(struct org_kde_kwin_blur *blur, struct wl_region *region)
{
    wl_proxy_marshal_flags((struct wl_proxy *)blur, 1, NULL,
        wl_proxy_get_version((struct wl_proxy *)blur), 0, region);
}

static inline void
org_kde_kwin_blur_commit(struct org_kde_kwin_blur *blur)
{
    wl_proxy_marshal_flags((struct wl_proxy *)blur, 0, NULL,
        wl_proxy_get_version((struct wl_proxy *)blur), 0);
}

static inline void
org_kde_kwin_blur_release(struct org_kde_kwin_blur *blur)
{
    wl_proxy_marshal_flags((struct wl_proxy *)blur, 2, NULL,
        wl_proxy_get_version((struct wl_proxy *)blur), WL_MARSHAL_FLAG_DESTROY);
}

bool blur_handle_global(App *a, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)ver;
    if (!strcmp(iface, "ext_background_effect_manager_v1")) {
        a->ext_bg_mgr = wl_registry_bind(r, name, &ext_background_effect_manager_v1_interface, 1);
        wl_proxy_add_listener((struct wl_proxy *)a->ext_bg_mgr, (void (**)(void))&ext_bg_listener, a);
        return true;
    }
    if (!strcmp(iface, "org_kde_kwin_blur_manager")) {
        a->kde_blur_mgr = wl_registry_bind(r, name, &org_kde_kwin_blur_manager_interface, 1);
        return true;
    }
    return false;
}

void update_surface_blur(App *a)
{
    if (!a->surf || !a->comp) return;

    int want_blur = (a->th.bg_style == BG_ACRYLIC && a->visible);
    int resized = want_blur && a->blur_applied &&
                  (a->blur_w != a->ui.base_w || a->blur_h != a->ui.base_h);
    if (want_blur == a->blur_applied && !resized) return;
    a->blur_applied = want_blur;
    if (want_blur) { a->blur_w = a->ui.base_w; a->blur_h = a->ui.base_h; }

    if (a->ext_bg_mgr) {
        if (want_blur) {
            if (!a->ext_bg_surf)
                a->ext_bg_surf = ext_background_effect_manager_v1_get_background_effect(a->ext_bg_mgr, a->surf);
            if (a->ext_bg_surf) {
                struct wl_region *reg = wl_compositor_create_region(a->comp);
                wl_region_add(reg, 0, 0, a->ui.base_w, a->ui.base_h);
                ext_background_effect_surface_v1_set_blur_region(a->ext_bg_surf, reg);
                wl_region_destroy(reg);
            }
        } else if (a->ext_bg_surf) {
            ext_background_effect_surface_v1_set_blur_region(a->ext_bg_surf, NULL);
            ext_background_effect_surface_v1_destroy(a->ext_bg_surf);
            a->ext_bg_surf = NULL;
        }
    } else if (a->kde_blur_mgr) {
        if (want_blur) {
            if (!a->kde_blur)
                a->kde_blur = org_kde_kwin_blur_manager_create(a->kde_blur_mgr, a->surf);
            if (a->kde_blur) {
                struct wl_region *reg = wl_compositor_create_region(a->comp);
                wl_region_add(reg, 0, 0, a->ui.base_w, a->ui.base_h);
                org_kde_kwin_blur_set_region(a->kde_blur, reg);
                org_kde_kwin_blur_commit(a->kde_blur);
                wl_region_destroy(reg);
            }
        } else {
            if (a->kde_blur) {
                org_kde_kwin_blur_release(a->kde_blur);
                a->kde_blur = NULL;
            }
            org_kde_kwin_blur_manager_unset(a->kde_blur_mgr, a->surf);
        }
    }
}

void blur_cleanup(App *a)
{
    if (a->ext_bg_surf) { ext_background_effect_surface_v1_destroy(a->ext_bg_surf); a->ext_bg_surf = NULL; }
    if (a->ext_bg_mgr)  { ext_background_effect_manager_v1_destroy(a->ext_bg_mgr); a->ext_bg_mgr = NULL; }
    if (a->kde_blur)    { org_kde_kwin_blur_release(a->kde_blur); a->kde_blur = NULL; }
    if (a->kde_blur_mgr){ org_kde_kwin_blur_manager_destroy(a->kde_blur_mgr); a->kde_blur_mgr = NULL; }
}