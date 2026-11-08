/* Smooth fades (smooth_fades): Ys darkens and brightens the screen in steps (the game's fade routine takes
 * each color from one level of 7 to the next, one step every ~4 frames; app/ys_fade.h). In the window, each step becomes a
 * continuous transition from the shown value to the new one, over the time between that color's steps (4 frames if there is no
 * previous one). Only the colors the game changed through a fade step (YsFadeFrame.step) transition smoothly: the other changes
 * (new drawing, blinking color, animated area colors) come out at once, as in the game. The image stays up to 3 frames behind
 * the game, only in brightness. None of this touches the console: only what the window shows. */
#ifndef PCE_APP_FADE_H
#define PCE_APP_FADE_H

#include <stdint.h>
#include "pce/vce.h"

#define FADE_Q 64      /* 1 level (of 7) */
#define FADE_PERIOD 4  /* frames per step when there is no previous step */
#define FADE_MAX_PERIOD 8

/* A value in steps (the level of one channel, or the level of the map fade in the crop) */
typedef struct FadeTrack {
    int16_t shown, target, step; /* in 1/FADE_Q of a level */
    uint8_t age, valid;          /* frames since the last change; valid 2 = it was a smooth step */
} FadeTrack;

/* One frame: level = the current value; a change of up to max_d levels transitions smoothly (0 = none). Returns the shown value
 * (1/FADE_Q of a level) */
int fade_track(FadeTrack *t, int level, int max_d);

/* The 512 VCE colors (9-bit color: GGGRRRBBB) */
typedef struct FadePalette {
    FadeTrack ch[512][3]; /* blue, red, green (the bit order) */
} FadePalette;

struct RedrawFrame;
struct YsFadeFrame;

/* The shown colors (rgb, ARGB) of the console's 512 colors (color) in one frame: the ones the game changed through a fade step
 * (yf->step; yf NULL = none) transition smoothly. on = 0: the console's colors, and the next frame with on starts with no
 * transition. Returns how many colors are in the middle of a transition */
int fade_colors(FadePalette *p, const uint16_t color[512], const struct YsFadeFrame *yf, uint32_t rgb[512], int on);

/* Fills f->rgb (the shown colors) and f->chan (for the equipment's direct colors) from f->color */
void fade_palette(FadePalette *p, struct RedrawFrame *f, const struct YsFadeFrame *yf, int on);

/* The console's image (ARGB) without the redraw through the palette (without smooth motion): each pixel by its color number
 * (Vce.frame_index), with the shown color */
typedef struct FadePixels {
    FadePalette pal;
    uint32_t rgb[512];
} FadePixels;

/* Replaces px (w x h, this frame's image; index = the color number of each pixel; color = the console's 512 colors at the end
 * of the frame) with the shown one. on = 0: leaves it as is and the next one starts with no transition */
void fade_pixels(FadePixels *s, uint32_t *px, const uint16_t *index, int w, int h, const uint16_t color[512],
                 const struct YsFadeFrame *yf, int on);

#endif
