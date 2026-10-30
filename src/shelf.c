#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include "shelf.h"
#include "render.h"
#include "bookart.h"
#include "spring.h"
#include "vmath.h"

#define MAX_MENUS 64
#define FOCUS_GAP 0.5f
#define GAP 0.03f
#define PAGE_GAP 4.6f   // pages sit apart on one long shelf, so a neighbouring page stays out of frame
#define SWAP 12.f       // how far a shelf slides when the menu changes
#define BOARD 0.06f
#define SQUARE 0.07f
#define WALL_PAINT "#72757a"
#define WASH 60.f       // candela; the dark graphite paint takes a strong bar to show colour

typedef struct {
    float T, H, D, tilt, yaw;
    RGBA color;
    Tex cover, cover_rm, spine, spine_rm, back;
    Mesh front_m, back_m, spine_m, pages_m, ao_m;
    Material cover_mat, back_mat, spine_mat;
    Spring x, z, rot, lean, lift, tz;
    Spring open;          // 0 closed, 1 the front cover swung all the way back
    Tex endpaper;         // painted the first time the book is opened
    Material endpaper_mat;
    Mesh inner_m, leaf_m;
} Book;

typedef struct {
    Spring lx, rx;
    bool visible;
} Ends;

typedef struct {
    Book *books;
    int count;
    Ends *ends;
    int ends_count;
    Spring offset, slide;
    bool visible;
} Row;

static struct {
    ShelfConfig cfg;
    int win_w, win_h;
    int per_page;
    Row rows[MAX_MENUS];
    int row_count;
    int menu, focus;
    Tex page_tex, ao_tex, led_tex;
    Tex oak_diff, oak_nor, oak_rough, wall_diff, wall_nor, wall_rough, cloth_nor;
    Tex photo_a, photo_b;
    float photo_mix;
    Mesh top_m, body_m, channel_m, wall_m, led_m, end_base_m, end_up_m;
    Material top_mat, body_mat, channel_mat, wall_mat, end_mat, page_mat;
    Spring exposure, lamp_power, look_x, led_w;
    Spring zoom;          // 0 the shelf, 1 the open book's first page filling the screen
    bool opening;         // the chosen book is being opened for a launch
    bool closing;
    float wash[3], wash_target[3]; // linear
    float dim_exposure;
    Camera cam;
    float cam_dist, cam_c;
    bool env_ok;
} S;

// ---------- helpers ----------

static double rand_seed(double seed)
{
    double x = sin(seed * 999.13) * 43758.5453;
    return x - floor(x);
}

static void hex_linear(const char *hex, float out[3])
{
    RGBA c = rgba_hex(hex, 1.f);
    out[0] = srgb_to_linear(c.r);
    out[1] = srgb_to_linear(c.g);
    out[2] = srgb_to_linear(c.b);
}

static void rgba_linear(RGBA c, float out[3])
{
    out[0] = srgb_to_linear(c.r);
    out[1] = srgb_to_linear(c.g);
    out[2] = srgb_to_linear(c.b);
}

static Material material(const char *hex, float roughness, float metalness)
{
    Material m;
    memset(&m, 0, sizeof(m));
    hex_linear(hex, m.color);
    m.roughness = roughness;
    m.metalness = metalness;
    m.map_repeat[0] = m.map_repeat[1] = 1.f;
    m.rough_repeat[0] = m.rough_repeat[1] = 1.f;
    m.metal_repeat[0] = m.metal_repeat[1] = 1.f;
    m.normal_repeat[0] = m.normal_repeat[1] = 1.f;
    m.normal_scale = 1.f;
    return m;
}

static void set_repeat(float r[2], float x, float y)
{
    r[0] = x;
    r[1] = y;
}

static void asset(char *buf, size_t n, const char *rel)
{
    snprintf(buf, n, "%s%s%s", S.cfg.assets, S.cfg.assets[strlen(S.cfg.assets) - 1] == '/' ? "" : "/", rel);
}

static void rgb_to_hsl(const float c[3], float *h, float *s, float *l)
{
    float mx = fmaxf(c[0], fmaxf(c[1], c[2])), mn = fminf(c[0], fminf(c[1], c[2]));
    *l = (mx + mn) / 2.f;
    if (mx == mn) { *h = *s = 0.f; return; }
    float d = mx - mn;
    *s = *l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == c[0]) *h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6.f : 0.f);
    else if (mx == c[1]) *h = (c[2] - c[0]) / d + 2.f;
    else *h = (c[0] - c[1]) / d + 4.f;
    *h /= 6.f;
}

static float hue(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

// Dark covers still need to read as a colour on the wall: floor saturation and lightness
static void wash_from(RGBA c, float out[3])
{
    float lin[3], h, s, l;
    rgba_linear(c, lin);
    rgb_to_hsl(lin, &h, &s, &l);
    s = fmaxf(s, 0.55f);
    l = fmaxf(l, 0.5f);
    float q = l < 0.5f ? l * (1.f + s) : l + s - l * s, p = 2.f * l - q;
    out[0] = hue(p, q, h + 1.f / 3.f);
    out[1] = hue(p, q, h);
    out[2] = hue(p, q, h - 1.f / 3.f);
}

static bool walled(void)
{
    return S.cfg.background == SHELF_BG_WALL || S.cfg.background == SHELF_BG_COLOR;
}

// ---------- layout ----------

static float layout_row(Row *row, int focus)
{
    int n = row->count;
    int p = focus / S.per_page, pages = (n + S.per_page - 1) / S.per_page;
    float *xs = malloc(sizeof(float) * (size_t) n);
    float x = 0.f, edge_l = 0.f, edge_r = 0.f;
    for (int pg = 0; pg < pages; pg++) {
        if (pg > 0)
            x += PAGE_GAP;
        float left = x;
        for (int k = pg * S.per_page; k < n && k < (pg + 1) * S.per_page; k++) {
            Book *b = &row->books[k];
            float w = k == focus ? b->D + FOCUS_GAP * 2.f : b->T + GAP;
            xs[k] = x + w / 2.f;
            x += w;
        }
        if (pg < row->ends_count) {
            row->ends[pg].visible = true;
            row->ends[pg].lx.t = left - 0.08f;
            row->ends[pg].rx.t = x + 0.08f;
        }
        if (pg == p) {
            edge_l = left;
            edge_r = x;
        }
    }
    for (int pg = pages; pg < row->ends_count; pg++)
        row->ends[pg].visible = false;
    row->offset.t = -(edge_l + edge_r) / 2.f;
    for (int k = 0; k < n; k++) {
        Book *b = &row->books[k];
        bool on = k == focus;
        b->x.t = xs[k];
        b->z.t = on ? 1.55f : 0.f;              // pulled toward you
        b->rot.t = on ? -VM_PI / 2.f : b->yaw;  // turned face-out
        b->lean.t = on ? -0.07f : 0.f;          // propped back a touch
        b->tz.t = on ? 0.f : b->tilt;
    }
    float fx = xs[focus] + row->offset.t;
    free(xs);
    return fx;
}

static void update_layout(bool immediate)
{
    if (S.row_count == 0)
        return;
    Row *row = &S.rows[S.menu];
    if (S.focus >= row->count)
        S.focus = row->count - 1;
    if (S.focus < 0)
        S.focus = 0;
    Book *b = &row->books[S.focus];
    float fx = layout_row(row, S.focus);
    S.look_x.t = fx * 0.06f;
    S.led_w.t = b->D * 0.86f;
    wash_from(b->color, S.wash_target);
    if (immediate) {
        for (int k = 0; k < row->count; k++) {
            Book *bk = &row->books[k];
            spring_snap(&bk->x); spring_snap(&bk->z); spring_snap(&bk->rot); spring_snap(&bk->lean); spring_snap(&bk->tz);
        }
        for (int e = 0; e < row->ends_count; e++) {
            spring_snap(&row->ends[e].lx);
            spring_snap(&row->ends[e].rx);
        }
        spring_snap(&row->offset);
        spring_snap(&S.look_x);
        spring_snap(&S.led_w);
        memcpy(S.wash, S.wash_target, sizeof(S.wash));
    }
}

// ---------- setup ----------

static void camera_fit(void)
{
    float W = (float) S.win_w, H = (float) S.win_h, aspect = W / H;
    S.per_page = W < H * 0.95f ? 3 : S.cfg.per_page;
    float span = 2.6f + FOCUS_GAP * 2.f + (float) (S.per_page - 1) * 0.78f + 2.4f;
    float tan_h = tanf(12.f * VM_PI / 180.f);
    S.cam_dist = fmaxf(span / (2.f * tan_h * aspect), 7.6f / (2.f * tan_h));
    float half = S.cam_dist * tan_h;
    // portrait: sit the shelf nearer the middle of the frame
    S.cam_c = W < H ? half * 0.32f + 1.2f : half * 0.5f + 0.05f;
    S.cam.proj = m4_perspective(24.f, aspect, 0.5f, 200.f);
}

bool shelf_init(const ShelfConfig *cfg, int win_w, int win_h, bool gles)
{
    memset(&S, 0, sizeof(S));
    S.cfg = *cfg;
    S.win_w = win_w;
    S.win_h = win_h;
    RenderQuality q = { cfg->msaa, cfg->scale };
    if (!render_init(gles, win_w, win_h, q))
        return false;
    camera_fit();

    char path[1200];
    if (cfg->room_hdr[0] != '\0')
        snprintf(path, sizeof(path), "%s", cfg->room_hdr);
    else
        asset(path, sizeof(path), "env/fireplace_2k.hdr");
    // the lounge photo is lit amber; pull it toward neutral graphite so the lamp stays the warm light
    S.env_ok = render_load_environment(path, 0.28f, 0.97f, 1.08f);

    ArtPaths ap;
    asset(ap.font_cover, sizeof(ap.font_cover), "fonts/BigShouldersDisplay-ExtraBold.ttf");
    asset(ap.font_spine, sizeof(ap.font_spine), "fonts/BigShouldersDisplay-Bold.ttf");
    asset(ap.font_note, sizeof(ap.font_note), "fonts/SchibstedGrotesk-SemiBold.ttf");
    asset(ap.cloth_color, sizeof(ap.cloth_color), "textures/cloth_col.jpg");
    asset(ap.cloth_rough, sizeof(ap.cloth_rough), "textures/cloth_rough.jpg");
    if (!art_init(&ap))
        return false;

    Canvas *c = art_page_texture();
    S.page_tex = tex_from_canvas(c, true, true, true);
    cv_free(c);
    c = art_ao_texture();
    S.ao_tex = tex_from_canvas(c, false, false, true);
    cv_free(c);
    c = art_led_texture();
    S.led_tex = tex_from_canvas(c, false, false, false);
    cv_free(c);

    asset(path, sizeof(path), "textures/oak_diff_h.jpg");
    S.oak_diff = tex_from_file(path, true, true);
    asset(path, sizeof(path), "textures/oak_nor_h.jpg");
    S.oak_nor = tex_from_file(path, false, true);
    asset(path, sizeof(path), "textures/oak_rough_h.jpg");
    S.oak_rough = tex_from_file(path, false, true);
    asset(path, sizeof(path), "textures/wall_diff.jpg");
    S.wall_diff = tex_from_file(path, true, true);
    asset(path, sizeof(path), "textures/wall_nor.jpg");
    S.wall_nor = tex_from_file(path, false, true);
    asset(path, sizeof(path), "textures/wall_rough.jpg");
    S.wall_rough = tex_from_file(path, false, true);
    asset(path, sizeof(path), "textures/cloth_nor.jpg");
    S.cloth_nor = tex_from_file(path, false, true);

    // oiled oak sideboard: a 3-unit tile so the grain reads at 1080p
    S.top_m = mesh_rounded_box(60.f, 0.42f, 5.2f, 3, 0.05f);
    S.top_mat = material("#a5806a", 0.9f, 0.f);
    S.top_mat.map = S.oak_diff.id; S.top_mat.normal_map = S.oak_nor.id; S.top_mat.rough_map = S.oak_rough.id;
    set_repeat(S.top_mat.map_repeat, 20.f, 1.75f);
    set_repeat(S.top_mat.normal_repeat, 20.f, 1.75f);
    set_repeat(S.top_mat.rough_repeat, 20.f, 1.75f);
    S.top_mat.normal_scale = 1.2f;
    S.top_mat.clearcoat = 0.35f;
    S.top_mat.clearcoat_roughness = 0.32f;
    S.top_mat.receive_shadow = true;

    S.body_m = mesh_box(60.f, 6.f, 4.8f);
    S.body_mat = material("#4a3426", 0.8f, 0.f);
    S.body_mat.map = S.oak_diff.id; S.body_mat.rough_map = S.oak_rough.id;
    set_repeat(S.body_mat.map_repeat, 20.f, 2.f);
    set_repeat(S.body_mat.rough_repeat, 20.f, 2.f);
    S.body_mat.receive_shadow = true;

    // the channel routed into the lip; only the segment under the chosen book lights
    S.channel_m = mesh_box(60.f, 0.075f, 0.03f);
    S.channel_mat = material("#0b0806", 0.9f, 0.f);
    S.led_m = mesh_plane(1.f, 0.05f);

    // plaster wall right behind the sideboard
    S.wall_m = mesh_plane(70.f, 34.f);
    S.wall_mat = material(WALL_PAINT, 1.f, 0.f);
    S.wall_mat.map = S.wall_diff.id; S.wall_mat.normal_map = S.wall_nor.id; S.wall_mat.rough_map = S.wall_rough.id;
    set_repeat(S.wall_mat.map_repeat, 70.f / 3.5f, 34.f / 3.5f);
    set_repeat(S.wall_mat.normal_repeat, 70.f / 3.5f, 34.f / 3.5f);
    set_repeat(S.wall_mat.rough_repeat, 70.f / 3.5f, 34.f / 3.5f);
    S.wall_mat.normal_scale = 0.7f;
    S.wall_mat.receive_shadow = true;
    if (cfg->background == SHELF_BG_COLOR) {
        RGBA wc = rgba(cfg->wall_color[0], cfg->wall_color[1], cfg->wall_color[2], 1.f);
        rgba_linear(wc, S.wall_mat.color);
    }
    for (int i = 0; i < 3; i++)
        S.wall_mat.color[i] *= cfg->overlay;

    // powder-coated steel bookends
    S.end_base_m = mesh_rounded_box(1.2f, 0.05f, 2.0f, 2, 0.02f);
    S.end_up_m = mesh_rounded_box(0.07f, 2.4f, 2.0f, 2, 0.03f);
    S.end_mat = material("#141416", 0.62f, 0.25f);
    S.end_mat.receive_shadow = true;

    S.page_mat = material("#ffffff", 0.95f, 0.f);
    S.page_mat.map = S.page_tex.id;
    S.page_mat.receive_shadow = true;

    S.exposure = spring_make(0.92f, 40.f, 1.f);
    S.lamp_power = spring_make(0.f, 60.f, 1.f);
    S.lamp_power.t = 1.f;
    S.look_x = spring_make(0.f, 60.f, 1.f);
    S.led_w = spring_make(2.f, 110.f, 0.9f);
    S.dim_exposure = 0.92f;
    // critically damped, so the push into the page never overshoots the paper
    S.zoom = spring_make(0.f, 30.f, 1.f);
    return true;
}

static Book make_book(const ShelfBook *e, int i, int menu_seed)
{
    Book b;
    memset(&b, 0, sizeof(b));
    int seed = i * 17 + menu_seed;
    b.T = 0.62f + (float) rand_seed(seed) * 0.22f;     // spine thickness
    b.H = 3.42f + (float) rand_seed(seed + 1) * 0.34f; // height
    b.D = 2.6f;                                        // cover width
    b.tilt = ((float) rand_seed(seed + 5) - 0.5f) * 0.022f; // books never stand dead straight
    b.yaw = ((float) rand_seed(seed + 6) - 0.5f) * 0.04f;
    b.color = e->has_color ? e->color : art_color_from_icon(e->icon);

    BookArtSpec spec = {
        .title = e->title, .note = e->note, .icon = e->icon, .icon_selected = e->icon_selected,
        .color = b.color,
        .dark_ink = e->ink < 0 ? art_prefers_dark_ink(b.color) : e->ink == 1,
        .spine_title = S.cfg.spine_titles,
        .D = b.D, .T = b.T, .H = b.H, .seed = seed
    };
    BookArt art;
    art_paint(&spec, &art);
    b.cover = tex_from_canvas(art.cover, true, false, true);
    b.cover_rm = tex_from_canvas(art.cover_rm, false, false, true);
    b.spine = tex_from_canvas(art.spine, true, false, true);
    b.spine_rm = tex_from_canvas(art.spine_rm, false, false, true);
    b.back = tex_from_canvas(art.back, true, false, true);
    art_free(&art);

    b.front_m = mesh_rounded_box(BOARD, b.H, b.D, 3, 0.028f);
    b.back_m = mesh_rounded_box(BOARD, b.H, b.D, 3, 0.028f);
    b.spine_m = mesh_rounded_box(b.T + 0.02f, b.H - 0.004f, 0.16f, 6, 0.075f);
    b.pages_m = mesh_box(b.T - BOARD * 2.f - 0.012f, b.H - SQUARE * 2.f, b.D - SQUARE - 0.12f);
    b.ao_m = mesh_plane(b.T * 1.5f + 0.34f, b.D * 1.16f + 0.34f);

    b.cover_mat = material("#ffffff", 1.f, 1.f);
    b.cover_mat.map = b.cover.id;
    b.cover_mat.rough_map = b.cover_rm.id;
    b.cover_mat.metal_map = b.cover_rm.id;
    b.cover_mat.normal_map = S.cloth_nor.id;
    set_repeat(b.cover_mat.normal_repeat, b.D / 4.5f, b.H / 4.5f);
    b.cover_mat.normal_scale = 0.9f;
    b.cover_mat.receive_shadow = true;
    b.spine_mat = b.cover_mat;
    b.spine_mat.map = b.spine.id;
    b.spine_mat.rough_map = b.spine_rm.id;
    b.spine_mat.metal_map = b.spine_rm.id;
    set_repeat(b.spine_mat.normal_repeat, b.T / 4.5f, b.H / 4.5f);
    b.back_mat = material("#ffffff", 0.86f, 0.f);
    b.back_mat.map = b.back.id;
    b.back_mat.normal_map = S.cloth_nor.id;
    set_repeat(b.back_mat.normal_repeat, b.D / 4.5f, b.H / 4.5f);
    b.back_mat.normal_scale = 0.9f;
    b.back_mat.receive_shadow = true;

    // z leads the turn, the way a hand pulls a book before turning it
    b.x = spring_make(0.f, 120.f, 0.86f);
    b.z = spring_make(0.f, 175.f, 0.82f);
    b.rot = spring_make(0.f, 105.f, 0.8f);
    b.lean = spring_make(0.f, 90.f, 0.72f);
    b.lift = spring_make(0.f, 160.f, 0.8f);
    b.tz = spring_make(0.f, 120.f, 0.9f);
    // the cover swings with a little weight, then settles
    b.open = spring_make(0.f, 70.f, 0.86f);
    b.inner_m = mesh_plane(b.D - 0.07f, b.H - 0.07f);
    b.leaf_m = mesh_plane(b.D - SQUARE - 0.13f, b.H - SQUARE * 2.f - 0.01f);
    return b;
}

int shelf_add_menu(const ShelfBook *books, int count)
{
    if (S.row_count >= MAX_MENUS || count <= 0)
        return -1;
    int idx = S.row_count++;
    Row *row = &S.rows[idx];
    memset(row, 0, sizeof(Row));
    row->books = calloc((size_t) count, sizeof(Book));
    row->count = count;
    int menu_seed = (idx + 1) * 101;
    for (int i = 0; i < count; i++)
        row->books[i] = make_book(&books[i], i, menu_seed);
    // one pair of bookends per page; they travel with their page
    row->ends_count = (count + 2) / 3;
    row->ends = calloc((size_t) row->ends_count, sizeof(Ends));
    for (int e = 0; e < row->ends_count; e++) {
        row->ends[e].lx = spring_make(0.f, 120.f, 0.86f);
        row->ends[e].rx = spring_make(0.f, 120.f, 0.86f);
    }
    row->offset = spring_make(0.f, 95.f, 0.92f);
    row->slide = spring_make(SWAP, 140.f, 0.9f);
    row->visible = false;
    return idx;
}

int shelf_per_page(void)
{
    return S.per_page;
}

void shelf_resize(int win_w, int win_h)
{
    S.win_w = win_w;
    S.win_h = win_h;
    RenderQuality q = { S.cfg.msaa, S.cfg.scale };
    render_resize(win_w, win_h, q);
    camera_fit();
    update_layout(true);
}

void shelf_show_menu(int menu, int focus)
{
    for (int i = 0; i < S.row_count; i++) {
        S.rows[i].visible = i == menu;
        S.rows[i].slide = spring_make(i == menu ? 0.f : SWAP, 140.f, 0.9f);
    }
    S.menu = menu;
    S.focus = focus;
    update_layout(true);
}

static void place_row(Row *row, mat4 *group);

void shelf_swap_menu(int menu, int focus, int direction)
{
    if (menu < 0 || menu >= S.row_count)
        return;
    Row *a = &S.rows[S.menu], *b = &S.rows[menu];
    b->visible = true;
    // the incoming row starts close in and stiff, so it lands while the old one is still leaving
    b->slide.v = b->slide.t = SWAP * 0.85f * (float) direction;
    b->slide.vel = 0.f;
    b->slide.t = 0.f;
    if (a != b)
        a->slide.t = -SWAP * (float) direction;
    S.menu = menu;
    S.focus = focus;
    update_layout(false);
    for (int k = 0; k < b->count; k++) {
        Book *bk = &b->books[k];
        spring_snap(&bk->x); spring_snap(&bk->z); spring_snap(&bk->rot); spring_snap(&bk->lean); spring_snap(&bk->tz);
        bk->x.v += 0.35f * (float) direction * (float) (k + 1);
    }
    spring_snap(&b->offset);
    for (int e = 0; e < b->ends_count; e++) {
        spring_snap(&b->ends[e].lx);
        spring_snap(&b->ends[e].rx);
    }
}

void shelf_enter(int menu, int focus)
{
    for (int i = 0; i < S.row_count; i++)
        S.rows[i].visible = false;
    S.menu = menu;
    shelf_swap_menu(menu, focus, 1);
}

void shelf_focus(int focus)
{
    S.focus = focus;
    update_layout(false);
}

void shelf_bump(int direction)
{
    if (S.row_count == 0)
        return;
    S.rows[S.menu].books[S.focus].x.vel += (float) direction * 1.6f;
}

void shelf_launch_pull(bool pulled)
{
    Book *b = &S.rows[S.menu].books[S.focus];
    if (pulled) {
        b->z.t = 3.0f;
        b->lean.t = 0.f;
        b->lift.t = 0.28f;
        S.lamp_power.t = 1.5f;
    }
    else {
        b->lift.t = 0.f;
        S.lamp_power.t = 1.f;
        update_layout(false);
    }
}

// Endpapers: a matt paper in a deeper tint of the cloth, with a little grain
static void paint_endpaper(Book *b)
{
    if (b->endpaper.id)
        return;
    Canvas *c = cv_new(512, 704);
    RGBA base = rgba(b->color.r * 0.52f + 10.f, b->color.g * 0.52f + 8.f, b->color.b * 0.52f + 6.f, 1.f);
    cv_clear(c, base);
    rng_seed((uint32_t) (b->color.r * 7.f + b->color.g * 13.f + b->color.b * 31.f) + 17u);
    for (int i = 0; i < 9000; i++) {
        float x = rng() * 512.f, y = rng() * 704.f, a = 0.03f + rng() * 0.05f;
        cv_rect(c, x, y, 1.5f, 1.5f, rng() < 0.5f ? rgba(255, 255, 255, a) : rgba(0, 0, 0, a), BLEND_NORMAL);
    }
    Stop vig[2] = { { 0.55f, rgba(0, 0, 0, 0.f) }, { 1.f, rgba(0, 0, 0, 0.28f) } };
    cv_radial(c, 256.f, 352.f, 470.f, vig, 2, BLEND_NORMAL);
    b->endpaper = tex_from_canvas(c, true, false, true);
    cv_free(c);
    b->endpaper_mat = material("#ffffff", 0.92f, 0.f);
    b->endpaper_mat.map = b->endpaper.id;
    b->endpaper_mat.normal_map = S.cloth_nor.id;
    set_repeat(b->endpaper_mat.normal_repeat, b->D / 9.f, b->H / 9.f);
    b->endpaper_mat.normal_scale = 0.25f;
    b->endpaper_mat.receive_shadow = true;
}

void shelf_launch_open(void)
{
    if (S.row_count == 0)
        return;
    Book *b = &S.rows[S.menu].books[S.focus];
    paint_endpaper(b);
    b->open.t = 1.f;
    S.opening = true;
    S.closing = false;
}

void shelf_launch_close(void)
{
    S.opening = false;
    S.closing = true;
    S.zoom.t = 0.f;
}

float shelf_zoom(void)
{
    return S.zoom.v;
}

void shelf_dim(float amount, bool dimmed)
{
    // the room lights go down: exposure falls and the lamp, LED and wash switch off
    S.exposure.t = dimmed ? 0.92f * (1.f - 0.67f * amount) : 0.92f;
    S.lamp_power.t = dimmed ? 0.f : 1.f;
}

void shelf_set_photo(const Canvas *blurred, bool crossfade)
{
    if (crossfade && S.photo_a.id) {
        tex_free(&S.photo_b);
        S.photo_b = tex_from_canvas(blurred, true, false, false);
        S.photo_mix = 0.f;
    }
    else {
        tex_free(&S.photo_a);
        tex_free(&S.photo_b);
        S.photo_a = tex_from_canvas(blurred, true, false, false);
        S.photo_mix = 0.f;
    }
}

void shelf_set_photo_mix(float mix)
{
    S.photo_mix = mix;
    if (mix >= 1.f && S.photo_b.id) {
        tex_free(&S.photo_a);
        S.photo_a = S.photo_b;
        S.photo_b.id = 0;
        S.photo_mix = 0.f;
    }
}

// ---------- frame ----------

static Spring *all_springs(int *n)
{
    static Spring *list[4096];
    int c = 0;
    list[c++] = &S.exposure;
    list[c++] = &S.lamp_power;
    list[c++] = &S.look_x;
    list[c++] = &S.led_w;
    list[c++] = &S.zoom;
    for (int r = 0; r < S.row_count; r++) {
        Row *row = &S.rows[r];
        if (c + 4 + row->count * 7 + row->ends_count * 2 >= 4096)
            break;
        list[c++] = &row->offset;
        list[c++] = &row->slide;
        for (int k = 0; k < row->count; k++) {
            Book *b = &row->books[k];
            list[c++] = &b->x; list[c++] = &b->z; list[c++] = &b->rot;
            list[c++] = &b->lean; list[c++] = &b->lift; list[c++] = &b->tz;
            list[c++] = &b->open;
        }
        for (int e = 0; e < row->ends_count; e++) {
            list[c++] = &row->ends[e].lx;
            list[c++] = &row->ends[e].rx;
        }
    }
    *n = c;
    return (Spring*) list;
}

bool shelf_update(float dt)
{
    if (dt > SPRING_MAX_DT)
        dt = SPRING_MAX_DT;
    int n;
    Spring **list = (Spring**) all_springs(&n);
    int sub = (int) ceilf(dt / SPRING_STEP);
    if (sub < 1)
        sub = 1;
    float h = dt / (float) sub;
    for (int s = 0; s < sub; s++)
        for (int i = 0; i < n; i++)
            spring_step(list[i], h);
    bool moving = false;
    for (int i = 0; i < n && !moving; i++)
        moving = !spring_rest(list[i]);

    if (S.row_count > 0) {
        Book *fb = &S.rows[S.menu].books[S.focus];
        if (S.opening && fb->open.v > 0.55f)
            S.zoom.t = 1.f;
        if (S.closing && S.zoom.v < 0.35f) {
            for (int r = 0; r < S.row_count; r++)
                for (int k = 0; k < S.rows[r].count; k++)
                    S.rows[r].books[k].open.t = 0.f;
            S.closing = false;
            shelf_launch_pull(false);
        }
    }

    float k = 1.f - expf(-dt * 3.5f), diff = 0.f;
    for (int i = 0; i < 3; i++) {
        S.wash[i] += (S.wash_target[i] - S.wash[i]) * k;
        diff += fabsf(S.wash_target[i] - S.wash[i]);
    }
    if (diff > 0.002f)
        moving = true;

    for (int r = 0; r < S.row_count; r++) {
        Row *row = &S.rows[r];
        if (r != S.menu && row->visible && spring_rest(&row->slide) && fabsf(row->slide.v) > SWAP * 0.9f)
            row->visible = false;
    }
    return moving;
}

static mat4 book_matrix(const Row *row, const Book *b)
{
    mat4 g = m4_translate(row->offset.v + row->slide.v, 0.f, 0.f);
    mat4 t = m4_translate(b->x.v, b->lift.v, b->z.v);
    mat4 rot = m4_euler_xyz(b->lean.v, b->rot.v, b->tz.v);
    return m4_mul(g, m4_mul(t, rot));
}

static void place_row(Row *row, mat4 *group)
{
    *group = m4_translate(row->offset.v + row->slide.v, 0.f, 0.f);
}

typedef enum { DRAW_SHADOW, DRAW_COLOR } DrawPass;

// the face of the text block, where the first leaf lies
static float leaf_x(const Book *b)
{
    return (b->T - BOARD * 2.f - 0.012f) / 2.f + 0.003f;
}

static void draw_rows(DrawPass pass)
{
    float black[3] = { 0, 0, 0 };
    for (int r = 0; r < S.row_count; r++) {
        Row *row = &S.rows[r];
        if (!row->visible)
            continue;
        mat4 group;
        place_row(row, &group);
        for (int k = 0; k < row->count; k++) {
            Book *b = &row->books[k];
            mat4 m = book_matrix(row, b);
            // the front board turns on a hinge along the spine edge
            float hx = b->T / 2.f, hz = b->D / 2.f - 0.08f;
            float swing = -2.97f * b->open.v;
            mat4 hinge = m4_mul(m4_translate(hx, 0.f, hz), m4_mul(m4_euler_xyz(0.f, swing, 0.f), m4_translate(-hx, 0.f, -hz)));
            mat4 front = m4_mul(m, m4_mul(hinge, m4_translate(b->T / 2.f - BOARD / 2.f, b->H / 2.f, -0.02f)));
            mat4 back = m4_mul(m, m4_translate(-b->T / 2.f + BOARD / 2.f, b->H / 2.f, -0.02f));
            mat4 spine = m4_mul(m, m4_translate(0.f, b->H / 2.f, b->D / 2.f - 0.08f));
            mat4 pages = m4_mul(m, m4_translate(0.f, b->H / 2.f, -SQUARE / 2.f - 0.05f));
            if (pass == DRAW_SHADOW) {
                render_shadow_mesh(&b->front_m, front);
                render_shadow_mesh(&b->back_m, back);
                render_shadow_mesh(&b->spine_m, spine);
                render_shadow_mesh(&b->pages_m, pages);
            }
            else {
                if (b->open.v > 0.001f && b->endpaper.id) {
                    mat4 inner = m4_mul(front, m4_mul(m4_translate(-BOARD / 2.f - 0.002f, 0.f, 0.f), m4_euler_xyz(0.f, -VM_PI / 2.f, 0.f)));
                    mat4 leaf = m4_mul(m, m4_mul(m4_translate(leaf_x(b), b->H / 2.f, -SQUARE / 2.f - 0.05f), m4_euler_xyz(0.f, VM_PI / 2.f, 0.f)));
                    render_mesh(&b->inner_m, &b->endpaper_mat, inner);
                    render_mesh(&b->leaf_m, &b->endpaper_mat, leaf);
                }
                render_mesh(&b->front_m, &b->cover_mat, front);
                render_mesh(&b->back_m, &b->back_mat, back);
                render_mesh(&b->spine_m, &b->spine_mat, spine);
                render_mesh(&b->pages_m, &S.page_mat, pages);
            }
        }
        for (int e = 0; e < row->ends_count; e++) {
            if (!row->ends[e].visible)
                continue;
            float xs[2] = { row->ends[e].lx.v, row->ends[e].rx.v };
            for (int s = 0; s < 2; s++) {
                mat4 base = m4_mul(group, m4_translate(xs[s], 0.025f, 0.f));
                mat4 up = m4_mul(group, m4_translate(xs[s], 1.2f, 0.f));
                if (pass == DRAW_SHADOW) {
                    render_shadow_mesh(&S.end_base_m, base);
                    render_shadow_mesh(&S.end_up_m, up);
                }
                else {
                    render_mesh(&S.end_base_m, &S.end_mat, base);
                    render_mesh(&S.end_up_m, &S.end_mat, up);
                }
            }
        }
    }
    if (pass == DRAW_SHADOW)
        return;
    // contact occlusion under every book, after the opaque pass
    for (int r = 0; r < S.row_count; r++) {
        Row *row = &S.rows[r];
        if (!row->visible)
            continue;
        for (int k = 0; k < row->count; k++) {
            Book *b = &row->books[k];
            mat4 m = book_matrix(row, b);
            mat4 ao = m4_mul(m, m4_mul(m4_translate(0.f, 0.004f, 0.f), m4_euler_xyz(-VM_PI / 2.f, 0.f, 0.f)));
            render_decal(&b->ao_m, S.ao_tex.id, black, 1.f, ao);
        }
    }
}

static bool cur_visible(void)
{
    return S.row_count > 0 && S.rows[S.menu].visible;
}

void shelf_render(void)
{
    // while the books are still being painted the room is drawn empty, the lamp over the middle
    float lamp_x = 0.f;
    if (S.row_count > 0 && S.rows[S.menu].visible) {
        Row *cur = &S.rows[S.menu];
        lamp_x = cur->offset.v + cur->slide.v + cur->books[S.focus].x.v;
    }
    float power = S.lamp_power.v;

    Lights l;
    memset(&l, 0, sizeof(l));
    l.key_pos = v3(-13.f, 9.f, 5.5f);
    l.key_target = v3(0.f, 0.f, 0.f);
    float kc[3];
    hex_linear("#fff1e2", kc);
    l.key_color = v3(kc[0] * 3.1f, kc[1] * 3.1f, kc[2] * 3.1f);
    // the one warm light: a picture-lamp pool on the chosen book
    float lc[3];
    hex_linear("#ffd6a6", lc);
    l.spot[0].pos = v3(lamp_x - 0.6f, 9.f, 6.5f);
    l.spot[0].target = v3(lamp_x, 1.8f, 1.6f);
    l.spot[0].color = v3(lc[0] * 90.f * power, lc[1] * 90.f * power, lc[2] * 90.f * power);
    l.spot[0].angle = 0.34f; l.spot[0].penumbra = 0.8f; l.spot[0].distance = 20.f; l.spot[0].decay = 1.5f;
    // bias light hidden behind the row, throwing the chosen book's colour across the wall
    float wi = walled() ? WASH * power : 0.f;
    l.spot[1].pos = v3(lamp_x, 2.1f, -0.5f);
    l.spot[1].target = v3(lamp_x, 2.9f, -2.3f);
    l.spot[1].color = v3(S.wash[0] * wi, S.wash[1] * wi, S.wash[2] * wi);
    l.spot[1].angle = 1.12f; l.spot[1].penumbra = 1.f; l.spot[1].distance = 10.f; l.spot[1].decay = 1.f;
    float gc[3];
    hex_linear("#ffc27a", gc);
    l.point.pos = v3(lamp_x, -0.75f, 3.3f);
    l.point.color = v3(gc[0] * 2.2f * power, gc[1] * 2.2f * power, gc[2] * 2.2f * power);
    l.point.distance = 3.2f;
    l.point.decay = 2.f;
    l.env_intensity = 0.55f;
    l.exposure = S.exposure.v;

    vec3 eye = v3(S.look_x.v, S.cam_c + 1.25f, S.cam_dist);
    vec3 target = v3(S.look_x.v * 2.5f, S.cam_c, 0.f);
    float z = clampf(S.zoom.v, 0.f, 1.f);
    if (z > 0.0005f && cur_visible()) {
        Row *cur = &S.rows[S.menu];
        Book *b = &cur->books[S.focus];
        mat4 m = book_matrix(cur, b);
        vec3 centre = m4_transform_point(m, v3(leaf_x(b), b->H / 2.f, -SQUARE / 2.f - 0.05f));
        vec3 normal = v3_norm(m4_transform_dir(m, v3(1.f, 0.f, 0.f)));
        float tan_h = tanf(12.f * VM_PI / 180.f), aspect = (float) S.win_w / (float) S.win_h;
        float leaf_w = b->D - SQUARE - 0.13f, leaf_h = b->H - SQUARE * 2.f - 0.01f;
        float dist = fminf(leaf_w / 2.f / (tan_h * aspect), leaf_h / 2.f / tan_h) * 0.94f;
        float e = z * z * (3.f - 2.f * z);
        eye = v3_add(v3_scale(eye, 1.f - e), v3_scale(v3_add(centre, v3_scale(normal, dist)), e));
        target = v3_add(v3_scale(target, 1.f - e), v3_scale(centre, e));
    }
    S.cam.eye = eye;
    S.cam.view = m4_look_at(eye, target, v3(0, 1, 0));

    Background bg;
    memset(&bg, 0, sizeof(bg));
    bg.kind = BG_CLEAR;
    if (S.cfg.background == SHELF_BG_ROOM && S.env_ok) {
        bg.kind = BG_ENV;
        bg.intensity = 0.6f * S.cfg.overlay;
        bg.blurriness = 0.035f;
    }
    else if (S.cfg.background == SHELF_BG_PHOTO && S.photo_a.id) {
        bg.kind = BG_PHOTO;
        bg.intensity = 0.6f * S.cfg.overlay;
        bg.photo_a = S.photo_a.id;
        bg.photo_b = S.photo_b.id;
        bg.photo_mix = S.photo_mix;
    }
    else if (S.cfg.background == SHELF_BG_TRANSPARENT) {
        for (int i = 0; i < 3; i++)
            bg.clear[i] = S.cfg.chroma_key[i] / 255.f;
    }

    render_shadow_begin(&l);
    draw_rows(DRAW_SHADOW);
    render_shadow_end();

    render_scene_begin(&S.cam, &l, &bg);
    mat4 id = m4_identity();
    render_mesh(&S.top_m, &S.top_mat, m4_mul(id, m4_translate(0.f, -0.21f, 0.35f)));
    render_mesh(&S.body_m, &S.body_mat, m4_translate(0.f, -3.42f, 0.2f));
    render_mesh(&S.channel_m, &S.channel_mat, m4_translate(0.f, -0.3f, 2.948f));
    if (walled())
        render_mesh(&S.wall_m, &S.wall_mat, m4_translate(0.f, 9.f, -2.3f));
    draw_rows(DRAW_COLOR);
    float led[3];
    hex_linear("#ffcf8a", led);
    float lp = fmaxf(0.05f, power);
    for (int i = 0; i < 3; i++)
        led[i] *= lp;
    render_additive(&S.led_m, S.led_tex.id, led, m4_mul(m4_translate(lamp_x, -0.3f, 2.965f), m4_scale(S.led_w.v, 1.f, 1.f)));
    render_scene_end();
}

void shelf_quit(void)
{
    art_quit();
    render_quit();
}

