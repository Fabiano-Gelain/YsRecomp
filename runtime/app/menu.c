#include "app/menu.h"

#include <stdio.h>
#include <string.h>

#include "app/menu_font.h"

#define CELL 8              /* 8x8 font and 8-pixel lines */
#define MAIN_COLS 23        /* width of the main window, in cells (with 1 of margin on each side) */
#define SUB_COLS 29         /* width of the other pages' windows */
#define STACK_DX (5 * CELL) /* each page opens 5 cells to the right of the one below */
#define TEXT_COL 3          /* arrow in cell 1, a space, the text in 3 */
#define TALLEST 144         /* the tallest window (Keyboard/Controller): the stack is centered vertically */
#define WHITE 0xFFFFFFFFu
#define GOLD 0xFFFFDB6Du     /* values */
#define OFF_GRAY 0xFFB6B6B6u /* "Off" value */
#define GRAY 0xFF929292u     /* footer, mode in use, "Apply" with no change */
#define BLACK 0xFF000000u

static const char *const k_main_labels[MENU_ITEM_COUNT] = { "Resume", "Video", "Image", "Audio", "Controls", "Gameplay",
                                                            "Quit game" };
static const char *const k_video_labels[MENU_VIDEO_COUNT] = { "Fullscreen", "Resolution", "Refresh rate", "V-Sync",
                                                              "Window size", "Apply", "Smooth motion", "Smooth fades", "Back" };
static const char *const k_image_labels[MENU_IMAGE_COUNT] = { "Integer scale", "Filter", "CRT effect", "CRT brightness",
                                                              "Pixel aspect", "Crop border", "HUD background",
                                                              "Visual equipment", "Back" };
static const char *const k_crt_values[VIDEO_CRT_COUNT] = { "Off",  "Scanlines", "Lines+mask", "Slot mask",
                                                             "Shadow mask", "Glow", "Glow+mask", "Glow+slot" };
static const char *const k_crop_values[CROP_COUNT] = { "Off", "Original", "Full", "Compact", "Wide", "Fill" };
static const char *const k_aspect_values[VIDEO_ASPECT_COUNT] = { "TV", "Square", "Stretch" };
static const char *const k_hud_values[CROP_HUD_COUNT] = {
    "Black",   "Charcoal", "Slate",  "Midnight", "Navy",    "Indigo", "Plum",   "Wine",
    "Oxblood", "Leather",  "Walnut", "Petrol",   "Deepsea", "Pine",   "Shadow",
};
static const char *const k_audio_labels[MENU_AUDIO_COUNT] = { "CD music", "Chip music", "Effects", "Voices",
                                                              "Master", "Chip filter", "Back" };
static const char *const k_controls_labels[MENU_CONTROLS_COUNT] = { "Keyboard", "Controller", "Back" };
static const char *const k_gameplay_labels[MENU_GAMEPLAY_COUNT] = { "Skip intro", "Pause in background", "Enemy HP bars",
                                                                        "Reward popups", "Instant dialog", "Back" };

void menu_open(Menu *m, const AudioVolumes *vol, int skip_intro, int pause_bg, const InputMap *input,
               const VideoSettings *video, const VideoModes *modes)
{
    memset(m, 0, sizeof(*m));
    m->open = 1;
    m->vol = *vol;
    m->skip_intro = skip_intro;
    m->pause_bg = pause_bg;
    m->input = *input;
    m->video = *video;
    m->pending = *video;
    m->modes = modes;
    m->sfx = MENU_SFX_OK; /* opens like the game's window */
}

static int *volume_of(Menu *m, int item)
{
    switch (item) {
    case MENU_AUDIO_MUSIC: return &m->vol.music;
    case MENU_AUDIO_CHIP_MUSIC: return &m->vol.chip_music;
    case MENU_AUDIO_EFFECTS: return &m->vol.effects;
    case MENU_AUDIO_VOICES: return &m->vol.voices;
    case MENU_AUDIO_MASTER: return &m->vol.master;
    default: return NULL;
    }
}

/* Items of the current page */
static int page_count(const Menu *m)
{
    switch (m->page) {
    case MENU_PAGE_VIDEO: return MENU_VIDEO_COUNT;
    case MENU_PAGE_IMAGE: return MENU_IMAGE_COUNT;
    case MENU_PAGE_AUDIO: return MENU_AUDIO_COUNT;
    case MENU_PAGE_CONTROLS: return MENU_CONTROLS_COUNT;
    case MENU_PAGE_GAMEPLAY: return MENU_GAMEPLAY_COUNT;
    case MENU_PAGE_KEYBOARD:
    case MENU_PAGE_CONTROLLER: return MENU_PAGE_COUNT;
    default: return MENU_ITEM_COUNT;
    }
}

static void copy_display(VideoSettings *to, const VideoSettings *from);

/* Goes back to the page above, with the cursor on the item that opened this one */
static void page_back(Menu *m)
{
    if (m->page == MENU_PAGE_KEYBOARD || m->page == MENU_PAGE_CONTROLLER) {
        m->item = m->page == MENU_PAGE_KEYBOARD ? MENU_CONTROLS_KEYBOARD : MENU_CONTROLS_CONTROLLER;
        m->page = MENU_PAGE_CONTROLS;
        return;
    }
    if (m->page == MENU_PAGE_VIDEO)
        copy_display(&m->pending, &m->video); /* leaving without "Apply": what was chosen doesn't count */
    m->cursor = m->page == MENU_PAGE_VIDEO   ? MENU_ITEM_VIDEO
                : m->page == MENU_PAGE_IMAGE ? MENU_ITEM_IMAGE
                : m->page == MENU_PAGE_AUDIO ? MENU_ITEM_AUDIO
                : m->page == MENU_PAGE_GAMEPLAY ? MENU_ITEM_GAMEPLAY
                                             : MENU_ITEM_CONTROLS;
    m->page = MENU_PAGE_MAIN;
}

static void page_enter(Menu *m, int page)
{
    m->page = page;
    m->item = 0;
}

/* Chosen resolution (0 = the Windows one) and those in the monitor's list */
static int resolution_list(const Menu *m, VideoMode *out, int max)
{
    return m->modes ? video_resolutions(m->modes, out, max) : 0;
}

/* Next value (step = +1/-1, wrapping around) in a list of n options, the current one in cur (-1 = none) */
static int cycle(int cur, int n, int step)
{
    return n <= 0 ? 0 : (cur + step + n) % n;
}

/* The 5 options that change the screen mode, from from to to */
static void copy_display(VideoSettings *to, const VideoSettings *from)
{
    to->fullscreen = from->fullscreen;
    to->res_w = from->res_w;
    to->res_h = from->res_h;
    to->refresh = from->refresh;
    to->vsync = from->vsync;
    to->window_scale = from->window_scale;
}

/* "Apply": the chosen mode takes effect and the confirmation starts */
static int video_apply(Menu *m)
{
    if (video_same_display(&m->pending, &m->video))
        return 0;
    m->before_apply = m->video;
    copy_display(&m->video, &m->pending);
    m->confirm_video = 1;
    m->keep = 1;
    m->confirm_left_ms = MENU_CONFIRM_MS;
    m->confirm_last_ms = 0;
    return MENU_CHANGED_VIDEO;
}

/* Reverts to the mode from before "Apply" */
static int video_revert(Menu *m)
{
    m->confirm_video = 0;
    copy_display(&m->video, &m->before_apply);
    copy_display(&m->pending, &m->before_apply);
    return MENU_CHANGED_VIDEO;
}

int menu_tick(Menu *m, uint64_t ms)
{
    if (!m->open || !m->confirm_video)
        return 0;
    if (m->confirm_last_ms)
        m->confirm_left_ms -= (int)(ms - m->confirm_last_ms);
    m->confirm_last_ms = ms;
    return m->confirm_left_ms <= 0 ? video_revert(m) : 0;
}

/* Fullscreen..Window size only change the chosen mode (pending), without a screen flash; Smooth motion applies now */
static int video_change(Menu *m, int step)
{
    if (m->item == MENU_VIDEO_SMOOTH) {
        m->video.smooth_motion = m->pending.smooth_motion = !m->video.smooth_motion;
        return MENU_CHANGED_VIDEO;
    }
    if (m->item == MENU_VIDEO_FADES) {
        m->video.smooth_fades = m->pending.smooth_fades = !m->video.smooth_fades;
        return MENU_CHANGED_VIDEO;
    }
    VideoSettings *v = &m->pending;
    switch (m->item) {
    case MENU_VIDEO_FULLSCREEN:
        v->fullscreen = !v->fullscreen;
        break;
    case MENU_VIDEO_RESOLUTION: {
        VideoMode res[VIDEO_MAX_MODES];
        int n = resolution_list(m, res, VIDEO_MAX_MODES), cur = 0, res_w = v->res_w, res_h = v->res_h;
        for (int i = 0; i < n; i++)
            if (res[i].w == v->res_w && res[i].h == v->res_h)
                cur = i + 1;
        int next = cycle(cur, n + 1, step); /* 0 = Desktop */
        v->res_w = next ? res[next - 1].w : 0;
        v->res_h = next ? res[next - 1].h : 0;
        if (v->res_w != res_w || v->res_h != res_h)
            v->refresh = 0; /* the chosen refresh rate may not exist at the new resolution */
        break;
    }
    case MENU_VIDEO_REFRESH: {
        int rates[VIDEO_MAX_MODES], n = 0, cur = 0;
        if (m->modes) {
            int w = v->res_w ? v->res_w : m->modes->desktop.w, h = v->res_w ? v->res_h : m->modes->desktop.h;
            n = video_rates(m->modes, w, h, rates, VIDEO_MAX_MODES);
        }
        for (int i = 0; i < n; i++)
            if (rates[i] == v->refresh)
                cur = i + 1;
        int next = cycle(cur, n + 1, step); /* 0 = Auto */
        v->refresh = next ? rates[next - 1] : 0;
        break;
    }
    case MENU_VIDEO_VSYNC:
        v->vsync = !v->vsync;
        break;
    case MENU_VIDEO_WINDOW_SIZE:
        v->window_scale = VIDEO_SCALE_MIN +
                          cycle(v->window_scale - VIDEO_SCALE_MIN, VIDEO_SCALE_MAX - VIDEO_SCALE_MIN + 1, step);
        break;
    default:
        return 0;
    }
    return 0;
}

static int image_change(Menu *m, int step)
{
    VideoSettings *v = &m->video;
    switch (m->item) {
    case MENU_IMAGE_INTEGER: v->integer_scale = !v->integer_scale; break;
    case MENU_IMAGE_FILTER: v->smooth = !v->smooth; break;
    case MENU_IMAGE_CRT: v->crt = cycle(v->crt, VIDEO_CRT_COUNT, step); break;
    case MENU_IMAGE_CRT_BRIGHT: v->crt_bright = !v->crt_bright; break;
    case MENU_IMAGE_ASPECT: v->aspect = cycle(v->aspect, VIDEO_ASPECT_COUNT, step); break;
    case MENU_IMAGE_CROP: /* Frame, Full, Compact, Wide, Fill (without the border, off, is not offered) */
        v->crop = CROP_FRAME + cycle(v->crop - CROP_FRAME, CROP_COUNT - CROP_FRAME, step);
        break;
    case MENU_IMAGE_HUD: v->hud_bg = cycle(v->hud_bg, CROP_HUD_COUNT, step); break;
    case MENU_IMAGE_EQUIP: v->equipment = !v->equipment; break;
    default: return 0;
    }
    return MENU_CHANGED_VIDEO;
}

int menu_waiting_key(const Menu *m)
{
    return m->open && m->waiting && m->page == MENU_PAGE_KEYBOARD;
}

int menu_waiting_button(const Menu *m)
{
    return m->open && m->waiting && m->page == MENU_PAGE_CONTROLLER;
}

static int capture(Menu *m, int result)
{
    m->notice = result == INPUT_SET_OK ? 0 : result;
    m->waiting = 0;
    return result == INPUT_SET_OK || result == INPUT_SET_SWAPPED ? MENU_CHANGED_INPUT : 0;
}

int menu_capture_key(Menu *m, int scancode)
{
    if (!menu_waiting_key(m))
        return 0;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        m->waiting = m->notice = 0;
        return 0;
    }
    return capture(m, input_set_key(&m->input, m->item, scancode, &m->swapped_with));
}

int menu_capture_button(Menu *m, int button)
{
    if (!menu_waiting_button(m))
        return 0;
    if (button == SDL_GAMEPAD_BUTTON_START) {
        m->waiting = m->notice = 0;
        return 0;
    }
    return capture(m, input_set_button(&m->input, m->item, button, &m->swapped_with));
}

/* Controller layout: Modern <-> Original (from Custom goes to Modern) */
static int next_layout(Menu *m)
{
    input_set_layout(&m->input, input_layout(&m->input) == INPUT_LAYOUT_MODERN ? INPUT_LAYOUT_ORIGINAL
                                                                                 : INPUT_LAYOUT_MODERN);
    return MENU_CHANGED_INPUT;
}

/* Gameplay page: On/Off; Reward popups: Off, Large, Small */
static int gameplay_toggle(Menu *m, int step)
{
    if (m->item == MENU_GAMEPLAY_SKIP_INTRO)
        m->skip_intro = !m->skip_intro;
    else if (m->item == MENU_GAMEPLAY_PAUSE_BG)
        m->pause_bg = !m->pause_bg;
    else if (m->item == MENU_GAMEPLAY_BARS) { /* stored with the screen options (video.h) */
        m->video.enemy_bars = !m->video.enemy_bars;
        return MENU_CHANGED_VIDEO;
    } else if (m->item == MENU_GAMEPLAY_POPUPS) {
        m->video.reward_popups = cycle(m->video.reward_popups, 3, step);
        return MENU_CHANGED_VIDEO;
    } else if (m->item == MENU_GAMEPLAY_DIALOG) { /* changes the game (app/ys_dialog.h) */
        m->video.instant_dialog = !m->video.instant_dialog;
        return MENU_CHANGED_VIDEO;
    } else
        return 0;
    return MENU_CHANGED_SETTING;
}

/* Video, Image, Audio, Controls and Gameplay pages */
static int submenu_key(Menu *m, MenuKey key)
{
    int back = m->page == MENU_PAGE_VIDEO      ? MENU_VIDEO_BACK
               : m->page == MENU_PAGE_IMAGE    ? MENU_IMAGE_BACK
               : m->page == MENU_PAGE_AUDIO    ? MENU_AUDIO_BACK
               : m->page == MENU_PAGE_GAMEPLAY ? MENU_GAMEPLAY_BACK
                                               : MENU_CONTROLS_BACK;
    int *vol = m->page == MENU_PAGE_AUDIO ? volume_of(m, m->item) : NULL;
    switch (key) {
    case MENU_KEY_LEFT:
    case MENU_KEY_RIGHT:
        if (m->page == MENU_PAGE_VIDEO)
            return video_change(m, key == MENU_KEY_RIGHT ? 1 : -1);
        if (m->page == MENU_PAGE_IMAGE)
            return image_change(m, key == MENU_KEY_RIGHT ? 1 : -1);
        if (m->page == MENU_PAGE_GAMEPLAY)
            return gameplay_toggle(m, key == MENU_KEY_RIGHT ? 1 : -1);
        if (m->page == MENU_PAGE_AUDIO && m->item == MENU_AUDIO_FILTER) {
            m->vol.chip_filter = !m->vol.chip_filter;
            return MENU_CHANGED_VOLUME;
        }
        if (vol) {
            int v = *vol + (key == MENU_KEY_RIGHT ? 10 : -10);
            v = v < 0 ? 0 : v > 100 ? 100 : v / 10 * 10;
            if (v != *vol) {
                *vol = v;
                return MENU_CHANGED_VOLUME;
            }
        }
        break;
    case MENU_KEY_OK:
        if (m->item == back) {
            page_back(m);
            break;
        }
        if (m->page == MENU_PAGE_VIDEO && m->item == MENU_VIDEO_APPLY)
            return video_apply(m);
        if (m->page == MENU_PAGE_VIDEO)
            return video_change(m, 1);
        if (m->page == MENU_PAGE_IMAGE)
            return image_change(m, 1);
        if (m->page == MENU_PAGE_GAMEPLAY)
            return gameplay_toggle(m, 1);
        if (m->page == MENU_PAGE_AUDIO && m->item == MENU_AUDIO_FILTER) {
            m->vol.chip_filter = !m->vol.chip_filter;
            return MENU_CHANGED_VOLUME;
        }
        if (m->page == MENU_PAGE_CONTROLS)
            page_enter(m, m->item == MENU_CONTROLS_KEYBOARD ? MENU_PAGE_KEYBOARD : MENU_PAGE_CONTROLLER);
        break;
    case MENU_KEY_BACK:
        page_back(m);
        break;
    default:
        break;
    }
    return 0;
}

/* Keyboard/Controller pages */
static int page_key(Menu *m, MenuKey key)
{
    switch (key) {
    case MENU_KEY_LEFT:
    case MENU_KEY_RIGHT:
        if (m->item == MENU_PAGE_DEFAULTS && m->page == MENU_PAGE_CONTROLLER)
            return next_layout(m);
        break;
    case MENU_KEY_OK:
        if (m->item < PAD_FN_COUNT) {
            m->waiting = 1;
            break;
        }
        if (m->item == MENU_PAGE_DEFAULTS && m->page == MENU_PAGE_CONTROLLER)
            return next_layout(m);
        if (m->item == MENU_PAGE_DEFAULTS) {
            InputMap before = m->input;
            if (m->page == MENU_PAGE_KEYBOARD)
                input_default_keys(&m->input);
            else
                input_default_buttons(&m->input);
            return memcmp(&before, &m->input, sizeof(before)) ? MENU_CHANGED_INPUT : 0;
        }
        /* fall through: Back */
    case MENU_KEY_BACK:
        page_back(m);
        break;
    default:
        break;
    }
    return 0;
}

static int menu_key_do(Menu *m, MenuKey key);

int menu_key(Menu *m, MenuKey key)
{
    if (!m->open)
        return 0;
    /* as in Ys's equipment window: moving (and changing a value, going back) = cursor; confirming = confirm */
    m->sfx = key == MENU_KEY_OK ? MENU_SFX_OK : MENU_SFX_MOVE;
    return menu_key_do(m, key);
}

static int menu_key_do(Menu *m, MenuKey key)
{
    m->notice = 0;
    if (m->waiting) {
        if (key == MENU_KEY_BACK)
            m->waiting = 0;
        return 0;
    }
    if (m->confirm_video) {
        /* "Keep these settings?": Yes keeps; No, Esc or the timeout revert to the previous mode */
        if (key == MENU_KEY_UP || key == MENU_KEY_DOWN)
            m->keep = !m->keep;
        else if (key == MENU_KEY_OK && m->keep)
            m->confirm_video = 0;
        else if (key == MENU_KEY_OK || key == MENU_KEY_BACK)
            return video_revert(m);
        return 0;
    }
    if (m->page != MENU_PAGE_MAIN && (key == MENU_KEY_UP || key == MENU_KEY_DOWN)) {
        int n = page_count(m);
        m->item = (m->item + (key == MENU_KEY_UP ? n - 1 : 1)) % n;
        return 0;
    }
    if (m->page == MENU_PAGE_KEYBOARD || m->page == MENU_PAGE_CONTROLLER)
        return page_key(m, key);
    if (m->page != MENU_PAGE_MAIN)
        return submenu_key(m, key);
    if (m->confirm_quit) {
        if (key == MENU_KEY_UP || key == MENU_KEY_DOWN)
            m->yes = !m->yes;
        else if (key == MENU_KEY_OK && m->yes)
            return MENU_QUIT;
        else if (key == MENU_KEY_OK || key == MENU_KEY_BACK)
            m->confirm_quit = 0;
        return 0;
    }
    switch (key) {
    case MENU_KEY_UP:
        m->cursor = (m->cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
        break;
    case MENU_KEY_DOWN:
        m->cursor = (m->cursor + 1) % MENU_ITEM_COUNT;
        break;
    case MENU_KEY_OK:
        if (m->cursor == MENU_ITEM_VIDEO || m->cursor == MENU_ITEM_IMAGE || m->cursor == MENU_ITEM_AUDIO ||
            m->cursor == MENU_ITEM_CONTROLS || m->cursor == MENU_ITEM_GAMEPLAY) {
            page_enter(m, m->cursor == MENU_ITEM_VIDEO      ? MENU_PAGE_VIDEO
                          : m->cursor == MENU_ITEM_IMAGE    ? MENU_PAGE_IMAGE
                          : m->cursor == MENU_ITEM_AUDIO    ? MENU_PAGE_AUDIO
                          : m->cursor == MENU_ITEM_GAMEPLAY ? MENU_PAGE_GAMEPLAY
                                                            : MENU_PAGE_CONTROLS);
            break;
        }
        if (m->cursor == MENU_ITEM_RESUME) {
            m->open = 0;
            return MENU_CLOSED;
        }
        if (m->cursor == MENU_ITEM_QUIT) {
            m->confirm_quit = 1;
            m->yes = 0;
        }
        break;
    case MENU_KEY_BACK:
        m->open = 0;
        return MENU_CLOSED;
    }
    return 0;
}

int menu_blink_on(uint64_t ms)
{
    return (ms * 60 / 1000 / 8) % 2 == 0;
}

/* ---- Drawing ---- */

typedef struct Canvas {
    uint32_t *pixels;
    int w, h, pitch;
} Canvas;

static void plot(const Canvas *c, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h)
        c->pixels[y * c->pitch + x] = color;
}

static void fill(const Canvas *c, int x0, int y0, int w, int h, uint32_t color)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            plot(c, x, y, color);
}

/* Text starting at (x, y) in pixels, one 8x8 letter per cell */
static void draw_text(const Canvas *c, int x, int y, const char *text, uint32_t color)
{
    for (; *text; text++, x += CELL) {
        uint8_t rows[8];
        if (!menu_font_glyph(*text, rows))
            continue;
        for (int r = 0; r < 8; r++)
            for (int k = 0; k < 8; k++)
                if (rows[r] & (0x80 >> k))
                    plot(c, x + k, y + r, color);
    }
}

static void draw_arrow(const Canvas *c, int x, int y)
{
    uint8_t rows[8];
    menu_font_arrow(rows);
    for (int r = 0; r < 8; r++)
        for (int k = 0; k < 8; k++)
            if (rows[r] & (0x80 >> k))
                plot(c, x + k, y + r, WHITE);
}

/* One line of the window: item (possible arrow, value on the right) or information (gray, centered) */
typedef struct Row {
    const char *label;
    char value[24];
    uint32_t label_color, value_color;
    int info;
} Row;

#define MAX_ROWS 12

typedef struct Page {
    const char *title;
    Row row[MAX_ROWS];
    int n;
    int sep_last; /* line separating the last one (Back, Quit game) */
    int cursor;   /* arrow row */
    int cols;
    const char *footer;
} Page;

static Row *add_row(Page *p, const char *label)
{
    Row *r = &p->row[p->n++];
    memset(r, 0, sizeof(*r));
    r->label = label;
    r->label_color = WHITE;
    r->value_color = GOLD;
    return r;
}

static void add_info(Page *p, const char *text)
{
    add_row(p, text)->info = 1;
}

static void add_onoff(Page *p, const char *label, int on)
{
    Row *r = add_row(p, label);
    snprintf(r->value, sizeof(r->value), on ? "On" : "Off");
    r->value_color = on ? GOLD : OFF_GRAY;
}

static void add_value(Page *p, const char *label, const char *value)
{
    snprintf(add_row(p, label)->value, sizeof(p->row[0].value), "%s", value);
}

static int page_height(const Page *p)
{
    /* margin, title, line, items, footer */
    return 8 + 8 + 8 + CELL * p->n + (p->sep_last ? 8 : 0) + 8 + 8 + 8;
}

/* Page window at (x, y): black background, 1-pixel white border with 1 of margin, centered title and a
 * line under it, the items, footer (only on the top window). arrow: arrow lit. */
static void draw_page(const Canvas *c, int x, int y, const Page *p, int arrow, int top)
{
    int w = p->cols * CELL, h = page_height(p);
    if (x + w > c->w)
        x = c->w - w;
    if (x < 0)
        x = 0;
    fill(c, x, y, w, h, BLACK);
    fill(c, x + 1, y + 1, w - 2, 1, WHITE);
    fill(c, x + 1, y + h - 2, w - 2, 1, WHITE);
    fill(c, x + 1, y + 1, 1, h - 2, WHITE);
    fill(c, x + w - 2, y + 1, 1, h - 2, WHITE);
    draw_text(c, x + (w - (int)strlen(p->title) * CELL) / 2, y + 8, p->title, WHITE);
    fill(c, x + 5, y + 19, w - 10, 1, WHITE);
    int ry = y + 24;
    for (int i = 0; i < p->n; i++, ry += CELL) {
        const Row *r = &p->row[i];
        if (i == p->n - 1 && p->sep_last) {
            fill(c, x + 5, ry + 3, w - 10, 1, WHITE);
            ry += 8;
        }
        if (r->info) {
            draw_text(c, x + (w - (int)strlen(r->label) * CELL) / 2, ry, r->label, GRAY);
            continue;
        }
        draw_text(c, x + TEXT_COL * CELL, ry, r->label, r->label_color);
        draw_text(c, x + (p->cols - 1 - (int)strlen(r->value)) * CELL, ry, r->value, r->value_color);
        if (i == p->cursor && arrow)
            draw_arrow(c, x + CELL, ry);
    }
    if (top && p->footer)
        draw_text(c, x + (w - (int)strlen(p->footer) * CELL) / 2, ry + 8, p->footer, GRAY);
}

static const char *select_help(const Menu *m)
{
    return m->pad_mode ? "A:SELECT B:BACK" : "ENTER:SELECT ESC:BACK";
}

static void main_page(const Menu *m, Page *p)
{
    memset(p, 0, sizeof(*p));
    p->title = "OPTIONS";
    p->cols = MAIN_COLS;
    p->sep_last = 1;
    for (int i = 0; i < MENU_ITEM_COUNT; i++)
        add_row(p, k_main_labels[i]);
    p->cursor = m->cursor;
    p->footer = select_help(m);
}

/* Video, Image, Audio, Controls and Gameplay pages; item = the page item with the arrow */
static void sub_page(const Menu *m, int page, int item, Page *p)
{
    memset(p, 0, sizeof(*p));
    p->cols = SUB_COLS;
    p->sep_last = 1;
    p->footer = select_help(m);
    p->cursor = item;
    char v[24];
    if (page == MENU_PAGE_VIDEO) {
        const VideoSettings *s = &m->pending; /* the chosen one */
        p->title = "VIDEO";
        add_onoff(p, k_video_labels[MENU_VIDEO_FULLSCREEN], s->fullscreen);
        if (s->res_w)
            snprintf(v, sizeof(v), "%dx%d", s->res_w, s->res_h);
        else
            snprintf(v, sizeof(v), "Desktop");
        add_value(p, k_video_labels[MENU_VIDEO_RESOLUTION], v);
        if (s->refresh)
            snprintf(v, sizeof(v), "%d Hz", s->refresh);
        else
            snprintf(v, sizeof(v), "Auto");
        add_value(p, k_video_labels[MENU_VIDEO_REFRESH], v);
        add_onoff(p, k_video_labels[MENU_VIDEO_VSYNC], s->vsync);
        snprintf(v, sizeof(v), "%dx", s->window_scale);
        add_value(p, k_video_labels[MENU_VIDEO_WINDOW_SIZE], v);
        /* "Apply" in gray while no new mode is chosen */
        add_row(p, k_video_labels[MENU_VIDEO_APPLY])->label_color =
            video_same_display(&m->pending, &m->video) ? GRAY : WHITE;
        add_onoff(p, k_video_labels[MENU_VIDEO_SMOOTH], m->video.smooth_motion);
        add_onoff(p, k_video_labels[MENU_VIDEO_FADES], m->video.smooth_fades);
        add_info(p, m->video_info); /* mode in use (not an option) */
        add_row(p, k_video_labels[MENU_VIDEO_BACK]);
        if (item >= MENU_VIDEO_BACK)
            p->cursor = item + 1;
    } else if (page == MENU_PAGE_IMAGE) {
        const VideoSettings *s = &m->video;
        p->title = "IMAGE";
        add_onoff(p, k_image_labels[MENU_IMAGE_INTEGER], s->integer_scale);
        add_value(p, k_image_labels[MENU_IMAGE_FILTER], s->smooth ? "Smooth" : "Sharp");
        add_value(p, k_image_labels[MENU_IMAGE_CRT], k_crt_values[s->crt >= 0 && s->crt < VIDEO_CRT_COUNT ? s->crt : 0]);
        if (s->crt == VIDEO_CRT_OFF)
            p->row[p->n - 1].value_color = OFF_GRAY;
        add_value(p, k_image_labels[MENU_IMAGE_CRT_BRIGHT], s->crt_bright ? "Bright" : "Normal");
        add_value(p, k_image_labels[MENU_IMAGE_ASPECT],
                  k_aspect_values[s->aspect >= 0 && s->aspect < VIDEO_ASPECT_COUNT ? s->aspect : 0]);
        add_value(p, k_image_labels[MENU_IMAGE_CROP], k_crop_values[s->crop >= 0 && s->crop < CROP_COUNT ? s->crop : 0]);
        add_value(p, k_image_labels[MENU_IMAGE_HUD],
                  k_hud_values[s->hud_bg >= 0 && s->hud_bg < CROP_HUD_COUNT ? s->hud_bg : 0]);
        add_onoff(p, k_image_labels[MENU_IMAGE_EQUIP], s->equipment);
        add_row(p, k_image_labels[MENU_IMAGE_BACK]);
    } else if (page == MENU_PAGE_AUDIO) {
        p->title = "AUDIO";
        for (int i = 0; i < MENU_AUDIO_FILTER; i++) {
            snprintf(v, sizeof(v), "%d%%", *volume_of((Menu *)m, i));
            add_value(p, k_audio_labels[i], v);
        }
        add_onoff(p, k_audio_labels[MENU_AUDIO_FILTER], m->vol.chip_filter);
        add_row(p, k_audio_labels[MENU_AUDIO_BACK]);
    } else if (page == MENU_PAGE_GAMEPLAY) {
        p->title = "GAMEPLAY";
        add_onoff(p, k_gameplay_labels[MENU_GAMEPLAY_SKIP_INTRO], m->skip_intro);
        add_onoff(p, k_gameplay_labels[MENU_GAMEPLAY_PAUSE_BG], m->pause_bg);
        add_onoff(p, k_gameplay_labels[MENU_GAMEPLAY_BARS], m->video.enemy_bars);
        static const char *const k_popups[3] = { "Off", "Large", "Small" };
        int pop = m->video.reward_popups >= 0 && m->video.reward_popups < 3 ? m->video.reward_popups : 0;
        add_value(p, k_gameplay_labels[MENU_GAMEPLAY_POPUPS], k_popups[pop]);
        if (!pop)
            p->row[p->n - 1].value_color = OFF_GRAY;
        add_onoff(p, k_gameplay_labels[MENU_GAMEPLAY_DIALOG], m->video.instant_dialog);
        add_row(p, k_gameplay_labels[MENU_GAMEPLAY_BACK]);
    } else {
        p->title = "CONTROLS";
        for (int i = 0; i < MENU_CONTROLS_COUNT; i++)
            add_row(p, k_controls_labels[i]);
    }
}

/* Keyboard/Controller pages */
static void keys_page(const Menu *m, Page *p, char *title, size_t title_size)
{
    int keyboard = m->page == MENU_PAGE_KEYBOARD;
    memset(p, 0, sizeof(*p));
    p->cols = SUB_COLS;
    p->sep_last = 1;
    p->cursor = m->item;
    if (m->notice == INPUT_SET_SWAPPED)
        snprintf(title, title_size, "Swapped with %s", input_function_name(m->swapped_with));
    else
        snprintf(title, title_size, "%s",
                 m->notice == INPUT_SET_RESERVED ? "Not allowed"
                 : m->waiting                    ? (keyboard ? "Press a key" : "Press a button")
                 : keyboard                      ? "KEYBOARD"
                                                 : "CONTROLLER");
    p->title = title;
    for (int i = 0; i < PAD_FN_COUNT; i++) {
        char v[11]; /* fits to the right of the longest name */
        snprintf(v, sizeof(v), "%s", keyboard ? input_key_name(m->input.key[i]) : input_button_name(m->input.button[i]));
        add_value(p, input_function_name(i), v);
    }
    if (keyboard)
        add_row(p, "Defaults");
    else
        add_value(p, "Layout", input_layout_name(input_layout(&m->input)));
    add_row(p, "Back");
    p->footer = m->waiting ? (m->pad_mode ? "MENU:CANCEL" : "ESC:CANCEL")
                : m->pad_mode ? "A:CHANGE B:BACK"
                              : "ENTER:CHANGE ESC:BACK";
}

/* Confirmations: "Quit the game?" (No/Yes) and "Keep these settings?" (Yes/No, with the time left) */
static void confirm_page(const Menu *m, Page *p, char *left, size_t left_size)
{
    memset(p, 0, sizeof(*p));
    p->cols = MAIN_COLS;
    p->footer = select_help(m);
    if (m->confirm_quit) {
        p->title = "QUIT THE GAME?";
        add_info(p, "Are you sure?");
        add_row(p, "No");
        add_row(p, "Yes");
        p->cursor = m->yes ? 2 : 1;
        return;
    }
    int s = (m->confirm_left_ms + 999) / 1000;
    snprintf(left, left_size, "Reverting in %d s", s < 0 ? 0 : s);
    p->title = "KEEP THESE SETTINGS?";
    add_info(p, left);
    add_row(p, "Yes");
    add_row(p, "No");
    p->cursor = m->keep ? 1 : 2;
}

void menu_draw(const Menu *m, uint32_t *pixels, int w, int h, int pitch, int blink_on)
{
    if (!m->open)
        return;
    Canvas c = { pixels, w, h, pitch };
    /* The window stack (main, page, inner page or confirmation) is centered; in a narrow image (wide and fill
     * crops) the pages shift less, so the top one fits */
    int dx = STACK_DX;
    if (2 * dx + SUB_COLS * CELL > w)
        dx = w > SUB_COLS * CELL ? (w - SUB_COLS * CELL) / 2 / CELL * CELL : 0;
    int x0 = (w - (2 * dx + SUB_COLS * CELL)) / 2 / CELL * CELL, y0 = (h - TALLEST) / 2 / CELL * CELL;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    Page pages[3];
    char title[48], left[32];
    int n = 1;
    main_page(m, &pages[0]);
    if (m->confirm_quit) {
        confirm_page(m, &pages[n++], left, sizeof(left));
    } else if (m->page == MENU_PAGE_KEYBOARD || m->page == MENU_PAGE_CONTROLLER) {
        sub_page(m, MENU_PAGE_CONTROLS,
                 m->page == MENU_PAGE_KEYBOARD ? MENU_CONTROLS_KEYBOARD : MENU_CONTROLS_CONTROLLER, &pages[n++]);
        keys_page(m, &pages[n++], title, sizeof(title));
    } else if (m->page != MENU_PAGE_MAIN) {
        sub_page(m, m->page, m->item, &pages[n++]);
        if (m->confirm_video)
            confirm_page(m, &pages[n++], left, sizeof(left));
    }
    for (int i = 0; i < n; i++) {
        int top = i == n - 1;
        /* Arrow: blinks on the top window (lit while waiting for the new key); on the ones below, still on the
         * item that opened the one above */
        draw_page(&c, x0 + dx * i, y0, &pages[i], top ? blink_on || m->waiting : 1, top);
    }
}
