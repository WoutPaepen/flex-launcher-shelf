#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include "overlay.h"
#include "render.h"
#include "raster.h"
#include "text.h"
#include "bookart.h"

#define MAX_TRAIL 16
#define MAX_TICKS 512
#define TOAST_TIME 6.f

enum { EASE_OUT, EASE_IO };

typedef struct {
    float v, from, to, t, dur;
    int ease;
} Tween;

typedef struct {
    Tex tex, shadow;
    int w, h;        // size of the text mask
    int ascent;      // mask top to baseline
    int pad;         // shadow padding on every side
    char key[512];
} Label;

typedef struct {
    float r, g, b, a;
} Color;

static const Color BONE = { 239 / 255.f, 234 / 255.f, 224 / 255.f, 1.f };
static const Color BONE_DIM = { 239 / 255.f, 234 / 255.f, 224 / 255.f, 0.66f };
static const Color BONE_FAINT = { 239 / 255.f, 234 / 255.f, 224 / 255.f, 0.28f };
static const Color LED = { 1.f, 207 / 255.f, 138 / 255.f, 1.f };

static const char *SVG_MOVE =
    "<svg viewBox=\"0 0 16 16\" width=\"16\" height=\"16\" fill=\"none\" stroke=\"#efeae0\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M6 3 1.5 8 6 13M10 3l4.5 5L10 13\"/></svg>";
static const char *SVG_BACK =
    "<svg viewBox=\"0 0 16 16\" width=\"16\" height=\"16\" fill=\"none\" stroke=\"#efeae0\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M6 4 2.5 7.5 6 11\"/><path d=\"M3 7.5h6.5a4 4 0 0 1 0 8H8\"/></svg>";

static struct {
    char font_medium[1100], font_semibold[1100], font_mono[1100];
    int W, H, per_page;
    float u;
    bool scrim;

    char trail[MAX_TRAIL][256];
    int trail_count;
    Label trail_labels[MAX_TRAIL];

    int rail_count, rail_focus, rail_per_page;

    bool clock_enabled, show_date;
    Label time_label, period_label, date_label;

    Label hint_move, hint_open, hint_back, key_ok;
    Tex svg_move, svg_back;
    int svg_size;

    Label status_label;
    char status_text[256];
    Tween status_op, status_y;
    float pulse;

    Tex toast_tex;
    int toast_w, toast_h;
    Tween toast_op, toast_y;
    float toast_timer;
    char toast_title[256], toast_body[1024], toast_code[1024];

    Tween blank_op;
    Tex blank_sticker;
    int sticker_size;
    char blank_icon[1024];
    Label blank_title, blank_sub;
    char blank_title_text[256], blank_sub_text[512];
    bool blank_bar, blank_on, blank_page;
    float hud;
    float bar_phase;

    Tween dim_op;
    Tween loading_op;
    Label loading_label;
    Tween scene_op;
} O;

// ---------- easing ----------

// cubic-bezier(x1, y1, x2, y2) as CSS evaluates it
static float bezier(float x1, float y1, float x2, float y2, float x)
{
    float t = x;
    for (int i = 0; i < 8; i++) {
        float it = 1.f - t;
        float fx = 3.f * it * it * t * x1 + 3.f * it * t * t * x2 + t * t * t - x;
        float dx = 3.f * it * it * x1 + 6.f * it * t * (x2 - x1) + 3.f * t * t * (1.f - x2);
        if (fabsf(fx) < 1e-5f || fabsf(dx) < 1e-6f)
            break;
        t -= fx / dx;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    }
    float it = 1.f - t;
    return 3.f * it * it * t * y1 + 3.f * it * t * t * y2 + t * t * t;
}

static float ease(int kind, float p)
{
    if (kind == EASE_IO)
        return bezier(0.65f, 0.f, 0.35f, 1.f, p);
    return bezier(0.16f, 1.f, 0.3f, 1.f, p);
}

static void tw_set(Tween *w, float to, float dur, int kind)
{
    if (w->to == to && (w->v == to || w->t < w->dur))
        return;
    w->from = w->v;
    w->to = to;
    w->t = 0.f;
    w->dur = dur;
    w->ease = kind;
}

static void tw_snap(Tween *w, float v)
{
    w->v = w->from = w->to = v;
    w->t = w->dur = 0.f;
}

static bool tw_step(Tween *w, float dt)
{
    if (w->v == w->to)
        return false;
    w->t += dt;
    float p = w->dur > 0.f ? w->t / w->dur : 1.f;
    if (p >= 1.f) {
        w->v = w->to;
        return true;
    }
    w->v = w->from + (w->to - w->from) * ease(w->ease, p);
    return true;
}

// ---------- text ----------

static float fs(float k, float floor_px)
{
    return fmaxf(O.u * k, floor_px);
}

static TTF_Font *font(const char *path, float size)
{
    return font_get(path, (int) lroundf(size));
}

// CSS "normal" line height and where the baseline sits in it
static void line_box(TTF_Font *f, float *height, float *baseline)
{
    float asc = (float) TTF_FontAscent(f), desc = (float) -TTF_FontDescent(f);
    float skip = (float) TTF_FontLineSkip(f);
    if (skip < asc + desc)
        skip = asc + desc;
    *height = skip;
    *baseline = (skip - asc - desc) / 2.f + asc;
}

// Baseline inside a line box of a set height (line-height: 1)
static float baseline_in(TTF_Font *f, float height)
{
    float asc = (float) TTF_FontAscent(f), desc = (float) -TTF_FontDescent(f);
    return (height - asc - desc) / 2.f + asc;
}

static Canvas *mask_canvas(const TextMask *m, int pad)
{
    Canvas *c = cv_new(m->w + pad * 2, m->h + pad * 2);
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            uint8_t a = m->a[y * m->w + x];
            uint8_t *p = c->px + ((size_t) (y + pad) * (size_t) c->w + (size_t) (x + pad)) * 4;
            p[0] = p[1] = p[2] = p[3] = a;
        }
    }
    return c;
}

static void label_free(Label *l)
{
    tex_free(&l->tex);
    tex_free(&l->shadow);
    l->w = l->h = 0;
    l->key[0] = '\0';
}

// Render a line of text once; it is redrawn only when the text or size changes
static void label_set(Label *l, const char *path, float size, float tracking_em, const char *text, float shadow_blur)
{
    char key[512];
    snprintf(key, sizeof(key), "%s|%.2f|%.3f|%.1f|%s", path, size, tracking_em, shadow_blur, text ? text : "");
    if (!strcmp(key, l->key))
        return;
    label_free(l);
    snprintf(l->key, sizeof(l->key), "%s", key);
    if (text == NULL || *text == '\0')
        return;
    TTF_Font *f = font(path, size);
    TextMask m = text_render(f, text, tracking_em * size);
    if (m.a == NULL)
        return;
    l->w = m.w;
    l->h = m.h;
    l->ascent = m.ascent;
    Canvas *c = mask_canvas(&m, 0);
    l->tex = tex_from_canvas(c, false, false, false);
    cv_free(c);
    if (shadow_blur > 0.f) {
        // text-shadow: 0 1px <blur> rgba(0, 0, 0, 0.5); the blur radius is twice the sigma
        float sigma = shadow_blur / 2.f;
        l->pad = (int) ceilf(sigma * 3.f);
        Canvas *s = mask_canvas(&m, l->pad);
        cv_blur(s, sigma);
        l->shadow = tex_from_canvas(s, false, false, false);
        cv_free(s);
    }
    text_free(&m);
}

static void draw_label(const Label *l, float x, float baseline, Color c, float alpha)
{
    if (l->tex.id == 0)
        return;
    float y = roundf(baseline) - (float) l->ascent;
    x = roundf(x);
    if (l->shadow.id) {
        float sc[4] = { 0.f, 0.f, 0.f, 0.5f * alpha * c.a };
        render_overlay_tex(l->shadow.id, x - (float) l->pad, y + 1.f - (float) l->pad,
            (float) (l->w + l->pad * 2), (float) (l->h + l->pad * 2), sc, true);
    }
    float col[4] = { c.r, c.g, c.b, c.a * alpha };
    render_overlay_tex(l->tex.id, x, y, (float) l->w, (float) l->h, col, true);
}

static void rect(float x, float y, float w, float h, float radius, Color c, float alpha)
{
    float col[4] = { c.r, c.g, c.b, c.a * alpha };
    render_overlay_rect(x, y, w, h, radius, col, 0.f, NULL);
}

// box-shadow: 0 <dy> <blur> <color>
static void box_shadow(float x, float y, float w, float h, float radius, float dy, float blur, float alpha)
{
    float col[4] = { 0.f, 0.f, 0.f, alpha };
    render_overlay_rect(x - blur, y + dy - blur, w + blur * 2.f, h + blur * 2.f, radius + blur, col, -blur, NULL);
}

static Tex svg_icon(const char *svg, int size)
{
    Tex t = { 0, 0, 0 };
    char *buf = strdup(svg);
    NSVGimage *img = nsvgParse(buf, "px", 96.f);
    free(buf);
    if (img == NULL)
        return t;
    NSVGrasterizer *r = nsvgCreateRasterizer();
    Canvas *c = cv_new(size, size);
    nsvgRasterize(r, img, 0, 0, (float) size / 16.f, c->px, size, size, size * 4);
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
    t = tex_from_canvas(c, false, false, false);
    cv_free(c);
    return t;
}

// ---------- setup ----------

bool overlay_init(const char *assets)
{
    memset(&O, 0, sizeof(O));
    const char *sep = assets[strlen(assets) - 1] == '/' ? "" : "/";
    snprintf(O.font_medium, sizeof(O.font_medium), "%s%sfonts/SchibstedGrotesk-Medium.ttf", assets, sep);
    snprintf(O.font_semibold, sizeof(O.font_semibold), "%s%sfonts/SchibstedGrotesk-SemiBold.ttf", assets, sep);
    snprintf(O.font_mono, sizeof(O.font_mono), "%s%sfonts/JetBrainsMono-Regular.ttf", assets, sep);
    if (font_get(O.font_medium, 20) == NULL || font_get(O.font_semibold, 20) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the interface fonts from %s", assets);
        return false;
    }
    if (font_get(O.font_mono, 20) == NULL)
        snprintf(O.font_mono, sizeof(O.font_mono), "%s", O.font_semibold);
    O.scrim = true;
    O.hud = 1.f;
    O.clock_enabled = true;
    O.show_date = true;
    tw_snap(&O.status_y, -1.f);
    tw_snap(&O.toast_y, -1.f);
    tw_snap(&O.scene_op, 0.f);
    return true;
}

void overlay_quit(void)
{
    for (int i = 0; i < MAX_TRAIL; i++)
        label_free(&O.trail_labels[i]);
    Label *all[] = { &O.time_label, &O.period_label, &O.date_label, &O.hint_move, &O.hint_open, &O.hint_back,
        &O.key_ok, &O.status_label, &O.blank_title, &O.blank_sub, &O.loading_label };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        label_free(all[i]);
    tex_free(&O.svg_move);
    tex_free(&O.svg_back);
    tex_free(&O.toast_tex);
    tex_free(&O.blank_sticker);
}

static void build_toast(void);
static void build_sticker(void);

void overlay_resize(int win_w, int win_h, int per_page)
{
    O.W = win_w;
    O.H = win_h;
    O.per_page = per_page;
    float u = fminf((float) win_w / (3.f + (float) (per_page - 1) * 1.14f + 1.24f), (float) win_h / 6.4f);
    if (fabsf(u - O.u) < 0.01f)
        return;
    O.u = u;
    int svg = (int) lroundf(u * 0.13f);
    if (svg != O.svg_size) {
        tex_free(&O.svg_move);
        tex_free(&O.svg_back);
        O.svg_move = svg_icon(SVG_MOVE, svg);
        O.svg_back = svg_icon(SVG_BACK, svg);
        O.svg_size = svg;
    }
    if (O.toast_title[0] != '\0')
        build_toast();
    if (O.blank_icon[0] != '\0')
        build_sticker();
}

void overlay_set_trail(const char **menus, int count)
{
    if (count > MAX_TRAIL)
        count = MAX_TRAIL;
    O.trail_count = count;
    for (int i = 0; i < count; i++)
        snprintf(O.trail[i], sizeof(O.trail[i]), "%s", menus[i]);
}

void overlay_set_rail(int count, int focus, int per_page)
{
    O.rail_count = count > MAX_TICKS ? MAX_TICKS : count;
    O.rail_focus = focus;
    O.rail_per_page = per_page;
}

void overlay_set_clock(bool enabled, bool show_date)
{
    O.clock_enabled = enabled;
    O.show_date = show_date;
}

void overlay_set_scrim(bool enabled)
{
    O.scrim = enabled;
}

// ---------- status, toast, blank ----------

void overlay_status(const char *text)
{
    if (text != NULL && *text != '\0') {
        snprintf(O.status_text, sizeof(O.status_text), "%s", text);
        tw_set(&O.status_op, 1.f, 0.3f, EASE_OUT);
        tw_set(&O.status_y, 0.f, 0.42f, EASE_OUT);
    }
    else {
        tw_set(&O.status_op, 0.f, 0.3f, EASE_OUT);
        tw_set(&O.status_y, -1.f, 0.42f, EASE_OUT);
    }
}

typedef struct {
    const char *path;
    float size;
    Color color;
} Style;

typedef struct {
    char text[256];
    int style;
    float w;
    bool space_before;
} Word;

// Lay out runs of words with greedy wrapping, painting them into the canvas when it is given
static float flow(Canvas *c, Word *words, int n, const Style *styles, float max_w, float x0, float *y, float line_h, float *widest)
{
    TTF_Font *base = font(styles[words[0].style].path, styles[words[0].style].size);
    float x = 0.f;
    int line_start = 0;
    for (int i = 0; i <= n; i++) {
        float space = 0.f;
        if (i < n && words[i].space_before && i > line_start) {
            TTF_Font *f = font(styles[words[i].style].path, styles[words[i].style].size);
            space = (float) text_width(f, " ", 0.f);
        }
        if (i == n || (i > line_start && x + space + words[i].w > max_w)) {
            // paint the finished line
            float lx = 0.f;
            float bl = *y + baseline_in(base, line_h);
            for (int k = line_start; k < i; k++) {
                const Style *s = &styles[words[k].style];
                TTF_Font *f = font(s->path, s->size);
                if (k > line_start && words[k].space_before)
                    lx += (float) text_width(f, " ", 0.f);
                if (c != NULL) {
                    TextMask m = text_render(f, words[k].text, 0.f);
                    if (m.a != NULL) {
                        RGBA col = rgba(s->color.r * 255.f, s->color.g * 255.f, s->color.b * 255.f, s->color.a);
                        cv_mask(c, m.a, m.w, m.h, m.w, roundf(x0 + lx), roundf(bl) - (float) m.ascent, false, col, BLEND_NORMAL);
                        text_free(&m);
                    }
                }
                lx += words[k].w;
            }
            if (lx > *widest)
                *widest = lx;
            *y += line_h;
            if (i == n)
                break;
            line_start = i;
            x = words[i].w;
            continue;
        }
        x += space + words[i].w;
    }
    return *y;
}

static int split_words(const char *text, int style, const Style *styles, Word *out, int max, bool first_space)
{
    int n = 0;
    const char *p = text;
    bool space = first_space;
    TTF_Font *f = font(styles[style].path, styles[style].size);
    while (*p && n < max) {
        while (*p == ' ') { p++; space = true; }
        if (!*p)
            break;
        const char *e = p;
        while (*e && *e != ' ')
            e++;
        Word *w = &out[n++];
        size_t len = (size_t) (e - p) < sizeof(w->text) - 1 ? (size_t) (e - p) : sizeof(w->text) - 1;
        memcpy(w->text, p, len);
        w->text[len] = '\0';
        w->style = style;
        w->space_before = space;
        w->w = (float) text_width(f, w->text, 0.f);
        space = false;
        p = e;
    }
    return n;
}

static void build_toast(void)
{
    tex_free(&O.toast_tex);
    float size = fs(0.18f, 14.f);
    float line_h = size * 1.4f;
    float max_w = fminf(O.u * 6.4f, (float) O.W * 0.92f) - O.u * 0.2f * 2.f;
    Style styles[3] = {
        { O.font_semibold, size, BONE },
        { O.font_medium, size, BONE_DIM },
        { O.font_mono, size * 0.92f, BONE }
    };
    Word title[64], body[256];
    int nt = split_words(O.toast_title, 0, styles, title, 64, false);
    int nb = split_words(O.toast_body, 1, styles, body, 200, false);
    if (O.toast_code[0] != '\0')
        nb += split_words(O.toast_code, 2, styles, body + nb, 256 - nb, true);

    // measure, then paint at the measured size
    float widest = 0.f, y = 0.f;
    if (nt)
        flow(NULL, title, nt, styles, max_w, 0.f, &y, line_h, &widest);
    y += size * 0.2f;
    if (nb)
        flow(NULL, body, nb, styles, max_w, 0.f, &y, line_h, &widest);
    int cw = (int) ceilf(fminf(widest, max_w)), ch = (int) ceilf(y);
    if (cw < 1 || ch < 1)
        return;
    Canvas *c = cv_new(cw, ch);
    y = 0.f;
    widest = 0.f;
    if (nt)
        flow(c, title, nt, styles, max_w, 0.f, &y, line_h, &widest);
    y += size * 0.2f;
    if (nb)
        flow(c, body, nb, styles, max_w, 0.f, &y, line_h, &widest);
    O.toast_tex = tex_from_canvas(c, false, false, false);
    O.toast_w = cw;
    O.toast_h = ch;
    cv_free(c);
}

void overlay_toast(const char *title, const char *body, const char *code)
{
    snprintf(O.toast_title, sizeof(O.toast_title), "%s", title ? title : "");
    snprintf(O.toast_body, sizeof(O.toast_body), "%s", body ? body : "");
    snprintf(O.toast_code, sizeof(O.toast_code), "%s", code ? code : "");
    build_toast();
    O.toast_timer = TOAST_TIME;
    tw_set(&O.toast_op, 1.f, 0.3f, EASE_OUT);
    tw_set(&O.toast_y, 0.f, 0.42f, EASE_OUT);
}

static void build_sticker(void)
{
    tex_free(&O.blank_sticker);
    int size = (int) lroundf(O.u * 0.7f);
    if (size < 8)
        return;
    O.sticker_size = size;
    Canvas *c = cv_new(size, size);
    float r = (float) size / 2.f;
    cv_circle(c, r, r, r, rgba(239, 234, 224, 1.f), BLEND_NORMAL);
    float inner = r - O.u * 0.035f;
    Canvas *icon = art_load_icon(O.blank_icon, (int) ceilf(inner * 2.f));
    if (icon != NULL) {
        cv_draw_circle_clip(c, icon, r, r, inner);
        cv_free(icon);
    }
    O.blank_sticker = tex_from_canvas(c, false, false, true);
    cv_free(c);
}

void overlay_blank(const char *icon, const char *title, const char *sub, bool bar)
{
    if (icon == NULL)
        icon = "";
    if (strcmp(icon, O.blank_icon) || O.blank_sticker.id == 0) {
        snprintf(O.blank_icon, sizeof(O.blank_icon), "%s", icon);
        build_sticker();
    }
    snprintf(O.blank_title_text, sizeof(O.blank_title_text), "%s", title ? title : "");
    snprintf(O.blank_sub_text, sizeof(O.blank_sub_text), "%s", sub ? sub : "");
    if (bar && !O.blank_bar)
        O.bar_phase = 0.f;
    O.blank_bar = bar;
    O.blank_on = true;
    O.blank_page = false;
    tw_set(&O.blank_op, 1.f, 0.42f, EASE_OUT);
}

void overlay_blank_page(const char *icon, const char *title, const char *sub, bool bar)
{
    overlay_blank(icon, title, sub, bar);
    O.blank_page = true;
}

void overlay_set_hud(float alpha)
{
    O.hud = alpha < 0.f ? 0.f : (alpha > 1.f ? 1.f : alpha);
}

void overlay_unblank(void)
{
    O.blank_on = false;
    tw_set(&O.blank_op, 0.f, 0.42f, EASE_OUT);
}

bool overlay_blanked(void)
{
    return O.blank_on && O.blank_op.v >= 1.f;
}

static float hud_alpha(void)
{
    return O.hud;
}

void overlay_dim(bool dimmed)
{
    tw_set(&O.dim_op, dimmed ? 1.f : 0.f, 1.5f, EASE_IO);
}

// The room and the overlay are already up while the books are painted; only the line fades
void overlay_loading(bool loading)
{
    tw_snap(&O.scene_op, 1.f);
    if (loading)
        tw_snap(&O.loading_op, 1.f);
    else
        tw_set(&O.loading_op, 0.f, 0.6f, EASE_OUT);
}

float overlay_scene_alpha(void)
{
    return O.scene_op.v;
}

bool overlay_update(float dt)
{
    bool moving = false;
    Tween *all[] = { &O.status_op, &O.status_y, &O.toast_op, &O.toast_y, &O.blank_op, &O.dim_op, &O.loading_op, &O.scene_op };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        moving |= tw_step(all[i], dt);
    if (O.toast_timer > 0.f) {
        O.toast_timer -= dt;
        if (O.toast_timer <= 0.f) {
            tw_set(&O.toast_op, 0.f, 0.3f, EASE_OUT);
            tw_set(&O.toast_y, -1.f, 0.42f, EASE_OUT);
        }
        moving = true;
    }
    if (O.status_op.v > 0.f) {
        O.pulse = fmodf(O.pulse + dt, 2.f);
        moving = true;
    }
    if (O.blank_op.v > 0.f && O.blank_bar) {
        O.bar_phase = fmodf(O.bar_phase + dt, 1.1f);
        moving = true;
    }
    return moving;
}

// ---------- drawing ----------

static void draw_top(void)
{
    float u = O.u, y0 = u * 0.34f, x0 = u * 0.62f;
    float size = fs(0.2f, 15.f), gap = u * 0.12f;
    TTF_Font *f = font(O.font_medium, size);
    float lh, bl;
    line_box(f, &lh, &bl);
    float x = x0;
    for (int i = 0; i < O.trail_count; i++) {
        bool here = i == O.trail_count - 1;
        Label *l = &O.trail_labels[i];
        label_set(l, here ? O.font_semibold : O.font_medium, size, here ? -0.01f : 0.f, O.trail[i], 12.f);
        draw_label(l, x, y0 + bl, here ? BONE : BONE_DIM, hud_alpha());
        x += (float) l->w;
        if (!here) {
            x += gap;
            rect(roundf(x), roundf(y0 + lh / 2.f - 1.f), u * 0.1f, 2.f, 0.f, BONE_FAINT, hud_alpha());
            x += u * 0.1f + gap;
        }
    }

    if (!O.clock_enabled)
        return;
    float right = (float) O.W - x0;
    float tsize = fs(0.38f, 22.f), psize = tsize * 0.42f, dsize = fs(0.18f, 14.f);
    TTF_Font *tf = font(O.font_medium, tsize), *df = font(O.font_medium, dsize);
    float tb = y0 + baseline_in(tf, tsize);
    float pw = O.period_label.tex.id ? (float) O.period_label.w + psize * 0.25f : 0.f;
    draw_label(&O.time_label, right - pw - (float) O.time_label.w, tb, BONE, hud_alpha());
    if (O.period_label.tex.id)
        draw_label(&O.period_label, right - (float) O.period_label.w, tb, BONE_DIM, hud_alpha());
    if (O.show_date) {
        float db = y0 + tsize + u * 0.08f + baseline_in(df, dsize);
        draw_label(&O.date_label, right - (float) O.date_label.w, db, BONE_DIM, hud_alpha());
    }
}

void overlay_set_time(const char *time, const char *period, const char *date)
{
    float tsize = fs(0.38f, 22.f);
    label_set(&O.time_label, O.font_medium, tsize, -0.03f, time, 12.f);
    label_set(&O.period_label, O.font_medium, tsize * 0.42f, 0.02f, period, 12.f);
    label_set(&O.date_label, O.font_medium, fs(0.18f, 14.f), 0.f, date, 12.f);
}

static void draw_rail(void)
{
    if (O.rail_count <= 0)
        return;
    float u = O.u, gap = u * 0.09f, tick_w = fmaxf(u * 0.028f, 2.f), brk = u * 0.16f;
    int per = O.rail_per_page > 0 ? O.rail_per_page : 1;
    int page = O.rail_focus / per, breaks = (O.rail_count - 1) / per;
    float total = (float) O.rail_count * tick_w + (float) breaks * brk + (float) (O.rail_count - 1 + breaks) * gap;
    float x = roundf(((float) O.W - total) / 2.f);
    float cy = (float) O.H - u * 1.02f - u * 0.12f;
    for (int k = 0; k < O.rail_count; k++) {
        if (k > 0 && k % per == 0)
            x += brk + gap;
        float h;
        Color c;
        if (k == O.rail_focus) { h = u * 0.24f; c = LED; }
        else if (k / per == page) { h = u * 0.1f; c = BONE_DIM; }
        else { h = u * 0.06f; c = BONE_FAINT; }
        rect(x, roundf(cy - h / 2.f), tick_w, roundf(h), 2.f, c, hud_alpha());
        x += tick_w + gap;
    }
}

// One key cap with an icon or a word in it; returns its width
static float key_cap(float x, float cy, GLuint icon, const Label *word)
{
    float u = O.u, h = u * 0.24f, pad = u * 0.05f;
    float content = icon ? (float) O.svg_size : (word ? (float) word->w : 0.f);
    float w = fmaxf(u * 0.24f, content + pad * 2.f);
    float y = cy - h / 2.f;
    float fill[4] = { 0, 0, 0, 0 };
    float border[4] = { BONE_FAINT.r, BONE_FAINT.g, BONE_FAINT.b, BONE_FAINT.a * hud_alpha() };
    render_overlay_rect(roundf(x), roundf(y), roundf(w), roundf(h), u * 0.06f, fill, 1.5f, border);
    if (icon) {
        float s = (float) O.svg_size;
        float col[4] = { 1, 1, 1, hud_alpha() };
        render_overlay_tex(icon, roundf(x + (w - s) / 2.f), roundf(cy - s / 2.f), s, s, col, false);
    }
    else if (word) {
        TTF_Font *f = font(O.font_semibold, fs(0.18f, 14.f) * 0.82f);
        float lh, bl;
        line_box(f, &lh, &bl);
        draw_label(word, x + (w - (float) word->w) / 2.f, cy - lh / 2.f + bl, BONE, hud_alpha());
    }
    return w;
}

static void draw_bottom(void)
{
    float u = O.u, size = fs(0.18f, 14.f);
    TTF_Font *f = font(O.font_medium, size);
    float lh, bl;
    line_box(f, &lh, &bl);
    float row_h = fmaxf(u * 0.24f, lh);
    float cy = (float) O.H - u * 0.32f - row_h / 2.f;
    float x = u * 0.62f, hint_gap = u * 0.07f;
    label_set(&O.hint_move, O.font_medium, size, 0.f, "Move", 0.f);
    label_set(&O.hint_open, O.font_medium, size, 0.f, "Open", 0.f);
    label_set(&O.hint_back, O.font_medium, size, 0.f, "Back", 0.f);
    label_set(&O.key_ok, O.font_semibold, size * 0.82f, 0.f, "OK", 0.f);
    float text_bl = cy - lh / 2.f + bl;

    x += key_cap(x, cy, O.svg_move.id, NULL) + hint_gap;
    draw_label(&O.hint_move, x, text_bl, BONE_DIM, hud_alpha());
    x += (float) O.hint_move.w + u * 0.3f;
    x += key_cap(x, cy, 0, &O.key_ok) + hint_gap;
    draw_label(&O.hint_open, x, text_bl, BONE_DIM, hud_alpha());
    x += (float) O.hint_open.w + u * 0.3f;
    // Back only means something inside a submenu
    if (O.trail_count > 1) {
        x += key_cap(x, cy, O.svg_back.id, NULL) + hint_gap;
        draw_label(&O.hint_back, x, text_bl, BONE_DIM, hud_alpha());
    }
}

static void draw_status(void)
{
    float op = O.status_op.v;
    if (op <= 0.f)
        return;
    float u = O.u, size = fs(0.18f, 14.f);
    label_set(&O.status_label, O.font_medium, size, 0.f, O.status_text, 0.f);
    TTF_Font *f = font(O.font_medium, size);
    float lh, bl;
    line_box(f, &lh, &bl);
    float dot = u * 0.07f, gap = u * 0.1f, pv = u * 0.09f, ph = u * 0.2f;
    float w = ph * 2.f + dot + gap + (float) O.status_label.w, h = lh + pv * 2.f;
    float x = roundf(((float) O.W - w) / 2.f), y = roundf(u * 0.32f + O.status_y.v * u * 0.12f);
    box_shadow(x, y, w, h, h / 2.f, u * 0.08f, u * 0.3f, 0.5f * op);
    rect(x, y, w, h, h / 2.f, (Color) { 14 / 255.f, 14 / 255.f, 17 / 255.f, 0.86f }, op);
    // the dot breathes between 35% and full while something is starting
    float p = O.pulse < 1.f ? O.pulse : 2.f - O.pulse;
    float pulse = 0.35f + 0.65f * ease(EASE_IO, p);
    rect(x + ph, y + h / 2.f - dot / 2.f, dot, dot, dot / 2.f, LED, op * pulse);
    draw_label(&O.status_label, x + ph + dot + gap, y + pv + bl, BONE, op);
}

static void draw_toast(void)
{
    float op = O.toast_op.v;
    if (op <= 0.f || O.toast_tex.id == 0)
        return;
    float u = O.u, pv = u * 0.13f, ph = u * 0.2f;
    float w = (float) O.toast_w + ph * 2.f, h = (float) O.toast_h + pv * 2.f;
    float x = roundf(((float) O.W - w) / 2.f), y = roundf(u * 0.32f + O.toast_y.v * u * 0.12f);
    box_shadow(x, y, w, h, u * 0.08f, u * 0.08f, u * 0.3f, 0.5f * op);
    rect(x, y, w, h, u * 0.08f, (Color) { 14 / 255.f, 14 / 255.f, 17 / 255.f, 0.86f }, op);
    float col[4] = { 1, 1, 1, op };
    render_overlay_tex(O.toast_tex.id, x + ph, y + pv, (float) O.toast_w, (float) O.toast_h, col, false);
}

static void draw_blank(void)
{
    float op = O.blank_op.v;
    if (op <= 0.f)
        return;
    if (!O.blank_page)
        rect(0, 0, (float) O.W, (float) O.H, 0.f, (Color) { 5 / 255.f, 5 / 255.f, 6 / 255.f, 1.f }, op);
    float u = O.u, gap = u * 0.18f;
    float tsize = fs(0.2f, 15.f), ssize = fs(0.18f, 14.f);
    label_set(&O.blank_title, O.font_medium, tsize, -0.01f, O.blank_title_text, 0.f);
    label_set(&O.blank_sub, O.font_medium, ssize, 0.f, O.blank_sub_text, 0.f);
    float tlh, tbl, slh = 0.f, sbl = 0.f;
    line_box(font(O.font_medium, tsize), &tlh, &tbl);
    if (O.blank_sub_text[0] != '\0')
        line_box(font(O.font_medium, ssize), &slh, &sbl);
    float sticker = (float) O.sticker_size;
    float total = sticker + gap + tlh + gap + slh + (O.blank_bar ? gap + 2.f : 0.f);
    float y = roundf(((float) O.H - total) / 2.f), cx = (float) O.W / 2.f;
    if (O.blank_sticker.id) {
        float col[4] = { 1, 1, 1, op };
        render_overlay_tex(O.blank_sticker.id, roundf(cx - sticker / 2.f), y, sticker, sticker, col, false);
    }
    y += sticker + gap;
    draw_label(&O.blank_title, cx - (float) O.blank_title.w / 2.f, y + tbl, BONE, op);
    y += tlh + gap;
    if (slh > 0.f)
        draw_label(&O.blank_sub, cx - (float) O.blank_sub.w / 2.f, y + sbl, BONE_DIM, op);
    y += slh;
    if (O.blank_bar) {
        y += gap;
        float bw = u * 1.4f, bx = roundf(cx - bw / 2.f);
        rect(bx, y, bw, 2.f, 2.f, (Color) { BONE.r, BONE.g, BONE.b, 0.12f }, op);
        // a 40% segment slides from -100% to 250% of its own width, clipped to the track
        float seg = bw * 0.4f, p = ease(EASE_IO, O.bar_phase / 1.1f);
        float sx = bx + seg * (-1.f + 3.5f * p);
        float x0 = fmaxf(sx, bx), x1 = fminf(sx + seg, bx + bw);
        if (x1 > x0)
            rect(x0, y, x1 - x0, 2.f, 1.f, LED, op);
    }
}

void overlay_draw(void)
{
    if (O.scrim && O.hud > 0.f)
        render_overlay_scrim(O.hud);
    if (O.loading_op.v > 0.f) {
        float size = fs(0.18f, 14.f), lh, bl;
        label_set(&O.loading_label, O.font_medium, size, 0.f, "Setting the shelf", 0.f);
        line_box(font(O.font_medium, size), &lh, &bl);
        draw_label(&O.loading_label, ((float) O.W - (float) O.loading_label.w) / 2.f, (float) O.H * 0.5f - lh / 2.f + bl, BONE, O.loading_op.v);
    }
    if (O.hud > 0.f) {
        draw_top();
        draw_rail();
        draw_bottom();
    }
    draw_status();
    draw_toast();
    if (O.dim_op.v > 0.f)
        rect(0, 0, (float) O.W, (float) O.H, 0.f, (Color) { 0, 0, 0, 0.35f }, O.dim_op.v);
    draw_blank();
}
