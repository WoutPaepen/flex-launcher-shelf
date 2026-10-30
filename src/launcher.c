#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_thread.h>
#include "launcher.h"
#include <launcher_config.h>
#include "image.h"
#include "util.h"
#include "debug.h"
#include "clock.h"
#include "gl.h"
#include "render.h"
#include "shelf.h"
#include "overlay.h"
#include "bookart.h"
#include "text.h"
#include "spring.h"
#include "platform/platform.h"
#ifdef __unix__
#include "platform/unix.h"
#else
#define CMD_SHUTDOWN "shutdown /s /t 0"
#define CMD_RESTART "shutdown /r /t 0"
#endif

#define EJECT_TIME 420       // the book slides out before the application starts
#define BLANK_TIME 420
#define POWER_TIMEOUT 15000  // still running this long after a shutdown means it did not happen
#define REPEAT_DELAY 500
#define REPEAT_INTERVAL 130
#define QUALITY_FRAMES 40

static void init_sdl(void);
static void init_sdl_image(void);
static void create_window(void);
static void init_sdl_ttf(void);
static void init_slideshow(void);
static void build_shelf(void);
static void update_slideshow(void);
static void update_screensaver(void);
static void update_launch(void);
static void move(int direction);
static void select_entry(void);
static void open_menu(const char *name);
static void go_back(void);
static void go_home(void);
static void handle_keypress(SDL_Keysym *key);
static void execute_command(const char *command);
static void poll_gamepad(void);
static void init_gamepad(Gamepad **gamepad, int device_index);
static void connect_gamepad(int device_index, bool open, bool raise_error);
static void disconnect_gamepad(int id, bool disconnect, bool remove);
static void open_controller(Gamepad *gamepad, bool raise_error);
static void cleanup(void);

// Initialize default settings
Config config = {
    .default_menu                     = NULL,
    .background_image                 = NULL,
    .slideshow_directory              = NULL,
    .title_font_path                  = NULL,
    .vsync                            = true,
    .fps_limit                        = -1,
    .application_timeout              = DEFAULT_APPLICATION_TIMEOUT * 1000,
    .titles_enabled                   = DEFAULT_TITLES_ENABLED,
    .title_font_size                  = DEFAULT_FONT_SIZE,
    .title_font_color.r               = DEFAULT_TITLE_FONT_COLOR_R,
    .title_font_color.g               = DEFAULT_TITLE_FONT_COLOR_G,
    .title_font_color.b               = DEFAULT_TITLE_FONT_COLOR_B,
    .title_font_color.a               = DEFAULT_TITLE_FONT_COLOR_A,
    .title_shadows                    = DEFAULT_TITLE_SHADOWS,
    .title_shadow_color.r             = DEFAULT_TITLE_SHADOW_COLOR_R,
    .title_shadow_color.g             = DEFAULT_TITLE_SHADOW_COLOR_G,
    .title_shadow_color.b             = DEFAULT_TITLE_SHADOW_COLOR_B,
    .title_shadow_color.a             = DEFAULT_TITLE_SHADOW_COLOR_A,
    .background_mode                  = BACKGROUND_WALL,
    .background_color.r               = DEFAULT_BACKGROUND_COLOR_R,
    .background_color.g               = DEFAULT_BACKGROUND_COLOR_G,
    .background_color.b               = DEFAULT_BACKGROUND_COLOR_B,
    .chroma_key_color.r               = DEFAULT_CHROMA_KEY_COLOR_R,
    .chroma_key_color.g               = DEFAULT_CHROMA_KEY_COLOR_G,
    .chroma_key_color.b               = DEFAULT_CHROMA_KEY_COLOR_B,
    .chroma_key_color.a               = DEFAULT_CHROMA_KEY_COLOR_A,
    .background_color.a               = 0xFF,
    .background_overlay               = DEFAULT_BACKGROUND_OVERLAY,
    .background_overlay_color.r       = DEFAULT_BACKGROUND_OVERLAY_COLOR_R,
    .background_overlay_color.g       = DEFAULT_BACKGROUND_OVERLAY_COLOR_G,
    .background_overlay_color.b       = DEFAULT_BACKGROUND_OVERLAY_COLOR_B,
    .background_overlay_color.a       = DEFAULT_BACKGROUND_OVERLAY_COLOR_A,
    .background_overlay_opacity[0]    = '\0',
    .highlight                        = true,
    .icon_size                        = DEFAULT_ICON_SIZE,
    .highlight_fill_color.r           = DEFAULT_HIGHLIGHT_FILL_COLOR_R,
    .highlight_fill_color.g           = DEFAULT_HIGHLIGHT_FILL_COLOR_G,
    .highlight_fill_color.b           = DEFAULT_HIGHLIGHT_FILL_COLOR_B,
    .highlight_fill_color.a           = DEFAULT_HIGHLIGHT_FILL_COLOR_A,
    .highlight_outline_color.r        = DEFAULT_HIGHLIGHT_OUTLINE_COLOR_R,
    .highlight_outline_color.g        = DEFAULT_HIGHLIGHT_OUTLINE_COLOR_G,
    .highlight_outline_color.b        = DEFAULT_HIGHLIGHT_OUTLINE_COLOR_B,
    .highlight_outline_color.a        = DEFAULT_HIGHLIGHT_OUTLINE_COLOR_A,
    .highlight_outline_size           = DEFAULT_HIGHLIGHT_OUTLINE_SIZE,
    .highlight_rx                     = DEFAULT_HIGHLIGHT_CORNER_RADIUS,
    .title_padding                    = -1,
    .max_buttons                      = DEFAULT_MAX_BUTTONS,
    .icon_spacing                     = -1,
    .highlight_vpadding               = -1,
    .highlight_hpadding               = -1,
    .title_opacity[0]                 = '\0',
    .highlight_fill_opacity[0]        = '\0',
    .highlight_outline_opacity[0]     = '\0',
    .vcenter[0]             = '\0',
    .icon_spacing_str[0]              = '\0',
    .scroll_indicators                = DEFAULT_SCROLL_INDICATORS,
    .scroll_indicator_fill_color.r    = DEFAULT_SCROLL_INDICATOR_FILL_COLOR_R,
    .scroll_indicator_fill_color.g    = DEFAULT_SCROLL_INDICATOR_FILL_COLOR_G,
    .scroll_indicator_fill_color.b    = DEFAULT_SCROLL_INDICATOR_FILL_COLOR_B,
    .scroll_indicator_fill_color.a    = DEFAULT_SCROLL_INDICATOR_FILL_COLOR_A,
    .scroll_indicator_outline_size    = DEFAULT_SCROLL_INDICATOR_OUTLINE_SIZE,
    .scroll_indicator_outline_color.r = DEFAULT_SCROLL_INDICATOR_OUTLINE_COLOR_R,
    .scroll_indicator_outline_color.g = DEFAULT_SCROLL_INDICATOR_OUTLINE_COLOR_G,
    .scroll_indicator_outline_color.b = DEFAULT_SCROLL_INDICATOR_OUTLINE_COLOR_B,
    .scroll_indicator_outline_color.a = DEFAULT_SCROLL_INDICATOR_OUTLINE_COLOR_A,
    .scroll_indicator_opacity[0]      = '\0',
    .title_oversize_mode              = OVERSIZE_TRUNCATE,
    .wrap_entries                     = DEFAULT_WRAP_ENTRIES,
    .reset_on_back                    = DEFAULT_RESET_ON_BACK,
    .mouse_select                     = DEFAULT_MOUSE_SELECT,
    .inhibit_os_screensaver           = DEFAULT_INHIBIT_OS_SCREENSAVER,
    .startup_cmd                      = NULL,
    .quit_cmd                         = NULL,
    .screensaver_enabled              = false,
    .screensaver_idle_time            = DEFAULT_SCREENSAVER_IDLE_TIME*1000,
    .screensaver_intensity_str[0]     = '\0',
    .screensaver_pause_slideshow      = DEFAULT_SCREENSAVER_PAUSE_SLIDESHOW,
    .gamepad_enabled                  = DEFAULT_GAMEPAD_ENABLED,
    .gamepad_device                   = DEFAULT_GAMEPAD_DEVICE,
    .gamepad_mappings_file            = NULL,
    .on_launch                        = ON_LAUNCH_BLANK,
    .debug                            = false,
    .exe_path                         = NULL,
    .first_menu                       = NULL,
    .num_menus                        = 0,
    .clock_enabled                    = DEFAULT_CLOCK_ENABLED,
    .clock_show_date                  = DEFAULT_CLOCK_SHOW_DATE,
    .clock_alignment                  = DEFAULT_CLOCK_ALIGNMENT,
    .clock_font_path                  = NULL,
    .clock_margin_str[0]              = '\0',
    .clock_margin                     = -1,
    .clock_font_color.r               = DEFAULT_CLOCK_FONT_COLOR_R,
    .clock_font_color.g               = DEFAULT_CLOCK_FONT_COLOR_G,
    .clock_font_color.b               = DEFAULT_CLOCK_FONT_COLOR_B,
    .clock_font_color.a               = DEFAULT_CLOCK_FONT_COLOR_A,
    .clock_shadows                    = DEFAULT_CLOCK_SHADOWS,
    .clock_shadow_color.r             = DEFAULT_CLOCK_SHADOW_COLOR_R,
    .clock_shadow_color.g             = DEFAULT_CLOCK_SHADOW_COLOR_G,
    .clock_shadow_color.b             = DEFAULT_CLOCK_SHADOW_COLOR_B,
    .clock_shadow_color.a             = DEFAULT_CLOCK_SHADOW_COLOR_A,
    .clock_opacity[0]                 = '\0',
    .clock_font_size                  = DEFAULT_CLOCK_FONT_SIZE,
    .clock_time_format                = DEFAULT_CLOCK_TIME_FORMAT,
    .clock_date_format                = DEFAULT_CLOCK_DATE_FORMAT,
    .clock_include_weekday            = DEFAULT_CLOCK_INCLUDE_WEEKDAY,
    .slideshow_image_duration         = DEFAULT_SLIDESHOW_IMAGE_DURATION,
    .slideshow_transition_time        = DEFAULT_SLIDESHOW_TRANSITION_TIME,
    .assets_dir                       = NULL,
    .room_hdr                         = NULL,
    .quality                          = QUALITY_AUTO,
    .spine_titles                     = true,
    .book_styles                      = NULL,
    .capture_path                     = NULL,
    .test_keys                        = NULL,
    .capture_wait                     = 1500,
    .window_width                     = 0,
    .window_height                    = 0
};

// Initialize default states
State state = { false };

typedef enum {
    LAUNCH_IDLE,
    LAUNCH_EJECT,     // the book is sliding out
    LAUNCH_WAITING,   // the process started, waiting for its window
    LAUNCH_POWER,     // a power command is about to run
    LAUNCH_POWER_SENT,
    LAUNCH_CLOSING    // :quit, fading to black first
} LaunchPhase;

// Global variables
SDL_Window *window                    = NULL;
SDL_GLContext gl_context              = NULL;
Menu *default_menu                    = NULL;
Menu *current_menu                    = NULL;
Entry *current_entry                  = NULL;
Slideshow *slideshow                  = NULL;
FILE *log_file                        = NULL;
Gamepad *gamepads                     = NULL;
GamepadControl *gamepad_controls      = NULL;
Hotkey *hotkeys                       = NULL;
SDL_Thread *slideshow_thread          = NULL;
SDL_Event event;
SDL_SysWMinfo wm_info;
SDL_DisplayMode display_mode;
Ticks ticks;
Geometry geo;
Uint32 refresh_period;

static struct {
    LaunchPhase phase;
    Uint32 t0;
    Entry *entry;
    char cmd[2048];
    char power[16];
    bool page_pending;    // print "Starting" on the open book once it fills the screen
    bool quit_after;      // OnLaunch=Quit: close once the page is up
} launch;

static int win_w, win_h;
static bool gles = false;
static bool dimmed = false;
static bool animating = true;
static Uint32 last_frame = 0;
static float base_scale = 1.f;
static float quality_scale = 1.f;
static float frame_times[QUALITY_FRAMES];
static int frame_count = 0;
static Uint32 last_drop = 0;
static ClockText clock_text;
static Uint32 photo_fade_start = 0;
static bool photo_fading = false;
static char assets_dir[MAX_PATH_CHARS + 1];

// A function to initialize SDL
static void init_sdl()
{
    // Set flags, hints
    Uint32 sdl_flags = SDL_INIT_VIDEO;
#ifdef __unix__
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, config.inhibit_os_screensaver ? "0" : "1");
    if (config.gamepad_enabled)
        sdl_flags |= SDL_INIT_GAMECONTROLLER;

    // Initialize SDL
    if (SDL_Init(sdl_flags) < 0)
        log_fatal("Could not initialize SDL\n%s", SDL_GetError());

    SDL_GetDesktopDisplayMode(0, &display_mode);
    geo.screen_width = display_mode.w;
    geo.screen_height = display_mode.h;
    refresh_period = 1000 / (Uint32) (display_mode.refresh_rate > 0 ? display_mode.refresh_rate : 60);
    geo.screen_margin = (int) (SCREEN_MARGIN * (float) geo.screen_height);
}

// Try a desktop OpenGL 3.3 core context first, then OpenGL ES 3.0 for ARM boards
static bool create_context(bool es)
{
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es ? 0 : 3);
#ifdef __APPLE__
    if (!es)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (config.window_width > 0 && config.window_height > 0)
        window = SDL_CreateWindow(PROJECT_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                     config.window_width, config.window_height, flags);
    else
        window = SDL_CreateWindow(PROJECT_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                     0, 0, flags | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (window == NULL)
        return false;
    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }
    gles = es;
    return true;
}

// A function to create the window and the OpenGL context
static void create_window()
{
    if (!create_context(false) && !create_context(true))
        log_fatal("Could not create an OpenGL 3.3 or OpenGL ES 3.0 context\n%s", SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);

    const char *missing = NULL;
    if (!gl_load(&missing))
        log_fatal("The graphics driver is missing %s", missing ? missing : "an OpenGL function");

    if (!config.vsync) {
        if (config.fps_limit > MIN_FPS_LIMIT && config.fps_limit <= display_mode.refresh_rate)
            refresh_period = 1000 / (Uint32) config.fps_limit;
        else
            config.vsync = true;
    }
    if (config.vsync) {
        // adaptive vsync when the driver has it, so a missed frame does not halve the rate
        if (SDL_GL_SetSwapInterval(-1) < 0)
            SDL_GL_SetSwapInterval(1);
    }
    else
        SDL_GL_SetSwapInterval(0);
    SDL_GL_GetDrawableSize(window, &win_w, &win_h);

#ifdef _WIN32
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
    if (config.background_mode == BACKGROUND_TRANSPARENT)
        make_window_transparent();
#endif
}

// A function to initialize the SDL_image library
static void init_sdl_image()
{
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP;
    if (!(IMG_Init(img_flags) & img_flags))
        log_fatal("Could not initialize SDL_image\n%s", IMG_GetError());
}

// A function to initialize SDL's TTF subsystem
static void init_sdl_ttf()
{
    if (TTF_Init() == -1)
        log_fatal("Could not initialize SDL_ttf\n%s", TTF_GetError());
}

// A function to find the directory holding the shelf's fonts, textures and room photograph
static void find_assets()
{
    const char *probe = "textures" PATH_SEPARATOR "oak_diff_h.jpg";
    char buffer[MAX_PATH_CHARS + 1];
    const char *candidates[4];
    int n = 0;
    if (config.assets_dir != NULL)
        candidates[n++] = config.assets_dir;
    char exe_assets[MAX_PATH_CHARS + 1];
    if (config.exe_path != NULL)
        candidates[n++] = join_paths(exe_assets, sizeof(exe_assets), 2, config.exe_path, PATH_ASSETS_EXE);
#ifdef __unix__
    candidates[n++] = PATH_ASSETS_SYSTEM;
#endif
    candidates[n++] = CURRENT_DIRECTORY PATH_ASSETS_EXE;
    for (int i = 0; i < n; i++) {
        join_paths(buffer, sizeof(buffer), 2, candidates[i], probe);
        if (file_exists(buffer)) {
            copy_string(assets_dir, candidates[i], sizeof(assets_dir));
            log_debug("Shelf assets found in %s", assets_dir);
            return;
        }
    }
    log_fatal("Could not find the shelf assets (fonts, textures, env). Set Assets= in the [Shelf] section of config.ini");
}

// A function to close subsystems and free memory before quitting
static void cleanup()
{
    SDL_WaitThread(slideshow_thread, NULL);
    overlay_quit();
    shelf_quit();
    font_cache_clear();
    if (gl_context != NULL) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    // Quit subsystems
    SDL_Quit();
    IMG_Quit();
    TTF_Quit();
    if (slideshow != NULL)
        quit_slideshow();

    // Close log file if open
    if (log_file != NULL)
        fclose(log_file);

    // Free dynamically allocated memory
    free(config.default_menu);
    free(config.background_image);
    free(config.title_font_path);
    free(config.exe_path);
    free(config.slideshow_directory);
    free(config.clock_font_path);
    free(config.gamepad_mappings_file);
    free(config.startup_cmd);
    free(config.assets_dir);
    free(config.room_hdr);
    free(config.capture_path);
    free(config.test_keys);

    // Free menu and entry linked lists
    Entry *entry = NULL;
    Entry *tmp_entry = NULL;
    Menu *menu = config.first_menu;
    Menu *tmp_menu = NULL;
    for (size_t i = 0; i < config.num_menus; i++) {
        free(menu->name);
        entry = menu->first_entry;
        for(size_t j = 0; j < menu->num_entries; j++) {
            free(entry->title);
            free(entry->icon_path);
            free(entry->icon_selected_path);
            free(entry->cmd);
            tmp_entry = entry;
            entry = entry->next;
            free(tmp_entry);
        }
        tmp_menu = menu;
        menu = menu->next;
        free(tmp_menu);
    }

    for (BookStyle *i = config.book_styles; i != NULL;) {
        BookStyle *next = i->next;
        free(i->title);
        free(i->color);
        free(i->note);
        free(i);
        i = next;
    }

    // Free hotkey linked list
    Hotkey *tmp_hotkey = NULL;
    for(Hotkey *i = hotkeys; i != NULL; i = i->next) {
        free(tmp_hotkey);
        free(i->cmd);
        tmp_hotkey = i;
    }
    free(tmp_hotkey);

    // Free gamepad control linked list
    GamepadControl *tmp_gamepad = NULL;
    for (GamepadControl *i = gamepad_controls; i != NULL; i = i->next) {
        free(tmp_gamepad);
        free(i->cmd);
        tmp_gamepad = i;
    }
    free(tmp_gamepad);

    if (config.gamepad_enabled)
        disconnect_gamepad(-1, false, true);
}

// A function to quit the slideshow mode in case of error or program exit
void quit_slideshow()
{
    for (int i = 0; i < slideshow->num_images; i++)
        free(slideshow->images[i]);
    free(slideshow->images);
    free(slideshow->order);
    free(slideshow);
    slideshow = NULL;
}

// A function to initialize the slideshow background mode
static void init_slideshow()
{
    if (config.slideshow_directory == NULL || !directory_exists(config.slideshow_directory)) {
        log_error("Slideshow directory '%s' does not exist, switching to the wall background",
            config.slideshow_directory ? config.slideshow_directory : "");
        config.background_mode = BACKGROUND_WALL;
        return;
    }
    slideshow = malloc(sizeof(Slideshow));
    *slideshow = (Slideshow) {
        .i = -1,
        .num_images = 0,
        .transition_surface = NULL,
        .transition_alpha = 0.f,
        .transition_change_rate = 0.f,
        .images = NULL,
        .order = NULL
    };
    scan_slideshow_directory(slideshow, config.slideshow_directory);
    if (!slideshow->num_images) {
        log_error("No images found in slideshow directory '%s', switching to the wall background",
            config.slideshow_directory);
        config.background_mode = BACKGROUND_WALL;
        quit_slideshow();
    }
    else if (slideshow->num_images == 1) {
        free(config.background_image);
        config.background_image = strdup(slideshow->images[0]);
        config.background_mode = BACKGROUND_IMAGE;
        quit_slideshow();
    }
    else {
        slideshow->order = malloc(sizeof(int) * (size_t) slideshow->num_images);
        random_array(slideshow->order, slideshow->num_images);
        if (config.debug)
            debug_slideshow(slideshow);
    }
}

// ---------- the shelf ----------

static ShelfBackground shelf_background()
{
    switch (config.background_mode) {
        case BACKGROUND_COLOR: return SHELF_BG_COLOR;
        case BACKGROUND_IMAGE:
        case BACKGROUND_SLIDESHOW: return SHELF_BG_PHOTO;
        case BACKGROUND_TRANSPARENT: return SHELF_BG_TRANSPARENT;
        case BACKGROUND_ROOM: return SHELF_BG_ROOM;
        default: return SHELF_BG_WALL;
    }
}

static const BookStyle *find_style(const char *title)
{
    for (BookStyle *i = config.book_styles; i != NULL; i = i->next) {
        if (!strcmp(i->title, title))
            return i;
    }
    return NULL;
}

// A cover line for entries that have none in [Book Notes]
static const char *default_note(const char *cmd, char *buffer, size_t size)
{
    if (!strncmp(cmd, SCMD_SUBMENU, strlen(SCMD_SUBMENU))) {
        const char *name = cmd + strlen(SCMD_SUBMENU);
        while (*name == ' ')
            name++;
        snprintf(buffer, size, "%s menu", name);
        return buffer;
    }
    if (!strcmp(cmd, SCMD_SHUTDOWN)) return "Turns the PC off";
    if (!strcmp(cmd, SCMD_RESTART)) return "Restarts the PC";
    if (!strcmp(cmd, SCMD_SLEEP)) return "Puts the PC to sleep";
    if (!strcmp(cmd, SCMD_QUIT)) return "Closes Flex Launcher";
    if (!strcmp(cmd, SCMD_HOME)) return "Back to the first shelf";
    if (!strcmp(cmd, SCMD_BACK)) return "The previous shelf";
    return NULL;
}

static float shelf_quality_scale()
{
    // never draw more than 1080p worth of scene pixels on a low-power GPU; High lifts that cap
    float cap = 1920.f / (float) win_w;
    if (config.quality == QUALITY_HIGH)
        return 1.f;
    if (config.quality == QUALITY_LOW)
        return fminf(1.f, cap) * 0.75f;
    return fminf(1.f, cap);
}

// A function to set up the renderer and the empty room
static void setup_shelf()
{
    ShelfConfig sc;
    memset(&sc, 0, sizeof(sc));
    copy_string(sc.assets, assets_dir, sizeof(sc.assets));
    if (config.room_hdr != NULL)
        copy_string(sc.room_hdr, config.room_hdr, sizeof(sc.room_hdr));
    sc.background = shelf_background();
    sc.wall_color[0] = config.background_color.r;
    sc.wall_color[1] = config.background_color.g;
    sc.wall_color[2] = config.background_color.b;
    sc.chroma_key[0] = config.chroma_key_color.r;
    sc.chroma_key[1] = config.chroma_key_color.g;
    sc.chroma_key[2] = config.chroma_key_color.b;
    sc.overlay = config.background_overlay ? 1.f - (float) config.background_overlay_color.a / 255.f : 1.f;
    sc.per_page = (int) config.max_buttons;
    sc.spine_titles = config.spine_titles;
    sc.msaa = config.quality == QUALITY_LOW ? 0 : 4;
    base_scale = quality_scale = shelf_quality_scale();
    sc.scale = quality_scale;
    if (!shelf_init(&sc, win_w, win_h, gles))
        log_fatal("Could not set up the shelf renderer (%s)", render_info());
}

static void frame(void);
static void save_capture(const char *path);

// One frame of the loading screen: the room and the overlay are up while books are painted
static void loading_frame()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            quit(EXIT_SUCCESS);
    }
    ticks.main = SDL_GetTicks();
    frame();

    // Test hook: --keys loading captures the loading screen itself
    if (config.capture_path != NULL && config.test_keys != NULL && !strcmp(config.test_keys, "loading")) {
        Uint32 until = SDL_GetTicks() + (Uint32) config.capture_wait;
        while (SDL_GetTicks() < until) {
            ticks.main = SDL_GetTicks();
            frame();
        }
        save_capture(config.capture_path);
        quit(EXIT_SUCCESS);
    }
}

// A function to paint every book, one menu at a time
static void build_shelf()
{
    for (Menu *menu = config.first_menu; menu != NULL; menu = menu->next) {
        menu->shelf_index = -1;
        menu->focus = 0;
        if (menu->num_entries == 0)
            continue;
        ShelfBook *books = calloc(menu->num_entries, sizeof(ShelfBook));
        char (*notes)[256] = calloc(menu->num_entries, sizeof(*notes));
        int k = 0;
        for (Entry *e = menu->first_entry; e != NULL; e = e->next, k++) {
            ShelfBook *b = &books[k];
            b->title = e->title;
            b->icon = e->icon_path;
            b->icon_selected = (e->icon_selected_path != NULL && file_exists(e->icon_selected_path)) ? e->icon_selected_path : NULL;
            b->ink = -1;
            const BookStyle *style = find_style(e->title);
            const char *note = style && style->note ? style->note : default_note(e->cmd, notes[k], sizeof(notes[k]));
            if (note != NULL && note != notes[k])
                copy_string(notes[k], note, sizeof(notes[k]));
            b->note = note != NULL ? notes[k] : NULL;
            if (style && style->color && style->color[0] == '#') {
                char hex[8];
                copy_string(hex, style->color, sizeof(hex));
                b->color = rgba_hex(hex, 1.f);
                b->has_color = true;
                if (strstr(style->color, ";dark"))
                    b->ink = 1;
                else if (strstr(style->color, ";light"))
                    b->ink = 0;
            }
        }
        menu->shelf_index = shelf_add_menu(books, (int) menu->num_entries);
        free(notes);
        free(books);
        loading_frame();
    }
}

static Entry *entry_at(Menu *menu, int index)
{
    Entry *e = menu->first_entry;
    for (int i = 0; i < index && e != NULL; i++)
        e = e->next;
    return e;
}

// Push the menu trail and the page rail to the overlay
static void update_hud()
{
    const char *trail[32];
    int n = 0;
    for (Menu *m = current_menu; m != NULL && n < 32; m = m->back)
        trail[n++] = m->name;
    for (int i = 0; i < n / 2; i++) {
        const char *t = trail[i];
        trail[i] = trail[n - 1 - i];
        trail[n - 1 - i] = t;
    }
    overlay_set_trail(trail, n);
    overlay_set_rail((int) current_menu->num_entries, current_menu->focus, shelf_per_page());
    current_entry = entry_at(current_menu, current_menu->focus);
    animating = true;
}

static void kick()
{
    animating = true;
}

// A function to move the selection; at either end it bumps unless entries wrap
static void move(int direction)
{
    int n = (int) current_menu->num_entries;
    int i = current_menu->focus + direction;
    if (i < 0)
        i = config.wrap_entries ? n - 1 : 0;
    if (i >= n)
        i = config.wrap_entries ? 0 : n - 1;
    if (i == current_menu->focus) {
        shelf_bump(direction);
        kick();
        return;
    }
    current_menu->focus = i;
    shelf_focus(i);
    update_hud();
}

static void swap_to(Menu *to, int direction)
{
    shelf_swap_menu(to->shelf_index, to->focus, direction);
    current_menu = to;
    update_hud();
}

// A function to open a submenu; it remembers where you were in it
static void open_menu(const char *name)
{
    Menu *menu = get_menu(name);
    if (menu == NULL || menu->shelf_index < 0) {
        log_error("Menu '%s' not found or has no entries", name);
        shelf_bump(1);
        kick();
        return;
    }
    if (menu == current_menu)
        return;
    menu->back = current_menu;
    swap_to(menu, 1);
}

// A function to load the previous menu
static void go_back()
{
    if (current_menu->back == NULL) {
        shelf_bump(-1);
        kick();
        return;
    }
    Menu *to = current_menu->back;
    if (config.reset_on_back) {
        current_menu->focus = 0;
        to->focus = 0;
    }
    current_menu->back = NULL;
    swap_to(to, -1);
}

static void go_home()
{
    if (current_menu == default_menu)
        return;
    for (Menu *m = config.first_menu; m != NULL; m = m->next)
        m->back = NULL;
    if (config.reset_on_back)
        default_menu->focus = 0;
    swap_to(default_menu, -1);
}

// ---------- launching ----------

// Back from an application (or it never came): pull out of the page, close the cover, shelve the book
static void finish_launch(bool returned_book)
{
    UNUSED(returned_book);
    shelf_launch_close();
    launch.page_pending = false;
    overlay_unblank();
    overlay_status(NULL);
    launch.phase = LAUNCH_IDLE;
    state.busy = false;
    state.wait_key = false;
    kick();
}

// A function to start an application with the eject, blank screen and timeout around it
static void launch_application(Entry *entry, const char *cmd)
{
    state.busy = true;
    launch.entry = entry;
    copy_string(launch.cmd, cmd, sizeof(launch.cmd));
    launch.t0 = ticks.main;
    launch.phase = LAUNCH_EJECT;
    if (entry != NULL)
        shelf_launch_pull(true);
    kick();
}

static void start_launch()
{
    char text[512];
    const char *title = launch.entry ? launch.entry->title : launch.cmd;
    snprintf(text, sizeof(text), "Starting %s", title);
    if (config.on_launch == ON_LAUNCH_NONE)
        overlay_status(text);
    else if (launch.entry != NULL) {
        // the book opens and the camera goes into its first page; the loader is printed there
        shelf_launch_open();
        launch.page_pending = true;
    }
    else
        overlay_blank(NULL, text, "", true);

    char *cmd = strdup(launch.cmd);
    bool started = start_process(cmd, true);
    free(cmd);
    if (!started) {
        char body[256];
        snprintf(text, sizeof(text), "%s didn't start", title);
        snprintf(body, sizeof(body), "The command could not be run. Check its command in config.ini:");
        finish_launch(launch.entry == NULL);
        overlay_toast(text, body, launch.cmd);
        return;
    }
    if (config.on_launch == ON_LAUNCH_QUIT) {
        if (!launch.page_pending)
            quit(EXIT_SUCCESS);
        launch.quit_after = true;
    }
    state.application_launching = true;
    ticks.application_launched = ticks.main;
    launch.phase = LAUNCH_WAITING;
}

// Shutdown, restart and sleep: a blank screen first, then the system command
static void power_command(Entry *entry, const char *cmd)
{
    state.busy = true;
    launch.entry = entry;
    copy_string(launch.power, cmd, sizeof(launch.power));
    const char *icon = entry ? entry->icon_path : NULL;
    if (!strcmp(cmd, SCMD_SHUTDOWN))
        overlay_blank(icon, "Shutting down", "The PC turns off now.", true);
    else if (!strcmp(cmd, SCMD_RESTART))
        overlay_blank(icon, "Restarting", "Back in a moment.", true);
    else
        overlay_blank(icon, "Going to sleep", "Press any key when you're back.", false);
    launch.t0 = ticks.main;
    launch.phase = LAUNCH_POWER;
    kick();
}

static void update_launch()
{
    Uint32 elapsed = ticks.main - launch.t0;
    if (launch.page_pending && shelf_zoom() > 0.97f) {
        char text[512];
        snprintf(text, sizeof(text), "Starting %s", launch.entry ? launch.entry->title : launch.cmd);
        overlay_blank_page(launch.entry ? launch.entry->icon_path : NULL, text, "", true);
        launch.page_pending = false;
    }
    if (launch.quit_after && !launch.page_pending && overlay_blanked())
        quit(EXIT_SUCCESS);
    if (launch.phase == LAUNCH_EJECT && elapsed >= EJECT_TIME)
        start_launch();
    else if (launch.phase == LAUNCH_POWER && elapsed >= BLANK_TIME) {
        launch.phase = LAUNCH_POWER_SENT;
        launch.t0 = ticks.main;
        if (!strcmp(launch.power, SCMD_SHUTDOWN))
            scmd_shutdown();
        else if (!strcmp(launch.power, SCMD_RESTART))
            scmd_restart();
        else {
            scmd_sleep();
            state.wait_key = true;
        }
    }
    else if (launch.phase == LAUNCH_POWER_SENT && strcmp(launch.power, SCMD_SLEEP) && elapsed >= POWER_TIMEOUT) {
        bool off = !strcmp(launch.power, SCMD_SHUTDOWN);
        finish_launch(true);
        overlay_toast(off ? "The PC didn't turn off" : "The PC didn't restart",
            "Flex Launcher sent the command, but the system is still running. Check that this user may run:",
            off ? CMD_SHUTDOWN : CMD_RESTART);
    }
    else if (launch.phase == LAUNCH_CLOSING && elapsed >= BLANK_TIME)
        quit(EXIT_SUCCESS);
    else if (launch.phase == LAUNCH_WAITING && state.application_launching &&
    ticks.main - ticks.application_launched > config.application_timeout) {
        // the window never appeared
        state.application_launching = false;
        char title[512], body[256];
        snprintf(title, sizeof(title), "%s didn't start", launch.entry ? launch.entry->title : launch.cmd);
        snprintf(body, sizeof(body), "Flex Launcher waited %u s for it to open. Check its command in config.ini:",
            (unsigned) (config.application_timeout / 1000));
        finish_launch(launch.entry == NULL);
        overlay_toast(title, body, launch.cmd);
    }
}

static void select_entry()
{
    if (current_entry == NULL)
        return;
    log_debug("Selected Entry:\n"
        "Title: %s\n"
        "Icon Path: %s\n"
        "Command: %s",
        current_entry->title,
        current_entry->icon_path,
        current_entry->cmd
    );
    const char *cmd = current_entry->cmd;
    if (!strcmp(cmd, SCMD_SHUTDOWN) || !strcmp(cmd, SCMD_RESTART) || !strcmp(cmd, SCMD_SLEEP))
        power_command(current_entry, cmd);
    else if (!strcmp(cmd, SCMD_QUIT)) {
        state.busy = true;
        overlay_blank(current_entry->icon_path, "Closing Flex Launcher", "", false);
        launch.phase = LAUNCH_CLOSING;
        launch.t0 = ticks.main;
        kick();
    }
    else if (cmd[0] == ':')
        execute_command(cmd);
    else
        launch_application(current_entry, cmd);
}

// A function to execute the user's command
static void execute_command(const char *command)
{
    char *cmd = strdup(command);

    // Parse special commands
    if (cmd[0] == ':') {
        char *delimiter = " ";
        char *special_command = strtok(cmd, delimiter);
        if (!strcmp(special_command, SCMD_SUBMENU)) {
            char *submenu = strtok(NULL, "");
            if (submenu != NULL)
                open_menu(submenu);
        }
        else if (!strcmp(special_command, SCMD_FORK)) {
            char *fork_command = strtok(NULL, "");
            if (fork_command != NULL)
                start_process(fork_command, false);
        }
        else if (!strcmp(special_command, SCMD_LEFT))
            move(-1);
        else if (!strcmp(special_command, SCMD_RIGHT))
            move(1);
        else if (!strcmp(special_command, SCMD_SELECT))
            select_entry();
        else if (!strcmp(special_command, SCMD_HOME))
            go_home();
        else if (!strcmp(special_command, SCMD_BACK))
            go_back();
        else if (!strcmp(special_command, SCMD_QUIT))
            quit(EXIT_SUCCESS);
        else if (!strcmp(special_command, SCMD_SHUTDOWN) || !strcmp(special_command, SCMD_RESTART) ||
        !strcmp(special_command, SCMD_SLEEP))
            power_command(NULL, special_command);
    }

    // Launch external application from a hotkey
    else
        launch_application(NULL, cmd);
    free(cmd);
}

// ---------- input ----------

// The first input after the lights went down only brings them back up
static bool wake()
{
    ticks.last_input = ticks.main;
    if (!dimmed)
        return false;
    dimmed = false;
    shelf_dim(0.f, false);
    overlay_dim(false);
    state.screensaver_active = false;
    state.slideshow_paused = false;
    ticks.slideshow_load = ticks.main;
    kick();
    return true;
}

// Every input goes through here: waking, dismissing the blank screen, then the command
static void command(const char *cmd)
{
    if (wake())
        return;
    if (state.busy) {
        if (state.wait_key)
            finish_launch(true);
        return;
    }
    if (cmd != NULL)
        execute_command(cmd);
}

// A function to handle key presses from keyboard
static void handle_keypress(SDL_Keysym *key)
{
    if (config.debug)
        log_debug("Key %s (#%X) detected", SDL_GetKeyName(key->sym), key->sym);

    // Hotkeys first, so a config can take any key
    for (Hotkey *i = hotkeys; i != NULL; i = i->next) {
        if (key->sym == i->keycode) {
            command(i->cmd);
            return;
        }
    }
    switch (key->sym) {
        case SDLK_LEFT: command(SCMD_LEFT); break;
        case SDLK_RIGHT: command(SCMD_RIGHT); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: command(SCMD_SELECT); break;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
        case SDLK_AC_BACK: command(SCMD_BACK); break;
        case SDLK_HOME:
        case SDLK_AC_HOME: command(SCMD_HOME); break;
        default: command(NULL); break;
    }
}

// A function to initialize the gamepad struct
static void init_gamepad(Gamepad **gamepad, int device_index)
{
    *gamepad = malloc(sizeof(Gamepad));
    **gamepad = (Gamepad) {
        .device_index = device_index,
        .id = (int) SDL_JoystickGetDeviceInstanceID(device_index),
        .controller = NULL,
        .next = NULL,
        .previous = NULL,
    };
}

// A function to open the SDL controller
static void open_controller(Gamepad *gamepad, bool raise_error)
{
    gamepad->controller = SDL_GameControllerOpen(gamepad->device_index);
    if (gamepad->controller == NULL) {
        if (raise_error)
            log_error("Could not open gamepad at device index %i", config.gamepad_device);
        return;
    }
    if (config.debug && raise_error) {
        char *mapping = SDL_GameControllerMapping(gamepad->controller);
        log_debug("Gamepad Mapping:\n%s", mapping);
        SDL_free(mapping);
    }
}

// A function to connect gamepad(s)
static void connect_gamepad(int device_index, bool open, bool raise_error)
{
    if (device_index >= 0) {
        Gamepad *gamepad = NULL;
        for (gamepad = gamepads; gamepad != NULL; gamepad = gamepad->next) {
            if (gamepad->id == device_index)
                break;
        }
        if (gamepad == NULL) {
            init_gamepad(&gamepad, device_index);
            if (gamepads == NULL)
                gamepads = gamepad;

            // Add to end of the linked list
            else {
                Gamepad *i;
                for (i = gamepads; i->next != NULL; i = i->next);
                i->next = gamepad;
                gamepad->previous = i;
            }
        }
        if (open)
            open_controller(gamepad, raise_error);
    }
    else if (open) {
        for (Gamepad *i = gamepads; i != NULL; i = i->next)
            open_controller(i, raise_error);
    }
}

// A function to disconnect gamepad(s)
static void disconnect_gamepad(int id, bool disconnect, bool remove)
{
    for (Gamepad *i = gamepads; i != NULL;) {
        if (id < 0 || i->id == id) {
            if (disconnect)
                SDL_GameControllerClose(i->controller);
            if (remove) {
                if (i->next != NULL)
                    i->next->previous = i->previous;
                if (i->previous != NULL)
                    i->previous->next = i->next;
                if (i == gamepads)
                    gamepads = i->next;
                Gamepad *tmp = i->next;
                free(i);
                i = tmp;
            }
            else {
                i->controller = NULL;
                i = i->next;
            }
        }
        else
            i = i->next;
    }
}

// A function to poll the connected gamepad for commands; only left and right repeat when held
static void poll_gamepad()
{
    int value_multiplier = 1;
    bool pressed;
    for (GamepadControl *i = gamepad_controls; i != NULL; i = i->next) {
        pressed = false;
        for (Gamepad *gamepad = gamepads; gamepad != NULL; gamepad = gamepad->next) {
            if (i->type == TYPE_AXIS_POS || i->type == TYPE_AXIS_NEG) {
                value_multiplier = i->type == TYPE_AXIS_POS ? 1 : -1;
                if (value_multiplier*SDL_GameControllerGetAxis(gamepad->controller, i->index) > GAMEPAD_DEADZONE) {
                    pressed = true;
                    break;
                }
            }
            else if (i->type == TYPE_BUTTON) {
                if (SDL_GameControllerGetButton(gamepad->controller, i->index)) {
                    pressed = true;
                    break;
                }
            }
        }
        if (!pressed) {
            i->repeat = 0;
            continue;
        }

        // repeat holds the time of the next repeat; 0 means the control was up
        if (i->repeat == 0) {
            log_debug("Gamepad %s detected", i->label);
            i->repeat = ticks.main + REPEAT_DELAY;
            command(i->cmd);
        }
        else if (ticks.main >= i->repeat && (!strcmp(i->cmd, SCMD_LEFT) || !strcmp(i->cmd, SCMD_RIGHT))) {
            i->repeat = ticks.main + REPEAT_INTERVAL;
            command(i->cmd);
        }
    }
}

static void show_background(SDL_Surface *surface, bool crossfade)
{
    Canvas *c = prepare_background(surface, win_w, win_h);
    if (c == NULL)
        return;
    shelf_set_photo(c, crossfade);
    cv_free(c);
    if (crossfade) {
        photo_fading = true;
        photo_fade_start = ticks.main;
    }
    kick();
}

// A function to update the slideshow
static void update_slideshow()
{
    if (slideshow == NULL || photo_fading || state.slideshow_paused)
        return;
    if (ticks.main - ticks.slideshow_load <= config.slideshow_image_duration)
        return;
    if (!state.slideshow_background_rendering && !state.slideshow_background_ready) {
        slideshow_thread = SDL_CreateThread(load_next_slideshow_background_async, "Slideshow Thread", (void*) slideshow);
        state.slideshow_background_rendering = true;
    }
    else if (state.slideshow_background_ready) {
        SDL_WaitThread(slideshow_thread, NULL);
        slideshow_thread = NULL;
        if (slideshow->transition_surface != NULL) {
            show_background(slideshow->transition_surface, config.slideshow_transition_time > 0);
            SDL_FreeSurface(slideshow->transition_surface);
            if (config.slideshow_transition_time == 0)
                shelf_set_photo_mix(1.f);
        }
        slideshow->transition_surface = NULL;
        state.slideshow_background_ready = false;
        ticks.slideshow_load = ticks.main;
    }
}

static bool update_photo_fade()
{
    if (!photo_fading)
        return false;
    float dur = (float) (config.slideshow_transition_time > 0 ? config.slideshow_transition_time : 1);
    float p = fminf(1.f, (float) (ticks.main - photo_fade_start) / dur);
    float e = p < 0.5f ? 4.f * p * p * p : 1.f - powf(-2.f * p + 2.f, 3.f) / 2.f;
    shelf_set_photo_mix(e);
    if (p >= 1.f) {
        photo_fading = false;
        ticks.slideshow_load = ticks.main;
    }
    return true;
}

// The screensaver: the room lights go down after the idle time
static void update_screensaver()
{
    if (dimmed || state.busy || ticks.main - ticks.last_input <= config.screensaver_idle_time)
        return;
    char intensity[PERCENT_MAX_CHARS];
    copy_string(intensity, config.screensaver_intensity_str[0] != '\0' ? config.screensaver_intensity_str : DEFAULT_SCREENSAVER_INTENSITY, sizeof(intensity));
    intensity[strlen(intensity) - 1] = '\0';
    float amount = fminf(fmaxf((float) atof(intensity) / 70.f, 0.f), 1.4f);
    dimmed = true;
    state.screensaver_active = true;
    shelf_dim(amount, true);
    overlay_dim(true);
    if (config.background_mode == BACKGROUND_SLIDESHOW && config.screensaver_pause_slideshow)
        state.slideshow_paused = true;
    kick();
}

static void update_clock()
{
    if (!config.clock_enabled)
        return;
    if (clock_update(&clock_text)) {
        overlay_set_time(clock_text.time, clock_text.period, config.clock_show_date ? clock_text.date : "");
        kick();
    }
}

static inline void pre_launch()
{
    if (gamepads != NULL)
        disconnect_gamepad(-1, true, false);
#ifdef _WIN32
    if (has_exit_hotkey())
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif
}

static inline void post_launch()
{
    // Rebaseline the timing after the program is done
    ticks.main = SDL_GetTicks();
    ticks.last_input = ticks.main;
    last_frame = ticks.main;
    if (config.gamepad_enabled)
        connect_gamepad(-1, true, false);
    ticks.slideshow_load = ticks.main;
    finish_launch(true);
    update_clock();
#ifdef _WIN32
    SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
#endif
}

// A function to quit the launcher
void quit(int status)
{
    log_debug("Quitting program");
    if (status != EXIT_SUCCESS)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            PROJECT_NAME,
            "A critical error occurred. Check the log file for details.",
            NULL
        );
    if (config.quit_cmd != NULL) {
        char *quit_cmd = config.quit_cmd;
        config.quit_cmd = NULL;
        start_process(quit_cmd, false);
        free(quit_cmd);
    }
    cleanup();
    exit(status);
}

// A function to print the version and other info to command line
void print_version(FILE *stream)
{
    SDL_version sdl_version;
    SDL_GetVersion(&sdl_version);
    const SDL_version *img_version = IMG_Linked_Version();
    const SDL_version *ttf_version = TTF_Linked_Version();
    fprintf(stream, PROJECT_NAME " version " PROJECT_VERSION ", using:" endline);
    fprintf(stream, "  SDL       %u.%u.%u" endline, sdl_version.major, sdl_version.minor, sdl_version.patch);
    fprintf(stream, "  SDL_image %u.%u.%u" endline, img_version->major, img_version->minor, img_version->patch);
    fprintf(stream, "  SDL_ttf   %u.%u.%u" endline, ttf_version->major, ttf_version->minor, ttf_version->patch);
}

// Drop the scene resolution when frames run slow; throttled or hidden frames say nothing about the GPU
static void auto_quality(float raw)
{
    if (config.quality != QUALITY_AUTO || raw <= 0.f || raw >= 0.1f)
        return;
    frame_times[frame_count++ % QUALITY_FRAMES] = raw;
    if (frame_count < QUALITY_FRAMES)
        return;
    float avg = 0.f;
    for (int i = 0; i < QUALITY_FRAMES; i++)
        avg += frame_times[i];
    avg /= QUALITY_FRAMES;
    if (avg > 1.f / 45.f && quality_scale > base_scale * 0.55f && ticks.main - last_drop > 1500) {
        quality_scale = fmaxf(base_scale * 0.55f, quality_scale - base_scale * 0.15f);
        last_drop = ticks.main;
        frame_count = 0;
        RenderQuality q = { 4, quality_scale };
        render_resize(win_w, win_h, q);
        log_debug("Frames averaged %.1f ms, scene scale now %.2f", avg * 1000.f, quality_scale);
    }
}

static void draw_frame()
{
    if (overlay_scene_alpha() > 0.f)
        shelf_render();
    render_overlay_begin();
    render_overlay_scene(overlay_scene_alpha());
    overlay_draw();
    render_overlay_end();
    SDL_GL_SwapWindow(window);
}

// Render while anything moves, otherwise sleep until the next event
static void frame()
{
    float raw = (float) (ticks.main - last_frame) / 1000.f;
    float dt = fminf(raw, SPRING_MAX_DT);
    last_frame = ticks.main;
    bool moving = shelf_update(dt);
    float z = fminf(fmaxf(shelf_zoom() * 1.6f, 0.f), 1.f);
    overlay_set_hud(1.f - z * z * (3.f - 2.f * z));
    moving |= overlay_update(dt);
    moving |= update_photo_fade();
    draw_frame();
    auto_quality(raw);
    if (!config.vsync) {
        Uint32 elapsed = SDL_GetTicks() - ticks.main;
        if (elapsed < refresh_period)
            SDL_Delay(refresh_period - elapsed);
    }
    animating = moving;
    if (!moving)
        frame_count = 0;
}

static void save_capture(const char *path)
{
    int w, h;
    unsigned char *px = NULL;
    draw_frame();
    render_read_pixels(&w, &h, &px);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (s != NULL) {
        // the default framebuffer has no meaningful alpha
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
        for (int i = 0; i < w * h; i++)
            px[i * 4 + 3] = 255;
        IMG_SavePNG(s, path);
        SDL_FreeSurface(s);
        printf("Saved %s (%ix%i)\n", path, w, h);
    }
    free(px);
}

// Test hook: play the --keys sequence, then save --capture and quit
static void run_test_script()
{
    static Uint32 next = 0;
    static char *cursor = NULL;
    static bool started = false, done = false;
    if (config.capture_path == NULL || done)
        return;
    if (!started) {
        started = true;
        cursor = config.test_keys;
        next = ticks.main + 1200;
    }
    if (ticks.main < next)
        return;
    if (cursor != NULL && *cursor != '\0') {
        char key[32];
        size_t len = strcspn(cursor, ",");
        copy_string(key, cursor, len + 1 < sizeof(key) ? len + 1 : sizeof(key));
        cursor += len + (cursor[len] == ',' ? 1 : 0);
        if (!strcmp(key, "left")) command(SCMD_LEFT);
        else if (!strcmp(key, "right")) command(SCMD_RIGHT);
        else if (!strcmp(key, "select")) command(SCMD_SELECT);
        else if (!strcmp(key, "back")) command(SCMD_BACK);
        else if (!strcmp(key, "home")) command(SCMD_HOME);
        else if (!strcmp(key, "dim")) ticks.last_input = 0, config.screensaver_idle_time = 0, config.screensaver_enabled = true;
        else command(NULL);
        // --wait counts from the last key
        next = ticks.main + (*cursor != '\0' ? 700 : 0);
        return;
    }
    done = true;
    Uint32 until = ticks.main + (Uint32) config.capture_wait;
    while (SDL_GetTicks() < until) {
        ticks.main = SDL_GetTicks();
        update_launch();
        if (config.screensaver_enabled)
            update_screensaver();
        frame();
    }
    save_capture(config.capture_path);
    quit(EXIT_SUCCESS);
}

static void resize_window()
{
    SDL_GL_GetDrawableSize(window, &win_w, &win_h);
    base_scale = quality_scale = shelf_quality_scale();
    shelf_resize(win_w, win_h);
    RenderQuality q = { config.quality == QUALITY_LOW ? 0 : 4, quality_scale };
    render_resize(win_w, win_h, q);
    overlay_resize(win_w, win_h, shelf_per_page());
    if (config.clock_enabled)
        overlay_set_time(clock_text.time, clock_text.period, config.clock_show_date ? clock_text.date : "");
    update_hud();
}

static void handle_event(SDL_Event *ev)
{
    switch (ev->type) {
        case SDL_QUIT:
            quit(EXIT_SUCCESS);
            break;

        case SDL_KEYDOWN:
            handle_keypress(&ev->key.keysym);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (config.mouse_select && ev->button.button == SDL_BUTTON_LEFT)
                command(SCMD_SELECT);
            else
                command(NULL);
            break;

        case SDL_MOUSEWHEEL: {
            static Uint32 wheel_lock = 0;
            if (ticks.main >= wheel_lock && (ev->wheel.y != 0 || ev->wheel.x != 0)) {
                wheel_lock = ticks.main + 140;
                command((ev->wheel.y < 0 || ev->wheel.x > 0) ? SCMD_RIGHT : SCMD_LEFT);
            }
            break;
        }

        case SDL_MOUSEMOTION:
            wake();
            break;

        case SDL_JOYDEVICEADDED:
            if (SDL_IsGameController(ev->jdevice.which) == SDL_TRUE) {
                log_debug("Gamepad connected with device index %i", ev->jdevice.which);
                if (config.gamepad_device < 0 || config.gamepad_device == ev->jdevice.which)
                    connect_gamepad(ev->jdevice.which, !state.application_running, true);
            }
            break;

        case SDL_JOYDEVICEREMOVED:
            log_debug("Gamepad disconnected");
            if (config.gamepad_device < 0 || config.gamepad_device == ev->jdevice.which)
                disconnect_gamepad(ev->jdevice.which, true, true);
            break;

        case SDL_WINDOWEVENT:
            if (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                log_debug("Lost keyboard focus");
                state.has_focus = false;
                if (state.application_launching) {
                    log_debug("Application detected");
                    state.application_launching = false;
                    state.application_running = true;
                    // with the launcher still on screen the book goes back; otherwise it stays open behind the app
                    if (config.on_launch == ON_LAUNCH_NONE)
                        shelf_launch_pull(false);
                    if (config.on_launch == ON_LAUNCH_NONE && launch.entry != NULL) {
                        char text[512];
                        snprintf(text, sizeof(text), "%s is running", launch.entry->title);
                        overlay_status(text);
                    }
                    pre_launch();
                }
#ifdef _WIN32
                else if (ticks.main - ticks.program_start < 10000)
                    set_foreground_window();
#endif
            }
            else if (ev->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                log_debug("Gained keyboard focus");
                state.has_focus = true;
            }
            else if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                resize_window();
            else if (ev->window.event == SDL_WINDOWEVENT_EXPOSED)
                kick();
            break;
#ifdef _WIN32
        case SDL_SYSWMEVENT:
            check_exit_hotkey(ev->syswm.msg);
            break;
#endif
    }
}

int main(int argc, char *argv[])
{
    char *config_file_path = NULL;
    config.exe_path = SDL_GetBasePath();

    // Handle command line arguments, find config file
    handle_arguments(argc, argv, &config_file_path);

    // Parse config file for settings and menu entries
    parse_config_file(config_file_path);
    free(config_file_path);

    // Get default menu
    if (config.default_menu == NULL)
        log_fatal("No default menu defined in config file");
    default_menu = get_menu(config.default_menu);
    if (default_menu == NULL)
        log_fatal("Default menu %s not found in config file", config.default_menu);

    // Initialize SDL, verify all settings are in their allowable range
    init_sdl();
    init_sdl_image();
    init_sdl_ttf();
    validate_settings(&geo);
    find_assets();
    if (config.background_mode == BACKGROUND_SLIDESHOW)
        init_slideshow();
    create_window();

    // Initialize timing
    ticks.main = SDL_GetTicks();
    ticks.last_input = ticks.main;
    ticks.program_start = ticks.main;
    last_frame = ticks.main;

    // Load gamepad overrides
    if (config.gamepad_enabled && config.gamepad_mappings_file != NULL) {
        if (SDL_GameControllerAddMappingsFromFile(config.gamepad_mappings_file) < 0) {
            log_error("Could not load gamepad mappings from %s\n%s",
                config.gamepad_mappings_file,
                SDL_GetError()
            );
        }
    }

    // The loading screen is the home screen without its books: room, lamp, menu name, clock and hints
    if (!overlay_init(assets_dir))
        log_fatal("Could not load the interface fonts");
    setup_shelf();
    overlay_resize(win_w, win_h, shelf_per_page());
    overlay_set_clock(config.clock_enabled, config.clock_show_date);
    overlay_set_scrim(config.background_mode != BACKGROUND_TRANSPARENT);
    const char *trail[1] = { default_menu->name };
    overlay_set_trail(trail, 1);
    overlay_set_rail(0, 0, shelf_per_page());
    if (config.clock_enabled) {
        clock_init();
        update_clock();
    }

    // Background photo for Image and Slideshow modes
    if (config.background_mode == BACKGROUND_IMAGE) {
        SDL_Surface *surface = config.background_image ? IMG_Load(config.background_image) : NULL;
        if (surface == NULL)
            log_error("Couldn't load background image %s", config.background_image ? config.background_image : "(not set)");
        else {
            show_background(surface, false);
            SDL_FreeSurface(surface);
        }
    }
    else if (config.background_mode == BACKGROUND_SLIDESHOW && slideshow != NULL) {
        SDL_Surface *surface = load_next_slideshow_background(slideshow, false);
        if (surface != NULL) {
            show_background(surface, false);
            SDL_FreeSurface(surface);
        }
        ticks.slideshow_load = ticks.main;
    }
    overlay_loading(true);
    ticks.main = last_frame = SDL_GetTicks();
    loading_frame();
    build_shelf();

#ifdef _WIN32
    if (has_exit_hotkey())
        register_exit_hotkey();
#endif

    if (config.debug) {
        debug_video(render_info(), &display_mode);
        debug_settings();
        debug_gamepad(gamepad_controls);
        debug_hotkeys(hotkeys);
        debug_menu_entries(config.first_menu, config.num_menus);
    }

    // Show the default menu, its lamp warming up as the shelf fades in
    current_menu = default_menu;
    if (current_menu->shelf_index < 0)
        log_fatal("Could not load default menu %s", config.default_menu);
    // the books slide onto the shelf the way a new menu arrives
    shelf_enter(current_menu->shelf_index, 0);
    update_hud();
    overlay_loading(false);
    ticks.main = SDL_GetTicks();

    if (config.startup_cmd != NULL)
        execute_command(config.startup_cmd);

    log_debug("Begin program loop");
    while (1) {
        ticks.main = SDL_GetTicks();
        bool got_event;
        if (animating || state.application_launching)
            got_event = SDL_PollEvent(&event);
        else {
            // idle: wait for input, waking for the gamepad, the clock and the screensaver
            int timeout = state.application_running ? APPLICATION_WAIT_PERIOD : (gamepads != NULL ? 16 : 250);
            got_event = SDL_WaitEventTimeout(&event, timeout);
            ticks.main = SDL_GetTicks();
            if (!animating)
                last_frame = ticks.main - (Uint32) (1000 / 60);
        }
        while (got_event) {
            handle_event(&event);
            got_event = SDL_PollEvent(&event);
        }

        // Update application state
        if (state.application_running && state.has_focus) {
            state.application_running = false;
            post_launch();
            log_debug("Application finished");
        }
        if (state.application_running)
            continue;

        if (gamepads != NULL)
            poll_gamepad();
        if (config.background_mode == BACKGROUND_SLIDESHOW)
            update_slideshow();
        if (config.screensaver_enabled)
            update_screensaver();
        update_clock();
        update_launch();
        run_test_script();
        if (animating)
            frame();
    }
    quit(EXIT_SUCCESS);
}
