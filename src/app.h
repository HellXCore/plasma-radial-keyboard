#ifndef APP_H
#define APP_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <linux/input.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include "protocol.h"

#define NGROUPS 8
#define NSLOTS 8
#define MAXLANG 8
#define MAXPADS 4
#define TOKLEN 16
#define MAX_BINDS 6

#define DEFAULT_LW 1100
#define DEFAULT_LH 500

#define SYM_BACKSPACE 0xff08u
#define SYM_RETURN    0xff0du
#define SYM_LEFT      0xff51u
#define SYM_UP        0xff52u
#define SYM_RIGHT     0xff53u
#define SYM_DOWN      0xff54u
#define SYM_SPACE     0x0020u

#define STICK_REDRAW_EPS 0.03f

enum {
    PLASMA_VK_NEVER = 0,
    PLASMA_VK_TOUCH = 1,
    PLASMA_VK_ALWAYS = 2
};

enum {
    INVOKE_AUTO = 0,
    INVOKE_MANUAL = 1,
    INVOKE_SMART = 2
};

enum { L_LOWER = 0, L_UPPER = 1, L_SYM = 2 };
enum { ST_NONE = 0, ST_CHANGED, ST_RELEASED };
enum { CHORD_PASS = 0, CHORD_SWALLOW, CHORD_FIRE, CHORD_TAP };

enum {
    BG_SOLID = 0,
    BG_ACRYLIC = 1,
    BG_TRANSLUCENT = 2
};

static const int TOK2SLOT[NSLOTS] = {0, 2, 4, 6, 1, 3, 5, 7};

typedef struct { char t[NSLOTS][TOKLEN]; } Group;

typedef struct {
    char name[16];
    Group g[3][NGROUPS];
    int has[3];
} Layout;

typedef struct {
    int backspace[MAX_BINDS]; int n_backspace;
    int space[MAX_BINDS];     int n_space;
    int enter[MAX_BINDS];     int n_enter;
    int shift[MAX_BINDS];     int n_shift;
    int sym[MAX_BINDS];       int n_sym;
    int layout[MAX_BINDS];    int n_layout;
    int hide[MAX_BINDS];      int n_hide;
} KeyBinds;

typedef struct { float peak; int sel; int active; } Stick;

typedef struct {
    int fd;
    int grabbed;
    int grab_pending;
    int grab_warned;
    int mn[ABS_CNT], mx[ABS_CNT];
    char name[64];
    char path[64];
    uint16_t vendor, product;
} Pad;

typedef struct {
    int a, b;
    int held_a, held_b;
    int fired;
    int pending;
} Chord;

typedef struct Kb {
    Layout lay[MAXLANG]; int nlay, cur;
    int sym, shift, sh_abs, sh_btn, rt_abs, rt_btn, rt_prev;
    int group;
    Stick L, R;
    float lx, ly, rx, ry;
    int hat[2];
    int dirty, hide;
    float ref[4];
    uint32_t held; int held_n; int64_t rep_at;
    char last[TOKLEN];
    KeyBinds binds;
    void *ud;
    void (*text)(void *ud, const char *utf8);
    void (*key)(void *ud, uint32_t keysym);
} Kb;

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

typedef struct App {
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
    int blur_w, blur_h;
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
    struct wl_callback *frame_cb;
    int64_t map_deadline;
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
    int cli_invoke;
    char cli_layout[PATH_MAX + 256];
    int cfg_has_error;
    char cfg_err_msg[256];
    Theme th;
    UiConfig ui;
    PangoFontDescription *fd_cache[2];
    char fd_family[128];
} App;

static inline int64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* input.c */
void pad_close(Pad *p);
int  pad_open_at(Pad *p, const char *path, int grab);
int  pad_id_wanted(uint16_t vendor, uint16_t product, const char *name);
int  parse_layouts(const char *text, Layout *out, int max);
void binds_set_defaults(KeyBinds *b);
int  kb_load_layouts(Kb *k, const char *path);
void kb_reset_input(Kb *k);
int  kb_layer(const Kb *k);
int  kb_tick(Kb *k, int64_t now);
void kb_key(Kb *k, int code, int v);
void kb_abs(Kb *k, const Pad *p, int code, int v);
void chord_reset(Chord *c);
int  chord_event(Chord *c, int code, int value);
int  pad_any_key_pressed(int fd);
int  pad_set_grab_safe(Pad *p, int grab);
void pads_apply_grab(App *a);
void pads_resync(App *a);
void pads_close_all(App *a);
int  map_btn_name(const char *s);
int  parse_btn_list(const char *spec, int *arr, int *cnt);
int  parse_combo(const char *spec, int *a, int *b);
const char *btn_label(int code);
const char *bind_label(const int *arr, int n);
int  vis_want(int im_active, int has_pad, int plasma_mode, int invoke_mode, int user_shown, int user_hidden);
int  input_armed_strict(int panel_shown, int mapped, int im_active);

/* render.c */
void app_redraw(App *a);
void render_cleanup_fonts(App *a);

/* blur.c */
bool blur_handle_global(App *a, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver);
void update_surface_blur(App *a);
void blur_cleanup(App *a);

/* config.c */
void plasma_mode_load(App *a);
void theme_load(App *a);
void radial_config_load(App *a, char *layout_path, size_t n);
void ini_cache_flush(void);
int  cfg_events_relevant(App *a);
void setup_cfg_watches(App *a);
int  watch_dir(int *fd, int *wd, const char *dir, uint32_t mask);
void drain_inotify(int fd);

/* panel.c */
int  has_free_buf(const App *a);
void panel_mark_mapped(App *a);
void panel_show(App *a);
void panel_hide(App *a);
void show_panel(App *a);
void hide_panel(App *a);
void apply_visibility(App *a);
int  is_input_active(const App *a);
void cb_text(void *ud, const char *t);
void cb_key(void *ud, uint32_t sym);
void reset_ime_state(App *a);
void panel_cleanup(App *a);
extern const struct wl_callback_listener frame_listener;
extern const struct zwp_input_method_v1_listener im_listener;

#endif /* APP_H */