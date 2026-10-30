// The 3D bookshelf: books, sideboard, wall and lights, with spring-driven layout
#pragma once
#include <stdbool.h>
#include "raster.h"

typedef enum {
    SHELF_BG_WALL,        // graphite wall, coloured by the bias light
    SHELF_BG_COLOR,       // wall painted in the configured colour
    SHELF_BG_ROOM,        // the photographed room, out of focus
    SHELF_BG_PHOTO,       // image or slideshow behind the shelf
    SHELF_BG_TRANSPARENT  // chroma key colour
} ShelfBackground;

typedef struct {
    char assets[1024];    // directory holding fonts/, icons/, textures/, env/
    char room_hdr[1024];  // optional override for the room photograph
    ShelfBackground background;
    float wall_color[3];  // sRGB 0..255 for SHELF_BG_COLOR
    float chroma_key[3];  // sRGB 0..255
    float overlay;        // 1 = no overlay, 0.5 = darkened by half
    int per_page;
    bool spine_titles;
    int msaa;
    float scale;
} ShelfConfig;

typedef struct {
    const char *title;
    const char *note;
    const char *icon;
    const char *icon_selected;
    RGBA color;
    bool has_color;
    int ink;              // -1 auto, 0 foil, 1 dark ink
} ShelfBook;

bool shelf_init(const ShelfConfig *cfg, int win_w, int win_h, bool gles);
void shelf_quit(void);
void shelf_resize(int win_w, int win_h);
// Paint and upload the books of one menu; menus are added in config order
int shelf_add_menu(const ShelfBook *books, int count);
int shelf_per_page(void);

void shelf_show_menu(int menu, int focus);                 // instantly, at startup
void shelf_swap_menu(int menu, int focus, int direction);  // slide the new shelf in
void shelf_enter(int menu, int focus);                     // the first shelf arrives after loading
void shelf_focus(int focus);
void shelf_bump(int direction);
void shelf_launch_pull(bool pulled);
void shelf_launch_open(void);   // swing the cover open and push into the first page
void shelf_launch_close(void);  // pull back, close the cover, return the book
float shelf_zoom(void);         // 1 when the open page fills the screen
void shelf_dim(float amount, bool dimmed); // screensaver: lights go down
void shelf_set_photo(const Canvas *blurred, bool crossfade);
void shelf_set_photo_mix(float mix);

// Advance springs; returns true while anything is still moving
bool shelf_update(float dt);
void shelf_render(void);
