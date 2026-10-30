// Text rasterization on top of SDL_ttf: coverage masks with optional letter spacing
#pragma once
#include <stdint.h>
#include <SDL_ttf.h>

typedef struct {
    int w, h;
    uint8_t *a;   // coverage 0..255, w*h
    int ascent;   // distance from the top of the mask to the baseline
} TextMask;

// Fonts are cached by path and pixel size and live for the whole run
TTF_Font *font_get(const char *path, int size);
void font_cache_clear(void);
// Advance width of a string in pixels; tracking is extra space after every glyph
int text_width(TTF_Font *font, const char *text, float tracking);
TextMask text_render(TTF_Font *font, const char *text, float tracking);
void text_free(TextMask *m);
