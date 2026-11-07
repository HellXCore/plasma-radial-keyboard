#include "app.h"

void cb_text(void *ud, const char *t)
{
    App *a = ud;
    if (!a->ctx) return;
    zwp_input_method_context_v1_commit_string(a->ctx, a->serial, t);
    wl_display_flush(a->dpy);
}

void cb_key(void *ud, uint32_t sym)
{
    App *a = ud;
    if (!a->ctx) return;
    if (sym == SYM_SPACE) { cb_text(ud, " "); return; }
    uint32_t t = (uint32_t)now_ms();
    zwp_input_method_context_v1_keysym(a->ctx, a->serial, t, sym, WL_KEYBOARD_KEY_STATE_PRESSED, 0);
    zwp_input_method_context_v1_keysym(a->ctx, a->serial, t, sym, WL_KEYBOARD_KEY_STATE_RELEASED, 0);
    wl_display_flush(a->dpy);
}

void reset_ime_state(App *a)
{
    if (a->ctx) {
        zwp_input_method_context_v1_modifiers(a->ctx, a->serial, 0, 0, 0, 0);
        wl_display_flush(a->dpy);
    }
}

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

int has_free_buf(const App *a)
{
    for (int i = 0; i < 2; i++)
        if (a->bufs[i].buf && !a->bufs[i].busy) return 1;
    return 0;
}

void panel_mark_mapped(App *a)
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

const struct wl_callback_listener frame_listener = { frame_done };

void panel_show(App *a)
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

void panel_hide(App *a)
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

int is_input_active(const App *a)
{
    if (!a->ctx) return 0;
    if (a->user_shown) return 1;
    return a->field_active;
}

void apply_visibility(App *a)
{
    int im_active = is_input_active(a);
    int want = vis_want(im_active, a->npads > 0, a->plasma_mode,
                        a->invoke, a->user_shown, a->user_hidden);
    if (want && !a->visible) panel_show(a);
    else if (!want && a->visible) panel_hide(a);
    else pads_apply_grab(a);
}

void show_panel(App *a)
{
    if (a->ctx != NULL && a->npads > 0) {
        a->user_hidden = 0;
        a->user_shown = 1;
        apply_visibility(a);
    }
}

void hide_panel(App *a)
{
    a->user_hidden = 1;
    a->user_shown = 0;
    apply_visibility(a);
}

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

const struct zwp_input_method_v1_listener im_listener = { im_activate, im_deactivate };

void panel_cleanup(App *a)
{
    if (a->ctx) zwp_input_method_context_v1_destroy(a->ctx);
    if (a->psurf) zwp_input_panel_surface_v1_destroy(a->psurf);
    if (a->surf) wl_surface_destroy(a->surf);
    for (int i = 0; i < 2; i++) buf_destroy(&a->bufs[i]);
}