#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>
#include <SDL_image.h>
#include "launcher.h"
#include <launcher_config.h>
#include "image.h"
#include "util.h"
#include "debug.h"
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

extern Config config;
extern State state;

// A function to load the next slideshow background from the struct
SDL_Surface *load_next_slideshow_background(Slideshow *slideshow, bool transition)
{
    SDL_Surface *surface = NULL;
    int initial_index = slideshow->i;
    int attempts = 0;
    do {
        // Increment slideshow background index and load background
        (slideshow->i)++;
        if (slideshow->i >= slideshow->num_images)
            slideshow->i = 0;
        surface = IMG_Load(slideshow->images[slideshow->order[slideshow->i]]);
        
        // If the loaded image has no alpha channel (e.g. JPEG), create one 
        // so that we can have transparency for the background transition
        if (surface != NULL && surface->format->format == SDL_PIXELFORMAT_RGB24 && transition) {
            SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0,
                                   surface->w,
                                   surface->h,
                                   32,
                                   SDL_PIXELFORMAT_ARGB8888
                                );
            Uint32 color = SDL_MapRGBA(tmp->format, 0, 0, 0, 0xFF);
            SDL_FillRect(tmp, NULL, color);
            SDL_BlitSurface(surface, NULL, tmp, NULL);
            SDL_FreeSurface(surface);
            surface = tmp;
            attempts++;
        } 
    } while (surface == NULL && slideshow->i != initial_index && attempts < slideshow->num_images);
    
    // Switch to color background mode if we failed to load any image from the array
    if (surface == NULL) {
        log_error(
            "Could not load any image from slideshow directory %s\n"
            "Changing background to color mode", 
            config.slideshow_directory
        );
        config.background_mode = BACKGROUND_COLOR;
    }

    // If only one image in the entire slideshow array was valid, switch to
    // single image background mode
    else if (slideshow->i == initial_index && surface != NULL) {
        log_error(
            "Could only load one image from slideshow directory %s\n"
            "Changing background to single image mode",
            config.slideshow_directory
        );
        config.background_mode = BACKGROUND_IMAGE;
    }
    return surface;
}

// A function to load a new slideshow background in a separate thread
int load_next_slideshow_background_async(void *data)
{
    Slideshow *slideshow = (Slideshow*) data;
    slideshow->transition_surface = load_next_slideshow_background(slideshow, true);
    state.slideshow_background_rendering = false;
    state.slideshow_background_ready = true;
    return 0;
}


// The backdrop is drawn at 960x540 and stretched, so the blur stays cheap on a low-power GPU
Canvas *prepare_background(SDL_Surface *surface, int width, int height)
{
    if (surface == NULL)
        return NULL;
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (conv == NULL)
        return NULL;
    SDL_LockSurface(conv);
    Canvas *src = cv_from_rgba(conv->pixels, conv->w, conv->h, conv->pitch);
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);

    int w = 960, h = (int) lroundf(960.f * (float) height / (float) width);
    if (h < 1)
        h = 1;
    Canvas *dst = cv_new(w, h);
    cv_clear(dst, rgba(0, 0, 0, 1.f));
    // object-fit: cover, overscanned by 20px on every side so the blurred edge never shows
    float scale = fmaxf((float) (w + 40) / (float) src->w, (float) (h + 40) / (float) src->h);
    float dw = (float) src->w * scale, dh = (float) src->h * scale;
    cv_draw(dst, src, ((float) w - dw) / 2.f, ((float) h - dh) / 2.f, dw, dh, 1.f, BLEND_NORMAL);
    cv_free(src);
    cv_blur(dst, 9.f);
    for (int i = 0; i < w * h; i++) {
        uint8_t *p = dst->px + (size_t) i * 4;
        float r = p[0] * 0.6f, g = p[1] * 0.6f, b = p[2] * 0.6f;
        float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = l + (r - l) * 0.8f; g = l + (g - l) * 0.8f; b = l + (b - l) * 0.8f;
        p[0] = (uint8_t) fminf(fmaxf(r, 0.f), 255.f);
        p[1] = (uint8_t) fminf(fmaxf(g, 0.f), 255.f);
        p[2] = (uint8_t) fminf(fmaxf(b, 0.f), 255.f);
        p[3] = 255;
    }
    return dst;
}
