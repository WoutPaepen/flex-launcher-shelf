// Background images: slideshow loading and the soft-focus treatment behind the shelf
#pragma once
#include <stdbool.h>
#include <SDL.h>
#include "raster.h"

SDL_Surface *load_next_slideshow_background(Slideshow *slideshow, bool transition);
int load_next_slideshow_background_async(void *data);
// Cover the window, then blur(9px) brightness(0.6) saturate(0.8) like the prototype's photo backdrop
Canvas *prepare_background(SDL_Surface *surface, int width, int height);
