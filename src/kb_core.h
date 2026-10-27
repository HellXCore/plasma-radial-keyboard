/* kb_core.h - Wayland-independent logic of the dual-stick radial keyboard. */
#ifndef KB_CORE_H
#define KB_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define NGROUPS 8
#define NSLOTS 8
#define MAXLANG 8
#define MAXPADS 4
#define TOKLEN 16
#define MAX_BINDS 6

enum {
    PLASMA_VK_NEVER = 0,     /* "Never" */
    PLASMA_VK_TOUCH = 1,     /* "With Touch and Tablet" */
    PLASMA_VK_ALWAYS = 2     /* "With Touch, Tablet, and Mouse" */
};

enum {
    INVOKE_AUTO = 0,
    INVOKE_MANUAL = 1,
    INVOKE_SMART = 2
};

static const int TOK2SLOT[NSLOTS] = {0, 2, 4, 6, 1, 3, 5, 7};

enum { L_LOWER = 0, L_UPPER = 1, L_SYM = 2 };

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

#define DEFAULT_LAYOUTS \
"[en]\n" \
"lower: a b c d | e f g h | i j k l | m n o p | q r s t | u v w x | y z @ # | . , ? ! ' - : ;\n" \
"sym: 1 2 3 4 | 5 6 7 8 | 9 0 - + | ( ) [ ] { } < > | @ # $ % ^ & * ~ | = / \\ <bar> <us> ` \" ' | . , ? ! : ; \xe2\x80\xa6 \xe2\x82\xac | \xe2\x82\xac \xc2\xa3 \xc2\xa5 \xc2\xa7 \xc2\xa9 \xc2\xb0 \xe2\x80\xa2 \xe2\x84\x96\n" \
"[ru]\n" \
"lower: \xd0\xb0 \xd0\xb1 \xd0\xb2 \xd0\xb3 \xd0\xb4 | \xd0\xb5 \xd1\x91 \xd0\xb6 \xd0\xb7 | \xd0\xb8 \xd0\xb9 \xd0\xba \xd0\xbb \xd0\xbc | \xd0\xbd \xd0\xbe \xd0\xbf \xd1\x80 | \xd1\x81 \xd1\x82 \xd1\x83 \xd1\x84 \xd1\x85 | \xd1\x86 \xd1\x87 \xd1\x88 \xd1\x89 | \xd1\x8a \xd1\x8b \xd1\x8c \xd1\x8d \xd1\x8e \xd1\x8f | . , ? ! ' - : ;\n"

static inline int bind_contains(const int *arr, int n, int code)
{
    for (int i = 0; i < n; i++) {
        if (arr[i] == code) return 1;
    }
    return 0;
}

static inline void binds_set_defaults(KeyBinds *b)
{
    b->n_backspace = 2; b->backspace[0] = BTN_EAST; b->backspace[1] = BTN_TL;
    b->n_space = 3;     b->space[0] = BTN_NORTH; b->space[1] = BTN_WEST; b->space[2] = BTN_TR;
    b->n_enter = 2;     b->enter[0] = BTN_SOUTH; b->enter[1] = BTN_START;
    b->n_shift = 1;     b->shift[0] = BTN_TL2;
    b->n_sym = 1;       b->sym[0] = BTN_THUMBL;
    b->n_layout = 1;    b->layout[0] = BTN_THUMBR;
    b->n_hide = 1;      b->hide[0] = BTN_SELECT;
}

static int u8_dec(const char *s, uint32_t *cp)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c >> 5) == 6 && s[1]) { *cp = ((c & 0x1f) << 6) | (s[1] & 0x3f); return 2; }
    if ((c >> 4) == 14 && s[1] && s[2]) {
        *cp = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); return 3;
    }
    if ((c >> 3) == 30 && s[1] && s[2] && s[3]) {
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
        return 4;
    }
    *cp = c; return 1;
}

static int u8_enc(uint32_t cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xc0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xe0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        o[2] = (char)(0x80 | (cp & 0x3f)); return 3;
    }
    o[0] = (char)(0xf0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); o[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static void tok_upper(const char *in, char *out)
{
    uint32_t cp; int n = u8_dec(in, &cp);
    if (in[0] && in[n] == 0) {
        int m = u8_enc((uint32_t)towupper((wint_t)cp), out);
        out[m] = 0;
    } else {
        snprintf(out, TOKLEN, "%s", in);
    }
}

static void store_token(Group *g, int ti, const char *p, size_t n)
{
    if (ti >= NSLOTS) return;
    char *dst = g->t[TOK2SLOT[ti]];
    if (n == 1 && *p == '_') { dst[0] = 0; return; }
    if (n == 5 && !strncmp(p, "<bar>", 5)) { strcpy(dst, "|"); return; }
    if (n == 4 && !strncmp(p, "<us>", 4)) { strcpy(dst, "_"); return; }
    if (n >= TOKLEN) n = TOKLEN - 1;
    memcpy(dst, p, n); dst[n] = 0;
}

static void parse_layer(const char *s, Group *g)
{
    memset(g, 0, sizeof(Group) * NGROUPS);
    int gi = 0, ti = 0;
    const char *p = s;
    while (*p && gi < NGROUPS) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        if (*p == '|') { gi++; ti = 0; p++; continue; }
        const char *q = p;
        while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '|') q++;
        store_token(&g[gi], ti++, p, (size_t)(q - p));
        p = q;
    }
}

static int parse_layouts(const char *text, Layout *out, int max)
{
    int n = 0; Layout *cur = NULL;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        const char *next = e ? e + 1 : p + strlen(p);
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[2048];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = 0;
        p = next;

        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        if (*s == '[') {
            char *c = strchr(s, ']');
            if (!c || n >= max) { cur = NULL; continue; }
            cur = &out[n++]; memset(cur, 0, sizeof *cur);
            size_t l = (size_t)(c - s - 1); if (l > 15) l = 15;
            memcpy(cur->name, s + 1, l);
            continue;
        }
        if (!cur) continue;
        int layer = -1; const char *body = NULL;
        if (!strncmp(s, "lower:", 6)) { layer = L_LOWER; body = s + 6; }
        else if (!strncmp(s, "upper:", 6)) { layer = L_UPPER; body = s + 6; }
        else if (!strncmp(s, "sym:", 4)) { layer = L_SYM; body = s + 4; }
        if (layer < 0) continue;
        parse_layer(body, cur->g[layer]); cur->has[layer] = 1;
    }
    for (int i = 0; i < n; i++) {
        Layout *L = &out[i];
        if (!L->has[L_UPPER] && L->has[L_LOWER]) {
            for (int g = 0; g < NGROUPS; g++)
                for (int s = 0; s < NSLOTS; s++)
                    tok_upper(L->g[L_LOWER][g].t[s], L->g[L_UPPER][g].t[s]);
            L->has[L_UPPER] = 1;
        }
        if (!L->has[L_SYM] && i > 0 && out[0].has[L_SYM]) {
            memcpy(L->g[L_SYM], out[0].g[L_SYM], sizeof L->g[L_SYM]);
            L->has[L_SYM] = 1;
        }
    }
    return n;
}

static inline int64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

enum { ST_NONE = 0, ST_CHANGED, ST_RELEASED };
typedef struct { float peak; int sel; int active; } Stick;

static inline int sector_of(float x, float y)
{
    float a = atan2f(x, -y);
    int s = (int)lrintf(a / (float)(M_PI / 4.0));
    return ((s % 8) + 8) % 8;
}

static int stick_update(Stick *s, float x, float y)
{
    float m = hypotf(x, y);
    if (m >= 0.55f) {
        if (!s->active) { s->active = 1; s->peak = 0; }
        if (m >= s->peak * 0.9f) {
            if (m > s->peak) s->peak = m;
            int sec = sector_of(x, y);
            if (sec != s->sel) { s->sel = sec; return ST_CHANGED; }
        }
        return ST_NONE;
    }
    if (m < 0.30f && s->active) { s->active = 0; s->peak = 0; return ST_RELEASED; }
    return ST_NONE;
}

typedef struct {
    int fd;
    int grabbed;
    int grab_pending;
    int mn[ABS_CNT], mx[ABS_CNT];
    char name[64];
    char path[64];
    uint16_t vendor, product;
} Pad;

static int pad_name_wanted(const char *n)
{
    if (!n || !*n) return 0;
    char l[96]; size_t i;
    for (i = 0; n[i] && i + 1 < sizeof l; i++) {
        unsigned char c = (unsigned char)n[i];
        l[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    l[i] = 0;
    if (strstr(l, "touchpad") || strstr(l, "trackpad") || strstr(l, "keyboard")
        || strstr(l, "mouse") || strstr(l, "accelerometer") || strstr(l, "motion sensors"))
        return 0;
    if (strstr(l, "dualshock") || strstr(l, "dualsense") || strstr(l, "dual shock")
        || strstr(l, "wireless controller") || strstr(l, "sony interactive"))
        return 1;
    if (strstr(l, "xbox") || strstr(l, "x-box") || strstr(l, "xinput")
        || strstr(l, "gamepad") || strstr(l, "controller"))
        return 1;
    return 0;
}

static int pad_id_wanted(uint16_t vendor, uint16_t product, const char *name)
{
    (void)product;
    if (vendor == 0x054c) return pad_name_wanted(name);
    if (vendor == 0x045e) return 1;
    if (vendor == 0x1532 && name && strstr(name, "Razer")) return 1;
    return pad_name_wanted(name);
}

static int pad_any_key_pressed(int fd)
{
    if (fd < 0) return 0;
    unsigned long key_states[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    if (ioctl(fd, EVIOCGKEY(sizeof key_states), key_states) < 0) return 0;
    for (size_t i = 0; i < sizeof(key_states) / sizeof(key_states[0]); i++) {
        if (key_states[i] != 0) return 1;
    }
    return 0;
}

static int pad_set_grab_safe(Pad *p, int grab)
{
    if (p->fd < 0) return 0;
    if (grab) {
        if (p->grabbed) { p->grab_pending = 0; return 1; }
        if (pad_any_key_pressed(p->fd)) {
            p->grab_pending = 1;
            return 0;
        }
        if (ioctl(p->fd, EVIOCGRAB, (void *)1) == 0) {
            p->grabbed = 1;
            p->grab_pending = 0;
            return 1;
        }
    } else {
        p->grab_pending = 0;
        if (p->grabbed) {
            ioctl(p->fd, EVIOCGRAB, (void *)0);
            p->grabbed = 0;
            return 1;
        }
    }
    return 0;
}

/* Логика видимости клавиатуры */
static inline int vis_want(int im_active, int has_pad, int plasma_mode,
                           int invoke_mode, int user_shown, int user_hidden)
{
    if (!im_active || !has_pad || user_hidden) return 0;
    if (invoke_mode == INVOKE_MANUAL) return user_shown;
    if (invoke_mode == INVOKE_SMART) return 1;
    if (plasma_mode != PLASMA_VK_ALWAYS) return user_shown;
    return 1;
}

static inline int input_armed_strict(int panel_shown, int mapped, int im_active)
{
    return panel_shown && mapped && im_active;
}

static inline int tbit(const unsigned long *a, int b)
{
    return (a[b / (8 * sizeof(long))] >> (b % (8 * sizeof(long)))) & 1;
}

static int pad_has_stick_layout(const unsigned long *ab, const unsigned long *kb)
{
    return tbit(ab, ABS_X) && tbit(ab, ABS_Y) && tbit(ab, ABS_RX) &&
           tbit(ab, ABS_RY) && (tbit(kb, BTN_SOUTH) || tbit(kb, BTN_GAMEPAD));
}

static int pad_open_at(Pad *p, const char *path, int grab)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    unsigned long ab[ABS_MAX / (8 * sizeof(long)) + 1] = {0};
    unsigned long kb[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof ab), ab) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kb), kb) < 0 ||
        !pad_has_stick_layout(ab, kb)) {
        close(fd); return -1;
    }
    struct input_id id; memset(&id, 0, sizeof id);
    ioctl(fd, EVIOCGID, &id);
    memset(p, 0, sizeof *p);
    p->fd = fd; p->vendor = id.vendor; p->product = id.product;
    ioctl(fd, EVIOCGNAME(sizeof p->name), p->name);
    snprintf(p->path, sizeof p->path, "%s", path);
    if (!pad_id_wanted(p->vendor, p->product, p->name)) {
        close(fd); p->fd = -1; return -1;
    }
    const int codes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    for (unsigned c = 0; c < sizeof codes / sizeof *codes; c++) {
        struct input_absinfo ai;
        if (tbit(ab, codes[c]) && ioctl(fd, EVIOCGABS(codes[c]), &ai) == 0) {
            p->mn[codes[c]] = ai.minimum; p->mx[codes[c]] = ai.maximum;
        }
    }
    if (grab) pad_set_grab_safe(p, 1);
    return 0;
}

static void pad_close(Pad *p)
{
    if (p->fd < 0) return;
    if (p->grabbed) ioctl(p->fd, EVIOCGRAB, (void *)0);
    close(p->fd); p->fd = -1; p->grabbed = 0; p->grab_pending = 0;
}

static float pad_norm(const Pad *p, int code, int v)
{
    int mn = p->mn[code], mx = p->mx[code];
    if (mx <= mn) return 0;
    float f = 2.0f * (float)(v - mn) / (float)(mx - mn) - 1.0f;
    return f < -1 ? -1 : (f > 1 ? 1 : f);
}

static float pad_norm01(const Pad *p, int code, int v)
{
    int mn = p->mn[code], mx = p->mx[code];
    if (mx <= mn) return 0;
    float f = (float)(v - mn) / (float)(mx - mn);
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

#define SYM_BACKSPACE 0xff08u
#define SYM_RETURN    0xff0du
#define SYM_LEFT      0xff51u
#define SYM_UP        0xff52u
#define SYM_RIGHT     0xff53u
#define SYM_DOWN      0xff54u
#define SYM_SPACE     0x0020u

typedef struct Kb {
    Layout lay[MAXLANG]; int nlay, cur;
    int sym, shift, sh_abs, sh_btn, rt_abs, rt_btn, rt_prev;
    int group;
    Stick L, R;
    float lx, ly, rx, ry;
    int hat[2];
    int dirty, hide;
    uint32_t held; int64_t rep_at;
    char last[TOKLEN];
    KeyBinds binds;
    void *ud;
    void (*text)(void *ud, const char *utf8);
    void (*key)(void *ud, uint32_t keysym);
} Kb;

static void kb_reset_input(Kb *k)
{
    k->L = (Stick){0, -1, 0}; k->R = (Stick){0, -1, 0};
    k->lx = k->ly = k->rx = k->ry = 0;
    k->sh_abs = k->sh_btn = k->rt_abs = k->rt_btn = k->rt_prev = k->shift = 0;
    k->hat[0] = k->hat[1] = 0; k->held = 0; k->hide = 0; k->dirty = 1;
}

static inline int kb_layer(const Kb *k) { return k->sym ? L_SYM : (k->shift ? L_UPPER : L_LOWER); }

static void kb_commit_slot(Kb *k, int slot)
{
    const char *t = k->lay[k->cur].g[kb_layer(k)][k->group].t[slot];
    if (!t[0]) return;
    snprintf(k->last, sizeof k->last, "%s", t);
    k->text(k->ud, t);
    k->dirty = 1;
}

static inline void kb_tap(Kb *k, uint32_t sym) { k->key(k->ud, sym); }

static void kb_hold(Kb *k, uint32_t sym, int down)
{
    if (down) { k->key(k->ud, sym); k->held = sym; k->rep_at = now_ms() + 400; }
    else if (k->held == sym) k->held = 0;
}

static int kb_tick(Kb *k, int64_t now)
{
    if (!k->held) return -1;
    if (now >= k->rep_at) { k->key(k->ud, k->held); k->rep_at = now + 45; }
    int64_t d = k->rep_at - now;
    return d < 0 ? 0 : (int)d;
}

static void kb_rt_update(Kb *k)
{
    int rt = k->rt_abs || k->rt_btn;
    if (rt && !k->rt_prev) kb_tap(k, SYM_RETURN);
    k->rt_prev = rt;
}

static void kb_hat(Kb *k, int axis, int v)
{
    int sgn = v < 0 ? -1 : (v > 0 ? 1 : 0);
    if (k->hat[axis] == sgn) return;
    uint32_t neg = axis ? SYM_UP : SYM_LEFT, pos = axis ? SYM_DOWN : SYM_RIGHT;
    if (k->hat[axis]) kb_hold(k, k->hat[axis] < 0 ? neg : pos, 0);
    k->hat[axis] = sgn;
    if (sgn) kb_hold(k, sgn < 0 ? neg : pos, 1);
}

static void kb_key(Kb *k, int code, int v)
{
    int down = (v == 1);
    if (v == 2) return;

    if (bind_contains(k->binds.backspace, k->binds.n_backspace, code)) {
        kb_hold(k, SYM_BACKSPACE, down);
    } else if (bind_contains(k->binds.space, k->binds.n_space, code)) {
        kb_hold(k, SYM_SPACE, down);
    } else if (bind_contains(k->binds.enter, k->binds.n_enter, code)) {
        if (down) kb_tap(k, SYM_RETURN);
    } else if (bind_contains(k->binds.shift, k->binds.n_shift, code)) {
        k->sh_btn = down; k->shift = k->sh_abs || k->sh_btn;
    } else if (bind_contains(k->binds.sym, k->binds.n_sym, code)) {
        if (down) k->sym = !k->sym;
    } else if (bind_contains(k->binds.layout, k->binds.n_layout, code)) {
        if (down && k->nlay > 0) k->cur = (k->cur + 1) % k->nlay;
    } else if (bind_contains(k->binds.hide, k->binds.n_hide, code)) {
        if (down) k->hide = 1;
    } else {
        switch (code) {
            case BTN_TR2: k->rt_btn = down; kb_rt_update(k); break;
            case BTN_DPAD_LEFT:  kb_hold(k, SYM_LEFT, down); break;
            case BTN_DPAD_RIGHT: kb_hold(k, SYM_RIGHT, down); break;
            case BTN_DPAD_UP:    kb_hold(k, SYM_UP, down); break;
            case BTN_DPAD_DOWN:  kb_hold(k, SYM_DOWN, down); break;
            case BTN_MODE: break;
            default: return;
        }
    }
    k->dirty = 1;
}

static void kb_abs(Kb *k, const Pad *p, int code, int v)
{
    switch (code) {
        case ABS_X: case ABS_Y:
            if (code == ABS_X) k->lx = pad_norm(p, code, v); else k->ly = pad_norm(p, code, v);
            if (stick_update(&k->L, k->lx, k->ly) == ST_CHANGED) k->group = k->L.sel;
            break;
        case ABS_RX: case ABS_RY:
            if (code == ABS_RX) k->rx = pad_norm(p, code, v); else k->ry = pad_norm(p, code, v);
            if (stick_update(&k->R, k->rx, k->ry) == ST_RELEASED && k->R.sel >= 0)
                kb_commit_slot(k, k->R.sel);
            break;
        case ABS_Z:
            k->sh_abs = pad_norm01(p, code, v) > 0.5f; k->shift = k->sh_abs || k->sh_btn; break;
        case ABS_RZ:
            k->rt_abs = pad_norm01(p, code, v) > 0.5f; kb_rt_update(k); break;
        case ABS_HAT0X: kb_hat(k, 0, v); break;
        case ABS_HAT0Y: kb_hat(k, 1, v); break;
        default: return;
    }
    k->dirty = 1;
}

static int kb_load_layouts(Kb *k, const char *path)
{
    char *buf = NULL;
    if (path) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < (1 << 20) && (buf = malloc((size_t)sz + 1))) {
                size_t r = fread(buf, 1, (size_t)sz, f); buf[r] = 0;
            }
            fclose(f);
        }
    }
    k->nlay = buf ? parse_layouts(buf, k->lay, MAXLANG) : 0;
    free(buf);
    if (k->nlay == 0) k->nlay = parse_layouts(DEFAULT_LAYOUTS, k->lay, MAXLANG);
    k->cur = 0;
    return k->nlay;
}

#endif