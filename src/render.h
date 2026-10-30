// OpenGL renderer for the shelf: meshes, materials, lights, shadows, the room
// environment and the 2D overlay. Lighting follows three.js MeshStandardMaterial
// and MeshPhysicalMaterial (clearcoat) so the native build matches the prototype.
#pragma once
#include <stdbool.h>
#include "gl.h"
#include "vmath.h"
#include "raster.h"

typedef struct {
    GLuint vao, vbo;
    int count;
} Mesh;

typedef struct {
    GLuint id;
    int w, h;
} Tex;

typedef struct {
    float color[3];       // linear
    float roughness, metalness;
    GLuint map;           // sRGB colour
    GLuint rough_map;     // roughness in green
    GLuint metal_map;     // metalness in blue
    GLuint normal_map;
    float map_repeat[2], rough_repeat[2], metal_repeat[2], normal_repeat[2];
    float normal_scale;
    float clearcoat, clearcoat_roughness;
    bool receive_shadow;
} Material;

typedef struct {
    vec3 pos, target, color; // colour is linear and multiplied by intensity
    float angle, penumbra, distance, decay;
} SpotLight;

typedef struct {
    vec3 pos, color;
    float distance, decay;
} PointLight;

typedef struct {
    vec3 key_pos, key_target, key_color;
    SpotLight spot[2];
    PointLight point;
    float env_intensity;
    float exposure;
} Lights;

typedef enum {
    BG_CLEAR,   // solid clear colour (also the chroma key)
    BG_ENV,     // the room photograph, slightly out of focus
    BG_PHOTO    // a user image or slideshow, pre-blurred on the CPU
} BackgroundKind;

typedef struct {
    BackgroundKind kind;
    float clear[3];       // sRGB 0..1
    float intensity;
    float blurriness;
    GLuint photo_a, photo_b;
    float photo_mix;
} Background;

typedef struct {
    mat4 view, proj;
    vec3 eye;
} Camera;

typedef struct {
    int msaa;             // samples for the scene buffer, 0 for none
    float scale;          // scene resolution relative to the window
} RenderQuality;

bool render_init(bool gles, int win_w, int win_h, RenderQuality q);
void render_quit(void);
void render_resize(int win_w, int win_h, RenderQuality q);
float render_scale(void);
const char *render_info(void);

// geometry
Mesh mesh_box(float w, float h, float d);
Mesh mesh_rounded_box(float w, float h, float d, int segments, float radius);
Mesh mesh_plane(float w, float h);
void mesh_free(Mesh *m);

// textures
Tex tex_from_canvas(const Canvas *c, bool srgb, bool repeat, bool mipmap);
Tex tex_from_file(const char *path, bool srgb, bool repeat);
void tex_free(Tex *t);
// Room environment: neutralised and prefiltered for every roughness
bool render_load_environment(const char *hdr_path, float saturation, float red, float blue);

// frame
void render_shadow_begin(const Lights *l);
void render_shadow_mesh(const Mesh *m, mat4 model);
void render_shadow_end(void);
void render_scene_begin(const Camera *cam, const Lights *l, const Background *bg);
void render_mesh(const Mesh *m, const Material *mat, mat4 model);
void render_decal(const Mesh *m, GLuint alpha_map, const float color[3], float opacity, mat4 model);
void render_additive(const Mesh *m, GLuint map, const float color[3], mat4 model);
void render_scene_end(void);

// 2D overlay in window pixels, origin top-left. Colours are sRGB 0..1.
void render_overlay_begin(void);
void render_overlay_scene(float alpha);
void render_overlay_scrim(float alpha);
void render_overlay_rect(float x, float y, float w, float h, float radius, const float fill[4], float border, const float border_color[4]);
void render_overlay_tex(GLuint tex, float x, float y, float w, float h, const float color[4], bool mask);
void render_overlay_end(void);

bool render_read_pixels(int *w, int *h, unsigned char **rgba); // top-down RGBA of the window
