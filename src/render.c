#include "app.h"

extern const struct wl_callback_listener frame_listener;

static void cairo_rgba(cairo_t *cr, const double *c)
{
    cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]);
}

void render_cleanup_fonts(App *a)
{
    for (int i = 0; i < 2; i++) {
        if (a->fd_cache[i]) {
            pango_font_description_free(a->fd_cache[i]);
            a->fd_cache[i] = NULL;
        }
    }
    a->fd_family[0] = 0;
}

static PangoFontDescription *font_make(App *a, int bold, double px_size)
{
    if (strcmp(a->fd_family, a->ui.font_family) != 0) {
        render_cleanup_fonts(a);
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
    pango_font_description_free(fd);
    g_object_unref(l);
    cairo_new_path(cr);
}

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
    pango_font_description_free(fd);
    g_object_unref(l);
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
    *x = cx + r * sin(th);
    *y = cy - r * cos(th);
}

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

    pango_font_description_free(fdk);
    g_object_unref(lk);
    if (ld) {
        pango_font_description_free(fdd);
        g_object_unref(ld);
    }
    return total;
}

typedef struct { const char *key, *desc; } Badge;

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

        double r_text = r0 + (R - r0) * 0.54;
        double x, y;
        sector_pos(cx, cy, r_text, s, &x, &y);
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
                    int tw, th_h;
                    pango_layout_get_pixel_size(tl, &tw, &th_h);
                    pango_font_description_free(tfd);
                    g_object_unref(tl);

                    if (tw <= maxw * 0.95) {
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

    /* Stick hub recess */
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r0 - 6.0 * a->ui.ui_scale, 0, 2 * M_PI);
    cairo_rgba(cr, th->hub);
    cairo_fill_preserve(cr);
    cairo_rgba(cr, th->border);
    cairo_set_line_width(cr, 1.2 * a->ui.ui_scale);
    cairo_stroke(cr);

    /* Dead Zone */
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, (r0 - 6.0 * a->ui.ui_scale) * 0.35, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, th->fg[0], th->fg[1], th->fg[2], 0.08);
    cairo_set_line_width(cr, 1.0 * a->ui.ui_scale);
    cairo_stroke(cr);

    /* Stick */
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
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
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

    /* Central status bar */
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

void app_redraw(App *a)
{
    if (!a->visible) return;
    Buf *b = NULL;
    for (int i = 0; i < 2; i++) {
        if (!a->bufs[i].busy && a->bufs[i].buf) {
            b = &a->bufs[i];
            break;
        }
    }
    if (!b) return;

    cairo_t *cr = cairo_create(b->cs);
    cairo_scale(cr, a->scale, a->scale);
    draw(a, cr);
    cairo_destroy(cr);
    cairo_surface_flush(b->cs);

    if (a->buf_scale != a->scale) {
        wl_surface_set_buffer_scale(a->surf, a->scale);
        a->buf_scale = a->scale;
    }

    if (!a->mapped && !a->frame_cb) {
        a->frame_cb = wl_surface_frame(a->surf);
        wl_callback_add_listener(a->frame_cb, &frame_listener, a);
        a->map_deadline = now_ms() + 150;
    }

    wl_surface_attach(a->surf, b->buf, 0, 0);
    wl_surface_damage(a->surf, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(a->surf);
    b->busy = 1;
    a->kb.dirty = 0;
    a->last_draw = now_ms();
    wl_display_flush(a->dpy);
}