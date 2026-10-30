#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hdr.h"

static void rgbe_to_float(const unsigned char *rgbe, float *out)
{
    if (rgbe[3] == 0) {
        out[0] = out[1] = out[2] = 0.f;
        return;
    }
    // same scale as three.js RGBELoader, so the room is exposed identically
    float f = ldexpf(1.f, (int) rgbe[3] - 128) / 255.f;
    out[0] = (float) rgbe[0] * f;
    out[1] = (float) rgbe[1] * f;
    out[2] = (float) rgbe[2] * f;
}

// Read one scanline, either flat or new-style run-length encoded
static bool read_scanline(FILE *f, int w, unsigned char *line)
{
    int c0 = fgetc(f), c1 = fgetc(f), c2 = fgetc(f), c3 = fgetc(f);
    if (c3 == EOF)
        return false;
    if (w < 8 || w > 0x7fff || c0 != 2 || c1 != 2 || (c2 & 0x80)) {
        // flat scanline
        line[0] = (unsigned char) c0; line[1] = (unsigned char) c1;
        line[2] = (unsigned char) c2; line[3] = (unsigned char) c3;
        return fread(line + 4, 4, (size_t) w - 1, f) == (size_t) w - 1;
    }
    if (((c2 << 8) | c3) != w)
        return false;
    for (int ch = 0; ch < 4; ch++) {
        int x = 0;
        while (x < w) {
            int count = fgetc(f);
            if (count == EOF)
                return false;
            if (count > 128) {
                count -= 128;
                int v = fgetc(f);
                if (v == EOF || x + count > w)
                    return false;
                for (int i = 0; i < count; i++)
                    line[(x++) * 4 + ch] = (unsigned char) v;
            }
            else {
                if (count == 0 || x + count > w)
                    return false;
                for (int i = 0; i < count; i++) {
                    int v = fgetc(f);
                    if (v == EOF)
                        return false;
                    line[(x++) * 4 + ch] = (unsigned char) v;
                }
            }
        }
    }
    return true;
}

bool hdr_load(const char *path, HdrImage *out)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    char buf[256];
    int w = 0, h = 0;
    bool ok = false;
    while (fgets(buf, sizeof(buf), f) != NULL) {
        if (buf[0] == '-' && sscanf(buf, "-Y %d +X %d", &h, &w) == 2) {
            ok = true;
            break;
        }
    }
    if (!ok || w <= 0 || h <= 0) {
        fclose(f);
        return false;
    }
    unsigned char *line = malloc((size_t) w * 4);
    float *rgb = malloc((size_t) w * (size_t) h * 3 * sizeof(float));
    for (int y = 0; y < h; y++) {
        if (!read_scanline(f, w, line)) {
            free(line);
            free(rgb);
            fclose(f);
            return false;
        }
        for (int x = 0; x < w; x++)
            rgbe_to_float(line + x * 4, rgb + ((size_t) y * (size_t) w + (size_t) x) * 3);
    }
    free(line);
    fclose(f);
    out->w = w;
    out->h = h;
    out->rgb = rgb;
    return true;
}

void hdr_free(HdrImage *img)
{
    free(img->rgb);
    img->rgb = NULL;
}
