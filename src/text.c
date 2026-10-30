#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "text.h"

typedef struct font_entry {
    char *path;
    int size;
    TTF_Font *font;
    struct font_entry *next;
} FontEntry;

static FontEntry *fonts = NULL;

TTF_Font *font_get(const char *path, int size)
{
    if (size < 1)
        size = 1;
    for (FontEntry *f = fonts; f != NULL; f = f->next) {
        if (f->size == size && !strcmp(f->path, path))
            return f->font;
    }
    TTF_Font *font = TTF_OpenFont(path, size);
    if (font == NULL)
        return NULL;
    // unhinted outlines match browser text rendering most closely
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    FontEntry *e = malloc(sizeof(FontEntry));
    e->path = strdup(path);
    e->size = size;
    e->font = font;
    e->next = fonts;
    fonts = e;
    return font;
}

void font_cache_clear(void)
{
    for (FontEntry *f = fonts; f != NULL;) {
        FontEntry *n = f->next;
        TTF_CloseFont(f->font);
        free(f->path);
        free(f);
        f = n;
    }
    fonts = NULL;
}

// Decode one UTF-8 code point, return bytes consumed
static int utf8_next(const char *s, Uint32 *cp)
{
    const unsigned char *u = (const unsigned char*) s;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && u[1]) { *cp = ((Uint32) (u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { *cp = ((Uint32) (u[0] & 0x0F) << 12) | ((Uint32) (u[1] & 0x3F) << 6) | (u[2] & 0x3F); return 3; }
    if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) { *cp = ((Uint32) (u[0] & 0x07) << 18) | ((Uint32) (u[1] & 0x3F) << 12) | ((Uint32) (u[2] & 0x3F) << 6) | (u[3] & 0x3F); return 4; }
    *cp = '?';
    return 1;
}

int text_width(TTF_Font *font, const char *text, float tracking)
{
    if (font == NULL || text == NULL || *text == '\0')
        return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text, &w, &h);
    if (tracking != 0.f) {
        int n = 0;
        Uint32 cp;
        for (const char *p = text; *p; p += utf8_next(p, &cp))
            n++;
        w += (int) lroundf(tracking * (float) n);
    }
    return w;
}

static void copy_alpha(SDL_Surface *s, uint8_t *dst, int dw, int dh, int ox)
{
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    if (conv == NULL)
        return;
    SDL_LockSurface(conv);
    for (int y = 0; y < conv->h && y < dh; y++) {
        const uint8_t *row = (const uint8_t*) conv->pixels + (size_t) y * (size_t) conv->pitch;
        for (int x = 0; x < conv->w; x++) {
            int tx = ox + x;
            if (tx < 0 || tx >= dw)
                continue;
            uint8_t a = row[x * 4 + 3];
            uint8_t *d = dst + (size_t) y * (size_t) dw + (size_t) tx;
            int v = *d + a;
            *d = (uint8_t) (v > 255 ? 255 : v);
        }
    }
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
}

TextMask text_render(TTF_Font *font, const char *text, float tracking)
{
    TextMask m = { 0, 0, NULL, 0 };
    if (font == NULL || text == NULL || *text == '\0')
        return m;
    SDL_Color white = { 255, 255, 255, 255 };
    m.ascent = TTF_FontAscent(font);
    m.h = TTF_FontHeight(font);
    if (tracking == 0.f) {
        SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, white);
        if (s == NULL)
            return m;
        m.w = s->w;
        m.h = s->h;
        m.a = calloc((size_t) m.w * (size_t) m.h, 1);
        copy_alpha(s, m.a, m.w, m.h, 0);
        SDL_FreeSurface(s);
        return m;
    }

    // letter-spaced text: place glyphs one by one
    m.w = text_width(font, text, tracking) + m.h;
    m.a = calloc((size_t) m.w * (size_t) m.h, 1);
    float pen = 0.f;
    Uint32 cp;
    char glyph[5];
    for (const char *p = text; *p;) {
        int n = utf8_next(p, &cp);
        memcpy(glyph, p, (size_t) n);
        glyph[n] = '\0';
        p += n;
        SDL_Surface *s = TTF_RenderUTF8_Blended(font, glyph, white);
        int adv = 0, hh = 0;
        TTF_SizeUTF8(font, glyph, &adv, &hh);
        if (s != NULL) {
            copy_alpha(s, m.a, m.w, m.h, (int) lroundf(pen));
            SDL_FreeSurface(s);
        }
        pen += (float) adv + tracking;
    }
    // trim the scratch width down to the laid-out advance
    int w = (int) ceilf(pen > 1.f ? pen : 1.f);
    if (w < m.w) {
        uint8_t *tight = malloc((size_t) w * (size_t) m.h);
        for (int y = 0; y < m.h; y++)
            memcpy(tight + (size_t) y * (size_t) w, m.a + (size_t) y * (size_t) m.w, (size_t) w);
        free(m.a);
        m.a = tight;
        m.w = w;
    }
    return m;
}

void text_free(TextMask *m)
{
    free(m->a);
    m->a = NULL;
}
