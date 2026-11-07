#include "app.h"
#include <ctype.h>

#define INOTIFY_BUFSZ ((sizeof(struct inotify_event) + NAME_MAX + 1) * 16)

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

#define INI_CACHE_N 4
static struct { char path[PATH_MAX + 64]; char *data; int valid; } ini_cache[INI_CACHE_N];

void ini_cache_flush(void)
{
    for (int i = 0; i < INI_CACHE_N; i++) {
        free(ini_cache[i].data);
        ini_cache[i].data = NULL;
        ini_cache[i].valid = 0;
    }
}

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

void theme_load(App *a)
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

void plasma_mode_load(App *a)
{
    ini_cache_flush();
    a->plasma_mode = plasma_vk_mode_from_file(a->kwinrc);
}

static void apply_cli_overrides(App *a, char *layout_path, size_t n)
{
    if (a->cli_invoke >= 0) a->invoke = a->cli_invoke;
    if (a->cli_layout[0]) snprintf(layout_path, n, "%s", a->cli_layout);
}

void radial_config_load(App *a, char *layout_path, size_t n)
{
    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/radialkb/config.ini", a->cfg_dir);
    char v[128];

    a->invoke = INVOKE_SMART;
    a->chord.a = BTN_SELECT; a->chord.b = BTN_START;
    snprintf(layout_path, n, "%s/radialkb/layouts.conf", a->cfg_dir);

    a->ui.ui_scale = 1.0;
    a->ui.font_scale = 1.0;
    snprintf(a->ui.font_family, sizeof a->ui.font_family, "Noto Sans");
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

int watch_dir(int *fd, int *wd, const char *dir, uint32_t mask)
{
    if (*fd < 0) *fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (*fd < 0) return -1;
    *wd = inotify_add_watch(*fd, dir, mask);
    return *wd < 0 ? -1 : 0;
}

void drain_inotify(int fd)
{
    char buf[INOTIFY_BUFSZ];
    while (read(fd, buf, sizeof buf) > 0) {}
}

int cfg_events_relevant(App *a)
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

void setup_cfg_watches(App *a)
{
    if (a->ino_cfg < 0) {
        a->ino_cfg = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (a->ino_cfg < 0) return;
    }
    uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE;
    if (a->wd_cfg < 0) {
        a->wd_cfg = inotify_add_watch(a->ino_cfg, a->cfg_dir, mask);
    }
    if (a->wd_radialkb < 0) {
        a->wd_radialkb = inotify_add_watch(a->ino_cfg, a->radialkb_dir, mask);
    }
}