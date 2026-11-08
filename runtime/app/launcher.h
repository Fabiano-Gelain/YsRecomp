/*
 * Start menu (launcher): before the game, the covers of the 3 PC Engine Ys games to choose from. Reference for
 * layout, colors, animations and sounds: docs/launcher_simulacao.html (approved by the user); sizes of the logo,
 * the texts, the window and the stars and the logo effects: docs/launcher_simulacao_grande.html (approved on
 * 2026-10-22).
 *
 * The screen has the game's look: drawn at 384x216 pixels (16:9) with the console palette (8 levels per channel,
 * 4x4 dithering). The background (banded sky, moon, mountains, Darm Tower) and the double-bordered blue window are
 * in those big pixels; the covers and the stars have twice the pixels. That is why the console image comes out at
 * 768x432 (LAUNCHER_IMG_W x LAUNCHER_IMG_H), with the big pixels as 2x2. The logo (the user's JPG without the
 * bottom text line) and the texts (the menu's 8x8 font, app/menu_font.h) are at screen resolution, on
 * top (launcher_draw_text, launcher_logo).
 *
 * User images (outside Git): pixel-art CD cases on a black background; each one is cropped to its outline
 * (what is not black) before being scaled down. Without an image, the cover is left out (the rest works).
 *
 * Pace: the animations advance in steps of 1/30 s (launcher_tick), as in the mock-up.
 */
#ifndef PCE_APP_LAUNCHER_H
#define PCE_APP_LAUNCHER_H

#include <stdint.h>

#define LAUNCHER_W 384 /* console pixels */
#define LAUNCHER_H 216
#define LAUNCHER_IMG_W (LAUNCHER_W * 2) /* covers and stars: twice the pixels */
#define LAUNCHER_IMG_H (LAUNCHER_H * 2)
#define LAUNCHER_GAMES 3
#define LAUNCHER_COVER_W 80 /* cover in console pixels (160x138 in the image); 88x76 until 2026-10-22 */
#define LAUNCHER_COVER_H 69
#define LAUNCHER_STARS 220
#define LAUNCHER_TICK_HZ 30

typedef enum LauncherKey { LAUNCHER_KEY_LEFT, LAUNCHER_KEY_RIGHT, LAUNCHER_KEY_OK } LauncherKey;

/* Sounds requested by launcher_key (Ys effects, app/launcher_sound.h) */
enum { LAUNCHER_SFX_NONE = -1, LAUNCHER_SFX_MOVE = 0, LAUNCHER_SFX_OK = 1, LAUNCHER_SFX_NO = 2 };

typedef struct LauncherGame {
    const char *name, *date;
    int released; /* 1 = starts the game */
} LauncherGame;

typedef struct Launcher {
    uint32_t bg[LAUNCHER_W * LAUNCHER_H]; /* background, in the big pixels */
    struct {
        int16_t x, y; /* in the 768x432 image */
        uint8_t phase;
        float kind;
    } stars[LAUNCHER_STARS];
    int star_count;
    uint32_t cover[LAUNCHER_GAMES][LAUNCHER_COVER_W * 2 * LAUNCHER_COVER_H * 2]; /* color */
    uint32_t gray[LAUNCHER_GAMES][LAUNCHER_COVER_W * 2 * LAUNCHER_COVER_H * 2];  /* black and white */
    int have_cover[LAUNCHER_GAMES];
    uint32_t *logo; /* cropped logo (without the bottom line), as it came from the JPG; NULL = no logo */
    int logo_w, logo_h;
    int sel;
    long tick;
    int msg;   /* 0 = name and date; 1 = loading; 2 = not released */
    int typed; /* letters of the message already typed */
    long done_tick; /* tick at which the loading message finished (0 = not yet) */
} Launcher;

extern const LauncherGame launcher_games[LAUNCHER_GAMES];

/* Background and stars; selection on Ys Book I & II */
void launcher_init(Launcher *L);
void launcher_free(Launcher *L);
/* User image (ARGB w x h): which 0 = logo, 1-3 = covers. 0 = ok, -1 = all black / out of memory */
int launcher_set_image(Launcher *L, int which, const uint32_t *argb, int w, int h);
/* Player key; returns the sound to play (LAUNCHER_SFX_*) */
int launcher_key(Launcher *L, LauncherKey k);
/* One step of 1/30 s. Returns 1 when the game should start (the loading message finished). */
int launcher_tick(Launcher *L);
/* The loading message has already appeared (the music can start fading) */
int launcher_starting(const Launcher *L);

/* Console image, 768x432 ARGB (opaque) */
void launcher_draw(const Launcher *L, uint32_t *out);
/* Text layers (2026-10-22 mock-up, docs/launcher_simulacao_grande.html): MAIN = project name and
 * window (font at ~3.1% of the height), HINT = help line (~2.2%) */
enum { LAUNCHER_TEXT_MAIN = 0, LAUNCHER_TEXT_HINT = 1 };
/* Texts of one layer in a transparent w x h image (16:9, pixels of the 8x8 font; alpha 0 = transparent) */
void launcher_draw_text(const Launcher *L, uint32_t *out, int w, int h, int part);
/* Size of the font pixel of layer part on screen (screen pixels) for a 16:9 area area_h high */
int launcher_text_scale(int area_h, int part);
/* Golden logo scaled down to w pixels wide (area), with alpha; *h returns the height. NULL without logo. */
uint32_t *launcher_logo(const Launcher *L, int w, int *h);
/* Logo in the 16:9 area (aw x ah): its rectangle, in area pixels (without the effects margin) */
void launcher_logo_rect(int aw, int ah, const Launcher *L, int *x, int *y, int *w, int *h);

/* Logo effects (2026-10-22 mock-up): black outline, brown relief below, golden glow around
 * pulsing (3.2 s) and a shine passing over the letters (4.5 s). The image has a margin of pad pixels around
 * the logo rectangle. */
typedef struct LauncherLogoFx {
    int w, h, pad, fw, fh;          /* logo w x h; image with the margin: fw x fh */
    uint32_t *gold;                 /* fw x fh: the golden logo (ARGB, alpha) */
    uint8_t *outline, *lip, *glow;  /* fw x fh: alpha of the outline, the relief and the glow */
    uint32_t *out;                  /* fw x fh: the assembled image */
} LauncherLogoFx;
/* Builds for the logo w pixels wide in an area area_h high. 0 = ok, -1 = no logo/memory */
int launcher_logo_fx_build(LauncherLogoFx *fx, const Launcher *L, int w, int area_h);
/* The image at step tick (1/30 s), fw x fh ARGB with alpha (not premultiplied) */
const uint32_t *launcher_logo_fx_render(LauncherLogoFx *fx, long tick);
void launcher_logo_fx_free(LauncherLogoFx *fx);

/* Tools (also used in the tests) */
/* Rectangle of what is not black (outline of the case). 0 = ok, -1 = all black */
int launcher_crop_box(const uint32_t *argb, int w, int h, int *x0, int *y0, int *cw, int *ch);
/* Scales the region (sx, sy, sw, sh) down to dw x dh by area averaging (like the browser's "high") */
void launcher_resample(const uint32_t *argb, int w, int sx, int sy, int sw, int sh, uint32_t *out, int dw, int dh);
/* Converts to the console palette with 4x4 dithering (gray = black and white, darkened) */
void launcher_pixelize(uint32_t *px, int w, int h, int gray);

#endif
