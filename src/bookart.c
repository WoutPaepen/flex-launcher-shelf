#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include "bookart.h"
#include "text.h"

// One cloth tile covers about 4.5 world units (30 cm) of real cloth
#define CLOTH_TILE 4.5f

static const RGBA FOIL = { 0xE6, 0xC7, 0x7F, 1.f };
static const RGBA DARK_INK = { 0x1A, 0x17, 0x12, 1.f };
static const RGBA LABEL_PAPER = { 0xEF, 0xE7, 0xD6, 1.f };

static ArtPaths paths;
static Canvas *cloth_detail = NULL; // the scanned weave, colour removed, centred on mid grey
static Canvas *cloth_rough = NULL;
static NSVGrasterizer *svg_raster = NULL;

static Canvas *surface_to_canvas(SDL_Surface *s)
{
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    if (conv == NULL)
        return NULL;
    SDL_LockSurface(conv);
    Canvas *c = cv_from_rgba(conv->pixels, conv->w, conv->h, conv->pitch);
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
    return c;
}

bool art_init(const ArtPaths *p)
{
    paths = *p;
    svg_raster = nsvgCreateRasterizer();
    SDL_Surface *s = IMG_Load(paths.cloth_color);
    if (s == NULL)
        return false;
    cloth_detail = surface_to_canvas(s);
    SDL_FreeSurface(s);
    s = IMG_Load(paths.cloth_rough);
    if (s == NULL)
        return false;
    cloth_rough = surface_to_canvas(s);
    SDL_FreeSurface(s);
    if (cloth_detail == NULL || cloth_rough == NULL)
        return false;

    // keep only the light and dark of the weave so it can be re-dyed per book
    size_t n = (size_t) cloth_detail->w * (size_t) cloth_detail->h;
    double sum = 0.0;
    uint8_t *px = cloth_detail->px;
    for (size_t i = 0; i < n; i++)
        sum += 0.2126 * px[i * 4] + 0.7152 * px[i * 4 + 1] + 0.0722 * px[i * 4 + 2];
    float mean = (float) (sum / (double) n);
    for (size_t i = 0; i < n; i++) {
        float l = 0.2126f * px[i * 4] + 0.7152f * px[i * 4 + 1] + 0.0722f * px[i * 4 + 2];
        float v = fminf(fmaxf(128.f + (l - mean) * 2.4f, 0.f), 255.f);
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = (uint8_t) v;
        px[i * 4 + 3] = 255;
    }
    return true;
}

void art_quit(void)
{
    cv_free(cloth_detail);
    cv_free(cloth_rough);
    cloth_detail = cloth_rough = NULL;
    if (svg_raster != NULL)
        nsvgDeleteRasterizer(svg_raster);
    svg_raster = NULL;
}

void art_free(BookArt *art)
{
    cv_free(art->cover);
    cv_free(art->cover_rm);
    cv_free(art->spine);
    cv_free(art->spine_rm);
    cv_free(art->back);
    memset(art, 0, sizeof(BookArt));
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !SDL_strcasecmp(s + a - b, suffix);
}

Canvas *art_load_icon(const char *path, int size)
{
    if (path == NULL)
        return NULL;
    Canvas *out = cv_new(size, size);
    if (ends_with(path, ".svg")) {
        NSVGimage *img = nsvgParseFromFile(path, "px", 96.f);
        if (img == NULL || img->width <= 0.f) {
            if (img != NULL)
                nsvgDelete(img);
            cv_free(out);
            return NULL;
        }
        float scale = (float) size / fmaxf(img->width, img->height);
        nsvgRasterize(svg_raster, img, 0, 0, scale, out->px, size, size, size * 4);
        nsvgDelete(img);
        return out;
    }
    SDL_Surface *s = IMG_Load(path);
    if (s == NULL) {
        cv_free(out);
        return NULL;
    }
    Canvas *src = surface_to_canvas(s);
    SDL_FreeSurface(s);
    if (src == NULL) {
        cv_free(out);
        return NULL;
    }
    cv_draw(out, src, 0.f, 0.f, (float) size, (float) size, 1.f, BLEND_NORMAL);
    cv_free(src);
    return out;
}

// ---------- cloth ----------

static void cv_tint(Canvas *c, RGBA color)
{
    size_t n = (size_t) c->w * (size_t) c->h;
    for (size_t i = 0; i < n; i++) {
        c->px[i * 4] = (uint8_t) color.r;
        c->px[i * 4 + 1] = (uint8_t) color.g;
        c->px[i * 4 + 2] = (uint8_t) color.b;
    }
}

static void wear(Canvas *x)
{
    float w = (float) x->w, h = (float) x->h, mn = fminf(w, h);
    // rubbed edges: the dye lifts and the threads show lighter
    for (int i = 0; i < 520; i++) {
        int side = i % 4;
        float t = rng(), depth = powf(rng(), 3.f) * mn * 0.03f;
        float px = side == 0 ? t * w : side == 1 ? w - depth : side == 2 ? t * w : depth;
        float py = side == 0 ? depth : side == 1 ? t * h : side == 2 ? h - depth : t * h;
        float a = 0.05f + rng() * 0.12f;
        cv_rect(x, px, py, 1.f + rng() * 3.f, 1.f + rng() * 3.f, rgba(230, 220, 200, a), BLEND_SCREEN);
    }
    float corners[4][2] = { { 0, 0 }, { w, 0 }, { 0, h }, { w, h } };
    Stop cs[2] = { { 0.f, rgba(225, 215, 195, 0.35f) }, { 1.f, rgba(225, 215, 195, 0.f) } };
    for (int i = 0; i < 4; i++)
        cv_radial(x, corners[i][0], corners[i][1], mn * 0.06f, cs, 2, BLEND_SCREEN);
    // handling grime toward the bottom
    Stop gs[2] = { { 0.f, rgba(0, 0, 0, 0.f) }, { 1.f, rgba(20, 14, 8, 0.22f) } };
    cv_gradient(x, 0.f, h * 0.82f, 0.f, h, gs, 2, BLEND_NORMAL);
}

static void dyed_cloth(Canvas *x, RGBA color, float units_w, int seed)
{
    rng_seed((uint32_t) seed * 2654435761u + 17u);
    cv_clear(x, color);
    float scale = (float) x->w / units_w * CLOTH_TILE / (float) cloth_detail->w;
    cv_pattern(x, cloth_detail, (float) ((seed * 137) % 400), (float) ((seed * 211) % 400), scale, 0.85f, BLEND_OVERLAY);
    // dye that took unevenly
    Canvas *n = cv_new(12, 16);
    for (int i = 0; i < 12 * 16; i++) {
        uint8_t v = (uint8_t) (90.f + rng() * 76.f);
        n->px[i * 4] = n->px[i * 4 + 1] = n->px[i * 4 + 2] = v;
        n->px[i * 4 + 3] = 255;
    }
    cv_draw(x, n, 0.f, 0.f, (float) x->w, (float) x->h, 0.18f, BLEND_OVERLAY);
    cv_free(n);
    wear(x);
}

// roughness lives in green; the cloth takes the scanned roughness
static void rough_cloth(Canvas *rm, float units_w, int seed)
{
    float scale = (float) rm->w / units_w * CLOTH_TILE / (float) cloth_rough->w;
    cv_pattern(rm, cloth_rough, (float) ((seed * 137) % 400), (float) ((seed * 211) % 400), scale, 1.f, BLEND_NORMAL);
    size_t n = (size_t) rm->w * (size_t) rm->h;
    for (size_t i = 0; i < n; i++) {
        rm->px[i * 4] = 0;
        rm->px[i * 4 + 2] = 0;
        rm->px[i * 4 + 3] = 255;
    }
}

// ---------- type ----------

typedef struct {
    TTF_Font *font;
    int size;
    char text[512];
} Fitted;

static void truncate_utf8(char *s)
{
    // drop a trailing ellipsis, then one more code point
    size_t len = strlen(s);
    if (len >= 3 && !strcmp(s + len - 3, "\xE2\x80\xA6"))
        s[len -= 3] = '\0';
    while (len > 0) {
        len--;
        if (((unsigned char) s[len] & 0xC0) != 0x80)
            break;
    }
    s[len] = '\0';
}

// Shrink the size until the text fits, then truncate with an ellipsis (OversizeMode)
static Fitted fit_text(const char *font_path, const char *text, int max, int min, float width)
{
    Fitted f;
    snprintf(f.text, sizeof(f.text), "%s", text);
    TTF_Font *font = font_get(font_path, max);
    int w = text_width(font, f.text, 0.f);
    int size = max;
    if (w > width && w > 0) {
        // jump close to the right size, then step like the prototype does
        float guess = (float) max * width / (float) w;
        size = max - 3 * (int) ceilf(((float) max - guess) / 3.f);
        if (size < min)
            size = min;
        if (size > max)
            size = max;
        while (size + 3 <= max && text_width(font_get(font_path, size + 3), f.text, 0.f) <= width)
            size += 3;
        while (size > min && text_width(font_get(font_path, size), f.text, 0.f) > width)
            size -= 3;
        if (size < min)
            size = min;
    }
    f.font = font_get(font_path, size);
    f.size = size;
    while (text_width(f.font, f.text, 0.f) > width && strlen(f.text) > 1) {
        truncate_utf8(f.text);
        strncat(f.text, "\xE2\x80\xA6", sizeof(f.text) - strlen(f.text) - 1);
        if (strlen(f.text) <= 3)
            break;
    }
    return f;
}

static void upper_utf8(const char *in, char *out, size_t n)
{
    size_t i = 0;
    for (; in[i] && i + 1 < n; i++) {
        unsigned char c = (unsigned char) in[i];
        out[i] = (char) ((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    out[i] = '\0';
}

static void ink_text(Canvas *ink, TTF_Font *font, const char *text, float x, float baseline, bool rotate)
{
    TextMask m = text_render(font, text, 0.f);
    if (m.a == NULL)
        return;
    if (rotate)
        cv_mask(ink, m.a, m.w, m.h, m.w, x - (float) m.ascent, baseline, true, rgba(255, 255, 255, 1.f), BLEND_NORMAL);
    else
        cv_mask(ink, m.a, m.w, m.h, m.w, x, baseline - (float) m.ascent, false, rgba(255, 255, 255, 1.f), BLEND_NORMAL);
    text_free(&m);
}

// Foil stamped into the cloth: a pressed shadow below, a lifted edge above, then the foil.
// The same shapes go into the roughness map: smooth metal for foil, satin for ink.
static void stamp(Canvas *x, Canvas *rm, Canvas *ink, RGBA color, bool foil)
{
    cv_tint(ink, rgba(0, 0, 0, 1));
    cv_draw(x, ink, 2.f, 3.f, (float) x->w, (float) x->h, 0.5f, BLEND_NORMAL);
    cv_tint(ink, rgba(255, 255, 255, 1));
    cv_draw(x, ink, -1.f, -1.5f, (float) x->w, (float) x->h, 0.18f, BLEND_NORMAL);
    cv_tint(ink, color);
    cv_draw(x, ink, 0.f, 0.f, (float) x->w, (float) x->h, 1.f, BLEND_NORMAL);
    cv_tint(ink, foil ? rgba(0, 92, 235, 1) : rgba(0, 150, 0, 1));
    cv_draw(rm, ink, 0.f, 0.f, (float) rm->w, (float) rm->h, 1.f, BLEND_NORMAL);
}

static void label(Canvas *x, Canvas *rm, float rm_scale, const Canvas *img, float cx, float cy, float r)
{
    cv_circle_soft(x, cx, cy + r * 0.03f, r, r * 0.04f, rgba(0, 0, 0, 0.35f));
    cv_circle(x, cx, cy, r, LABEL_PAPER, BLEND_NORMAL);
    if (img != NULL)
        cv_draw_circle_clip(x, img, cx, cy, r * 0.88f);
    cv_circle(rm, cx * rm_scale, cy * rm_scale, r * rm_scale, rgba(0, 170, 0, 1.f), BLEND_NORMAL);
}

// ---------- books ----------

static void paint_cover(const BookArtSpec *s, BookArt *out)
{
    Canvas *x = cv_new(COVER_W, COVER_H);
    Canvas *rm = cv_new(COVER_W / 2, COVER_H / 2);
    dyed_cloth(x, s->color, s->D, s->seed);
    rough_cloth(rm, s->D, s->seed);

    // hinge joint beside the spine
    cv_rect(x, 42, 0, 8, COVER_H, rgba(0, 0, 0, 0.3f), BLEND_NORMAL);
    cv_rect(x, 52, 0, 5, COVER_H, rgba(255, 255, 255, 0.07f), BLEND_NORMAL);
    Canvas *icon = art_load_icon(s->icon_selected != NULL ? s->icon_selected : s->icon, 256);
    if (icon == NULL && s->icon_selected != NULL)
        icon = art_load_icon(s->icon, 256);
    label(x, rm, 0.5f, icon, 190, 205, 112);
    cv_free(icon);

    char upper[512];
    upper_utf8(s->title, upper, sizeof(upper));
    Fitted t = fit_text(paths.font_cover, upper, 250, 120, COVER_W - 170);
    Canvas *ink = cv_new(COVER_W, COVER_H);
    ink_text(ink, t.font, t.text, 84, 850, false);
    cv_rect(ink, 84, 888, COVER_W - 168, 4, rgba(255, 255, 255, 1), BLEND_NORMAL);
    if (s->note != NULL && *s->note) {
        Fitted n = fit_text(paths.font_note, s->note, 42, 42, COVER_W - 168);
        ink_text(ink, n.font, n.text, 84, 956, false);
    }
    stamp(x, rm, ink, s->dark_ink ? DARK_INK : FOIL, !s->dark_ink);
    cv_free(ink);
    out->cover = x;
    out->cover_rm = rm;
}

void art_paint_spine(const BookArtSpec *s, BookArt *out)
{
    Canvas *x = cv_new(SPINE_W, SPINE_H);
    Canvas *rm = cv_new(SPINE_W, SPINE_H);
    dyed_cloth(x, s->color, s->T, s->seed + 3);
    rough_cloth(rm, s->T, s->seed + 3);
    RGBA ink_color = s->dark_ink ? DARK_INK : FOIL;

    Canvas *ink = cv_new(SPINE_W, SPINE_H);
    cv_rect(ink, 26, 46, SPINE_W - 52, 4, rgba(255, 255, 255, 1), BLEND_NORMAL);
    cv_rect(ink, 26, 57, SPINE_W - 52, 2, rgba(255, 255, 255, 1), BLEND_NORMAL);
    cv_rect(ink, 26, SPINE_H - 59, SPINE_W - 52, 2, rgba(255, 255, 255, 1), BLEND_NORMAL);
    cv_rect(ink, 26, SPINE_H - 50, SPINE_W - 52, 4, rgba(255, 255, 255, 1), BLEND_NORMAL);
    stamp(x, rm, ink, ink_color, !s->dark_ink);
    cv_free(ink);

    Canvas *icon = art_load_icon(s->icon, 256);
    label(x, rm, 1.f, icon, SPINE_W / 2.f, 150, 64);
    cv_free(icon);

    if (s->spine_title) {
        char upper[512];
        upper_utf8(s->title, upper, sizeof(upper));
        Fitted t = fit_text(paths.font_spine, upper, 128, 70, SPINE_H - 350);
        ink = cv_new(SPINE_W, SPINE_H);
        ink_text(ink, t.font, t.text, SPINE_W / 2.f + (float) t.size * 0.34f, SPINE_H - 104, true);
        stamp(x, rm, ink, ink_color, !s->dark_ink);
        cv_free(ink);
    }

    // the rounded spine catches less light toward its edges
    Stop g[4] = {
        { 0.f, rgba(0, 0, 0, 0.35f) }, { 0.18f, rgba(0, 0, 0, 0.f) },
        { 0.82f, rgba(0, 0, 0, 0.f) }, { 1.f, rgba(0, 0, 0, 0.35f) }
    };
    cv_gradient(x, 0, 0, SPINE_W, 0, g, 4, BLEND_NORMAL);
    cv_free(out->spine);
    cv_free(out->spine_rm);
    out->spine = x;
    out->spine_rm = rm;
}

void art_paint(const BookArtSpec *s, BookArt *out)
{
    memset(out, 0, sizeof(BookArt));
    paint_cover(s, out);
    art_paint_spine(s, out);
    out->back = cv_new(BACK_W, BACK_H);
    dyed_cloth(out->back, s->color, s->D, s->seed + 7);
}

// ---------- shared textures ----------

Canvas *art_page_texture(void)
{
    Canvas *c = cv_new(512, 512);
    rng_seed(0x5eed1234u);
    cv_clear(c, rgba(0xE9, 0xDF, 0xCA, 1));
    for (int i = 0; i < 512; i++) {
        float a = rng() * 0.18f;
        RGBA col = rng() > 0.5f ? rgba(120, 100, 70, a) : rgba(255, 252, 240, a);
        cv_rect(c, (float) i, 0, 1, 512, col, BLEND_NORMAL);
    }
    return c;
}

// Contact occlusion laid under every book: two blurred rectangles on black
Canvas *art_ao_texture(void)
{
    Canvas *c = cv_new(256, 256);
    cv_clear(c, rgba(0, 0, 0, 1));
    Canvas *soft = cv_new(256, 256);
    cv_rect(soft, 52, 46, 152, 164, rgba(255, 255, 255, 0.95f), BLEND_NORMAL);
    cv_blur(soft, 16.f);
    cv_draw(c, soft, 0, 0, 256, 256, 1.f, BLEND_NORMAL);
    cv_clear(soft, rgba(0, 0, 0, 0));
    cv_rect(soft, 70, 62, 116, 132, rgba(255, 255, 255, 1), BLEND_NORMAL);
    cv_blur(soft, 4.f);
    cv_draw(c, soft, 0, 0, 256, 256, 1.f, BLEND_NORMAL);
    cv_free(soft);
    return c;
}

// The diffuser in front of the LED strip: soft ends, soft top and bottom
Canvas *art_led_texture(void)
{
    Canvas *c = cv_new(256, 32);
    Stop h[4] = {
        { 0.f, rgba(255, 255, 255, 0.f) }, { 0.12f, rgba(255, 255, 255, 1.f) },
        { 0.88f, rgba(255, 255, 255, 1.f) }, { 1.f, rgba(255, 255, 255, 0.f) }
    };
    cv_gradient(c, 0, 0, 256, 0, h, 4, BLEND_NORMAL);
    Stop v[3] = { { 0.f, rgba(0, 0, 0, 0.25f) }, { 0.5f, rgba(0, 0, 0, 1.f) }, { 1.f, rgba(0, 0, 0, 0.25f) } };
    cv_gradient(c, 0, 0, 0, 32, v, 3, BLEND_DEST_IN);
    return c;
}

// ---------- colour from the icon ----------

static void rgb_to_hsl(float r, float g, float b, float *h, float *s, float *l)
{
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    *l = (mx + mn) / 2.f;
    if (mx == mn) {
        *h = *s = 0.f;
        return;
    }
    float d = mx - mn;
    *s = *l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) *h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) *h = (b - r) / d + 2.f;
    else *h = (r - g) / d + 4.f;
    *h /= 6.f;
}

static float hue_to_rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

RGBA art_color_from_icon(const char *path)
{
    RGBA fallback = rgba(0x34, 0x38, 0x3F, 1.f);
    Canvas *c = art_load_icon(path, 64);
    if (c == NULL)
        return fallback;
    double sr = 0, sg = 0, sb = 0, sw = 0;
    for (int i = 0; i < 64 * 64; i++) {
        const uint8_t *p = c->px + i * 4;
        if (p[3] < 128)
            continue;
        float h, s, l;
        rgb_to_hsl(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, &h, &s, &l);
        if (l < 0.08f || l > 0.92f)
            continue;
        double w = 0.05 + s * s;
        sr += p[0] * w; sg += p[1] * w; sb += p[2] * w; sw += w;
    }
    cv_free(c);
    if (sw <= 0)
        return fallback;
    float h, s, l;
    rgb_to_hsl((float) (sr / sw) / 255.f, (float) (sg / sw) / 255.f, (float) (sb / sw) / 255.f, &h, &s, &l);
    // book cloth: saturated enough to read, dark enough to carry gold foil
    s = s < 0.1f ? s : fminf(fmaxf(s, 0.45f), 0.75f);
    l = fminf(fmaxf(l, 0.24f), 0.42f);
    float q = l < 0.5f ? l * (1.f + s) : l + s - l * s, p = 2.f * l - q;
    return rgba(hue_to_rgb(p, q, h + 1.f / 3.f) * 255.f, hue_to_rgb(p, q, h) * 255.f, hue_to_rgb(p, q, h - 1.f / 3.f) * 255.f, 1.f);
}

bool art_prefers_dark_ink(RGBA c)
{
    return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.f > 0.55f;
}
