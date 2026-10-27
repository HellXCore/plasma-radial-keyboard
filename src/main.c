/* plasma-radial-keyboard - dual-stick radial on-screen keyboard for KWin/Wayland. */
#define _GNU_SOURCE
#include <ctype.h>
#include <limits.h>
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
#define INOTIFY_BUFSZ ((sizeof(struct inotify_event) + NAME_MAX + 1) * 8)

typedef struct {
    struct wl_buffer *buf; void *data; size_t size; int busy;
    cairo_surface_t *cs;
} Buf;

typedef struct {
    double bg[4], border[4], wedge[4], wedge_border[4], wedge_empty[4], hub[4];
    double fg[4], fg_dim[4], accent[4], accent_fg[4];
    int dark;
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
    int scale, buf_scale;
    Buf bufs[2]; int bw, bh;
    int visible;
    int mapped;               /* 1 только если панель реально показана на экране */
    Kb kb; Pad pads[MAXPADS]; int npads;
    int grab_pref; volatile sig_atomic_t running;
    int64_t last_draw;
    int plasma_mode;          /* PLASMA_VK_* */
    int invoke;               /* INVOKE_AUTO / INVOKE_MANUAL */
    int user_shown;           /* manual / Never override */
    int user_hidden;          /* Select: stay hidden until next activate/toggle */
    int toggle_a, toggle_b;   /* evdev codes for the show/hide chord */
    int chord_a, chord_b, chord_fired;
    int ino_input, wd_input;
    int ino_cfg, wd_cfg;
    int pads_dirty;
    int64_t pads_rescan_at;
    char cfg_dir[PATH_MAX];
    char kwinrc[PATH_MAX + 32];
    char kdeglobals[PATH_MAX + 32];
    Theme th;
    UiConfig ui;
} App;

static App A;

static void color_set(double *c, double r, double g, double b, double a)
{ c[0] = r; c[1] = g; c[2] = b; c[3] = a; }

static int parse_rgb(const char *s, double *o)
{
    int r = 0, g = 0, b = 0, a = 255;
    int n = sscanf(s, "%d,%d,%d,%d", &r, &g, &b, &a);
    if (n < 3) return 0;
    o[0] = r / 255.0; o[1] = g / 255.0; o[2] = b / 255.0;
    o[3] = (n >= 4) ? (a / 255.0) : 1.0;
    return 1;
}

static int ini_get(const char *path, const char *group, const char *key, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[1024], cur[128] = "";
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || !*s) continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            cur[0] = 0;
            if (e) {
                size_t L = (size_t)(e - s - 1); if (L > sizeof cur - 1) L = sizeof cur - 1;
                memcpy(cur, s + 1, L); cur[L] = 0;
            }
            continue;
        }
        if (strcmp(cur, group)) continue;
        size_t kl = strlen(key);
        if (strncmp(s, key, kl) || s[kl] != '=') continue;
        s += kl + 1;
        char *nl = strchr(s, '\n'); if (nl) *nl = 0;
        char *cr = strchr(s, '\r'); if (cr) *cr = 0;
        snprintf(out, n, "%s", s);
        found = 1; break;
    }
    fclose(f);
    return found;
}

/* Нативная палитра Breeze Dark по умолчанию */
static void theme_defaults_breeze_dark(Theme *t)
{
    color_set(t->bg, 0.145, 0.165, 0.188, 0.94);       /* Breeze Dark #252a30 */
    color_set(t->border, 0.29, 0.33, 0.38, 0.60);   /* Тонкая рамка панели */
    color_set(t->wedge, 0.20, 0.23, 0.27, 0.95);
    color_set(t->wedge_border, 0.32, 0.36, 0.42, 0.50);
    color_set(t->wedge_empty, 0.16, 0.18, 0.21, 0.50);
    color_set(t->hub, 0.12, 0.14, 0.16, 0.90);
    color_set(t->fg, 0.93, 0.94, 0.95, 1.0);          /* Breeze text */
    color_set(t->fg_dim, 0.65, 0.69, 0.74, 1.0);
    color_set(t->accent, 0.235, 0.576, 0.886, 1.0);    /* Breeze Blue #3daee9 */
    color_set(t->accent_fg, 1.0, 1.0, 1.0, 1.0);
    t->dark = 1;
}

/* Генерация цветовой схемы из kdeglobals с точным следованием стилю Breeze */
static void theme_load(App *a)
{
    theme_defaults_breeze_dark(&a->th);
    char col[64], fg[64], sel[64], btn[64];
    double bgc[4], fgc[4], selc[4], btnc[4];
    int ok_bg  = ini_get(a->kdeglobals, "Colors:Window", "BackgroundNormal", col, sizeof col) && parse_rgb(col, bgc);
    int ok_fg  = ini_get(a->kdeglobals, "Colors:Window", "ForegroundNormal", fg, sizeof fg) && parse_rgb(fg, fgc);
    int ok_sel = ini_get(a->kdeglobals, "Colors:Selection", "BackgroundNormal", sel, sizeof sel) && parse_rgb(sel, selc);
    int ok_btn = ini_get(a->kdeglobals, "Colors:Button", "BackgroundNormal", btn, sizeof btn) && parse_rgb(btn, btnc);

    if (ok_bg && ok_fg) {
        Theme *t = &a->th;
        double lum = 0.2126 * bgc[0] + 0.7152 * bgc[1] + 0.0722 * bgc[2];
        t->dark = lum < 0.5;

        color_set(t->bg, bgc[0], bgc[1], bgc[2], 0.95);
        color_set(t->fg, fgc[0], fgc[1], fgc[2], 0.98);
        color_set(t->fg_dim, fgc[0] * 0.7 + bgc[0] * 0.3,
                  fgc[1] * 0.7 + bgc[1] * 0.3,
                  fgc[2] * 0.7 + bgc[2] * 0.3, 1.0);

        if (ok_sel) color_set(t->accent, selc[0], selc[1], selc[2], 1.0);
        else color_set(t->accent, 0.24, 0.58, 0.89, 1.0);

        if (ok_btn) {
            color_set(t->wedge, btnc[0], btnc[1], btnc[2], 0.95);
        } else {
            double mix = t->dark ? 0.18 : 0.10;
            color_set(t->wedge, bgc[0] * (1 - mix) + fgc[0] * mix,
                      bgc[1] * (1 - mix) + fgc[1] * mix,
                      bgc[2] * (1 - mix) + fgc[2] * mix, 0.95);
        }

        color_set(t->wedge_border, t->fg[0], t->fg[1], t->fg[2], t->dark ? 0.12 : 0.18);
        color_set(t->border, t->fg[0], t->fg[1], t->fg[2], 0.15);
        color_set(t->wedge_empty, bgc[0] * 0.88, bgc[1] * 0.88, bgc[2] * 0.88, 0.40);
        color_set(t->hub, bgc[0] * 0.75, bgc[1] * 0.75, bgc[2] * 0.75, 0.90);
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
    return -1;
}

static void parse_toggle(const char *spec, int *a, int *b)
{
    char buf[64]; snprintf(buf, sizeof buf, "%s", spec ? spec : "select+start");
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
    char *plus = strchr(buf, '+');
    if (!plus) { *a = BTN_SELECT; *b = BTN_START; return; }
    *plus = 0;
    int x = map_btn_name(buf), y = map_btn_name(plus + 1);
    *a = x > 0 ? x : BTN_SELECT;
    *b = y > 0 ? y : BTN_START;
}

static double parse_coord_double(const char *s)
{
    if (!s) return 0.0;
    char buf[128];
    snprintf(buf, sizeof buf, "%.127s", s);
    for (char *p = buf; *p; p++) {
        if (*p == ',') *p = '.';
    }
    return strtod(buf, NULL);
}

static void radial_config_load(App *a, char *layout_path, size_t n)
{
    char path[PATH_MAX + 32];
    snprintf(path, sizeof path, "%s/radialkb/config.ini", a->cfg_dir);
    char v[128];

    a->ui.ui_scale = 1.0;
    a->ui.font_scale = 1.0;
    snprintf(a->ui.font_family, sizeof a->ui.font_family, "Noto Sans");

    if (ini_get(path, "general", "invoke", v, sizeof v)) {
        if (!strcmp(v, "manual")) a->invoke = INVOKE_MANUAL;
        else if (!strcmp(v, "auto")) a->invoke = INVOKE_AUTO;
    }
    if (ini_get(path, "general", "toggle", v, sizeof v)) parse_toggle(v, &a->toggle_a, &a->toggle_b);
    else parse_toggle("select+start", &a->toggle_a, &a->toggle_b);
    if (ini_get(path, "general", "layout", v, sizeof v))
        snprintf(layout_path, n, "%s", v);

    /* UI & Шрифты */
    if (ini_get(path, "ui", "scale", v, sizeof v)) {
        double d = parse_coord_double(v);
        if (d >= 0.5 && d <= 3.0) a->ui.ui_scale = d;
    }
    if (ini_get(path, "ui", "font_scale", v, sizeof v)) {
        double d = parse_coord_double(v);
        if (d >= 0.5 && d <= 3.0) a->ui.font_scale = d;
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

    /* Если ширина/высота не заданы явно, масштабируем базовый размер окна пропорционально ui_scale */
    if (!custom_w) a->ui.base_w = (int)(DEFAULT_LW * a->ui.ui_scale);
    if (!custom_h) a->ui.base_h = (int)(DEFAULT_LH * a->ui.ui_scale);
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

static void text_c(cairo_t *cr, App *a, const char *s, double x, double y, double px,
                   const double *col, int bold)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    char fdesc[192];
    snprintf(fdesc, sizeof fdesc, "%s %s", a->ui.font_family, bold ? "Bold" : "Regular");
    PangoFontDescription *fd = pango_font_description_from_string(fdesc);
    pango_font_description_set_absolute_size(fd, px * a->ui.font_scale * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, s, -1);
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    cairo_rgba(cr, col);
    cairo_move_to(cr, x - w / 2.0, y - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd); g_object_unref(l);
    cairo_new_path(cr); /* Сбрасываем текущую точку Pango, чтобы она не тянулась линиями */
}

static void text_fit(cairo_t *cr, App *a, const char *s, double x, double y, double maxw, double px,
                     const double *col, int bold)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    char fdesc[192];
    snprintf(fdesc, sizeof fdesc, "%s %s", a->ui.font_family, bold ? "Bold" : "Regular");
    PangoFontDescription *fd = pango_font_description_from_string(fdesc);
    pango_font_description_set_absolute_size(fd, px * a->ui.font_scale * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
    pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(l, s, -1);
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    cairo_rgba(cr, col);
    cairo_move_to(cr, x - maxw / 2.0, y - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd); g_object_unref(l);
    cairo_new_path(cr); /* Сбрасываем текущую точку Pango */
}

static void wedge(cairo_t *cr, double cx, double cy, double r0, double r1, int sector)
{
    double th = sector * (M_PI / 4) - M_PI / 2;
    double half = M_PI / 8 - 0.035;
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

static void draw_ring(cairo_t *cr, App *a, double cx, double cy, double R, double r0, int is_left)
{
    Kb *k = &a->kb;
    const Theme *th = &a->th;
    const Layout *Ly = &k->lay[k->cur];
    int layer = kb_layer(k);
    const Group *grp = &Ly->g[layer][k->group];
    double maxw = (R - r0) * 0.72;

    for (int s = 0; s < NSLOTS; s++) {
        int hot = is_left ? (s == k->group) : (k->R.active && k->R.sel == s);
        wedge(cr, cx, cy, r0, R, s);
        if (hot) {
            cairo_rgba(cr, th->accent);
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, 1.5);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
            cairo_stroke(cr);
        } else if (!is_left && !grp->t[s][0]) {
            cairo_rgba(cr, th->wedge_empty);
            cairo_fill(cr);
        } else {
            cairo_rgba(cr, th->wedge);
            cairo_fill_preserve(cr);
            cairo_rgba(cr, th->wedge_border);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        double x, y; sector_pos(cx, cy, (R + r0) / 2, s, &x, &y);
        const double *text_col = hot ? th->accent_fg : th->fg;

        if (is_left) {
            char buf[96] = "";
            const Group *g = &Ly->g[layer][s];
            for (int i = 0; i < 4; i++) {
                if (i) strncat(buf, " ", sizeof buf - strlen(buf) - 1);
                strncat(buf, g->t[TOK2SLOT[i]], sizeof buf - strlen(buf) - 1);
            }
            int extra = 0;
            for (int i = 4; i < NSLOTS; i++) if (g->t[TOK2SLOT[i]][0]) extra = 1;
            if (extra) {
                strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
                for (int i = 4; i < NSLOTS; i++) {
                    if (i > 4) strncat(buf, " ", sizeof buf - strlen(buf) - 1);
                    strncat(buf, g->t[TOK2SLOT[i]], sizeof buf - strlen(buf) - 1);
                }
            }
            text_fit(cr, a, buf, x, y, maxw, 15, text_col, hot);
        } else if (grp->t[s][0]) {
            text_fit(cr, a, grp->t[s], x, y, maxw, strlen(grp->t[s]) > 2 ? 20 : 28, text_col, 1);
        }
    }

    /* Центральный круг (хаб) */
    cairo_new_path(cr); /* ГАРАНТИРУЕТ отсутствие соединительной наклонной линии */
    cairo_arc(cr, cx, cy, r0 - 6, 0, 2 * M_PI);
    cairo_rgba(cr, th->hub); cairo_fill_preserve(cr);
    cairo_rgba(cr, th->wedge_border); cairo_set_line_width(cr, 1.0); cairo_stroke(cr);

    /* Точка положения стика */
    cairo_new_path(cr); /* Сбрасываем путь */
    double sx = is_left ? k->lx : k->rx, sy = is_left ? k->ly : k->ry;
    cairo_arc(cr, cx + sx * (r0 - 16), cy + sy * (r0 - 16), 8 * a->ui.ui_scale, 0, 2 * M_PI);
    cairo_rgba(cr, th->accent); cairo_fill(cr);
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

    /* Фон окна Breeze */
    double rr = 16.0 * a->ui.ui_scale;
    cairo_new_path(cr);
    cairo_arc(cr, lw - rr, rr, rr, -M_PI / 2, 0);
    cairo_arc(cr, lw - rr, lh - rr, rr, 0, M_PI / 2);
    cairo_arc(cr, rr, lh - rr, rr, M_PI / 2, M_PI);
    cairo_arc(cr, rr, rr, rr, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
    cairo_rgba(cr, th->bg); cairo_fill_preserve(cr);
    cairo_rgba(cr, th->border); cairo_set_line_width(cr, 1.0); cairo_stroke(cr);

    /* Масштабируемые кольца с безопасным отступом от краев */
    double R = 138.0 * a->ui.ui_scale;
    double r0 = 46.0 * a->ui.ui_scale;
    double cy = lh * 0.44;
    double margin = 50.0 * a->ui.ui_scale;
    double lx = R + margin;
    double rx = lw - (R + margin);

    draw_ring(cr, a, lx, cy, R, r0, 1);
    draw_ring(cr, a, rx, cy, R, r0, 0);

    /* Центральная панель статуса */
    char st[160]; char lang[16];
    snprintf(lang, sizeof lang, "%s", k->lay[k->cur].name);
    for (char *c = lang; *c; c++) *c = (char)toupper((unsigned char)*c);
    snprintf(st, sizeof st, "%s   %s   %s%s", lang,
             k->sym ? "123" : (k->shift ? "ABC" : "abc"),
             k->last[0] ? "\xe2\x86\x92 " : "", k->last);

    text_c(cr, a, st, lw / 2.0, cy - 24 * a->ui.ui_scale, 22, th->fg, 1);
    text_fit(cr, a, "LEFT stick: group\nRIGHT stick: flick, release to type",
             lw / 2.0, cy + 24 * a->ui.ui_scale, 300 * a->ui.ui_scale, 13, th->fg_dim, 0);

    /* Подсказки кнопок снизу */
    text_c(cr, a, "LT Shift   LB/B Backspace   RB/X/Y Space   RT/A/Start Enter",
           lw / 2.0, lh - 56 * a->ui.ui_scale, 14, th->fg_dim, 0);
    text_c(cr, a, "D-pad Arrows   L3 123/abc   R3 Layout   Select Hide   Select+Start Toggle",
           lw / 2.0, lh - 28 * a->ui.ui_scale, 14, th->fg_dim, 0);
}

/* ======================= pads / grab / hotplug ======================= */
static void pads_apply_grab(App *a)
{
    int grab = a->grab_pref && input_armed(a->visible, a->mapped);
    for (int i = 0; i < a->npads; i++) {
        pad_set_grab(&a->pads[i], grab);
    }
}

static int pad_index_by_path(App *a, const char *path)
{
    for (int i = 0; i < a->npads; i++) {
        if (a->pads[i].fd >= 0 && !strcmp(a->pads[i].path, path)) {
            return i;
        }
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
        /* При отключении пада сбрасываем состояние нажатий, чтобы не залипали модификаторы / Super */
        kb_reset_input(&a->kb);
        a->chord_a = a->chord_b = a->chord_fired = 0;
        a->pads[i] = a->pads[a->npads - 1];
        live[i] = live[a->npads - 1];
        a->npads--;
    }
    pads_apply_grab(a);
}

static void pads_close_all(App *a)
{
    for (int i = 0; i < a->npads; i++) {
        pad_close(&a->pads[i]);
    }
    a->npads = 0;
    kb_reset_input(&a->kb);
}

/* ======================= panel show / hide / redraw ======================= */
static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    App *a = data;
    wl_callback_destroy(cb);
    if (a->visible && !a->mapped) {
        a->mapped = 1;
        pads_apply_grab(a);
    }
}

static const struct wl_callback_listener frame_listener = { frame_done };

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

    struct wl_callback *cb = wl_surface_frame(a->surf);
    wl_callback_add_listener(cb, &frame_listener, a);

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
        for (int i = 0; i < 2; i++) { buf_destroy(&a->bufs[i]); }
        if (buf_create(a, &a->bufs[0], w, h) || buf_create(a, &a->bufs[1], w, h)) {
            fprintf(stderr, "radialkb: cannot allocate shm buffers\n"); return;
        }
        a->bw = w; a->bh = h;
    }
    a->visible = 1;
    kb_reset_input(&a->kb);
    app_redraw(a);
}

static void panel_hide(App *a)
{
    if (!a->visible && !a->surf) return;
    a->visible = 0;
    a->mapped = 0;                      /* Окно скрыто — снимаем активность */
    pads_apply_grab(a);                 /* Немедленно отпускаем EVIOCGRAB для игр */
    if (a->surf) {
        wl_surface_attach(a->surf, NULL, 0, 0);
        wl_surface_commit(a->surf);
    }
    for (int i = 0; i < 2; i++) a->bufs[i].busy = 0;
    kb_reset_input(&a->kb);
    if (a->dpy) wl_display_flush(a->dpy);
}

static int im_active(App *a) { return a->ctx != NULL; }

static void apply_visibility(App *a)
{
    int want = vis_want(im_active(a), a->npads > 0, a->plasma_mode,
                        a->invoke == INVOKE_MANUAL, a->user_shown, a->user_hidden);
    if (want && !a->visible) panel_show(a);
    else if (!want && a->visible) panel_hide(a);
    else pads_apply_grab(a);
}

static void toggle_panel(App *a)
{
    if (a->visible) {
        a->user_hidden = 1;
        a->user_shown = 0;
    } else if (im_active(a) && a->npads > 0) {
        a->user_hidden = 0;
        a->user_shown = 1;
    }
    apply_visibility(a);
}

/* ======================= input method protocol ======================= */
static void ctx_surrounding(void *d, struct zwp_input_method_context_v1 *c, const char *t, uint32_t cu, uint32_t an)
{ (void)d; (void)c; (void)t; (void)cu; (void)an; }
static void ctx_reset(void *d, struct zwp_input_method_context_v1 *c) { (void)d; (void)c; }
static void ctx_content_type(void *d, struct zwp_input_method_context_v1 *c, uint32_t h, uint32_t p)
{ (void)d; (void)c; (void)h; (void)p; }
static void ctx_invoke(void *d, struct zwp_input_method_context_v1 *c, uint32_t b, uint32_t i)
{ (void)d; (void)c; (void)b; (void)i; }
static void ctx_commit_state(void *d, struct zwp_input_method_context_v1 *c, uint32_t serial)
{ (void)c; ((App *)d)->serial = serial; }
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
    if (a->ctx == ctx) { zwp_input_method_context_v1_destroy(ctx); a->ctx = NULL; }
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

static int chord_key(App *a, int code, int down)
{
    if (code == a->toggle_a) a->chord_a = down;
    if (code == a->toggle_b) a->chord_b = down;
    if (a->chord_a && a->chord_b) {
        if (!a->chord_fired) { a->chord_fired = 1; toggle_panel(a); }
        return 1;
    }
    if (!a->chord_a && !a->chord_b) a->chord_fired = 0;
    return 0;
}

/* ======================= main ======================= */
int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "C.UTF-8");
    setlocale(LC_NUMERIC, "C"); /* Предотвращает проблемы с разделителем '.' в конфигах */
    A.scale = 1; A.grab_pref = 1; A.running = 1;
    A.ino_input = A.wd_input = A.ino_cfg = A.wd_cfg = -1;
    A.toggle_a = BTN_SELECT; A.toggle_b = BTN_START;
    A.invoke = INVOKE_AUTO;

    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(A.cfg_dir, sizeof A.cfg_dir, "%s", xdg);
    else if (home) snprintf(A.cfg_dir, sizeof A.cfg_dir, "%s/.config", home);
    else snprintf(A.cfg_dir, sizeof A.cfg_dir, ".");
    snprintf(A.kwinrc, sizeof A.kwinrc, "%s/kwinrc", A.cfg_dir);
    snprintf(A.kdeglobals, sizeof A.kdeglobals, "%s/kdeglobals", A.cfg_dir);

    char layout_path[PATH_MAX + 32] = "";
    snprintf(layout_path, sizeof layout_path, "%s/radialkb/layouts.conf", A.cfg_dir);
    radial_config_load(&A, layout_path, sizeof layout_path);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-grab")) A.grab_pref = 0;
        else if (!strcmp(argv[i], "--manual")) A.invoke = INVOKE_MANUAL;
        else if (!strcmp(argv[i], "--auto")) A.invoke = INVOKE_AUTO;
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc) {
            snprintf(layout_path, sizeof layout_path, "%s", argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--no-grab] [--auto|--manual] [--layout FILE]\n", argv[0]);
            return 2;
        }
    }
    if (getenv("RADIALKB_NO_GRAB")) A.grab_pref = 0;
    if (getenv("RADIALKB_MANUAL")) A.invoke = INVOKE_MANUAL;

    plasma_mode_load(&A);
    theme_load(&A);

    for (int i = 0; i < MAXPADS; i++) A.pads[i].fd = -1;
    A.kb.ud = &A; A.kb.text = cb_text; A.kb.key = cb_key;
    kb_load_layouts(&A.kb, layout_path[0] ? layout_path : NULL);
    kb_reset_input(&A.kb);

    watch_dir(&A.ino_input, &A.wd_input, "/dev/input",
              IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO);
    watch_dir(&A.ino_cfg, &A.wd_cfg, A.cfg_dir,
              IN_CREATE | IN_MODIFY | IN_MOVED_TO | IN_ATTRIB);

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
        if (wl_display_flush(A.dpy) < 0 && errno != EAGAIN) { wl_display_cancel_read(A.dpy); break; }

        struct pollfd fds[4 + MAXPADS]; int map[4 + MAXPADS]; int nf = 0;
        fds[nf] = (struct pollfd){ wfd, POLLIN, 0 }; map[nf++] = -1;
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
        int rep = input_armed(A.visible, A.mapped) ? kb_tick(&A.kb, now) : -1;
        if (rep >= 0) timeout = rep;
        if (A.visible && A.kb.dirty) {
            int since = (int)(now - A.last_draw);
            int wait = since >= 16 ? 0 : 16 - since;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }
        if (A.pads_dirty) {
            int wait = (int)(A.pads_rescan_at - now);
            if (wait < 0) wait = 0;
            if (timeout < 0 || wait < timeout) timeout = wait;
        }
        int r = poll(fds, (nfds_t)nf, timeout);
        if (r < 0 && errno != EINTR) { wl_display_cancel_read(A.dpy); break; }
        if (r > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_read_events(A.dpy) < 0) break;
        } else wl_display_cancel_read(A.dpy);
        if (wl_display_dispatch_pending(A.dpy) < 0) break;

        if (r > 0 && i_in >= 0 && (fds[i_in].revents & POLLIN)) {
            drain_inotify(A.ino_input);
            A.pads_dirty = 1;
            A.pads_rescan_at = now_ms() + 250;
        }
        if (r > 0 && i_cfg >= 0 && (fds[i_cfg].revents & POLLIN)) {
            drain_inotify(A.ino_cfg);
            plasma_mode_load(&A);
            theme_load(&A);
            radial_config_load(&A, layout_path, sizeof layout_path);
            A.kb.dirty = 1;
            apply_visibility(&A);
        }

        if (A.pads_dirty && now_ms() >= A.pads_rescan_at) {
            A.pads_dirty = 0;
            pads_resync(&A);
            apply_visibility(&A);
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
                            int down = ev[e].value == 1;
                            if (ev[e].value != 2 && chord_key(&A, ev[e].code, down))
                                continue;
                            /* Передаём ввод только если окно клавиатуры действительно на экране */
                            if (input_armed(A.visible, A.mapped))
                                kb_key(&A.kb, ev[e].code, ev[e].value);
                        } else if (input_armed(A.visible, A.mapped)) {
                            kb_abs(&A.kb, p, ev[e].code, ev[e].value);
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    pad_close(p);
                    kb_reset_input(&A.kb);
                    A.pads_dirty = 1;
                    A.pads_rescan_at = now_ms();
                }
            }
        }
        if (A.kb.hide) {
            A.kb.hide = 0;
            A.user_hidden = 1;
            A.user_shown = 0;
            apply_visibility(&A);
        }
        if (A.visible && A.kb.dirty && now_ms() - A.last_draw >= 16) app_redraw(&A);
    }
    pads_close_all(&A);
    if (A.ctx) zwp_input_method_context_v1_destroy(A.ctx);
    if (A.ino_input >= 0) close(A.ino_input);
    if (A.ino_cfg >= 0) close(A.ino_cfg);
    wl_display_disconnect(A.dpy);
    return 0;
}
