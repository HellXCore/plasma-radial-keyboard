#include "app.h"
#include <locale.h>

static App A;

static void on_signal(int s) { (void)s; A.running = 0; }

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
    else
        blur_handle_global(a, r, name, iface, ver);
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

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
            wl_events |= POLLOUT;
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
                            if (cr == CHORD_TAP && armed) {
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
    render_cleanup_fonts(&A);
    blur_cleanup(&A);
    panel_cleanup(&A);

    wl_registry_destroy(reg);
    wl_display_flush(A.dpy);
    ini_cache_flush();
    if (A.ino_input >= 0) close(A.ino_input);
    if (A.ino_cfg >= 0) close(A.ino_cfg);
    wl_display_disconnect(A.dpy);
    return 0;
}