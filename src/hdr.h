// Radiance .hdr (RGBE) loader for the room environment
#pragma once
#include <stdbool.h>

typedef struct {
    int w, h;
    float *rgb; // linear RGB, rows top to bottom
} HdrImage;

bool hdr_load(const char *path, HdrImage *out);
void hdr_free(HdrImage *img);
