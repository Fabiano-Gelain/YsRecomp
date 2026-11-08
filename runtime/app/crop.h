/*
 * Crop of the border and the overscan: an image option on the PC side only (does not touch the console).
 *
 * In the game, the Ys image (341 x 242 pixels, 7.16 MHz mode) has a fixed golden border around the map and
 * the H.P/EXP/GOLD panel below. Measured on the recorded sessions (border identical in every frame with
 * gameplay, in every area with scrolling; docs/ys_notes.md):
 *   - lines 0-10 and columns 0-7 / 328-340: always black;
 *   - border: single-color outline on lines 11, 25, 156 and 170 and on columns 8 and 327; inside it, a
 *     black line around the map (lines 26 and 155, columns 23 and 312);
 *   - map: columns 24-311 (288) x lines 27-154 (128);
 *   - panel: columns 8-327 (320, as wide as the border) x lines 179-219 (171-178 black);
 *   - overscan: lines 235-241 repeat the top of the border.
 *
 * Levels (crop key in YsRecomp.ini, default frame; "Crop border" item on the Image page):
 *   off   = whole image;
 *   frame = 320 x 213 (columns 8-327, lines 11-223): the whole border and the panel, with 4 black lines of
 *           breathing room below; the black around and the overscan go away; the bottom outline of the bars is drawn
 *           the same as the top one (the ENEMY one has a missing corner in the game);
 *   full  = 288 x 166, with nothing of the border: the map (288 x 128) and, below, a band of CROP_HUD_H lines in
 *           the chosen background color (hud_background key, "HUD background" item: black and gradients,
 *           crop_hud_pixel). The original panel (crop_panel: black transparent, except inside the bars) is
 *           drawn on top by the window, at the size it would have in frame mode, centered in the band
 *           (crop_hud_place).
 *   compact / wide / fill = the map and, below, a band of CROP_BAND_H lines in the chosen
 *           background with the HUD in two lines (crop_compose; pixels the same size as the map's), built from the
 *           panel's own letters and numbers: on top the 1st label (H.P), the player's bar (compressed to
 *           CROP_HUD_BAR_W, full = max life) and its number, and on the right the middle labels (in Ys II, the
 *           M.P); below the next-to-last (EXP) on the left and the last (GOLD) on the right. The labels, numbers and
 *           bars are found through the cells the game writes in the panel (app/ys_panel.h; formerly, through the
 *           image colors), so the same holds for Ys I and Ys II.
 *           The panel's ENEMY does not appear (it used to float in the top right corner of the map; now the life bar
 *           sits above the enemy, app/battle.h). Compact shows the whole map (288 x 128); fill crops the
 *           map until the image has the screen's aspect ratio (crop_band_size: with TV pixels, 6:7 in Ys's 341
 *           column mode, the image is taller than the screen and lines go from top and bottom; with square pixels
 *           columns go from the sides; in fill, when lines would go, the map stays almost whole and the band
 *           lower, with the HUD in 3/4 pixels, CROP_BAND_SMALL_H; then the game's windows, outside scenes, also
 *           come out at 3/4, CropView.shrink); wide crops half of that (with the
 *           normal band). In wide and fill, a game window (read from the game, crop_set_windows) that goes
 *           out of the shown part (SELECT menu) makes the image slide until it fits (CropState.pan_q, crop_pan_step);
 *           a window with no saved background (scenes with a portrait, like the bar's; Ys II fullscreens) or touching
 *           the map edge (GAME OVER) shows the whole map with the HUD band below (CropState.scene; formerly, without
 *           the HUD). Load
 *           screen: as in frame. ("hud" in the .ini = compact.)
 * Without the border (off) only internally (CROP_OFF): the menu and the .ini offer frame, full, compact, wide and fill
 * ("off" in the .ini becomes frame).
 * The result is drawn as a smaller image, with the same pixel aspect ratio (TV/square).
 *
 * Only applies with the border on screen (intros and game scenes appear whole). The game says so, through the disc
 * program that is running (app/ys_screen.h; crop_set_screen before each crop_frame): the Ys I and
 * Ys II game programs are always the map with the border and the panel (also with a black screen, the border being
 * drawn or the screen split delayed), the load menu's is the load screen. Formerly the image was checked pixel by
 * pixel (crop_detect, which stayed only for checking sessions: pce_trace --crop-log) with 30 frames of tolerance.
 * With the options menu open, the crop goes on and the menu is drawn on top of everything at the scale of the
 * cropped image (like the game's own menus; main.c).
 *
 * Game load screen (list of places; border down to line 234, no panel, text on lines 45-209):
 * cropped to the same size as the game, so the zoom does not change when entering the game. Frame: recomposed with the
 * whole border (top edge, lines 11-26; the middle with all the text, 37-217; bottom edge, 219-234).
 * Full: the inner area, lines 44-209 (288 x 166).
 */
#ifndef PCE_APP_CROP_H
#define PCE_APP_CROP_H

#include <stdint.h>

#include "app/fade.h"
#include "app/ys_panel.h"

enum { CROP_OFF, CROP_FRAME, CROP_FULL, CROP_COMPACT, CROP_WIDE, CROP_FILL, CROP_COUNT };
#define CROP_IS_BAND(mode) ((mode) >= CROP_FULL && (mode) <= CROP_FILL) /* HUD in two lines below the map */
/* Background of the panel (full) and of the HUD band (compact, wide, fill): flat colors and dithered gradients (chosen
 * by the user among simulations, 2026-10-20) */
/* Backgrounds: black; top-to-bottom gradients (by color; the last 4, by Ys places); metallic bands;
 * light in the center; left to right */
enum { CROP_HUD_BLACK, CROP_HUD_CHARCOAL, CROP_HUD_SLATE, CROP_HUD_MIDNIGHT, CROP_HUD_NAVY, CROP_HUD_INDIGO,
       CROP_HUD_PLUM, CROP_HUD_WINE, CROP_HUD_OXBLOOD, CROP_HUD_LEATHER, CROP_HUD_WALNUT, CROP_HUD_PETROL,
       CROP_HUD_DEEPSEA, CROP_HUD_PINE, CROP_HUD_SHADOW, CROP_HUD_COUNT };
#define CROP_HUD_H 38 /* lines of the HUD band in full (below the map) */
#define CROP_PANEL_W 320 /* the game's panel: columns 8-327, lines 179-219 */
#define CROP_PANEL_H 41
#define CROP_MAP_W 288         /* map columns */
#define CROP_MAP_H 128         /* map lines in full */
#define CROP_MAP_X 24          /* the map in the console image: columns 24-311, lines 27-154 */
#define CROP_MAP_Y 27
#define CROP_FRAME_VIEW_H 213  /* image lines in frame mode (11-223) */
/* Compact, wide and fill: the map at line 0 and the HUD band below (text lines at 132 and 143) */
#define CROP_BAND_H 26
#define CROP_BAND_VIEW_H (CROP_MAP_H + CROP_BAND_H)
#define CROP_HUD_BAR_W 80 /* compressed bars, outline included (the same in both games and in the three modes) */
#define CROP_BAND_MIN_W 224  /* the crop does not go past this */
#define CROP_BAND_MIN_MAP_H 96
/* Fill on a screen wider than the image (e.g.: TV pixels at 16:9): only 4 map lines go (formerly, 16) and
 * the band gets lower, at least CROP_BAND_SMALL_H lines, with the HUD in 3/4 pixels (CropView.hud_q).
 * User's layout: on the left, at mid height, H.P, the bar, the number and Ys II's M.P in one line (the bar
 * shortens if needed); on the right, EXP above GOLD. Half size with the whole map was tested: too small
 * next to the game's pixels */
#define CROP_BAND_SMALL_H 18
#define CROP_HUD_SMALL_Q 3 /* the letters and the bar in that band: 3/4 of the size */
/* Map columns and lines shown in mode mode and lines of the HUD band, with pixels of width/height par on a
 * screen of aspect ratio screen (width/height; 0 = 16:9, < 0 = stretched: no crop); par 0 = TV pixels in
 * 341 column mode (6:7) */
void crop_band_size(int mode, double par, double screen, int *w, int *map_h, int *band_h);
/* Screens with a border (crop_detect, CropState.active) */
enum { CROP_SCREEN_NONE, CROP_SCREEN_GAME, CROP_SCREEN_LOAD };

/* A game window on the map: columns x0-x1 and lines y0-y1 (on the map, inclusive) */
typedef struct CropRect {
    int x0, x1, y0, y1;
} CropRect;
#define CROP_WINDOWS_MAX 8

typedef struct CropPiece {
    int sx, sy, w, h; /* rectangle of the console image */
    int dx, dy;       /* where it goes in the cropped image */
} CropPiece;

typedef struct CropView {
    int w, h; /* size of the cropped image */
    int count;
    CropPiece piece[3];
    int hud;      /* below the map (line 128), the panel band (which the window draws on top) */
    int band;     /* compact, wide, fill: the band and the HUD in two lines below the map (crop_compose) */
    int band_y;   /* line where the band starts (the map lines shown) */
    int hud_q;    /* 0, or the band HUD in smaller pixels (quarter pixels, CROP_HUD_SMALL_Q): sub a multiple of 4/hud_q */
    int pan_q, pan_qy; /* wide, fill: the map slid beyond piece[0] (0-3 quarter pixels; smooth motion) */
    int darken;   /* game fade in 1/FADE_Q of a step (CropState.darken_q; the floating ENEMY, over the map, darkens along) */
    int whiten;   /* steps of the game's flash (CropState.whiten; crop_view_state): the band and the HUD brighten along */
    int hud_bg;   /* CROP_HUD_*: the caller chooses (crop_view leaves black) */
    int fix_bars; /* completes the bottom outline of the bars (frame mode) */
    int map_x, map_y; /* where the map (288 x 128) is in the cropped image (crop_darken; negative = cropped) */
    int next_on, next; /* the EXP left to the next level (crop_next_exp); next_on 0 = the panel's EXP */
    const YsPanel *panel; /* this frame's panel cells (CropState.panel; NULL = no HUD or bars) */
    /* fill with the HUD at 3/4 (crop_small_hud), outside scenes: this frame's game windows come out at 3/4, around
     * the map point (shrink_cx, shrink_cy) (the middle of all of them together); what they covered and is no longer
     * covered shows shrink_map (the map from before they opened, MAP_W x MAP_H; NULL = black). crop_compose */
    int shrink_n, shrink_cx, shrink_cy;
    CropRect shrink[CROP_WINDOWS_MAX];
    const uint32_t *shrink_map;
    int shrink_map_w;           /* line width of shrink_map */
    const uint32_t *shrink_spr; /* the current sprites (CropState.spr_px, width shrink_spr_w), on top of shrink_map */
    int shrink_spr_w;
    const uint32_t *shrink_fix; /* inside the windows (CropState.fix_px, width shrink_spr_w): alpha 0xFF = this pixel */
} CropView;

typedef struct CropState {
    int active; /* CROP_SCREEN_*: screen with a border in the image */
    int ys_screen; /* the screen of this frame's game program (crop_set_screen) */
    /* After the flash to white (end of Ys I), no crop until the game switches programs (the next scene starts
     * from white still in the game program); white_screen = that program's screen */
    int white_out, white_screen;
    long frames, framed; /* frames seen; with the border (counters) */
    /* Fade on screen changes (read from the game, crop_set_fade): the game darkens only the map (the fade routine
     * takes each color from one level of 7 to the next through the game's table) and leaves the border and the panel
     * lit; the window darkens the rest by the same level and the same table */
    int ys_dark, ys_add; /* this frame's map fade (app/ys_fade.h: 7 - YsFadeFrame.map_level, map_add; 0 = none) */
    int ys_stray;        /* colors lit outside the fade (YsFadeFrame.map_stray; crop_set_fade_stray) */
    int darken;    /* steps to darken outside the map (7 - the map level; 0 = nothing) */
    int darken_q;  /* the shown one, in 1/FADE_Q of a step (with smooth_fades, the steps pass continuously) */
    int smooth_fades; /* the caller turns on (video.h: smooth_fades) */
    FadeTrack darken_t;
    /* Flash (end of Ys I; read from the game): the whole screen, with the border, the panel and the black around,
     * gains one level (of 7) in each channel every 6 frames. whiten = this frame's levels (1-6), 0 = no flash; the crop
     * goes on and, with the image all white, turns off */
    int whiten;
    /* this frame's game windows (crop_set_windows, before crop_frame; app/ys_windows.h), on the map and 1
     * pixel inward (the drawn border); ys_scene = some window with no saved background (scene with a portrait,
     * fullscreen) */
    int ys_n, ys_scene;
    CropRect ys_r[CROP_WINDOWS_MAX];
    /* scene held after the window with no background closes: the portrait stays drawn until the darkening (or
     * CROP_SCENE_HOLD_MAX frames); scene_held = frames held */
    int scene_hold, scene_held;
    /* Game windows on the map: the columns and lines they take (on the map), kept up to CROP_WINDOW_OFF_FRAMES frames
     * with none (when switching windows, one frame comes out with none) */
    int window, window_misses, win_x0, win_x1, win_y0, win_y1;
    int scene; /* wide, fill: scene (game window with no saved background, scene_hold) or window touching the map edge
                * (GAME OVER) or larger than the shown part: whole map and the HUD */
    int pan_q, pan_qy; /* wide, fill: current slide of the map, in 1/4 pixel (crop_pan_step) */
    double par, screen; /* the caller: width/height of a pixel and of the screen (crop_band_size) */
    int next_on, next;  /* the caller, every frame: crop_next_exp of that frame's panel (goes to the CropView) */
    /* the open windows (the game's, outside scenes); all shrink around the middle of the ones that opened first
     * (grp_cx, grp_cy: the lower ones do not jump when another opens on top) */
    int grp_n, grp_cx, grp_cy;
    CropRect grp_r[CROP_WINDOWS_MAX];
    /* crop_ring (or NULL): the map without the windows and this frame's sprites, the size of the frame */
    const uint32_t *bg_px, *spr_px;
    /* the caller, after crop_ring (valid until the next crop_frame): inside the shrunk windows, the pixels with alpha
     * 0xFF replace the frame's (app/redraw.h: redraw_ring, fix): without Adol and the dark mask the game draws in them */
    const uint32_t *fix_px;
    YsPanel panel; /* the caller, every frame: ys_panel_read of that frame (goes to the CropView) */
} CropState;
#define CROP_WINDOW_OFF_FRAMES 8
#define CROP_SCENE_HOLD_MAX 600
#define CROP_PAN_STEP_Q 8 /* slide per game frame: 2 pixels */

/* this frame's game windows (SELECT menu, inventory, shop, conversation...), the caller, before crop_frame:
 * boxes = the boxes in console image pixels (app/ys_windows.h: ys_windows_boxes; x0, y0, x1, y1, inclusive),
 * scene = some window with no saved background (scene with a portrait, Ys II fullscreen: whole map and the HUD).
 * Without the call, the last frame's stay */
void crop_set_windows(CropState *s, const int (*boxes)[4], int n, int scene);
/* this frame's game fade (app/ys_fade.h), the caller, before crop_frame: level = the map level (0 =
 * black, 7 = full), add = the brightening (flash), table = the game's table (table[channel * 8 + level]; NULL = not
 * seen yet: no darkening). Without the call, the last frame's stays */
void crop_set_fade(CropState *s, int level, int add, const uint8_t *table);
/* YsFadeFrame.map_stray of the frame: with colors lit outside the fade, the scene does not end at the level's black */
void crop_set_fade_stray(CropState *s, int stray);
/* the screen of this frame's game program (CROP_SCREEN_*; app/ys_screen.h: ys_screen_layout), the caller, before
 * crop_frame. Without the call, the last frame's stays */
void crop_set_screen(CropState *s, int screen);
/* mode mode with the low band and the HUD at 3/4 (fill with TV pixels on a wide screen, crop_band_size):
 * the game's windows (outside scenes) and the kill gains (app/battle.h) also come out at 3/4 */
int crop_small_hud(int mode, double par, double screen);
/* Where the point (0, 0) of the console image lands in the cropped image v (out_sub times larger, from src with
 * sub = 1) and the map rectangle in it (clip: x0, y0, x1, y1, the end outside) */
void crop_map_out(const CropView *v, int out_sub, int *ox, int *oy, int clip[4]);
/* after crop_frame, with windows open (CropState.grp_n): bg = this frame's background redrawn with the VRAM
 * from before they opened, spr = this frame's sprites (app/redraw.h: redraw_ring; the size of the frame; they stay
 * valid until the next crop_frame). Around the shrunk windows goes the bg with the current sprites on top (it
 * used to be a snapshot of the map when the bg did not match the image: it brought the characters from before the
 * menu, frozen) */
void crop_ring(CropState *s, const uint32_t *bg, const uint32_t *spr);
/* The open windows (grp_r) in console image pixels (x0, y0, x1, y1, inclusive; for redraw_ring); returns
 * how many */
int crop_ring_boxes(const CropState *s, int boxes[CROP_WINDOWS_MAX][4]);

/* After crop_frame, once per new frame: scene and slide of mode mode (wide and fill only) */
void crop_pan_step(CropState *s, int mode);
/* For checking only (pce_trace --crop-log; the crop uses the game's screen, crop_set_screen): CROP_SCREEN_GAME /
 * CROP_SCREEN_LOAD = the shape of that border (single-color outlines, the black lines and the black around) is in this
 * image (w x h, ARGB); 0 = none; -1 = all-black image */
int crop_detect(const uint32_t *px, int w, int h);
/* New console frame (after crop_set_screen): updates s->active */
void crop_frame(CropState *s, const uint32_t *px, int w, int h);
/* Crop of level mode for a w x h image with the border active (CROP_SCREEN_*). 0 = no crop (whole
 * image). */
int crop_view(int mode, int active, int w, int h, CropView *v);
/* The same, with the scene and slide of s (wide, fill) */
int crop_view_state(int mode, const CropState *s, int w, int h, CropView *v);
/* Builds the cropped image: src with (width w) x sub pixels per pixel in each direction (sub = 1, or 4 with
 * smooth motion), output (v->w * sub) x (v->h * sub), the rest black. */
void crop_compose(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst);
/* The same, with the output out_sub times larger (a multiple of sub): the smaller HUD (v->hud_q quarter pixels)
 * needs out_sub a multiple of 4/hud_q (otherwise, it comes out at normal size) */
void crop_compose_sub(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst, int out_sub);
/* The out_sub for crop_compose_sub without smooth motion (src with sub 1): 4 with the HUD at 3/4, 2 at 1/2, else 1 */
int crop_out_sub(const CropView *v);
/* Darkens by darken/FADE_Q steps (of 7) everything in the w x h image (sub times larger, line of stride pixels)
 * except the map (288 x 128 at map_x, map_y of the image, possibly going out of it; map_y = CROP_NO_MAP: no map):
 * the game's fade also on the border and the panel, through the game's table at level 7 - darken/FADE_Q (between two
 * levels, the linear transition between the two values) */
#define CROP_NO_MAP (-10000)
void crop_darken(uint32_t *px, int stride, int w, int h, int sub, int map_x, int map_y, int darken);
/* The same, for the cropped image v (sub times larger): in compact, wide and fill modes, the HUD band (the cropped
 * map does not extend over it); in the others, everything except the map */
struct CropView;
void crop_darken_view(uint32_t *px, int stride, const struct CropView *v, int sub, int darken);
/* The EXP left to the next level (the panel cells and the frame's RAM, ys_panel_read): the next level ($2C4A)
 * minus the EXP written in the panel (not $2ABB: the game passes the saves through it when listing them); 0 at max
 * level */
int crop_next_exp(const YsPanel *panel);
/* Background names in the .ini (lowercase, in the order of CROP_HUD_*) */
extern const char *const crop_hud_names[CROP_HUD_COUNT];
/* ARGB color of background bg at point (x, y) of a band of w x h pixels: 4 shades between the background's two colors,
 * 2x2 dithering between them; the style says where each shade goes (top to bottom, metallic band with the light
 * in the middle, light in the center, left to right) */
uint32_t crop_hud_pixel(int bg, int x, int y, int w, int h);
/* The color of the band's last line at column x of w, without dithering (below the image, in compact and wide modes, the
 * window continues the band with it) */
uint32_t crop_hud_bottom(int bg, int x, int w);
/* The game's panel (CROP_PANEL_W x CROP_PANEL_H, at original size) from the image src (sub times
 * larger), into out (line width out_w): black transparent (alpha 0: the background band shows), except
 * inside the bars, and the bottom outline of the bars complete (the bars through the panel cells, panel) */
void crop_panel(const uint32_t *src, int src_w, int sub, const YsPanel *panel, uint32_t *out, int out_w);
/* Flash (CropState.whiten): brightens the w x h image (line of stride pixels) by whiten levels, the alpha stays */
void crop_whiten(uint32_t *px, int stride, int w, int h, int whiten);
/* The panel lines of src (1 pixel per pixel, width src_w) with the flash of whiten levels undone, for
 * crop_panel (sub 1); whiten 0: src itself */
const uint32_t *crop_unwhiten_panel(const uint32_t *src, int src_w, int whiten);
/* What the HUD finds in the panel, left to right: each label (columns l0-l1 lit) and its number
 * (v0-v1; -1 = no number), the bars (outline on line bar_y, columns bar_x0-bar_x1; bar_len lit pixels = max
 * life) and whether the numbers are those of the chosen save (save screen). For checking against the old way (pce_trace
 * --crop-log, "panel from the game") */
#define CROP_HUD_ITEMS 6
typedef struct CropHudLayout {
    int n, l0[CROP_HUD_ITEMS], l1[CROP_HUD_ITEMS], v0[CROP_HUD_ITEMS], v1[CROP_HUD_ITEMS];
    int bars, bar_y[2], bar_x0[2], bar_x1[2], bar_len[2];
    int save;
} CropHudLayout;
void crop_hud_layout(const YsPanel *panel, CropHudLayout *o);
/* Where to draw the panel on screen (output ow x oh) in full mode, with the cropped image in *full: at the size
 * it would have in frame mode, centered in the band below the map (smaller, if it does not fit) */
struct VideoSettings;
struct VideoRect;
void crop_hud_place(int ow, int oh, int divider, const struct VideoSettings *v, const struct VideoRect *full,
                    struct VideoRect *panel);
#endif
