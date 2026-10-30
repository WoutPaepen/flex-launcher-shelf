// CPU raster helpers for painting book covers, spines and overlay pieces.
// They follow HTML canvas semantics: straight (non-premultiplied) RGBA in sRGB,
// with source-over, overlay, screen, multiply and destination-in compositing.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int w, h;
    uint8_t *px; // RGBA, rows top to bottom
} Canvas;

typedef struct { float r, g, b, a; } RGBA; // r,g,b 0..255, a 0..1

typedef enum {
    BLEND_NORMAL,
    BLEND_OVERLAY,
    BLEND_SCREEN,
    BLEND_MULTIPLY,
    BLEND_DEST_IN
} Blend;

typedef struct {
    float pos;
    RGBA color;
} Stop;

Canvas *cv_new(int w, int h);
void cv_free(Canvas *c);
Canvas *cv_copy(const Canvas *c);
Canvas *cv_from_rgba(const uint8_t *rgba, int w, int h, int pitch);

void cv_clear(Canvas *c, RGBA color);
void cv_rect(Canvas *c, float x, float y, float w, float h, RGBA color, Blend blend);
void cv_circle(Canvas *c, float cx, float cy, float r, RGBA color, Blend blend);
// A soft circle like a canvas shadow: gaussian edge with the given sigma
void cv_circle_soft(Canvas *c, float cx, float cy, float r, float sigma, RGBA color);
// Tile src over the whole canvas: canvas = translate(tx, ty) * scale(s) * src
void cv_pattern(Canvas *dst, const Canvas *src, float tx, float ty, float s, float alpha, Blend blend);
// Stretch src into a rectangle with bilinear filtering
void cv_draw(Canvas *dst, const Canvas *src, float dx, float dy, float dw, float dh, float alpha, Blend blend);
// Draw src into the square around (cx, cy) with half size r, clipped to a circle of radius r
void cv_draw_circle_clip(Canvas *dst, const Canvas *src, float cx, float cy, float r);
void cv_gradient(Canvas *c, float x0, float y0, float x1, float y1, const Stop *stops, int n, Blend blend);
void cv_radial(Canvas *c, float cx, float cy, float r, const Stop *stops, int n, Blend blend);
// Paint a coverage mask (0..255) in a colour. With rotate set the mask is turned 90 degrees
// counter-clockwise, so text runs from bottom to top like a book spine.
void cv_mask(Canvas *c, const uint8_t *mask, int mw, int mh, int pitch, float dx, float dy, bool rotate, RGBA color, Blend blend);
// Separable gaussian blur on every channel
void cv_blur(Canvas *c, float sigma);

static inline RGBA rgba(float r, float g, float b, float a) { RGBA c = { r, g, b, a }; return c; }
RGBA rgba_hex(const char *hex, float a);

// Deterministic random numbers for paint (xorshift)
void rng_seed(uint32_t seed);
float rng(void);
