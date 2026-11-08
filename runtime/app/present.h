/*
 * Draws the game image on screen with the image options (framing in app/video.h).
 * Uses only the SDL_Renderer, no window: the tests draw into a software renderer.
 *
 *   Sharp (default): upscales by integers without smoothing until past the final size, then downscales with a
 *     linear filter (the edge between two pixels blends at most 1 screen pixel).
 *   Integer scale: the image is drawn directly at the exact size (kx x ky screen pixels per pixel).
 *   Smooth: bilinear filter straight from the original image.
 *   CRT, over the image (multiplying the colors):
 *     scanlines: each image line is bright in the middle and dark at the edges (only with 3 or more screen pixels
 *       per line; below that the lines have no room to show);
 *     grille (mask): screen columns alternating red/green/blue, like the tube's phosphors; slot: the same
 *       columns cut into bricks, offset from one triad to the next; shadow: color dots in a triangle
 *       (2 columns per color, each 2-line band half a triad ahead). Size by output: 1 screen pixel
 *       per color up to ~1080 lines, 2 from 1260 on (shadow: doubles only from 2000 on);
 *     then the image is lightly added back, to restore part of the brightness both take away;
 *     diffuse glow: the blurred image (downscaled to 1/8 in steps and upscaled with a linear filter), squared
 *       (bright spreads much more than dark), added on top: bright bleeds into the lines and the neighbors.
 */
#ifndef PCE_APP_PRESENT_H
#define PCE_APP_PRESENT_H

#include <SDL3/SDL.h>

#include "app/video.h"

#define PRESENT_BLOOM_TEX 6

typedef struct Presenter {
    SDL_Texture *big; /* image upscaled by integers, unsmoothed */
    int big_w, big_h;
    SDL_Texture *lines; /* CRT scanline profile: 1 x (image height * PRESENT_LINE_ROWS) */
    int lines_h;  /* key of the cached profile (lines and brightness) */
    SDL_Texture *mask; /* one tile of the grille (repeated across the screen) */
    int mask_key;      /* key of the cached grille (kind, bright, size; 0 = none) */
    /* diffuse glow: the image downscaled to 1/2, 1/4, 1/8, the square of the 1/8 one and that downscaled to 1/16 and 1/32 */
    SDL_Texture *bloom[PRESENT_BLOOM_TEX];
    int bloom_w, bloom_h;  /* size of the 1/2 one (half the output) */
    float mask_y; /* screen line where the grille starts (slot and shadow change from line to line) */
    SDL_Texture *over; /* layer (present_overlay) upscaled by integers */
    int over_w, over_h;
    SDL_Texture *over_lines; /* CRT scanline profile of the layer */
    int over_lines_h;
    float mask_x; /* screen column where the image's CRT grille starts (the layer continues it) */
    SDL_Texture *lay, *lay_big; /* transparent layer (present_layer): at the final size; upscaled by integers */
    int lay_w, lay_h, lay_big_w, lay_big_h;
    SDL_Texture *lay_lines; /* CRT scanline profile of the transparent layer */
    int lay_lines_h;
} Presenter;

/* Clears the output (black) and draws the w x h image from frame (divider = master cycles per pixel; sub = the image
 * is sub times larger in each direction in the texture, with positions between pixels: app/motion.h). Does not call
 * SDL_RenderPresent. Changes frame's scale mode and blending on every call. */
void present_draw(Presenter *p, SDL_Renderer *r, SDL_Texture *frame, int w, int h, int sub, int divider,
                  const VideoSettings *v);
/* Game image assembled on the GPU (app/gpu_draw.h): texture pixels per pixel of the w x h image for the
 * current output: those of Sharp's integer upscale and of integer scale (video_layout); in Smooth, REDRAW_SUB
 * (the same bilinear filter as before, from a 4x larger image) */
void present_canvas_scale(SDL_Renderer *r, int w, int h, int divider, const VideoSettings *v, int *kx, int *ky);
/* Like present_draw, with the image already assembled at that scale in canvas (w * kx x h * ky) */
void present_draw_canvas(Presenter *p, SDL_Renderer *r, SDL_Texture *canvas, int w, int h, int kx, int ky,
                         int divider, const VideoSettings *v);
/* Draws the w x h image of tex on top, in the output rectangle dst, sharp like the game image (or
 * smooth, with v->smooth), with the CRT at its scale (h lines; the grille continues the image's) (e.g. the panel
 * of the full crop, app/crop.h). Call after present_draw. */
void present_overlay(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, const VideoRect *dst,
                     const VideoSettings *v);
/* Same, with the CRT lines at the scale of lines lines (e.g. the launcher, 768x432 with 2x2 pixels: 216 lines).
 * Without present_draw before: p->mask_x = column where the grille starts. */
void present_overlay_lines(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, int lines,
                           const VideoRect *dst, const VideoSettings *v);
/* Same, for a layer with transparent parts (alpha 0; e.g. the options menu over the cropped image):
 * assembled separately, so the CRT applies only to what it draws; transparent shows what is underneath */
void present_layer(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, const VideoRect *dst,
                   const VideoSettings *v);
/* Same, with the CRT lines at the scale of lines lines in dst (e.g. the launcher logo, at screen resolution,
 * with dst aligned to the background's 216 lines) */
void present_layer_lines(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, int lines,
                         const VideoRect *dst, const VideoSettings *v);
void present_free(Presenter *p);

#endif
