// The flat layer over the shelf: menu trail, clock, page rail, key hints,
// the status pill, the failure toast and the blank screen shown while launching
#pragma once
#include <stdbool.h>

bool overlay_init(const char *assets);
void overlay_quit(void);
void overlay_resize(int win_w, int win_h, int per_page);

// The menu trail, parents first; the last one is the menu on screen
void overlay_set_trail(const char **menus, int count);
void overlay_set_rail(int count, int focus, int per_page);
void overlay_set_clock(bool enabled, bool show_date);
void overlay_set_time(const char *time, const char *period, const char *date);
void overlay_set_scrim(bool enabled);

void overlay_status(const char *text);                     // NULL hides it
void overlay_toast(const char *title, const char *body, const char *code);
void overlay_blank(const char *icon, const char *title, const char *sub, bool bar);
// The same, printed on the open book filling the screen: no backdrop of its own
void overlay_blank_page(const char *icon, const char *title, const char *sub, bool bar);
// The menu trail, clock, rail and hints step back while the camera is inside a book
void overlay_set_hud(float alpha);
void overlay_unblank(void);
bool overlay_blanked(void);                                // fully covering the shelf
void overlay_dim(bool dimmed);
void overlay_loading(bool loading);

// Advance transitions; returns true while anything is still animating
bool overlay_update(float dt);
// Scene alpha for the fade-in when the shelf first appears
float overlay_scene_alpha(void);
void overlay_draw(void);
