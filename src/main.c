/* plasma-radial-keyboard - dual-stick radial on-screen keyboard for KWin/Wayland. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include "protocol.h"
#include "kb_core.h"

#define DEFAULT_LW 1100
#define DEFAULT_LH 500
#define INOTIFY_BUFSZ ((sizeof(struct inotify_event) + NAME_MAX + 1) * 16)

enum {
    BG_SOLID = 0,
    BG_ACRYLIC = 1,
    BG_TRANSLUCENT = 2
};

/* ======================= Wayland Background Blur Protocols ======================= */
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

/* ================================================================================= */

typedef struct {
    struct wl_buffer *buf; void *data; size_t size; int busy;
    cairo_surface_t *cs;
} Buf;

typedef struct {
    double bg[4], border[4], wedge[4], wedge_border[4], wedge_empty[4], hub[4];
    double fg[4], fg_dim[4], accent[4], accent_fg[4], err[4];
    int dark;
    int bg_style;
    double bg_opacity;
    double corner_radius;
    char accent_cfg[64];
} Theme;

typedef struct {
    double ui_scale;
    double font_scale;
    char font_family[128];
    int base_w, base_h;
} UiConfig;

typedef struct {
    struct wl_display *dpy;
    struct wl_compositor *comp; struct wl_shm *shm; struct wl_output *out;
    struct zwp_input_method_v1 *im; struct zwp_input_panel_v1 *panel;
    struct wl_surface *surf; struct zwp_input_panel_surface_v1 *psurf;
    struct zwp_input_method_context_v1 *ctx; uint32_t serial;
    struct ext_background_effect_manager_v1 *ext_bg_mgr;
    struct ext_background_effect_surface_v1 *ext_bg_surf;
    struct org_kde_kwin_blur_manager *kde_blur_mgr;
    struct org_kde_kwin_blur *kde_blur;
    int blur_applied;
    int blur_w, blur_h;                  /* логический размер, для которого задан регион */
    int scale, buf_scale;
    Buf bufs[2]; int bw, bh;
    int field_active;
    int field_has_surrounding;
    uint32_t cursor_pos;
    uint32_t anchor_pos;
    uint32_t content_hint;
    uint32_t content_purpose;
    char surrounding[512];
    int visible;
    int mapped;
    Kb kb; Pad pads[MAXPADS]; int npads;
    int grab_pref; volatile sig_atomic_t running;
    int64_t last_draw;
    struct wl_callback *frame_cb;        /* ожидающий callback первого кадра */
    int64_t map_deadline;                /* запасной срок, если callback не придёт */
    int plasma_mode;
    int invoke;
    int user_shown;
    int user_hidden;
    Chord chord;
    int ino_input, wd_input;
    int ino_cfg, wd_cfg, wd_radialkb;
    int cfg_dirty;
    int64_t cfg_reload_at;
    int pads_dirty;
    int64_t pads_rescan_at;
    char cfg_dir[PATH_MAX];
    char radialkb_dir[PATH_MAX + 64];
    char layout_path[PATH_MAX + 256];
    char kwinrc[PATH_MAX + 64];
    char kdeglobals[PATH_MAX + 64];
    int cli_invoke;                      /* -1, если режим не задан в командной строке/окружении */
    char cli_layout[PATH_MAX + 256];     /* --layout FILE */
    int cfg_has_error;
    char cfg_err_msg[256];
    Theme th;
    UiConfig ui;
    PangoFontDescription *fd_cache[2];   /* regular, bold */
    char fd_family[128];
} App;

static App A;

static void color_set(double *c, double r, double g, double b, double a)
{ c[0] = r; c[1] = g; c[2] = b; c[3] = a; }

static double parse_coord_double(const char *s, int *ok)
{
    if (ok) *ok = 0;
    if (!s) return 0.0;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (!*s) return 0.0;

    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    if (!isdigit((unsigned char)*s) && *s != '.' && *s != ',') return 0.0;

    double int_part = 0.0;
    int has_digits = 0;
    while (isdigit((unsigned char)*s)) {
        int_part = int_part * 10.0 + (*s - '0');
        s++;
        has_digits = 1;
    }

    double frac_part = 0.0;
    if (*s == '.' || *s == ',') {
        s++;
        double div = 10.0;
        while (isdigit((unsigned char)*s)) {
            frac_part += (*s - '0') / div;
            div *= 10.0;
            s++;
            has_digits = 1;
        }
    }

    if (!has_digits) return 0.0;
    if (ok) *ok = 1;
    return sign * (int_part + frac_part);
}

static char *trim_whitespace(char *str)
{
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

/* Комментарий — только строка, начинающаяся с '#' или ';'. Нельзя резать по '#'
   внутри строки: иначе `accent_color = #3daee9` превращается в пустое значение. */
static void strip_comment(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';') *p = '\0';
}

static int parse_rgb(const char *s, double *o)
{
    if (!s || !*s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') {
        unsigned int hex = 0;
        if (sscanf(s + 1, "%x", &hex) != 1) return 0;
        size_t len = strlen(s + 1);
        if (len == 6) {
            o[0] = ((hex >> 16) & 0xff) / 255.0;
            o[1] = ((hex >> 8) & 0xff) / 255.0;
            o[2] = (hex & 0xff) / 255.0;
            o[3] = 1.0;
            return 1;
        } else if (len == 8) {
            o[0] = ((hex >> 24) & 0xff) / 255.0;
            o[1] = ((hex >> 16) & 0xff) / 255.0;
            o[2] = ((hex >> 8) & 0xff) / 255.0;
            o[3] = (hex & 0xff) / 255.0;
            return 1;
        }
        return 0;
    }
    int r = 0, g = 0, b = 0, a = 255;
    int n = sscanf(s, "%d,%d,%d,%d", &r, &g, &b, &a);
    if (n < 3) return 0;
    o[0] = r / 255.0; o[1] = g / 255.0; o[2] = b / 255.0;
    o[3] = (n >= 4) ? (a / 255.0) : 1.0;
    return 1;
}

/* Кеш файлов конфигурации: за один проход перезагрузки каждый файл читается
   с диска один раз, а не по fopen()+разбору на каждый ключ (их ~30). Сбрасывается
   в начале каждой перезагрузки (plasma_mode_load). */
#define INI_CACHE_N 4
static struct { char path[PATH_MAX + 64]; char *data; int valid; } ini_cache[INI_CACHE_N];

static void ini_cache_flush(void)
{
    for (int i = 0; i < INI_CACHE_N; i++) {
        free(ini_cache[i].data);
        ini_cache[i].data = NULL;
        ini_cache[i].valid = 0;
    }
}

/* Возвращает содержимое файла или NULL (отсутствие файла тоже кешируется) */
static const char *ini_cache_load(const char *path)
{
    int slot = -1;
    for (int i = 0; i < INI_CACHE_N; i++) {
        if (ini_cache[i].valid && !strcmp(ini_cache[i].path, path)) return ini_cache[i].data;
        if (!ini_cache[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) { ini_cache_flush(); slot = 0; }

    char *buf = NULL;
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz >= 0 && sz < (1 << 20) && (buf = malloc((size_t)sz + 1))) {
            size_t r = fread(buf, 1, (size_t)sz, f);
            buf[r] = 0;
        }
        fclose(f);
    }
    snprintf(ini_cache[slot].path, sizeof ini_cache[slot].path, "%s", path);
    ini_cache[slot].data = buf;
    ini_cache[slot].valid = 1;
    return buf;
}

static int ini_get(const char *path, const char *group, const char *key, char *out, size_t n)
{
    const char *p = ini_cache_load(path);
    if (!p) return 0;
    char line[1024], cur[128] = "";
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = 0;
        p = e ? e + 1 : p + strlen(p);

        strip_comment(line);
        char *s = trim_whitespace(line);
        if (!*s) continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            cur[0] = 0;
            if (end) {
                size_t L = (size_t)(end - s - 1); if (L > sizeof cur - 1) L = sizeof cur - 1;
                memcpy(cur, s + 1, L); cur[L] = 0;
                trim_whitespace(cur);
            }
            continue;
        }
        if (strcasecmp(cur, group) != 0) continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim_whitespace(s);
        char *val = trim_whitespace(eq + 1);
        if (!strcasecmp(k, key)) {
            snprintf(out, n, "%s", val);
            return 1;
        }
    }
    return 0;
}

static void theme_defaults_breeze_dark(Theme *t)
{
    color_set(t->bg, 0.145, 0.165, 0.188, 0.85);
    color_set(t->border, 0.40, 0.45, 0.52, 0.40);
    color_set(t->wedge, 0.20, 0.23, 0.27, 0.85);
    color_set(t->wedge_border, 0.93, 0.94, 0.95, 0.14);
    color_set(t->wedge_empty, 0.16, 0.18, 0.21, 0.25);
    color_set(t->hub, 0.11, 0.12, 0.15, 0.95);
    color_set(t->fg, 0.93, 0.94, 0.95, 1.0);
    color_set(t->fg_dim, 0.65, 0.69, 0.74, 1.0);
    color_set(t->accent, 0.235, 0.576, 0.886, 1.0);
    color_set(t->accent_fg, 1.0, 1.0, 1.0, 1.0);
    color_set(t->err, 0.93, 0.12, 0.14, 0.95);
    t->dark = 1;
    t->bg_style = BG_ACRYLIC;
    t->bg_opacity = 0.85;
    t->corner_radius = 18.0;
    t->accent_cfg[0] = 0;
}

static void theme_load(App *a)
{
    theme_defaults_breeze_dark(&a->th);
    char col[64], fg[64], sel[64], btn[64], acc[64];
    double bgc[4], fgc[4], selc[4], btnc[4], accc[4];
    int ok_bg  = ini_get(a->kdeglobals, "Colors:Window", "BackgroundNormal", col, sizeof col) && parse_rgb(col, bgc);
    int ok_fg  = ini_get(a->kdeglobals, "Colors:Window", "ForegroundNormal", fg, sizeof fg) && parse_rgb(fg, fgc);
    int ok_sel = ini_get(a->kdeglobals, "Colors:Selection", "BackgroundNormal", sel, sizeof sel) && parse_rgb(sel, selc);
    int ok_btn = ini_get(a->kdeglobals, "Colors:Button", "BackgroundNormal", btn, sizeof btn) && parse_rgb(btn, btnc);
    int ok_acc = ini_get(a->kdeglobals, "General", "AccentColor", acc, sizeof acc) && parse_rgb(acc, accc);

    Theme *t = &a->th;

    char cfg_theme[PATH_MAX + 64];
    snprintf(cfg_theme, sizeof cfg_theme, "%s/radialkb/config.ini", a->cfg_dir);
    char val[64];
    if (ini_get(cfg_theme, "theme", "background_style", val, sizeof val)) {
        if (!strcmp(val, "solid")) t->bg_style = BG_SOLID;
        else if (!strcmp(val, "translucent")) t->bg_style = BG_TRANSLUCENT;
        else t->bg_style = BG_ACRYLIC;
    }
    if (ini_get(cfg_theme, "theme", "background_opacity", val, sizeof val)) {
        int ok; double op = parse_coord_double(val, &ok);
        if (ok && op >= 0.1 && op <= 1.0) t->bg_opacity = op;
    }
    if (ini_get(cfg_theme, "theme", "corner_radius", val, sizeof val)) {
        int ok; double cr = parse_coord_double(val, &ok);
        if (ok && cr >= 0.0 && cr <= 48.0) t->corner_radius = cr;
    }
    if (ini_get(cfg_theme, "theme", "accent_color", val, sizeof val)) {
        snprintf(t->accent_cfg, sizeof t->accent_cfg, "%s", val);
    }

    if (ok_bg && ok_fg) {
        double lum = 0.2126 * bgc[0] + 0.7152 * bgc[1] + 0.0722 * bgc[2];
        t->dark = lum < 0.5;

        double base_alpha = (t->bg_style == BG_SOLID) ? 1.0 : t->bg_opacity;
        color_set(t->bg, bgc[0], bgc[1], bgc[2], base_alpha);
        color_set(t->fg, fgc[0], fgc[1], fgc[2], 0.98);
        color_set(t->fg_dim, fgc[0] * 0.7 + bgc[0] * 0.3,
                  fgc[1] * 0.7 + bgc[1] * 0.3,
                  fgc[2] * 0.7 + bgc[2] * 0.3, 1.0);

        if (t->accent_cfg[0] && strcmp(t->accent_cfg, "auto") != 0 && parse_rgb(t->accent_cfg, accc)) {
            color_set(t->accent, accc[0], accc[1], accc[2], 1.0);
        } else if (ok_acc) {
            color_set(t->accent, accc[0], accc[1], accc[2], 1.0);
        } else if (ok_sel) {
            color_set(t->accent, selc[0], selc[1], selc[2], 1.0);
        } else {
            color_set(t->accent, 0.235, 0.576, 0.886, 1.0);
        }

        if (ok_btn) {
            double btn_a = (t->bg_style == BG_SOLID) ? 0.95 : 0.82;
            color_set(t->wedge, btnc[0], btnc[1], btnc[2], btn_a);
        } else {
            double mix = t->dark ? 0.18 : 0.10;
            color_set(t->wedge, bgc[0] * (1 - mix) + fgc[0] * mix,
                      bgc[1] * (1 - mix) + fgc[1] * mix,
                      bgc[2] * (1 - mix) + fgc[2] * mix, 0.85);
        }

        color_set(t->wedge_border, t->fg[0], t->fg[1], t->fg[2], t->dark ? 0.14 : 0.18);
        color_set(t->border, t->dark ? 1.0 : 0.0, t->dark ? 1.0 : 0.0, t->dark ? 1.0 : 0.0, t->dark ? 0.18 : 0.12);
        color_set(t->wedge_empty, bgc[0] * 0.85, bgc[1] * 0.85, bgc[2] * 0.85, 0.25);
        color_set(t->hub, bgc[0] * 0.65, bgc[1] * 0.65, bgc[2] * 0.65, 0.92);
    }
}

static int plasma_vk_mode_from_file(const char *path)
{
    char v[64];
    if (ini_get(path, "Wayland", "VirtualKeyboardMode", v, sizeof v))
        return atoi(v);
    if (ini_get(path, "Wayland", "VirtualKeyboardEnabled", v, sizeof v)) {
        if (!strcmp(v, "false") || !strcmp(v, "0")) return PLASMA_VK_NEVER;
        return PLASMA_VK_TOUCH;
    }
    return PLASMA_VK_TOUCH;
}

static void plasma_mode_load(App *a)
{
    ini_cache_flush();   /* начало прохода: файлы (kwinrc, kdeglobals, config.ini) читаем заново */
    a->plasma_mode = plasma_vk_mode_from_file(a->kwinrc);
}

static int map_btn_name(const char *s)
{
    if (!strcmp(s, "select") || !strcmp(s, "share") || !strcmp(s, "back")) return BTN_SELECT;
    if (!strcmp(s, "start") || !strcmp(s, "options") || !strcmp(s, "menu")) return BTN_START;
    if (!strcmp(s, "l3") || !strcmp(s, "thumbl")) return BTN_THUMBL;
    if (!strcmp(s, "r3") || !strcmp(s, "thumbr")) return BTN_THUMBR;
    if (!strcmp(s, "mode") || !strcmp(s, "guide") || !strcmp(s, "ps")) return BTN_MODE;
    if (!strcmp(s, "lb") || !strcmp(s, "l1")) return BTN_TL;
    if (!strcmp(s, "rb") || !strcmp(s, "r1")) return BTN_TR;
    if (!strcmp(s, "lt") || !strcmp(s, "l2")) return BTN_TL2;
    if (!strcmp(s, "rt") || !strcmp(s, "r2")) return BTN_TR2;
    if (!strcmp(s, "a") || !strcmp(s, "cross") || !strcmp(s, "south")) return BTN_SOUTH;
    if (!strcmp(s, "b") || !strcmp(s, "circle") || !strcmp(s, "east")) return BTN_EAST;
    if (!strcmp(s, "x") || !strcmp(s, "square") || !strcmp(s, "west")) return BTN_WEST;
    if (!strcmp(s, "y") || !strcmp(s, "triangle") || !strcmp(s, "north")) return BTN_NORTH;
    return -1;
}

static int parse_btn_list(const char *spec, int *arr, int *cnt)
{
    *cnt = 0;
    if (!spec || !*spec) return 1;
    char buf[128]; snprintf(buf, sizeof buf, "%s", spec);
    char *p = buf;
    int err = 0;
    while (*p && *cnt < MAX_BINDS) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *e = p;
        while (*e && *e != ',' && *e != ' ' && *e != '\t') {
            *e = (char)tolower((unsigned char)*e);
            e++;
        }
        char term = *e; *e = 0;
        int code = map_btn_name(p);
        if (code > 0) arr[(*cnt)++] = code;
        else err = 1;
        if (!term) break;
        p = e + 1;
    }
    return !err;
}

static int parse_combo(const char *spec, int *a, int *b)
{
    char buf[128]; snprintf(buf, sizeof buf, "%s", spec ? spec : "select+start");
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
    char *plus = strchr(buf, '+');
    if (!plus) {
        int x = map_btn_name(trim_whitespace(buf));
        *a = x > 0 ? x : BTN_SELECT;
        *b = -1;
        return (x > 0);
    }
    *plus = 0;
    int x = map_btn_name(trim_whitespace(buf)), y = map_btn_name(trim_whitespace(plus + 1));
    *a = x > 0 ? x : BTN_SELECT;
    *b = y > 0 ? y : BTN_START;
    return (x > 0 && y > 0);
}

/* Параметры командной строки/окружения важнее config.ini, в том числе после hot-reload */
static void apply_cli_overrides(App *a, char *layout_path, size_t n)
{
    if (a->cli_invoke >= 0) a->invoke = a->cli_invoke;
    if (a->cli_layout[0]) snprintf(layout_path, n, "%s", a->cli_layout);
}

static void radial_config_load(App *a, char *layout_path, size_t n)
{
    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/radialkb/config.ini", a->cfg_dir);
    char v[128];

    /* Сброс к значениям по умолчанию: ключ, удалённый из config.ini, должен
       возвращать дефолт, а не оставлять значение прошлой загрузки. */
    a->invoke = INVOKE_SMART;
    a->chord.a = BTN_SELECT; a->chord.b = BTN_START;
    snprintf(layout_path, n, "%s/radialkb/layouts.conf", a->cfg_dir);

    a->ui.ui_scale = 1.0;
    a->ui.font_scale = 1.0;
    snprintf(a->ui.font_family, sizeof a->ui.font_family, "Noto Sans");
    /* Размеры по умолчанию задаём ДО раннего return: без config.ini они иначе
       оставались 0, и буфер 0x0 не создавался (панель не показывалась). */
    a->ui.base_w = DEFAULT_LW;
    a->ui.base_h = DEFAULT_LH;
    binds_set_defaults(&a->kb.binds);
    a->cfg_has_error = 0;
    a->cfg_err_msg[0] = 0;

    FILE *f_test = fopen(path, "r");
    if (!f_test) { apply_cli_overrides(a, layout_path, n); return; }
    fclose(f_test);

    if (ini_get(path, "general", "invoke", v, sizeof v)) {
        if (!strcmp(v, "manual")) a->invoke = INVOKE_MANUAL;
        else if (!strcmp(v, "auto")) a->invoke = INVOKE_AUTO;
        else if (!strcmp(v, "smart")) a->invoke = INVOKE_SMART;
        else {
            a->cfg_has_error = 1;
            snprintf(a->cfg_err_msg, sizeof a->cfg_err_msg, "Unknown invoke mode: '%s'", v);
        }
    }

    if (ini_get(path, "general", "show", v, sizeof v) ||
        ini_get(path, "general", "toggle", v, sizeof v)) {
        if (!parse_combo(v, &a->chord.a, &a->chord.b)) {
            a->cfg_has_error = 1;
            snprintf(a->cfg_err_msg, sizeof a->cfg_err_msg, "Invalid show combo: '%s'", v);
        }
    }

    if (ini_get(path, "general", "layout", v, sizeof v)) {
        if (v[0] == '/') snprintf(layout_path, n, "%s", v);
        else snprintf(layout_path, n, "%s/radialkb/%.128s", a->cfg_dir, v);
    }

    if (ini_get(path, "controls", "backspace", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.backspace, &a->kb.binds.n_backspace)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "space", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.space, &a->kb.binds.n_space)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "enter", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.enter, &a->kb.binds.n_enter)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "shift", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.shift, &a->kb.binds.n_shift)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "symbols", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.sym, &a->kb.binds.n_sym)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "layout_switch", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.layout, &a->kb.binds.n_layout)) a->cfg_has_error = 1;
    if (ini_get(path, "controls", "hide", v, sizeof v))
        if (!parse_btn_list(v, a->kb.binds.hide, &a->kb.binds.n_hide)) a->cfg_has_error = 1;

    if (ini_get(path, "ui", "scale", v, sizeof v)) {
        int ok; double d = parse_coord_double(v, &ok);
        if (ok && d >= 0.5 && d <= 3.0) {
            a->ui.ui_scale = d;
        } else {
            a->cfg_has_error = 1;
            snprintf(a->cfg_err_msg, sizeof a->cfg_err_msg, "Invalid scale: '%s'", v);
        }
    }
    if (ini_get(path, "ui", "font_scale", v, sizeof v)) {
        int ok; double d = parse_coord_double(v, &ok);
        if (ok && d >= 0.5 && d <= 3.0) {
            a->ui.font_scale = d;
        } else {
            a->cfg_has_error = 1;
            snprintf(a->cfg_err_msg, sizeof a->cfg_err_msg, "Invalid font_scale: '%s'", v);
        }
    }
    if (ini_get(path, "ui", "font_family", v, sizeof v)) {
        snprintf(a->ui.font_family, sizeof a->ui.font_family, "%.*s",
                 (int)(sizeof(a->ui.font_family) - 1), v);
    }

    int custom_w = 0, custom_h = 0;
    if (ini_get(path, "ui", "width", v, sizeof v)) {
        int w = atoi(v); if (w >= 600 && w <= 3840) { a->ui.base_w = w; custom_w = 1; }
    }
    if (ini_get(path, "ui", "height", v, sizeof v)) {
        int h = atoi(v); if (h >= 300 && h <= 2160) { a->ui.base_h = h; custom_h = 1; }
    }

    if (!custom_w) a->ui.base_w = (int)(DEFAULT_LW * a->ui.ui_scale);
    if (!custom_h) a->ui.base_h = (int)(DEFAULT_LH * a->ui.ui_scale);

    apply_cli_overrides(a, layout_path, n);

    if (a->cfg_has_error) {
        fprintf(stderr, "radialkb: [CONFIG WARNING] %s (using fallback values)\n",
                a->cfg_err_msg[0] ? a->cfg_err_msg : "syntax error in config.ini");
    }
}

/* ======================= blur protocol handling ======================= */
static void update_surface_blur(App *a)
{
    if (!a->surf || !a->comp) return;

    int want_blur = (a->th.bg_style == BG_ACRYLIC && a->visible);
    /* Регион надо задавать заново и при смене размера панели (hot-reload) */
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
                wl_region_add(reg, 0, 0, a->ui.base_w, a->ui.base_h);   /* логические координаты, без * scale */
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
                wl_region_add(reg, 0, 0, a->ui.base_w, a->ui.base_h);   /* логические координаты, без * scale */
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

/* ======================= text output ======================= */
static void cb_text(void *ud, const char *t)
{
    App *a = ud;
    if (!a->ctx) return;
    zwp_input_method_context_v1_commit_string(a->ctx, a->serial, t);
    wl_display_flush(a->dpy);
}

static void cb_key(void *ud, uint32_t sym)
{
    App *a = ud;
    if (!a->ctx) return;
    if (sym == SYM_SPACE) { cb_text(ud, " "); return; }
    uint32_t t = (uint32_t)now_ms();
    zwp_input_method_context_v1_keysym(a->ctx, a->serial, t, sym, WL_KEYBOARD_KEY_STATE_PRESSED, 0);
    zwp_input_method_context_v1_keysym(a->ctx, a->serial, t, sym, WL_KEYBOARD_KEY_STATE_RELEASED, 0);
    wl_display_flush(a->dpy);
}

static void reset_ime_state(App *a)
{
    if (a->ctx) {
        zwp_input_method_context_v1_modifiers(a->ctx, a->serial, 0, 0, 0, 0);
        wl_display_flush(a->dpy);
    }
}

/* ======================= shm buffers ======================= */
static void buf_release(void *data, struct wl_buffer *b) { (void)b; ((Buf *)data)->busy = 0; }
static const struct wl_buffer_listener buf_listener = { buf_release };

static void buf_destroy(Buf *b)
{
    if (b->cs) cairo_surface_destroy(b->cs);
    if (b->buf) wl_buffer_destroy(b->buf);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof *b);
}

static int buf_create(App *a, Buf *b, int w, int h)
{
    int stride = w * 4; size_t size = (size_t)stride * h;
    int fd = memfd_create("radialkb", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0) { if (fd >= 0) close(fd); return -1; }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return -1; }
    struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, (int)size);
    b->buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); close(fd);
    wl_buffer_add_listener(b->buf, &buf_listener, b);
    b->data = data; b->size = size; b->busy = 0;
    b->cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
    return 0;
}

/* ======================= drawing ======================= */
static void cairo_rgba(cairo_t *cr, const double *c)
{ cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]); }

/* Описание шрифта собирается один раз на семейство; здесь только копия с нужным
   размером (без разбора строки pango_font_description_from_string на каждый текст) */
static PangoFontDescription *font_make(App *a, int bold, double px_size)
{
    if (strcmp(a->fd_family, a->ui.font_family) != 0) {
        for (int i = 0; i < 2; i++) {
            if (a->fd_cache[i]) { pango_font_description_free(a->fd_cache[i]); a->fd_cache[i] = NULL; }
        }
        snprintf(a->fd_family, sizeof a->fd_family, "%s", a->ui.font_family);
    }
    int i = bold ? 1 : 0;
    if (!a->fd_cache[i]) {
        char desc[192];
        snprintf(desc, sizeof desc, "%s %s", a->ui.font_family, bold ? "Bold" : "Regular");
        a->fd_cache[i] = pango_font_description_from_string(desc);
    }
    PangoFontDescription *fd = pango_font_description_copy(a->fd_cache[i]);
    pango_font_description_set_absolute_size(fd, px_size * PANGO_SCALE);
    return fd;
}

static void text_c(cairo_t *cr, App *a, const char *s, double x, double y, double px,
                   const double *col, int bold)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = font_make(a, bold, px * a->ui.ui_scale * a->ui.font_scale);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, s, -1);
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    cairo_rgba(cr, col);
    cairo_move_to(cr, x - w / 2.0, y - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd); g_object_unref(l);
    cairo_new_path(cr);
}

/* Выравнивание одиночных символов и знаков пунктуации по чернильным границам (Optical Center) */
static void draw_optical_text(cairo_t *cr, App *a, const char *s, double cx, double cy,
                              double pt_size, const double *col, int bold, int optical_center)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = font_make(a, bold, pt_size * a->ui.ui_scale * a->ui.font_scale);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, s, -1);

    PangoRectangle ink_rect, log_rect;
    pango_layout_get_pixel_extents(l, &ink_rect, &log_rect);

    double draw_x, draw_y;
    if (optical_center && ink_rect.width > 0 && ink_rect.height > 0) {
        draw_x = cx - (ink_rect.x + ink_rect.width / 2.0);
        draw_y = cy - (ink_rect.y + ink_rect.height / 2.0);
    } else {
        draw_x = cx - log_rect.width / 2.0;
        draw_y = cy - log_rect.height / 2.0;
    }

    cairo_rgba(cr, col);
    cairo_move_to(cr, draw_x, draw_y);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);
    cairo_new_path(cr);
}

static void text_fit(cairo_t *cr, App *a, const char *s, double x, double y, double maxw, double px,
                     const double *col, int bold)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = font_make(a, bold, px * a->ui.ui_scale * a->ui.font_scale);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
    pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
    pango_layout_set_spacing(l, (int)(3.0 * a->ui.ui_scale * PANGO_SCALE));
    pango_layout_set_text(l, s, -1);
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    cairo_rgba(cr, col);
    cairo_move_to(cr, x - maxw / 2.0, y - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd); g_object_unref(l);
    cairo_new_path(cr);
}

static void wedge_breeze(cairo_t *cr, double cx, double cy, double r0, double r1, int sector)
{
    double th = sector * (M_PI / 4) - M_PI / 2;
    double half = M_PI / 8 - 0.048;
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r1, th - half, th + half);
    cairo_arc_negative(cr, cx, cy, r0, th + half, th - half);
    cairo_close_path(cr);
}

static void sector_pos(double cx, double cy, double r, int sector, double *x, double *y)
{
    double th = sector * (M_PI / 4);
    *x = cx + r * sin(th); *y = cy - r * cos(th);
}

/* Рисует «пилюлю» с названием кнопки и подписью. При measure=1 только измеряет.
   Возвращает полную ширину (пилюля + подпись), чтобы ряд можно было выложить без наложений. */
static double draw_pill_badge(cairo_t *cr, App *a, const char *key, const char *desc,
                              double x, double y, double h, int measure)
{
    const Theme *th = &a->th;
    double r = h / 2.0;

    PangoLayout *lk = pango_cairo_create_layout(cr);
    PangoFontDescription *fdk = font_make(a, 1, (h * 0.52) * a->ui.font_scale);
    pango_layout_set_font_description(lk, fdk);
    pango_layout_set_text(lk, key, -1);
    int kw, kh; pango_layout_get_pixel_size(lk, &kw, &kh);

    double pad_x = 8.0 * a->ui.ui_scale;
    double box_w = kw + pad_x * 2;
    if (box_w < h) box_w = h;
    double total = box_w;

    PangoLayout *ld = NULL;
    PangoFontDescription *fdd = NULL;
    int dw = 0, dh = 0;
    if (desc && *desc) {
        ld = pango_cairo_create_layout(cr);
        fdd = font_make(a, 0, (h * 0.50) * a->ui.font_scale);
        pango_layout_set_font_description(ld, fdd);
        pango_layout_set_text(ld, desc, -1);
        pango_layout_get_pixel_size(ld, &dw, &dh);
        total += 6.0 * a->ui.ui_scale + dw;
    }

    if (!measure) {
        cairo_new_path(cr);
        cairo_arc(cr, x + box_w - r, y + r, r, -M_PI / 2, M_PI / 2);
        cairo_arc(cr, x + r, y + r, r, M_PI / 2, 1.5 * M_PI);
        cairo_close_path(cr);

        cairo_rgba(cr, th->wedge);
        cairo_fill_preserve(cr);
        cairo_rgba(cr, th->border);
        cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
        cairo_stroke(cr);

        cairo_rgba(cr, th->fg);
        cairo_move_to(cr, x + (box_w - kw) / 2.0, y + (h - kh) / 2.0);
        pango_cairo_show_layout(cr, lk);

        if (ld) {
            cairo_rgba(cr, th->fg_dim);
            cairo_move_to(cr, x + box_w + 6.0 * a->ui.ui_scale, y + (h - dh) / 2.0);
            pango_cairo_show_layout(cr, ld);
        }
    }

    pango_font_description_free(fdk); g_object_unref(lk);
    if (ld) { pango_font_description_free(fdd); g_object_unref(ld); }
    return total;
}

/* Подписи кнопок — те же имена, что принимает map_btn_name() в config.ini */
static const char *btn_label(int code)
{
    switch (code) {
        case BTN_SOUTH: return "A";   case BTN_EAST:  return "B";
        case BTN_WEST:  return "X";   case BTN_NORTH: return "Y";
        case BTN_TL: return "LB";     case BTN_TR: return "RB";
        case BTN_TL2: return "LT";    case BTN_TR2: return "RT";
        case BTN_THUMBL: return "L3"; case BTN_THUMBR: return "R3";
        case BTN_SELECT: return "Select"; case BTN_START: return "Start";
        case BTN_MODE: return "Guide";
        default: return "?";
    }
}

static const char *bind_label(const int *arr, int n) { return n > 0 ? btn_label(arr[0]) : "-"; }

typedef struct { const char *key, *desc; } Badge;

/* Ряд подсказок по центру: ширина считается заранее, поэтому длинные имена не налезают друг на друга */
static void draw_badge_row(cairo_t *cr, App *a, const Badge *b, int n, double lw, double y, double h)
{
    double gap = 18.0 * a->ui.ui_scale, total = 0;
    for (int i = 0; i < n; i++)
        total += draw_pill_badge(cr, a, b[i].key, b[i].desc, 0, 0, h, 1) + (i ? gap : 0);
    double x = lw / 2.0 - total / 2.0;
    for (int i = 0; i < n; i++)
        x += draw_pill_badge(cr, a, b[i].key, b[i].desc, x, y, h, 0) + gap;
}

static void draw_ring(cairo_t *cr, App *a, double cx, double cy, double R, double r0, int is_left)
{
    Kb *k = &a->kb;
    const Theme *th = &a->th;
    const Layout *Ly = &k->lay[k->cur];
    int layer = kb_layer(k);
    const Group *grp = &Ly->g[layer][k->group];
    double maxw = (R - r0) * 0.95;

    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    for (int s = 0; s < NSLOTS; s++) {
        int hot = is_left ? (s == k->group) : (k->R.active && k->R.sel == s);
        wedge_breeze(cr, cx, cy, r0, R, s);

        if (hot) {
            cairo_pattern_t *pat = cairo_pattern_create_linear(cx, cy - R, cx, cy + R);
            cairo_pattern_add_color_stop_rgba(pat, 0.0, th->accent[0] * 1.08, th->accent[1] * 1.08, th->accent[2] * 1.08, 1.0);
            cairo_pattern_add_color_stop_rgba(pat, 1.0, th->accent[0] * 0.92, th->accent[1] * 0.92, th->accent[2] * 0.92, 1.0);
            cairo_set_source(cr, pat);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(pat);

            cairo_set_line_width(cr, 1.8 * a->ui.ui_scale);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, th->dark ? 0.60 : 0.85);
            cairo_stroke(cr);
        } else if (!is_left && !grp->t[s][0]) {
            cairo_rgba(cr, th->wedge_empty);
            cairo_fill(cr);
        } else {
            cairo_rgba(cr, th->wedge);
            cairo_fill_preserve(cr);
            cairo_rgba(cr, th->wedge_border);
            cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
            cairo_stroke(cr);
        }

        /* Оптимальный оптический радиус в широкой части сектора */
        double r_text = r0 + (R - r0) * 0.54;
        double x, y; sector_pos(cx, cy, r_text, s, &x, &y);
        const double *text_col = hot ? th->accent_fg : th->fg;

        if (is_left) {
            const Group *g = &Ly->g[layer][s];
            const char *tokens[NSLOTS];
            int n_tok = 0;
            for (int i = 0; i < NSLOTS; i++) {
                const char *t = g->t[TOK2SLOT[i]];
                if (t && t[0]) tokens[n_tok++] = t;
            }

            if (n_tok > 0) {
                char line1[64] = "";
                char line2[64] = "";

                if (n_tok <= 3) {
                    for (int i = 0; i < n_tok; i++) {
                        if (i > 0) strncat(line1, "  ", sizeof(line1) - strlen(line1) - 1);
                        strncat(line1, tokens[i], sizeof(line1) - strlen(line1) - 1);
                    }
                } else if (n_tok == 4) {
                    char test_line[64] = "";
                    for (int i = 0; i < 4; i++) {
                        if (i > 0) strncat(test_line, " ", sizeof(test_line) - strlen(test_line) - 1);
                        strncat(test_line, tokens[i], sizeof(test_line) - strlen(test_line) - 1);
                    }

                    PangoLayout *tl = pango_cairo_create_layout(cr);
                    PangoFontDescription *tfd = font_make(a, hot, 14.5 * a->ui.ui_scale * a->ui.font_scale);
                    pango_layout_set_font_description(tl, tfd);
                    pango_layout_set_text(tl, test_line, -1);
                    int tw, th_h; pango_layout_get_pixel_size(tl, &tw, &th_h);
                    pango_font_description_free(tfd); g_object_unref(tl);

                    if (tw <= maxw * 0.95) {   /* maxw уже в масштабированных пикселях */
                        snprintf(line1, sizeof(line1), "%s", test_line);
                    } else {
                        snprintf(line1, sizeof(line1), "%s  %s", tokens[0], tokens[1]);
                        snprintf(line2, sizeof(line2), "%s  %s", tokens[2], tokens[3]);
                    }
                } else if (n_tok == 5) {
                    snprintf(line1, sizeof(line1), "%s %s %s", tokens[0], tokens[1], tokens[2]);
                    snprintf(line2, sizeof(line2), "%s %s", tokens[3], tokens[4]);
                } else if (n_tok == 6) {
                    snprintf(line1, sizeof(line1), "%s %s %s", tokens[0], tokens[1], tokens[2]);
                    snprintf(line2, sizeof(line2), "%s %s %s", tokens[3], tokens[4], tokens[5]);
                } else if (n_tok == 7) {
                    snprintf(line1, sizeof(line1), "%s %s %s %s", tokens[0], tokens[1], tokens[2], tokens[3]);
                    snprintf(line2, sizeof(line2), "%s %s %s", tokens[4], tokens[5], tokens[6]);
                } else {
                    snprintf(line1, sizeof(line1), "%s %s %s %s", tokens[0], tokens[1], tokens[2], tokens[3]);
                    snprintf(line2, sizeof(line2), "%s %s %s %s", tokens[4], tokens[5], tokens[6], tokens[7]);
                }

                char full_buf[128];
                if (line2[0]) {
                    snprintf(full_buf, sizeof(full_buf), "%s\n%s", line1, line2);
                } else {
                    snprintf(full_buf, sizeof(full_buf), "%s", line1);
                }

                double pt = (n_tok >= 7) ? 12.0 : ((n_tok >= 5) ? 13.0 : 14.5);
                text_fit(cr, a, full_buf, x, y, maxw * 1.15, pt, text_col, hot);
            }
        } else if (grp->t[s][0]) {
            const char *tok = grp->t[s];
            double badge_r = 17.5 * a->ui.ui_scale;

            cairo_new_path(cr);
            cairo_arc(cr, x, y, badge_r, 0, 2 * M_PI);
            if (hot) {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, th->dark ? 0.28 : 0.40);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
                cairo_set_line_width(cr, 1.8 * a->ui.ui_scale);
                cairo_stroke(cr);
            } else {
                cairo_rgba(cr, th->wedge_empty);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, th->fg[0], th->fg[1], th->fg[2], th->dark ? 0.16 : 0.22);
                cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
                cairo_stroke(cr);
            }

            double pt = (strlen(tok) > 2) ? 14.0 : (strlen(tok) == 1 ? 22.0 : 18.0);
            draw_optical_text(cr, a, tok, x, y, pt, text_col, 1, 1);
        } else {
            cairo_new_path(cr);
            cairo_arc(cr, x, y, 3.0 * a->ui.ui_scale, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, th->fg[0], th->fg[1], th->fg[2], 0.16);
            cairo_fill(cr);
        }
    }

    /* Углубление хаба стика */
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r0 - 6.0 * a->ui.ui_scale, 0, 2 * M_PI);
    cairo_rgba(cr, th->hub); cairo_fill_preserve(cr);
    cairo_rgba(cr, th->border); cairo_set_line_width(cr, 1.2 * a->ui.ui_scale); cairo_stroke(cr);

    /* Мёртвая зона */
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, (r0 - 6.0 * a->ui.ui_scale) * 0.35, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, th->fg[0], th->fg[1], th->fg[2], 0.08);
    cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
    cairo_stroke(cr);

    /* Стик */
    double sx = is_left ? k->lx : k->rx;
    double sy = is_left ? k->ly : k->ry;
    double max_travel = r0 - 18.0 * a->ui.ui_scale;
    double puck_x = cx + sx * max_travel;
    double puck_y = cy + sy * max_travel;

    if (hypotf((float)sx, (float)sy) > 0.15f) {
        cairo_new_path(cr);
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, puck_x, puck_y);
        cairo_rgba(cr, th->accent);
        cairo_set_line_width(cr, 2.0 * a->ui.ui_scale);
        cairo_stroke(cr);
    }

    cairo_new_path(cr);
    cairo_arc(cr, puck_x, puck_y, 9.0 * a->ui.ui_scale, 0, 2 * M_PI);
    cairo_rgba(cr, th->wedge);
    cairo_fill_preserve(cr);
    cairo_rgba(cr, th->accent);
    cairo_set_line_width(cr, 2.0 * a->ui.ui_scale);
    cairo_stroke(cr);

    cairo_new_path(cr);
    cairo_arc(cr, puck_x, puck_y, 3.5 * a->ui.ui_scale, 0, 2 * M_PI);
    cairo_rgba(cr, th->accent);
    cairo_fill(cr);
}

static void draw(App *a, cairo_t *cr)
{
    Kb *k = &a->kb;
    const Theme *th = &a->th;
    double lw = a->ui.base_w;
    double lh = a->ui.base_h;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double rr = th->corner_radius * a->ui.ui_scale;
    cairo_new_path(cr);
    cairo_arc(cr, lw - rr, rr, rr, -M_PI / 2, 0);
    cairo_arc(cr, lw - rr, lh - rr, rr, 0, M_PI / 2);
    cairo_arc(cr, rr, lh - rr, rr, M_PI / 2, M_PI);
    cairo_arc(cr, rr, rr, rr, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);

    if (th->bg_style == BG_ACRYLIC) {
        cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, lh);
        double top_col[4] = {
            th->bg[0] + (th->dark ? 0.08 : -0.04),
            th->bg[1] + (th->dark ? 0.08 : -0.04),
            th->bg[2] + (th->dark ? 0.09 : -0.04),
            th->bg[3]
        };
        cairo_pattern_add_color_stop_rgba(pat, 0.0, top_col[0], top_col[1], top_col[2], top_col[3]);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, th->bg[0], th->bg[1], th->bg[2], th->bg[3]);
        cairo_set_source(cr, pat);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(pat);

        cairo_rgba(cr, th->border);
        cairo_set_line_width(cr, 1.2 * a->ui.ui_scale);
        cairo_stroke(cr);

        cairo_new_path(cr);
        cairo_arc(cr, rr + 1.0, rr + 1.0, rr, M_PI, 1.5 * M_PI);
        cairo_line_to(cr, lw - rr - 1.0, 1.0);
        cairo_arc(cr, lw - rr - 1.0, rr + 1.0, rr, 1.5 * M_PI, 1.75 * M_PI);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, th->dark ? 0.22 : 0.40);
        cairo_set_line_width(cr, 1.2 * a->ui.ui_scale);
        cairo_stroke(cr);
    } else {
        cairo_rgba(cr, th->bg);
        cairo_fill_preserve(cr);
        cairo_rgba(cr, th->border);
        cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
        cairo_stroke(cr);
    }

    double R = 138.0 * a->ui.ui_scale;
    double r0 = 46.0 * a->ui.ui_scale;
    double cy = lh * 0.45;
    double margin = 52.0 * a->ui.ui_scale;
    double lx = R + margin;
    double rx = lw - (R + margin);

    draw_ring(cr, a, lx, cy, R, r0, 1);
    draw_ring(cr, a, rx, cy, R, r0, 0);

    if (a->cfg_has_error) {
        double ex = lw - 24.0 * a->ui.ui_scale;
        double ey = 24.0 * a->ui.ui_scale;
        double er = 5.0 * a->ui.ui_scale;

        cairo_new_path(cr);
        cairo_arc(cr, ex, ey, er * 2.2, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, th->err[0], th->err[1], th->err[2], 0.25);
        cairo_fill(cr);

        cairo_new_path(cr);
        cairo_arc(cr, ex, ey, er, 0, 2 * M_PI);
        cairo_rgba(cr, th->err);
        cairo_fill(cr);
    }

    /* Центральная статусная плашка */
    double card_w = 200.0 * a->ui.ui_scale;
    double card_h = 78.0 * a->ui.ui_scale;
    double card_x = lw / 2.0 - card_w / 2.0;
    double card_y = cy - card_h / 2.0;
    double card_r = 14.0 * a->ui.ui_scale;

    cairo_new_path(cr);
    cairo_arc(cr, card_x + card_w - card_r, card_y + card_r, card_r, -M_PI / 2, 0);
    cairo_arc(cr, card_x + card_w - card_r, card_y + card_h - card_r, card_r, 0, M_PI / 2);
    cairo_arc(cr, card_x + card_r, card_y + card_h - card_r, card_r, M_PI / 2, M_PI);
    cairo_arc(cr, card_x + card_r, card_y + card_r, card_r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);

    cairo_rgba(cr, th->wedge);
    cairo_fill_preserve(cr);
    cairo_rgba(cr, th->border);
    cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
    cairo_stroke(cr);

    char lang[16];
    snprintf(lang, sizeof lang, "%s", k->lay[k->cur].name);
    for (char *c = lang; *c; c++) *c = (char)toupper((unsigned char)*c);

    char st[128];
    snprintf(st, sizeof st, "%s  •  %s", lang, k->sym ? "123" : (k->shift ? "ABC" : "abc"));
    text_c(cr, a, st, lw / 2.0, card_y + 22.0 * a->ui.ui_scale, 16.0, th->fg, 1);

    if (k->last[0]) {
        char typed_preview[32];
        snprintf(typed_preview, sizeof typed_preview, "→ %s", k->last);
        text_c(cr, a, typed_preview, lw / 2.0, card_y + 52.0 * a->ui.ui_scale, 22.0, th->accent, 1);
    } else {
        text_c(cr, a, "Flick R-Stick to type", lw / 2.0, card_y + 52.0 * a->ui.ui_scale, 11.5, th->fg_dim, 0);
    }

    double badge_h = 24.0 * a->ui.ui_scale;
    double by1 = lh - 56.0 * a->ui.ui_scale;
    double by2 = lh - 28.0 * a->ui.ui_scale;

    const KeyBinds *kbn = &k->binds;
    Badge row1[] = {
        { bind_label(kbn->shift, kbn->n_shift),         "Shift" },
        { bind_label(kbn->backspace, kbn->n_backspace), "Backspace" },
        { bind_label(kbn->space, kbn->n_space),         "Space" },
        { bind_label(kbn->enter, kbn->n_enter),         "Enter" },
    };
    Badge row2[] = {
        { "D-pad",                                      "Arrows" },
        { bind_label(kbn->sym, kbn->n_sym),             "Symbols" },
        { bind_label(kbn->layout, kbn->n_layout),       "Layout" },
        { bind_label(kbn->hide, kbn->n_hide),           "Hide" },
    };
    draw_badge_row(cr, a, row1, 4, lw, by1, badge_h);
    draw_badge_row(cr, a, row2, 4, lw, by2, badge_h);
}

/* ======================= pads / grab / hotplug ======================= */
static void pads_apply_grab(App *a)
{
    int should_grab = a->grab_pref && input_armed_strict(a->visible, a->mapped, a->ctx != NULL);
    for (int i = 0; i < a->npads; i++) {
        pad_set_grab_safe(&a->pads[i], should_grab);
    }
}

static int pad_index_by_path(App *a, const char *path)
{
    for (int i = 0; i < a->npads; i++) {
        if (a->pads[i].fd >= 0 && !strcmp(a->pads[i].path, path)) return i;
    }
    return -1;
}

static void pads_resync(App *a)
{
    glob_t g;
    int live[MAXPADS] = {0};
    if (glob("/dev/input/event*", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            int idx = pad_index_by_path(a, g.gl_pathv[i]);
            if (idx >= 0) { live[idx] = 1; continue; }
            if (a->npads >= MAXPADS) continue;
            Pad tmp;
            if (pad_open_at(&tmp, g.gl_pathv[i], 0) == 0) {
                a->pads[a->npads] = tmp;
                live[a->npads] = 1;
                fprintf(stderr, "radialkb: pad connected: %s (%s %04x:%04x)\n",
                        tmp.name, tmp.path, tmp.vendor, tmp.product);
                a->npads++;
            }
        }
        globfree(&g);
    }
    for (int i = a->npads - 1; i >= 0; i--) {
        if (live[i] && a->pads[i].fd >= 0) continue;
        fprintf(stderr, "radialkb: pad removed: %s\n", a->pads[i].name);
        pad_close(&a->pads[i]);
        kb_reset_input(&a->kb);
        reset_ime_state(a);
        chord_reset(&a->chord);
        a->pads[i] = a->pads[a->npads - 1];
        live[i] = live[a->npads - 1];
        a->npads--;
    }
    pads_apply_grab(a);
}

static void pads_close_all(App *a)
{
    for (int i = 0; i < a->npads; i++) pad_close(&a->pads[i]);
    a->npads = 0;
    kb_reset_input(&a->kb);
    reset_ime_state(a);
}

/* ======================= panel show / hide / redraw ======================= */
static void panel_mark_mapped(App *a)
{
    a->map_deadline = 0;
    if (a->visible && !a->mapped) {
        a->mapped = 1;
        pads_apply_grab(a);
    }
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    App *a = data;
    wl_callback_destroy(cb);
    if (a->frame_cb == cb) a->frame_cb = NULL;
    panel_mark_mapped(a);
}

static const struct wl_callback_listener frame_listener = { frame_done };

/* Есть ли свободный SHM-буфер (иначе рисовать нечем — ждём wl_buffer.release) */
static inline int has_free_buf(const App *a)
{
    for (int i = 0; i < 2; i++)
        if (a->bufs[i].buf && !a->bufs[i].busy) return 1;
    return 0;
}

static void app_redraw(App *a)
{
    if (!a->visible) return;
    Buf *b = NULL;
    for (int i = 0; i < 2; i++) if (!a->bufs[i].busy && a->bufs[i].buf) { b = &a->bufs[i]; break; }
    if (!b) return;
    cairo_t *cr = cairo_create(b->cs);
    cairo_scale(cr, a->scale, a->scale);
    draw(a, cr);
    cairo_destroy(cr);
    cairo_surface_flush(b->cs);
    if (a->buf_scale != a->scale) {
        wl_surface_set_buffer_scale(a->surf, a->scale); a->buf_scale = a->scale;
    }

    /* frame-callback нужен только чтобы узнать, что панель показана (mapped).
       Если композитор его не пришлёт, map_deadline в главном цикле подстрахует. */
    if (!a->mapped && !a->frame_cb) {
        a->frame_cb = wl_surface_frame(a->surf);
        wl_callback_add_listener(a->frame_cb, &frame_listener, a);
        a->map_deadline = now_ms() + 150;
    }

    wl_surface_attach(a->surf, b->buf, 0, 0);
    wl_surface_damage(a->surf, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(a->surf);
    b->busy = 1;
    a->kb.dirty = 0; a->last_draw = now_ms();
    wl_display_flush(a->dpy);
}

static void panel_show(App *a)
{
    int w = a->ui.base_w * a->scale;
    int h = a->ui.base_h * a->scale;
    if (a->bw != w || a->bh != h || !a->bufs[0].buf) {
        for (int i = 0; i < 2; i++) buf_destroy(&a->bufs[i]);
        if (buf_create(a, &a->bufs[0], w, h) || buf_create(a, &a->bufs[1], w, h)) {
            fprintf(stderr, "radialkb: cannot allocate shm buffers\n"); return;
        }
        a->bw = w; a->bh = h;
    }
    a->visible = 1;
    update_surface_blur(a);
    kb_reset_input(&a->kb);
    app_redraw(a);
}

static void panel_hide(App *a)
{
    if (!a->visible && !a->surf) return;
    a->visible = 0;
    a->mapped = 0;
    a->map_deadline = 0;
    if (a->frame_cb) { wl_callback_destroy(a->frame_cb); a->frame_cb = NULL; }
    update_surface_blur(a);
    pads_apply_grab(a);
    if (a->surf) {
        wl_surface_attach(a->surf, NULL, 0, 0);
        wl_surface_commit(a->surf);
    }
    for (int i = 0; i < 2; i++) a->bufs[i].busy = 0;
    kb_reset_input(&a->kb);
    reset_ime_state(a);
    if (a->dpy) wl_display_flush(a->dpy);
}

static inline int is_input_active(const App *a)
{
    if (!a->ctx) return 0;
    if (a->user_shown) return 1;
    return a->field_active;
}

static void apply_visibility(App *a)
{
    int im_active = is_input_active(a);
    int want = vis_want(im_active, a->npads > 0, a->plasma_mode,
                        a->invoke, a->user_shown, a->user_hidden);
    if (want && !a->visible) panel_show(a);
    else if (!want && a->visible) panel_hide(a);
    else pads_apply_grab(a);
}

static void show_panel(App *a)
{
    if (a->ctx != NULL && a->npads > 0) {
        a->user_hidden = 0;
        a->user_shown = 1;
        apply_visibility(a);
    }
}

static void hide_panel(App *a)
{
    a->user_hidden = 1;
    a->user_shown = 0;
    apply_visibility(a);
}

/* ======================= input method protocol ======================= */
static void ctx_surrounding(void *d, struct zwp_input_method_context_v1 *c, const char *t, uint32_t cu, uint32_t an)
{
    (void)c;
    App *a = d;
    a->field_has_surrounding = 1;
    a->cursor_pos = cu;
    a->anchor_pos = an;
    if (t) {
        snprintf(a->surrounding, sizeof(a->surrounding), "%s", t);
    } else {
        a->surrounding[0] = '\0';
    }
    a->field_active = 1;
    /* user_hidden здесь НЕ сбрасываем: surrounding_text приходит при каждой правке
       текста, и панель, скрытая вручную, всплывала бы снова. Сброс — только при
       смене фокуса (im_activate/im_deactivate) или по хоткею show. */
    apply_visibility(a);
}

static void ctx_reset(void *d, struct zwp_input_method_context_v1 *c)
{
    (void)c;
    App *a = d;
    a->field_has_surrounding = 0;
}

static void ctx_content_type(void *d, struct zwp_input_method_context_v1 *c, uint32_t h, uint32_t p)
{
    (void)c;
    App *a = d;
    a->content_hint = h;
    a->content_purpose = p;
    a->field_active = 1;
}

static void ctx_invoke(void *d, struct zwp_input_method_context_v1 *c, uint32_t b, uint32_t i)
{ (void)d; (void)c; (void)b; (void)i; }

static void ctx_commit_state(void *d, struct zwp_input_method_context_v1 *c, uint32_t serial)
{
    (void)c;
    App *a = d;
    a->serial = serial;
    if (a->field_active) {
        apply_visibility(a);
    }
}

static void ctx_pref_lang(void *d, struct zwp_input_method_context_v1 *c, const char *l)
{ (void)d; (void)c; (void)l; }

static const struct zwp_input_method_context_v1_listener ctx_listener = {
    ctx_surrounding, ctx_reset, ctx_content_type, ctx_invoke, ctx_commit_state, ctx_pref_lang
};

static void im_activate(void *d, struct zwp_input_method_v1 *im, struct zwp_input_method_context_v1 *ctx)
{
    (void)im; App *a = d;
    if (a->ctx) zwp_input_method_context_v1_destroy(a->ctx);
    a->ctx = ctx; a->serial = 0;
    a->field_active = 0;
    a->field_has_surrounding = 0;
    a->surrounding[0] = '\0';
    zwp_input_method_context_v1_add_listener(ctx, &ctx_listener, a);
    kb_reset_input(&a->kb);
    a->user_hidden = 0;
    a->user_shown = 0;
    plasma_mode_load(a);
    pads_resync(a);
    apply_visibility(a);
}

static void im_deactivate(void *d, struct zwp_input_method_v1 *im, struct zwp_input_method_context_v1 *ctx)
{
    (void)im; App *a = d;
    if (a->ctx == ctx) {
        zwp_input_method_context_v1_destroy(ctx);
        a->ctx = NULL;
    }
    a->field_active = 0;
    a->field_has_surrounding = 0;
    a->user_shown = 0;
    a->user_hidden = 0;
    panel_hide(a);
}

static const struct zwp_input_method_v1_listener im_listener = { im_activate, im_deactivate };

/* ======================= registry / output ======================= */
static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                         int32_t sp, const char *mk, const char *md, int32_t tr)
{ (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)mk; (void)md; (void)tr; }
static void out_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{ (void)d; (void)o; (void)f; (void)w; (void)h; (void)r; }
static void out_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void out_scale(void *d, struct wl_output *o, int32_t s) { (void)o; ((App *)d)->scale = s > 0 ? s : 1; }
static void out_name(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; (void)n; }
static void out_desc(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; (void)n; }
static const struct wl_output_listener out_listener = {
    out_geometry, out_mode, out_done, out_scale, out_name, out_desc
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    App *a = d;
    if (!strcmp(iface, "wl_compositor"))
        a->comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 3 ? ver : 3);
    else if (!strcmp(iface, "wl_shm"))
        a->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_output") && !a->out) {
        a->out = wl_registry_bind(r, name, &wl_output_interface, ver < 2 ? ver : 2);
        wl_output_add_listener(a->out, &out_listener, a);
    } else if (!strcmp(iface, "zwp_input_method_v1"))
        a->im = wl_registry_bind(r, name, &zwp_input_method_v1_interface, 1);
    else if (!strcmp(iface, "zwp_input_panel_v1"))
        a->panel = wl_registry_bind(r, name, &zwp_input_panel_v1_interface, 1);
    else if (!strcmp(iface, "ext_background_effect_manager_v1")) {
        a->ext_bg_mgr = wl_registry_bind(r, name, &ext_background_effect_manager_v1_interface, 1);
        wl_proxy_add_listener((struct wl_proxy *)a->ext_bg_mgr, (void (**)(void))&ext_bg_listener, a);
    } else if (!strcmp(iface, "org_kde_kwin_blur_manager")) {
        a->kde_blur_mgr = wl_registry_bind(r, name, &org_kde_kwin_blur_manager_interface, 1);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

static void on_signal(int s) { (void)s; A.running = 0; }

static int watch_dir(int *fd, int *wd, const char *dir, uint32_t mask)
{
    if (*fd < 0) *fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (*fd < 0) return -1;
    *wd = inotify_add_watch(*fd, dir, mask);
    return *wd < 0 ? -1 : 0;
}

static void drain_inotify(int fd)
{
    char buf[INOTIFY_BUFSZ];
    while (read(fd, buf, sizeof buf) > 0) {}
}

/* Разбирает события каталога конфигов и возвращает 1, только если затронут
   файл, который мы реально читаем (иначе любая запись в ~/.config будила бы
   перезагрузку). Буфер выровнен под struct inotify_event. */
static int cfg_events_relevant(App *a)
{
    char buf[INOTIFY_BUFSZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    const char *lay = strrchr(a->layout_path, '/');
    lay = lay ? lay + 1 : a->layout_path;
    int hit = 0;
    ssize_t len;
    while ((len = read(a->ino_cfg, buf, sizeof buf)) > 0) {
        for (char *p = buf; p < buf + len; ) {
            const struct inotify_event *ev = (const struct inotify_event *)p;
            if (ev->mask & IN_Q_OVERFLOW) hit = 1;
            else if (ev->len > 0) {
                if (!strcmp(ev->name, "kwinrc") || !strcmp(ev->name, "kdeglobals") ||
                    !strcmp(ev->name, "radialkb"))
                    hit = 1;
                else if (ev->wd == a->wd_radialkb &&
                         (!strcmp(ev->name, "config.ini") || !strcmp(ev->name, lay)))
                    hit = 1;
            }
            p += sizeof *ev + ev->len;
        }
    }
    return hit;
}

static void setup_cfg_watches(App *a)
{
    if (a->ino_cfg < 0) {
        a->ino_cfg = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (a->ino_cfg < 0) return;
    }
    /* KConfig и большинство редакторов пишут атомарно (rename) — IN_MODIFY/IN_ATTRIB не нужны */
    uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE;
    if (a->wd_cfg < 0) {
        a->wd_cfg = inotify_add_watch(a->ino_cfg, a->cfg_dir, mask);
    }
    if (a->wd_radialkb < 0) {
        a->wd_radialkb = inotify_add_watch(a->ino_cfg, a->radialkb_dir, mask);
    }
}

/* ======================= main ======================= */
int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "C.UTF-8");
    setlocale(LC_NUMERIC, "C");
    A.scale = 1; A.grab_pref = 1; A.running = 1;
    A.ino_input = A.wd_input = A.ino_cfg = A.wd_cfg = A.wd_radialkb = -1;
    A.cfg_dirty = 0; A.cfg_reload_at = 0;
    A.chord.a = BTN_SELECT; A.chord.b = BTN_START;
    A.invoke = INVOKE_SMART; A.cli_invoke = -1;

    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(A.cfg_dir, sizeof A.cfg_dir, "%s", xdg);
    else if (home) snprintf(A.cfg_dir, sizeof A.cfg_dir, "%s/.config", home);
    else snprintf(A.cfg_dir, sizeof A.cfg_dir, ".");
    snprintf(A.kwinrc, sizeof A.kwinrc, "%s/kwinrc", A.cfg_dir);
    snprintf(A.kdeglobals, sizeof A.kdeglobals, "%s/kdeglobals", A.cfg_dir);
    snprintf(A.radialkb_dir, sizeof A.radialkb_dir, "%s/radialkb", A.cfg_dir);
    snprintf(A.layout_path, sizeof A.layout_path, "%s/radialkb/layouts.conf", A.cfg_dir);

    radial_config_load(&A, A.layout_path, sizeof A.layout_path);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-grab")) A.grab_pref = 0;
        else if (!strcmp(argv[i], "--manual")) A.cli_invoke = A.invoke = INVOKE_MANUAL;
        else if (!strcmp(argv[i], "--smart")) A.cli_invoke = A.invoke = INVOKE_SMART;
        else if (!strcmp(argv[i], "--auto")) A.cli_invoke = A.invoke = INVOKE_AUTO;
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc) {
            snprintf(A.cli_layout, sizeof A.cli_layout, "%s", argv[++i]);
            snprintf(A.layout_path, sizeof A.layout_path, "%s", A.cli_layout);
        } else {
            fprintf(stderr, "usage: %s [--no-grab] [--manual|--smart|--auto] [--layout FILE]\n", argv[0]);
            return 2;
        }
    }
    if (getenv("RADIALKB_NO_GRAB")) A.grab_pref = 0;
    if (getenv("RADIALKB_MANUAL")) A.cli_invoke = A.invoke = INVOKE_MANUAL;

    plasma_mode_load(&A);
    theme_load(&A);

    for (int i = 0; i < MAXPADS; i++) A.pads[i].fd = -1;
    A.kb.ud = &A; A.kb.text = cb_text; A.kb.key = cb_key;
    kb_load_layouts(&A.kb, A.layout_path[0] ? A.layout_path : NULL);
    kb_reset_input(&A.kb);

    watch_dir(&A.ino_input, &A.wd_input, "/dev/input",
              IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO);
    setup_cfg_watches(&A);

    pads_resync(&A);

    A.dpy = wl_display_connect(NULL);
    if (!A.dpy) { fprintf(stderr, "radialkb: cannot connect to Wayland\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(A.dpy);
    wl_registry_add_listener(reg, &reg_listener, &A);
    wl_display_roundtrip(A.dpy); wl_display_roundtrip(A.dpy);

    if (!A.comp || !A.shm) { fprintf(stderr, "radialkb: missing wl_compositor/wl_shm\n"); return 1; }
    if (!A.im || !A.panel) {
        fprintf(stderr, "radialkb: zwp_input_method_v1 / zwp_input_panel_v1 are not available.\n");
        return 1;
    }
    A.surf = wl_compositor_create_surface(A.comp);
    A.psurf = zwp_input_panel_v1_get_input_panel_surface(A.panel, A.surf);
    zwp_input_panel_surface_v1_set_toplevel(A.psurf, A.out, ZWP_INPUT_PANEL_SURFACE_V1_POSITION_CENTER_BOTTOM);
    zwp_input_method_v1_add_listener(A.im, &im_listener, &A);
    wl_display_roundtrip(A.dpy);

    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    int wfd = wl_display_get_fd(A.dpy);

    while (A.running) {
        while (wl_display_prepare_read(A.dpy) != 0) wl_display_dispatch_pending(A.dpy);
        short wl_events = POLLIN;
        if (wl_display_flush(A.dpy) < 0) {
            if (errno != EAGAIN) { wl_display_cancel_read(A.dpy); break; }
            wl_events |= POLLOUT;   /* сокет переполнен: ждём возможности дописать запросы */
        }

        struct pollfd fds[4 + MAXPADS]; int map[4 + MAXPADS]; int nf = 0;
        fds[nf] = (struct pollfd){ wfd, wl_events, 0 }; map[nf++] = -1;
        int i_in = -1, i_cfg = -1;
        if (A.ino_input >= 0) { i_in = nf; fds[nf] = (struct pollfd){ A.ino_input, POLLIN, 0 }; map[nf++] = -2; }
        if (A.ino_cfg >= 0)   { i_cfg = nf; fds[nf] = (struct pollfd){ A.ino_cfg, POLLIN, 0 }; map[nf++] = -3; }
        for (int i = 0; i < A.npads; i++) {
            if (A.pads[i].fd >= 0) {
                fds[nf] = (struct pollfd){ A.pads[i].fd, POLLIN, 0 };
                map[nf++] = i;
            }
        }

        int64_t now = now_ms();
        int timeout = -1;
        int is_armed = input_armed_strict(A.visible, A.mapped, A.ctx != NULL);
        int rep = is_armed ? kb_tick(&A.kb, now) : -1;
        if (rep >= 0) timeout = rep;
        /* Без свободного буфера таймаут не ставим: poll проснётся по wl_buffer.release
           через сокет Wayland. Иначе poll(0) превращается в busy-loop на 100% CPU. */
        if (A.visible && A.kb.dirty && has_free_buf(&A)) {
            int since = (int)(now - A.last_draw);
            int wait = since >= 16 ? 0 : 16 - since;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }
        if (A.visible && !A.mapped && A.map_deadline) {
            int wait = (int)(A.map_deadline - now);
            if (wait < 0) wait = 0;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }
        if (A.pads_dirty) {
            int wait = (int)(A.pads_rescan_at - now);
            if (wait < 0) wait = 0;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }
        if (A.cfg_dirty) {
            int wait = (int)(A.cfg_reload_at - now);
            if (wait < 0) wait = 0;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }

        int r = poll(fds, (nfds_t)nf, timeout);
        if (r < 0 && errno != EINTR) { wl_display_cancel_read(A.dpy); break; }
        if (r > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_read_events(A.dpy) < 0) break;
        } else wl_display_cancel_read(A.dpy);
        if (wl_display_dispatch_pending(A.dpy) < 0) break;

        /* callback первого кадра не пришёл — не оставляем клавиатуру навсегда «неактивной» */
        if (A.visible && !A.mapped && A.map_deadline && now_ms() >= A.map_deadline)
            panel_mark_mapped(&A);

        if (r > 0 && i_in >= 0 && (fds[i_in].revents & POLLIN)) {
            drain_inotify(A.ino_input);
            A.pads_dirty = 1;
            A.pads_rescan_at = now_ms() + 250;
        }
        if (r > 0 && i_cfg >= 0 && (fds[i_cfg].revents & POLLIN)) {
            int relevant = cfg_events_relevant(&A);
            setup_cfg_watches(&A);
            if (relevant) {
                A.cfg_dirty = 1;
                A.cfg_reload_at = now_ms() + 80;
            }
        }

        if (A.pads_dirty && now_ms() >= A.pads_rescan_at) {
            A.pads_dirty = 0;
            pads_resync(&A);
            apply_visibility(&A);
        }

        /* Применение горячей подгрузки конфига после дебаунса */
        if (A.cfg_dirty && now_ms() >= A.cfg_reload_at) {
            A.cfg_dirty = 0;
            plasma_mode_load(&A);
            theme_load(&A);
            radial_config_load(&A, A.layout_path, sizeof A.layout_path);
            kb_load_layouts(&A.kb, A.layout_path[0] ? A.layout_path : NULL);
            update_surface_blur(&A);
            A.kb.dirty = 1;

            int nw = A.ui.base_w * A.scale;
            int nh = A.ui.base_h * A.scale;
            if (A.visible && (A.bw != nw || A.bh != nh)) {
                panel_show(&A);
            } else {
                apply_visibility(&A);
                if (A.visible) app_redraw(&A);
            }
            fprintf(stderr, "radialkb: configuration reloaded successfully\n");
        }

        if (r > 0) {
            for (int f = 1; f < nf; f++) {
                if (map[f] < 0) continue;
                if (!(fds[f].revents & (POLLIN | POLLERR | POLLHUP))) continue;
                Pad *p = &A.pads[map[f]];
                if (p->fd < 0) continue;
                struct input_event ev[64]; ssize_t n;
                while ((n = read(p->fd, ev, sizeof ev)) > 0) {
                    for (size_t e = 0; e < (size_t)n / sizeof ev[0]; e++) {
                        if (ev[e].type != EV_KEY && ev[e].type != EV_ABS) continue;
                        if (ev[e].type == EV_KEY) {
                            int armed = input_armed_strict(A.visible, A.mapped, A.ctx != NULL);
                            int cr = chord_event(&A.chord, ev[e].code, ev[e].value);
                            if (cr == CHORD_FIRE) show_panel(&A);
                            if (cr == CHORD_TAP && armed) {   /* одиночный тап кнопки аккорда */
                                kb_key(&A.kb, ev[e].code, 1);
                                kb_key(&A.kb, ev[e].code, 0);
                            }
                            if (cr != CHORD_PASS) continue;
                            if (armed) kb_key(&A.kb, ev[e].code, ev[e].value);
                        } else if (input_armed_strict(A.visible, A.mapped, A.ctx != NULL)) {
                            kb_abs(&A.kb, p, ev[e].code, ev[e].value);
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    pad_close(p);
                    kb_reset_input(&A.kb);
                    reset_ime_state(&A);
                    A.pads_dirty = 1;
                    A.pads_rescan_at = now_ms();
                } else if (p->grab_pending && !pad_any_key_pressed(p->fd)) {
                    pad_set_grab_safe(p, 1);
                }
            }
        }
        if (A.kb.hide) {
            A.kb.hide = 0;
            hide_panel(&A);
        }
        if (A.visible && A.kb.dirty && now_ms() - A.last_draw >= 16) app_redraw(&A);
    }
    pads_close_all(&A);
    if (A.ext_bg_surf) ext_background_effect_surface_v1_destroy(A.ext_bg_surf);
    if (A.ext_bg_mgr) ext_background_effect_manager_v1_destroy(A.ext_bg_mgr);
    if (A.kde_blur) org_kde_kwin_blur_release(A.kde_blur);
    if (A.kde_blur_mgr) org_kde_kwin_blur_manager_destroy(A.kde_blur_mgr);
    if (A.ctx) zwp_input_method_context_v1_destroy(A.ctx);
    if (A.ino_input >= 0) close(A.ino_input);
    if (A.ino_cfg >= 0) close(A.ino_cfg);
    wl_display_disconnect(A.dpy);
    return 0;
}