/*
 * Image of the new smooth motion assembled on the GPU, in screen pixels (no longer 1/4 pixel).
 *
 * Once per game frame (gpu_draw_frame), the CPU prepares, from frame B:
 *   base: the real image, with color 0 in the stretches the VDC draws (background and sprites go on top);
 *   map bands: each piece of the image with a continuous map (the same scroll and the same smooth motion
 *     offset) becomes a band with GPU_MARGIN pixels of margin around it, each line with the VRAM of the image line
 *     that shows it (the game writes to VRAM while the image goes out); color 0 transparent. With the item windows in
 *     front, two more: the whole window and the window only where it is opaque. On the engine's map lines
 *     (MotionLayout.sides), the band repeats B's last map column (keep_sides in motion.c);
 *   drawings of the 64 sprites, already flipped and in their colors, with what the 16-per-line limit cut in B
 *     transparent (the limit as the console showed it).
 * For each image (gpu_draw_render), only rectangles, at scale kx x ky (that of the integer upscale of app/present.h),
 * with the positions from app/motion.h rounded to that grid (q(v) = floor(v * k + 0.5); with k = 4 it is the CPU
 * drawing, motion_render):
 *   sprite layers, from last to first (the lower-numbered one stays on top): "all" and "in front" (the
 *     winner of each pixel, with alpha erased where it is behind the background: the GPU's own blend, the
 *     erase one); with windows, also "in front of the windows" and that one "in front";
 *   in each stretch of lines and columns: color 0 (from the base), the "all" layer, the opaque background and the "in front" layer.
 *     As in the VDC: the winning sprite behind only shows where the background is transparent. With windows: the whole window,
 *     the sprites that stay in front of it, the opaque window and those sprites "in front".
 * Differences from the CPU drawing outside integer positions (only in rare frames): the 16-sprite limit per
 * line and the sprites in front of the windows are B's (the CPU redoes them at the image positions); a map line
 * shifted to the neighboring line uses the neighbor's VRAM; the strip at the map's edge (keep_sides) does not repeat
 * sprites.
 * Without the custom blend (software renderer) gpu_draw_init returns 0; with a frame it cannot assemble
 * (lines with different VDC start or count, too many bands), gpu_draw_render returns 0. In both cases the
 * caller draws on the CPU (motion_render).
 */
#ifndef PCE_APP_GPU_DRAW_H
#define PCE_APP_GPU_DRAW_H

#include <SDL3/SDL.h>

#include "app/crop.h"
#include "app/motion.h"

#define GPU_MARGIN (MOTION_JUMP + 1) /* the map offset in an image stays below this (more: clamped to it) */
#define GPU_PIECES 32
#define GPU_REGIONS 96
#define GPU_BAND_W (VCE_MAX_WIDTH + 2 * GPU_MARGIN + 2)
#define GPU_BAND_H 1024
#define GPU_ATLAS_W 256 /* 8 x 8 sprites of up to 32 x 64 */
#define GPU_ATLAS_H 512
#define GPU_LAYERS 4
#define GPU_MASK_SLOTS REDRAW_DRAW_MASKS

typedef struct GpuPiece {
    int top, end; /* image lines */
    int y0;       /* 1st line of the band (with the margin) */
    int x0;       /* VCE column of the 1st visible pixel */
} GpuPiece;

typedef struct GpuRegion {
    int top, end;  /* image lines */
    int x, x1;     /* image columns */
    int piece;     /* map band (-1 = background off) */
    int sprites;   /* sprites on (CR) */
} GpuRegion;

#define GPU_BAND_STRIDE (GPU_BAND_W * 3) /* opaque background, whole window, opaque window */

/* What each image's drawing uses from the prepared frame */
typedef struct GpuFrameInfo {
    long seq;
    int ready;             /* 0 = this frame cannot be assembled on the GPU */
    int w, h, raster0;     /* raster0: VDC line of image line 0 */
    int x0;                /* VCE column of the 1st visible pixel (the same on all lines) */
    int windows;           /* item windows in front (layers 2 and 3) */
    int npieces, nregions;
    GpuPiece piece[GPU_PIECES];
    GpuRegion region[GPU_REGIONS];
    uint8_t front[64], keep[64], used[64];
    int spr_w[64], spr_h[64];
    int nbehind;
    RedrawMask mask[GPU_MASK_SLOTS];
} GpuFrameInfo;

/* Frame prepared on the CPU (gpu_draw_prepare) and the images that go to the textures (gpu_draw_upload).
 * Prepared on the prep thread (runtime/main.c), without touching SDL; sent later on the window thread */
typedef struct GpuPrep {
    GpuFrameInfo f;
    int rows; /* band lines */
    uint32_t base[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    uint32_t band[GPU_BAND_STRIDE * GPU_BAND_H];
    uint32_t atlas[GPU_ATLAS_W * GPU_ATLAS_H];
    uint32_t masks[64 * GPU_MASK_SLOTS * 32];
    /* scratch */
    MotionLayout l0, l1;
    uint16_t vram[VDC_VRAM_SIZE], sat[VDC_SAT_SIZE];
    int8_t row_piece[VCE_ACTIVE_LINES];
    uint16_t bgl[GPU_BAND_W];
} GpuPrep;

typedef struct GpuDraw {
    SDL_Renderer *r;
    int ok;                /* the GPU does the erase blend */
    SDL_BlendMode erase;
    SDL_Texture *base, *bands, *atlas, *layer[GPU_LAYERS], *canvas, *crop;
    /* masks by the game's rule (Motion.ym.block; they erase in all layers) side by side in 32 x 64
     * (GPU_MASK_SLOTS) */
    SDL_Texture *masks;
    SDL_Texture *stickers; /* bars and gains (app/battle.h): BATTLE_STICKERS drawings one below the other */
    int layer_w, layer_h, canvas_w, canvas_h, crop_w, crop_h;
    GpuFrameInfo f; /* sent frame (gpu_draw_upload) */
    /* Counters */
    long frames, renders, unready;
} GpuDraw;

/* 1 = the GPU can assemble (creates the fixed textures) */
int gpu_draw_init(GpuDraw *g, SDL_Renderer *r);
void gpu_draw_free(GpuDraw *g);

/* New frame b (already with motion_step), CPU only (no SDL: can run on another thread). ok = the GPU can
 * assemble (GpuDraw.ok). window_front: the item windows in front (s->window_front) */
void gpu_draw_prepare(GpuPrep *p, int ok, Motion *m, Redraw *s, const RedrawFrame *b, int window_front);
/* Sends the prepared frame to the textures (on the window thread) */
void gpu_draw_upload(GpuDraw *g, const GpuPrep *p);
/* Both at once (tools) */
void gpu_draw_frame(GpuDraw *g, Motion *m, Redraw *s, const RedrawFrame *b, int window_front);

/* Assembles in g->canvas (b->w * kx x b->h * ky) the image with the positions of lay (motion_layout of b). Returns 0 if it
 * could not (the caller draws on the CPU). Returns with the render target on the window. */
int gpu_draw_render(GpuDraw *g, const RedrawFrame *b, const MotionLayout *lay, int kx, int ky);

/* Cropped image v (app/crop.h) in g->crop (v->w * kx x v->h * ky): the pieces of g->canvas (assembled at the same
 * scale) and, on top, over (the crop of the frame assembled on the CPU, over_sub times larger, transparent where the
 * map moves). Returns g->crop, or NULL. */
SDL_Texture *gpu_draw_crop(GpuDraw *g, const CropView *v, SDL_Texture *over, int over_sub, int kx, int ky);

/* Draws in g->canvas (after gpu_draw_render, same scale) the drawings of bt at positions x/y (battle_place),
 * only inside the map */
struct Battle;
void gpu_draw_battle(GpuDraw *g, const struct Battle *bt, const double *x, const double *y, int kx, int ky);

#endif
