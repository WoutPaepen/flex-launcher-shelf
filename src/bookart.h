// Paints the cloth, foil and labels of every book on the shelf
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raster.h"

#define COVER_W 768
#define COVER_H 1064
#define SPINE_W 192
#define SPINE_H 988
#define BACK_W 256
#define BACK_H 354

typedef struct {
    const char *title;
    const char *note;          // printed under the title on the cover, may be NULL
    const char *icon;          // sticker on the spine
    const char *icon_selected; // sticker on the cover (Selected Icon Override), may be NULL
    RGBA color;                // cloth colour
    bool dark_ink;             // black ink instead of gold foil on light cloth
    bool spine_title;
    float D, T, H;             // cover width, spine thickness, height in world units
    int seed;
} BookArtSpec;

typedef struct {
    Canvas *cover, *cover_rm;  // rm: roughness in green, metalness in blue
    Canvas *spine, *spine_rm;
    Canvas *back;
} BookArt;

typedef struct {
    char font_cover[1024];
    char font_spine[1024];
    char font_note[1024];
    char cloth_color[1024];
    char cloth_rough[1024];
} ArtPaths;

bool art_init(const ArtPaths *paths);
void art_quit(void);
void art_paint(const BookArtSpec *spec, BookArt *out);
void art_paint_spine(const BookArtSpec *spec, BookArt *out);
void art_free(BookArt *art);

Canvas *art_page_texture(void);
Canvas *art_ao_texture(void);
Canvas *art_led_texture(void);
// Load a PNG/JPG/WebP or SVG icon at a square size
Canvas *art_load_icon(const char *path, int size);
// A cloth colour that suits an icon, for entries without a configured colour
RGBA art_color_from_icon(const char *path);
bool art_prefers_dark_ink(RGBA color);
