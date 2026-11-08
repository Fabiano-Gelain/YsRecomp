/*
 * Options menu of the PC version. Drawn by the runtime over the game's image, which stays paused while it
 * is open: nothing here touches the simulated console.
 *
 * Looks like Ys's own SELECT menu: black-background windows, 1-pixel white border with a 1-pixel margin,
 * the game's 8x8 font in uppercase (app/menu_font.h), 8-pixel lines, centered title, the little arrow on
 * the left (with a space before the text) blinking 8 frames on and 8 off, values on the right in light
 * gold ("Off" in light gray) and the help footer in gray. Each page opens in a new window over the previous
 * one, shifted to the right (like INVENTORY over the game's menu); the ones below keep the arrow still on the
 * item that opened the one above.
 *
 * Main page: "Resume", "Video", "Image", "Audio", "Controls", "Gameplay" and "Quit game" (asks for
 * confirmation: "Are you sure?" Yes/No, with No selected at first).
 *   Gameplay: "Skip intro" On/Off (open straight into the game's main menu; skipping screen by screen with
 *     Enter/A is always on; takes effect the next time the game starts) and "Pause in background" On/Off;
 *     "Enemy HP bars" and "Reward popups" On/Off (app/battle.h; stored with the screen options, m->video);
 *     "Instant dialog" On/Off (app/ys_dialog.h: changes the game; the recorded session notes the switch).
 *   Video: Fullscreen, Resolution, Refresh rate, V-Sync, Window size (app/video.h; left/right change the
 *     value; they take effect with "Apply"), Smooth motion and a line with the mode in use.
 *   Image: Integer scale On/Off, Filter Sharp/Smooth, CRT effect Off/Scanlines/Lines+mask, CRT brightness
 *     Normal/Bright, Pixel aspect TV/Square/Stretch, Crop border Frame/Full/Compact, HUD background
 *     Black/Teal/Parchment/Graphite/Blue (panel background in Full) (app/video.h; applied at once, without
 *     changing the screen mode).
 *   Audio: volumes (CD music, chip music, effects, voices, master; left/right change in steps of 10).
 *   Controls: "Keyboard" and "Controller" (pages with the key/button for each PC-Engine button, with the
 *     name of what it does in Ys; choosing an item waits for the new key; "Defaults" restores the default,
 *     "Layout" chooses the controller scheme).
 * Every page ends in "Back"; footer with the help keys.
 */
#ifndef PCE_APP_MENU_H
#define PCE_APP_MENU_H

#include <stdint.h>

#include "app/input.h"
#include "app/video.h"
#include "pce/mix.h"

typedef enum MenuKey {
    MENU_KEY_UP,
    MENU_KEY_DOWN,
    MENU_KEY_LEFT,
    MENU_KEY_RIGHT,
    MENU_KEY_OK,   /* Enter, button I */
    MENU_KEY_BACK, /* Esc, button II: goes back (or closes the menu) */
} MenuKey;

/* Result of menu_key (bits) */
#define MENU_CHANGED_VOLUME 1 /* a volume changed: apply now */
#define MENU_CLOSED 2         /* the menu closed: the game resumes */
#define MENU_QUIT 4           /* the player confirmed they want to quit */
#define MENU_CHANGED_SETTING 8 /* another option changed (Gameplay): save to the config */
#define MENU_CHANGED_INPUT 16  /* key or button changed: apply now (m->input) */
#define MENU_CHANGED_VIDEO 32  /* screen option changed: apply now (m->video) */

/* Main page */
typedef enum MenuItem {
    MENU_ITEM_RESUME,
    MENU_ITEM_VIDEO,
    MENU_ITEM_IMAGE,
    MENU_ITEM_AUDIO,
    MENU_ITEM_CONTROLS,
    MENU_ITEM_GAMEPLAY,
    MENU_ITEM_QUIT,
    MENU_ITEM_COUNT
} MenuItem;

/* Video page */
enum {
    MENU_VIDEO_FULLSCREEN,
    MENU_VIDEO_RESOLUTION,
    MENU_VIDEO_REFRESH,
    MENU_VIDEO_VSYNC,
    MENU_VIDEO_WINDOW_SIZE,
    MENU_VIDEO_APPLY,  /* applies the 5 options above (which change the screen mode) and asks for confirmation */
    MENU_VIDEO_SMOOTH, /* smooth motion (app/motion.h) */
    MENU_VIDEO_FADES,  /* smooth fades (app/fade.h) */
    MENU_VIDEO_BACK,
    MENU_VIDEO_COUNT
};

/* Image page */
enum { MENU_IMAGE_INTEGER, MENU_IMAGE_FILTER, MENU_IMAGE_CRT, MENU_IMAGE_CRT_BRIGHT, MENU_IMAGE_ASPECT, MENU_IMAGE_CROP, MENU_IMAGE_HUD,
       MENU_IMAGE_EQUIP /* equipment on Adol */, MENU_IMAGE_BACK, MENU_IMAGE_COUNT };

/* Audio page */
enum {
    MENU_AUDIO_MUSIC,
    MENU_AUDIO_CHIP_MUSIC,
    MENU_AUDIO_EFFECTS,
    MENU_AUDIO_VOICES,
    MENU_AUDIO_MASTER,
    MENU_AUDIO_FILTER, /* chip filter */
    MENU_AUDIO_BACK,
    MENU_AUDIO_COUNT
};

/* Gameplay page */
enum { MENU_GAMEPLAY_SKIP_INTRO, MENU_GAMEPLAY_PAUSE_BG, MENU_GAMEPLAY_BARS, MENU_GAMEPLAY_POPUPS, MENU_GAMEPLAY_DIALOG, MENU_GAMEPLAY_BACK,
       MENU_GAMEPLAY_COUNT };

/* Controls page */
enum { MENU_CONTROLS_KEYBOARD, MENU_CONTROLS_CONTROLLER, MENU_CONTROLS_BACK, MENU_CONTROLS_COUNT };

/* Keyboard/Controller pages: the PC-Engine buttons (PadFunction), then these. On the Controller page,
 * "Defaults" is "Layout" (Modern/Original; Custom when some button was changed by hand). */
#define MENU_PAGE_DEFAULTS PAD_FN_COUNT
#define MENU_PAGE_BACK (PAD_FN_COUNT + 1)
#define MENU_PAGE_COUNT (PAD_FN_COUNT + 2)

typedef enum MenuPage {
    MENU_PAGE_MAIN,
    MENU_PAGE_VIDEO,
    MENU_PAGE_AUDIO,
    MENU_PAGE_CONTROLS,
    MENU_PAGE_KEYBOARD,
    MENU_PAGE_CONTROLLER,
    MENU_PAGE_IMAGE,
    MENU_PAGE_GAMEPLAY
} MenuPage;

typedef struct Menu {
    int open;
    int confirm_quit; /* "Are you sure?" screen */
    int cursor;       /* MenuItem (main page) */
    int yes;          /* in the confirmation: 1 = Yes selected */
    AudioVolumes vol;
    int skip_intro;
    int pause_bg; /* "Pause in background": pauses the game when the window loses focus */
    InputMap input;
    VideoSettings video;
    const VideoModes *modes; /* monitor modes (for Resolution/Refresh rate); NULL = only "Desktop"/"Auto" */
    char video_info[32];     /* mode in use, shown on the Video page (the window updates it) */
    int page;    /* MenuPage */
    int item;    /* cursor on the other pages */
    int waiting; /* waiting for the new key/button of the chosen item */
    int notice;  /* notice in the title: INPUT_SET_SWAPPED/INPUT_SET_RESERVED (gone on the next key) */
    int pad_mode;     /* last device used (the window updates it): 1 = controller, 0 = keyboard; changes the footer */
    int swapped_with; /* with INPUT_SET_SWAPPED: the function that got the old key/button */
    /* Video page: Fullscreen..Window size change only pending; "Apply" moves it to video and opens "Keep these
     * settings?" (keep = Yes selected), which reverts by itself to the previous mode after MENU_CONFIRM_MS */
    VideoSettings pending, before_apply;
    int confirm_video, keep;
    int confirm_left_ms;
    uint64_t confirm_last_ms;
    int sfx; /* the effect to play after menu_key (MENU_SFX_*); whoever plays it sets MENU_SFX_NONE */
} Menu;

/* Menu effects (Ys's own, as in the equipment window; the same numbers as LAUNCHER_SFX_*) */
enum { MENU_SFX_NONE = -1, MENU_SFX_MOVE = 0, MENU_SFX_OK = 1 };

/* Opens with the current options, cursor on the first item. */
void menu_open(Menu *m, const AudioVolumes *vol, int skip_intro, int pause_bg, const InputMap *input,
               const VideoSettings *video, const VideoModes *modes);
/* One key; returns MENU_CHANGED_*, MENU_CLOSED, MENU_QUIT (bits, or 0). */
int menu_key(Menu *m, MenuKey key);
/* On each window loop, with the clock in ms: counts the screen-mode confirmation time; when it runs out,
 * reverts to the previous mode and returns MENU_CHANGED_VIDEO. */
int menu_tick(Menu *m, uint64_t ms);
#define MENU_CONFIRM_MS 10000
/* Waiting for a new key (Keyboard page) or new button (Controller): the window passes the raw key/button.
 * Esc and the Menu button cancel; a key/button of another function: the two swap (notice "Swapped with ...").
 * Returns MENU_CHANGED_INPUT or 0. */
int menu_waiting_key(const Menu *m);
int menu_waiting_button(const Menu *m);
int menu_capture_key(Menu *m, int scancode);
int menu_capture_button(Menu *m, int button);

/* Draws over the image (ARGB, w x h, pitch in pixels), centered. blink_on: arrow lit. */
void menu_draw(const Menu *m, uint32_t *pixels, int w, int h, int pitch, int blink_on);

/* Arrow lit at this instant (8 frames at 60 Hz on, 8 off), by the clock in ms. */
int menu_blink_on(uint64_t ms);

#endif
