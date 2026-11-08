/*
 * Display of the PC version: fullscreen, resolution, refresh rate, V-Sync, window size, and the image
 * framing. Only math (no SDL video), for the tests; the window (main.c) applies it.
 *
 * Options in YsRecomp.ini:
 *   fullscreen=1          fullscreen (default; 0 = window)
 *   resolution=desktop    in fullscreen: the Windows one, or WxH (e.g. 1920x1080) from the monitor's list
 *   refresh_rate=auto     in fullscreen: refresh rate in Hz, or auto (below)
 *   vsync=1               1 = waits for the display refresh; 0 = shows each frame when it is ready
 *                         (for G-Sync/FreeSync; without them the image may "tear")
 *   window_scale=3        window size: 2 to 6 times the original image
 * Image options (drawing in app/present.h):
 *   integer_scale=0       1 = scale by integers (every pixel the same size; larger borders)
 *   filter=sharp          sharp = crisp; smooth = smoothed (bilinear filter straight from the original image)
 *   crt=off               old TV effect: off, scanlines (dark lines), mask (lines + grid of color
 *                         columns), slot (lines + brick grid), shadow (lines + color dots),
 *                         glow (lines + diffuse glow), glow_mask, glow_slot (app/present.h)
 *   crt_brightness=normal normal or bright (the brighter CRT: softer lines, lighter grid, more glow)
 *   aspect=tv             tv = 8:7 pixels as on the TV; square = square pixels; stretch = stretched to fill the
 *                         whole screen (no borders; the proportion changes)
 *   crop=frame            Ys frame crop (app/crop.h): frame (rectangle), full (recomposed),
 *                         compact (the map and the HUD in two rows below it; "hud", the old name, also
 *                         works), wide (crops a bit off the sides), fill (crops more and fills a 16:9 screen)
 *   hud_background=black  background of the panel (full) and of the HUD (compact, wide, fill): black (plain); charcoal, slate,
 *                         midnight, navy, indigo, plum, wine, oxblood, leather, walnut, petrol, deepsea, pine,
 *                         shadow (dark top-to-bottom gradients; removed names map to the closest one)
 *   visual_equipment=1    1 = the equipped sword, shield and armor appear on Adol on the map (app/equip.h)
 *   enemy_hp_bars=1       1 = health bar above the wounded enemy, for 3 s after the hit (app/battle.h)
 *   reward_popups=1       "+N EXP" and "+N GOLD" where the enemy died: 0 = no, 1 = game font in 2 lines,
 *                         2 = small font (3x5) in one line
 * Smooth motion (app/motion.h):
 *   smooth_motion=1       1 (default) = one image per display refresh, with the map and the sprites between pixels,
 *                         reading the game engine (app/motion.h; the image is ~1 frame behind), assembled on the
 *                         GPU in screen pixels (app/gpu_draw.h); 0 = 60 images, like the console.
 *                         The old one, which guessed from the frame, was removed (the former 2 counts as 1). F6 switches
 *                         the assembly: GPU <-> CPU
 *   smooth_fades=1        1 (default) = the game's fades (7 steps, one every ~4 frames) become continuous: each
 *                         step goes from the shown value to the new one over the time until the next (app/fade.h)
 *
 * Pace: a console frame lasts 1/60.05 s. On a 60/120/180/240 Hz display, each frame stays on screen
 * 1/2/3/4 refreshes. At 144 Hz a frame stays sometimes 2, sometimes 3 refreshes (144 is not a multiple of 60); with
 * smooth motion, each image shows the position at the time it appears (single clock, app/pacing.h) and the
 * motion is smooth. "auto" = the Windows refresh rate, without changing the monitor mode (user decision).
 *
 * Image: 8:7 pixels as on the TV (Geargrafx: HuC6260::GetPixelAspectRatio = clock divider / 3.5;
 * 4 cycles per pixel in 256-pixel mode), filling the screen, centered. Sharp and with no pixels of
 * different widths: first scales up by integers (without smoothing) past the final size,
 * and only then scales down with a linear filter (the edge between two pixels blends at most 1 screen pixel).
 */
#ifndef PCE_APP_VIDEO_H
#define PCE_APP_VIDEO_H

#include "app/config.h"
#include "app/crop.h"

typedef struct VideoSettings {
    int fullscreen;
    int res_w, res_h; /* 0 = the Windows one */
    int refresh;      /* Hz rounded; 0 = auto */
    int vsync;
    int window_scale; /* VIDEO_SCALE_MIN..VIDEO_SCALE_MAX */
    int integer_scale;
    int smooth;
    int crt;          /* VIDEO_CRT_* */
    int crt_bright;   /* brighter CRT (lighter lines and grid, more glow) */
    int aspect;       /* VIDEO_ASPECT_*: 8:7 pixels, square, or stretched to the whole screen */
    int crop;         /* CROP_*: frame crop (app/crop.h) */
    int hud_bg;       /* CROP_HUD_*: background of the panel (full) and of the HUD (compact, wide, fill) */
    int smooth_motion; /* smooth motion: more than 60 images per second (app/motion.h) */
    int smooth_fades;  /* game fades continuous, not in steps (app/fade.h) */
    int equipment;     /* equipment on Adol (app/equip.h) */
    int enemy_bars;    /* health bar near the wounded enemy (app/battle.h; Gameplay menu) */
    int reward_popups; /* EXP and GOLD earned near the dead enemy: 0 no, 1 large, 2 small */
    int instant_dialog; /* dialogue text all at once (app/ys_dialog.h; changes the game; Gameplay menu) */
} VideoSettings;

/* CRT presets: lines, grid (aperture = columns; slot = staggered bricks; shadow = dots
 * in a triangle) and diffuse glow (glow, bloom). The lines apply to all but Off. */
enum {
    VIDEO_CRT_OFF,
    VIDEO_CRT_SCANLINES,
    VIDEO_CRT_MASK,
    VIDEO_CRT_SLOT,
    VIDEO_CRT_SHADOW,
    VIDEO_CRT_GLOW,
    VIDEO_CRT_GLOW_MASK,
    VIDEO_CRT_GLOW_SLOT,
    VIDEO_CRT_COUNT
};
enum { VIDEO_ASPECT_TV, VIDEO_ASPECT_SQUARE, VIDEO_ASPECT_STRETCH, VIDEO_ASPECT_COUNT };

#define VIDEO_SCALE_MIN 2
#define VIDEO_SCALE_MAX 6

typedef struct VideoMode {
    int w, h;  /* real screen pixels */
    float hz;
} VideoMode;

#define VIDEO_MAX_MODES 128

/* Modes of the monitor the window is on: the current Windows one and the fullscreen ones (SDL list). */
typedef struct VideoModes {
    VideoMode desktop;
    VideoMode list[VIDEO_MAX_MODES];
    int count;
} VideoModes;

/* Default: fullscreen, Windows resolution, auto refresh rate, V-Sync on, 3x window; sharp image,
 * 8:7, no integer scale nor CRT. */
void video_defaults(VideoSettings *v);
/* 1 = both ask for the same display mode (everything but the image options, which do not change the mode). */
int video_same_display(const VideoSettings *a, const VideoSettings *b);
/* Reads the options (invalid values go back to the default) and puts the missing ones in the config. Returns 1
 * if it added any key. */
int video_load(Config *cfg, VideoSettings *v);
void video_store(Config *cfg, const VideoSettings *v);

/* How many display refreshes per console frame (1..4) for smooth motion; 0 = this
 * rate is not a multiple of 60 (1.5% tolerance: 59.94 and 119.88 Hz qualify). */
int video_refreshes_per_frame(float hz);

/* Fullscreen mode the options ask for on this monitor. *exclusive = 1 when the monitor mode must be
 * changed (resolution or refresh rate different from the Windows one); 0 = "borderless" fullscreen, at the
 * Windows resolution and refresh rate. Resolution the monitor does not have: the Windows one. */
VideoMode video_fullscreen_mode(const VideoModes *modes, const VideoSettings *v, int *exclusive);

/* Distinct resolutions in the list, from largest to smallest. Returns how many. */
int video_resolutions(const VideoModes *modes, VideoMode *out, int max);
/* Refresh rates (rounded Hz, no repeats) the monitor has at resolution w x h, from highest to lowest. */
int video_rates(const VideoModes *modes, int w, int h, int *out, int max);

/* Where to draw a src_w x src_h image (divider = master cycles per pixel, 4/3/2) on an
 * out_w x out_h output: rectangle *dst (centered; 8:7 pixels, or square, or the whole output with v->aspect) and integer
 * scale *kx, *ky of the unsmoothed stage (the scaled image exceeds the size of dst; dst still scales down with a linear
 * filter). With v->integer_scale, dst measures exactly src_w*kx x src_h*ky: ky = the largest integer that fits
 * in the height and kx = ky times the pixel aspect, rounded (8:7 pixels as 6x5, 7x6...); if not even 1x
 * fits, back to normal. v = NULL: default. */
typedef struct VideoRect {
    int x, y, w, h;
} VideoRect;
void video_layout(int out_w, int out_h, int src_w, int src_h, int divider, const VideoSettings *v, VideoRect *dst,
                  int *kx, int *ky);

/* Window size (image area) for the options' scale, with the 256x242 image of 8:7 (or square) pixels. */
void video_window_size(const VideoSettings *v, int *w, int *h);

#endif
