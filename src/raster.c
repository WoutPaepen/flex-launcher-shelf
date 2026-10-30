#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "raster.h"

static uint32_t rng_state = 0x9E3779B9u;

void rng_seed(uint32_t seed)
{
    rng_state = seed ? seed : 0x9E3779B9u;
}

float rng(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return (float) (x >> 8) / 16777216.f;
}

Canvas *cv_new(int w, int h)
{
    Canvas *c = malloc(sizeof(Canvas));
    c->w = w;
    c->h = h;
    c->px = calloc((size_t) w * (size_t) h, 4);
    return c;
}

void cv_free(Canvas *c)
{
    if (c == NULL)
        return;
    free(c->px);
    free(c);
}

Canvas *cv_copy(const Canvas *c)
{
    Canvas *r = cv_new(c->w, c->h);
    memcpy(r->px, c->px, (size_t) c->w * (size_t) c->h * 4);
    return r;
}

Canvas *cv_from_rgba(const uint8_t *rgba, int w, int h, int pitch)
{
    Canvas *c = cv_new(w, h);
    for (int y = 0; y < h; y++)
        memcpy(c->px + (size_t) y * (size_t) w * 4, rgba + (size_t) y * (size_t) pitch, (size_t) w * 4);
    return c;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

RGBA rgba_hex(const char *hex, float a)
{
    if (*hex == '#')
        hex++;
    return rgba((float) (hexval(hex[0]) * 16 + hexval(hex[1])),
                (float) (hexval(hex[2]) * 16 + hexval(hex[3])),
                (float) (hexval(hex[4]) * 16 + hexval(hex[5])),
                a);
}

static inline float blend_channel(Blend blend, float cb, float cs)
{
    switch (blend) {
        case BLEND_OVERLAY:
            return cb <= 0.5f ? 2.f * cb * cs : 1.f - 2.f * (1.f - cb) * (1.f - cs);
        case BLEND_SCREEN:
            return cb + cs - cb * cs;
        case BLEND_MULTIPLY:
            return cb * cs;
        default:
            return cs;
    }
}

// Composite one source colour (0..1 channels, alpha already multiplied by coverage) onto a pixel
static inline void put(uint8_t *d, float r, float g, float b, float a, Blend blend)
{
    if (a <= 0.f && blend != BLEND_DEST_IN)
        return;
    float da = d[3] / 255.f;
    if (blend == BLEND_DEST_IN) {
        d[3] = (uint8_t) (da * a * 255.f + 0.5f);
        return;
    }
    float dr = d[0] / 255.f, dg = d[1] / 255.f, db = d[2] / 255.f;
    if (blend != BLEND_NORMAL) {
        // mix the blended colour with the source by the backdrop alpha
        r = (1.f - da) * r + da * blend_channel(blend, dr, r);
        g = (1.f - da) * g + da * blend_channel(blend, dg, g);
        b = (1.f - da) * b + da * blend_channel(blend, db, b);
    }
    float oa = a + da * (1.f - a);
    if (oa <= 0.f) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    float or_ = (r * a + dr * da * (1.f - a)) / oa;
    float og = (g * a + dg * da * (1.f - a)) / oa;
    float ob = (b * a + db * da * (1.f - a)) / oa;
    d[0] = (uint8_t) (fminf(fmaxf(or_, 0.f), 1.f) * 255.f + 0.5f);
    d[1] = (uint8_t) (fminf(fmaxf(og, 0.f), 1.f) * 255.f + 0.5f);
    d[2] = (uint8_t) (fminf(fmaxf(ob, 0.f), 1.f) * 255.f + 0.5f);
    d[3] = (uint8_t) (fminf(oa, 1.f) * 255.f + 0.5f);
}

static inline void put_rgba(uint8_t *d, RGBA c, float coverage, Blend blend)
{
    put(d, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a * coverage, blend);
}

void cv_clear(Canvas *c, RGBA color)
{
    uint8_t v[4] = { (uint8_t) color.r, (uint8_t) color.g, (uint8_t) color.b, (uint8_t) (color.a * 255.f + 0.5f) };
    for (int i = 0; i < c->w * c->h; i++)
        memcpy(c->px + i * 4, v, 4);
}

// Coverage of the pixel span [p, p+1) by [a, b)
static inline float span(float p, float a, float b)
{
    float lo = fmaxf(p, a), hi = fminf(p + 1.f, b);
    return hi > lo ? hi - lo : 0.f;
}

void cv_rect(Canvas *c, float x, float y, float w, float h, RGBA color, Blend blend)
{
    int x0 = (int) floorf(x), x1 = (int) ceilf(x + w);
    int y0 = (int) floorf(y), y1 = (int) ceilf(y + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        float cy = span((float) py, y, y + h);
        for (int px = x0; px < x1; px++) {
            float cov = cy * span((float) px, x, x + w);
            put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, color, cov, blend);
        }
    }
}

void cv_circle(Canvas *c, float cx, float cy, float r, RGBA color, Blend blend)
{
    int x0 = (int) floorf(cx - r - 1), x1 = (int) ceilf(cx + r + 1);
    int y0 = (int) floorf(cy - r - 1), y1 = (int) ceilf(cy + r + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = (float) px + 0.5f - cx, dy = (float) py + 0.5f - cy;
            float cov = fminf(fmaxf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0.f), 1.f);
            if (cov > 0.f)
                put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, color, cov, blend);
        }
    }
}

void cv_circle_soft(Canvas *c, float cx, float cy, float r, float sigma, RGBA color)
{
    float ext = r + sigma * 3.f + 1.f;
    int x0 = (int) floorf(cx - ext), x1 = (int) ceilf(cx + ext);
    int y0 = (int) floorf(cy - ext), y1 = (int) ceilf(cy + ext);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    float s = fmaxf(sigma, 0.5f) * 1.41421356f;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = (float) px + 0.5f - cx, dy = (float) py + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - r;
            float cov = 0.5f * erfcf(d / s);
            if (cov > 0.002f)
                put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, color, cov, BLEND_NORMAL);
        }
    }
}

static inline void sample_bilinear(const Canvas *s, float u, float v, bool wrap, float out[4])
{
    // u, v in source pixel coordinates (pixel centres at +0.5)
    u -= 0.5f;
    v -= 0.5f;
    int x0 = (int) floorf(u), y0 = (int) floorf(v);
    float fx = u - (float) x0, fy = v - (float) y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (wrap) {
        x0 = ((x0 % s->w) + s->w) % s->w; x1 = ((x1 % s->w) + s->w) % s->w;
        y0 = ((y0 % s->h) + s->h) % s->h; y1 = ((y1 % s->h) + s->h) % s->h;
    }
    else {
        if (x0 < 0) x0 = 0; if (x0 >= s->w) x0 = s->w - 1;
        if (x1 < 0) x1 = 0; if (x1 >= s->w) x1 = s->w - 1;
        if (y0 < 0) y0 = 0; if (y0 >= s->h) y0 = s->h - 1;
        if (y1 < 0) y1 = 0; if (y1 >= s->h) y1 = s->h - 1;
    }
    const uint8_t *a = s->px + ((size_t) y0 * (size_t) s->w + (size_t) x0) * 4;
    const uint8_t *b = s->px + ((size_t) y0 * (size_t) s->w + (size_t) x1) * 4;
    const uint8_t *cc = s->px + ((size_t) y1 * (size_t) s->w + (size_t) x0) * 4;
    const uint8_t *d = s->px + ((size_t) y1 * (size_t) s->w + (size_t) x1) * 4;
    // premultiplied interpolation so transparent texels do not bleed colour
    float pa[4] = { a[3] / 255.f, b[3] / 255.f, cc[3] / 255.f, d[3] / 255.f };
    float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    float al = w[0] * pa[0] + w[1] * pa[1] + w[2] * pa[2] + w[3] * pa[3];
    for (int k = 0; k < 3; k++) {
        float v_ = w[0] * a[k] * pa[0] + w[1] * b[k] * pa[1] + w[2] * cc[k] * pa[2] + w[3] * d[k] * pa[3];
        out[k] = al > 0.f ? v_ / al / 255.f : 0.f;
    }
    out[3] = al;
}

void cv_pattern(Canvas *dst, const Canvas *src, float tx, float ty, float s, float alpha, Blend blend)
{
    float inv = 1.f / s;
    float smp[4];
    for (int py = 0; py < dst->h; py++) {
        for (int px = 0; px < dst->w; px++) {
            float u = ((float) px + 0.5f - tx) * inv, v = ((float) py + 0.5f - ty) * inv;
            sample_bilinear(src, u, v, true, smp);
            put(dst->px + ((size_t) py * (size_t) dst->w + (size_t) px) * 4, smp[0], smp[1], smp[2], smp[3] * alpha, blend);
        }
    }
}

void cv_draw(Canvas *dst, const Canvas *src, float dx, float dy, float dw, float dh, float alpha, Blend blend)
{
    int x0 = (int) floorf(dx), x1 = (int) ceilf(dx + dw);
    int y0 = (int) floorf(dy), y1 = (int) ceilf(dy + dh);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    float sx = (float) src->w / dw, sy = (float) src->h / dh;
    float smp[4];
    for (int py = y0; py < y1; py++) {
        float cy = span((float) py, dy, dy + dh);
        for (int px = x0; px < x1; px++) {
            float cov = cy * span((float) px, dx, dx + dw);
            sample_bilinear(src, ((float) px + 0.5f - dx) * sx, ((float) py + 0.5f - dy) * sy, false, smp);
            put(dst->px + ((size_t) py * (size_t) dst->w + (size_t) px) * 4, smp[0], smp[1], smp[2], smp[3] * alpha * cov, blend);
        }
    }
}

void cv_draw_circle_clip(Canvas *dst, const Canvas *src, float cx, float cy, float r)
{
    int x0 = (int) floorf(cx - r), x1 = (int) ceilf(cx + r);
    int y0 = (int) floorf(cy - r), y1 = (int) ceilf(cy + r);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    float scale = (float) src->w / (2.f * r);
    float smp[4];
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fx = (float) px + 0.5f - cx, fy = (float) py + 0.5f - cy;
            float cov = fminf(fmaxf(r - sqrtf(fx * fx + fy * fy) + 0.5f, 0.f), 1.f);
            if (cov <= 0.f)
                continue;
            // supersample 2x2 when shrinking so small stickers stay smooth
            float acc[4] = { 0, 0, 0, 0 };
            int ss = scale > 1.2f ? 2 : 1;
            for (int j = 0; j < ss; j++) {
                for (int i = 0; i < ss; i++) {
                    float ox = ((float) i + 0.5f) / (float) ss, oy = ((float) j + 0.5f) / (float) ss;
                    sample_bilinear(src, ((float) px + ox - (cx - r)) * scale, ((float) py + oy - (cy - r)) * scale, false, smp);
                    acc[0] += smp[0] * smp[3]; acc[1] += smp[1] * smp[3]; acc[2] += smp[2] * smp[3]; acc[3] += smp[3];
                }
            }
            float n = (float) (ss * ss), a = acc[3] / n;
            if (a <= 0.f)
                continue;
            put(dst->px + ((size_t) py * (size_t) dst->w + (size_t) px) * 4, acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], a * cov, BLEND_NORMAL);
        }
    }
}

static RGBA stop_color(const Stop *stops, int n, float t)
{
    if (t <= stops[0].pos)
        return stops[0].color;
    for (int i = 1; i < n; i++) {
        if (t <= stops[i].pos) {
            float f = (t - stops[i - 1].pos) / fmaxf(stops[i].pos - stops[i - 1].pos, 1e-6f);
            RGBA a = stops[i - 1].color, b = stops[i].color;
            // canvas interpolates premultiplied colours
            float al = a.a + (b.a - a.a) * f;
            RGBA r;
            if (al > 0.f) {
                r.r = (a.r * a.a + (b.r * b.a - a.r * a.a) * f) / al;
                r.g = (a.g * a.a + (b.g * b.a - a.g * a.a) * f) / al;
                r.b = (a.b * a.a + (b.b * b.a - a.b * a.a) * f) / al;
            }
            else
                r.r = r.g = r.b = 0.f;
            r.a = al;
            return r;
        }
    }
    return stops[n - 1].color;
}

void cv_gradient(Canvas *c, float x0, float y0, float x1, float y1, const Stop *stops, int n, Blend blend)
{
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int py = 0; py < c->h; py++) {
        for (int px = 0; px < c->w; px++) {
            float t = (((float) px + 0.5f - x0) * dx + ((float) py + 0.5f - y0) * dy) / len2;
            RGBA col = stop_color(stops, n, t);
            put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, col, 1.f, blend);
        }
    }
}

void cv_radial(Canvas *c, float cx, float cy, float r, const Stop *stops, int n, Blend blend)
{
    int x0 = (int) floorf(cx - r), x1 = (int) ceilf(cx + r);
    int y0 = (int) floorf(cy - r), y1 = (int) ceilf(cy + r);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = (float) px + 0.5f - cx, dy = (float) py + 0.5f - cy;
            float t = sqrtf(dx * dx + dy * dy) / r;
            if (t > 1.f)
                continue;
            put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, stop_color(stops, n, t), 1.f, blend);
        }
    }
}

void cv_mask(Canvas *c, const uint8_t *mask, int mw, int mh, int pitch, float dx, float dy, bool rotate, RGBA color, Blend blend)
{
    int ox = (int) floorf(dx), oy = (int) floorf(dy);
    for (int my = 0; my < mh; my++) {
        for (int mx = 0; mx < mw; mx++) {
            uint8_t m = mask[(size_t) my * (size_t) pitch + (size_t) mx];
            if (!m)
                continue;
            // rotated 90 degrees counter-clockwise: text runs upward, glyph tops face left
            int px = rotate ? ox + my : ox + mx;
            int py = rotate ? oy - mx : oy + my;
            if (px < 0 || py < 0 || px >= c->w || py >= c->h)
                continue;
            put_rgba(c->px + ((size_t) py * (size_t) c->w + (size_t) px) * 4, color, m / 255.f, blend);
        }
    }
}

static void blur_line(float *line, float *tmp, int n, const float *k, int r)
{
    for (int i = 0; i < n; i++) {
        float s = 0.f;
        for (int j = -r; j <= r; j++) {
            int idx = i + j;
            if (idx < 0) idx = 0;
            if (idx >= n) idx = n - 1;
            s += line[idx] * k[j + r];
        }
        tmp[i] = s;
    }
    memcpy(line, tmp, sizeof(float) * (size_t) n);
}

void cv_blur(Canvas *c, float sigma)
{
    int r = (int) ceilf(sigma * 3.f);
    if (r < 1)
        return;
    float *k = malloc(sizeof(float) * (size_t) (2 * r + 1));
    float sum = 0.f;
    for (int i = -r; i <= r; i++) {
        k[i + r] = expf(-(float) (i * i) / (2.f * sigma * sigma));
        sum += k[i + r];
    }
    for (int i = 0; i <= 2 * r; i++)
        k[i] /= sum;
    int n = c->w > c->h ? c->w : c->h;
    float *line = malloc(sizeof(float) * (size_t) n), *tmp = malloc(sizeof(float) * (size_t) n);
    for (int ch = 0; ch < 4; ch++) {
        for (int y = 0; y < c->h; y++) {
            for (int x = 0; x < c->w; x++)
                line[x] = c->px[((size_t) y * (size_t) c->w + (size_t) x) * 4 + (size_t) ch];
            blur_line(line, tmp, c->w, k, r);
            for (int x = 0; x < c->w; x++)
                c->px[((size_t) y * (size_t) c->w + (size_t) x) * 4 + (size_t) ch] = (uint8_t) fminf(line[x] + 0.5f, 255.f);
        }
        for (int x = 0; x < c->w; x++) {
            for (int y = 0; y < c->h; y++)
                line[y] = c->px[((size_t) y * (size_t) c->w + (size_t) x) * 4 + (size_t) ch];
            blur_line(line, tmp, c->h, k, r);
            for (int y = 0; y < c->h; y++)
                c->px[((size_t) y * (size_t) c->w + (size_t) x) * 4 + (size_t) ch] = (uint8_t) fminf(line[y] + 0.5f, 255.f);
        }
    }
    free(line);
    free(tmp);
    free(k);
}
