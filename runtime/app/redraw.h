/*
 * Frame redraw (formerly in app/smooth.h): the game frame copied at the end (VRAM, sprites, colors and what the
 * VDC used on each line: VdcLineLog) and redrawn as the VDC would draw it, with the positions smooth motion
 * asks for (app/motion.h) and at 1/k pixel.
 *
 *   Check: each new frame is redrawn at its own positions and compared with the real image; if
 *     it differs (something the redraw does not reproduce, e.g. VRAM changed mid-frame), that frame is shown as
 *     it came, without smooth motion.
 *   Item window in front (window_front): app/game_fixes.h.
 *   Equipment on Adol (equip): Adol's sprite comes out with the pixels of app/equip.h (fix_equip).
 *
 * Only math and memory (no SDL): the tests and pce_trace use it directly.
 */
#ifndef PCE_APP_REDRAW_H
#define PCE_APP_REDRAW_H

#include <stdint.h>

#include "app/equip.h"
#include "app/ys_engine.h"
#include "app/ys_windows.h"
#include "app/ys_scroll.h"
#include "app/ys_reward.h"
#include "pce/vce.h"
#include "pce/vdc.h"

#define REDRAW_SUB_SHIFT 2 /* 1/4 pixel (CPU drawing) */
#define REDRAW_SUB (1 << REDRAW_SUB_SHIFT)
#define REDRAW_RING 256 /* Ys keeps the map in a ring of 32 tile rows: it wraps mid-image */

/* Mask by the game's rule (app/ys_masks.h): like the game's mask sprites (black, behind the background), hides
 * the pixels of all sprites from there on in the order; where the background is transparent color 0 stays (black in Ys, like the
 * game's mask). Attached to B's map. */
#define REDRAW_BEHIND 64 /* masks of one frame (with the ones rebuilt around the shrunk windows) */
#define REDRAW_DRAW_MASKS REDRAW_BEHIND
typedef struct RedrawMask {
    int x, y, w, h;     /* position in B (pixels: x from the 1st visible pixel, y in VDC lines) */
    int before;         /* enters the order before B's sprite with this number */
    int xq, yq;         /* (drawing) position in 1/k */
    uint32_t rows[64];  /* pixels of each line (bit x = mask column x) */
} RedrawMask;

/* Opaque pixels of each line of sprite e (4 table words) with VRAM vram; returns the width, *h the height */
int redraw_sprite_bits(const uint16_t *vram, const uint16_t *e, int mode1, uint32_t *rows, int *h);

/* A game frame, copied at the end of the frame (game thread): VRAM and sprites of the 1st drawn line and the
 * writes made during drawing (Ys copies drawings into VRAM while the image goes out) */
typedef struct RedrawFrame {
    uint16_t vram[VDC_VRAM_SIZE];
    uint16_t sat[VDC_SAT_SIZE];
    uint16_t color[512];
    uint32_t rgb[512];    /* the colors shown (smooth fade, app/fade.h; redraw_capture puts the console's) */
    uint8_t chan[3][8];   /* each level (blue, red, green) of Adol's sprite palette, as shown (direct color) */
    VdcLineInfo rows[VCE_ACTIVE_LINES];
    VdcLogWrite writes[VDC_LOG_WRITES];
    int write_count;
    uint32_t pixels[VCE_MAX_WIDTH * VCE_ACTIVE_LINES]; /* the real image (ARGB) */
    int w, h, divider;
    long seq;
    int exact; /* -1 = not checked; 1 = the redraw at its own positions equals the image; 0 = not */
    int exact_known; /* exact already came from fix_equip's redraw (the same as redraw_check, which does not redo it) */
    YsFrameList eng; /* Ys engine list in this frame (ys_engine_frame, outside redraw_capture) */
    EquipInfo equip; /* Adol's equipment at the end of the frame (equip_read, outside redraw_capture) */
    EquipColors equip_colors; /* its colors in this frame (equip_colors, outside redraw_capture) */
    YsWinFrame wins; /* game windows at the end of the frame (ys_windows_snapshot, outside redraw_capture) */
    YsScrollFrame scroll; /* game scroll changes in this frame (ys_scroll_frame, outside redraw_capture) */
    int torn_win; /* tearing inside the game windows (fix_torn_log; app/game_fixes.h: fix_torn_window) */
    YsRewardFrame rewards; /* rewards from kills since the last prepared frame (app/ys_reward.h) */
} RedrawFrame;


/* Direct color (9 bits: the equipment on Adol) with the shown levels of f->chan */
static inline uint32_t redraw_direct_rgb(const RedrawFrame *f, uint16_t c)
{
    return 0xFF000000u | (uint32_t)f->chan[1][(c >> 3) & 7] << 16 | (uint32_t)f->chan[2][(c >> 6) & 7] << 8 |
           f->chan[0][c & 7];
}

/* Map edges: the frame's sides are in the background itself, just outside the visible map, and the game rewrites them
 * when the camera moves. B's shifted map brings in a strip from outside B's map (up to ~4 pixels): it comes from
 * A's map, where those columns were still visible map; if not even A had them, it repeats B's edge column. */
typedef struct RedrawSides {
    const uint8_t *rows;    /* map lines (1 per image line) */
    int l, r;               /* map columns in the image (l..r-1) */
    const uint16_t *a_vram; /* A's VRAM, or NULL */
    int a_left;             /* background pixel at the 1st map column in A */
} RedrawSides;

/* Background pixel bgx (line bgy) outside B's map (b_left: background pixel at the 1st map column in B; vram and win:
 * B's), as RedrawSides says */
uint16_t redraw_side_pixel(const RedrawSides *sd, const uint16_t *vram, int reg, int b_left, int bgx, int bgy,
                           const uint8_t *win);

/* Redraw.layer: the whole image; only the background, without sprites (alpha 0x80 where it is transparent, 0xFF where it is
 * opaque); only the sprites, without the background (alpha 0 = none, 0xFF = in front of the background, 0x80 = behind the opaque background) */
enum { REDRAW_LAYER_ALL, REDRAW_LAYER_BG, REDRAW_LAYER_SPRITES };

typedef struct Redraw {
    uint16_t vram[VDC_VRAM_SIZE], sat[VDC_SAT_SIZE]; /* as they were on the line being drawn */
    const RedrawSides *sides; /* map edges (redraw_draw), or NULL */
    const RedrawMask *masks; /* drawing masks (redraw_draw; xq/yq filled; up to REDRAW_DRAW_MASKS), or NULL */
    int nmasks;
    long mask_hidden;        /* sprite pixels the masks hid (pce_trace) */
    long pal_seq;
    int fade, pal_fade; /* fade = 1: draws with the shown colors (RedrawFrame.rgb), not the console's */
    uint32_t pal[512];
    uint32_t *out; /* smooth motion image on the CPU, (w*REDRAW_SUB) x (h*REDRAW_SUB) */
    int out_w, out_h;
    uint8_t *from_sprite; /* (pce_trace) the size of out: 1 = the pixel came from a sprite; NULL = not recorded */
    long frames, exact_frames; /* checked (redraw_check) and equal */
    int window_front; /* item window in front of the sprites (app/game_fixes.h: fix_window) */
    uint8_t win[128 * 64]; /* scratch: window tiles, 1 per map entry */
    Equip *equip;          /* equipment on Adol, or NULL */
    int layer;             /* REDRAW_LAYER_* (redraw_frame; around the shrunk windows, app/crop.h) */
    const uint8_t *skip;   /* 1 per sprite table entry: do not draw (redraw_ring), or NULL */
    const RedrawFrame *eq_frame; /* scratch (redraw_equip): the frame being drawn and each entry's sprite */
    uint8_t eq_done[64];
    const uint16_t *eq_img[64];
} Redraw;

/* Start of drawing b: the sprites with equipment are looked up again (redraw_equip) */
void redraw_equip_begin(Redraw *s, const RedrawFrame *b);
/* Table entry i (e) with VRAM vram, on the 1st line where it shows in the drawing: Adol's image with the
 * equipment (app/equip.h: equip_sprite) or NULL */
const uint16_t *redraw_equip(Redraw *s, const uint16_t *vram, const uint16_t *e, int i);

/* Copies the frame that just ended (log = Vdc.line_log, which is left ready for the next frame) */
void redraw_capture(RedrawFrame *f, VdcLineLog *log, const Vce *vce, long seq);

/* 0 = ok; -1 = out of memory */
int redraw_init(Redraw *s);
void redraw_free(Redraw *s);

/* Checks b (b->exact) by redrawing it at its own positions. */
void redraw_check(Redraw *s, RedrawFrame *b);

/* Draws b at 1/k pixel (k = 1 << ks) at the given positions, in b's list order (app/motion.h): horizontal
 * scroll of each line bx_q (in 1/k), map line of each sub-line bg_line[(r << ks) + j] (in pixels) and
 * position of each sprite sx_q/sy_q (x from the 1st visible pixel, y in VDC lines, in 1/k).
 * out: (b->w << ks) x (b->h << ks). */
void redraw_draw(Redraw *s, const RedrawFrame *b, int ks, const int *bx_q, const int *bg_line, const int *sx_q,
                 const int *sy_q, uint32_t *out);
/* Around the fill's shrunk windows (app/crop.h: crop_ring), the VRAM of the windowless frame 2 before the 1st
 * (the one right before may have the window half drawn) */
typedef struct RedrawRing {
    uint16_t keep[VDC_VRAM_SIZE], next[VDC_VRAM_SIZE];
    int keep_ok, next_ok;
    /* The scroll and the map lines of that frame. On the large menu screens (the save one) the game puts the whole image
     * in a single scroll, with the map elsewhere in the background map: the earlier VRAM only applies with the earlier scroll */
    VdcLineInfo keep_rows[VCE_ACTIVE_LINES], next_rows[VCE_ACTIVE_LINES];
    uint8_t keep_map[VCE_ACTIVE_LINES], next_map[VCE_ACTIVE_LINES];
    long one_scroll_frames; /* frames with a window in which it was used (pce_trace) */
    /* the sprites left out (redraw_ring) of frame skip_seq (-1 = none): the map drawing also leaves them out
     * (Redraw.skip; the old mask the game leaves on Adol when closing the inventory cut his body) */
    uint8_t skip[64];
    long skip_seq;
    uint8_t own[64]; /* of those, the windows' (by the game, RedrawFrame.wins.own; ring_skip) */
    /* the windows' sprites (those entirely inside them), each with the last frame it was seen there (ghost_seq):
     * when closing the equipment, Ys leaves the pink frames in the table 2 more frames, which around the shrunk window
     * (with SELECT still open) or on the map were left loose and large; up to RING_GHOST_FRAMES later, identical ones (the 4
     * words) are also left out */
    uint16_t ghost[64][4];
    long ghost_seq[64];
    int ghost_n;
    /* the game's masks that touch a window (built with its filled cells) are removed (skip) and these go in,
     * by the game's rule with the map from before the windows (app/ys_masks.h: ys_masks_unwindowed), from frame skip_seq; the
     * map drawing also uses them (motion_set_ring) */
    RedrawMask masks[REDRAW_BEHIND];
    int nmasks;
    long no_list; /* frames with a window without the engine list: the game's masks stay (pce_trace) */
    long remade, remade_frames; /* masks rebuilt without the window and in how many frames (pce_trace) */
    long hidden_px;             /* sprite pixels they hid in the layer around the windows (pce_trace) */
    long hole_sprites, hole_frames; /* sprites of the windows' characters left out and in how many frames */
} RedrawRing;
#define RING_GHOST_FRAMES 8
/* New frame b; windows = how many game windows it has, in boxes (x0, y0, x1, y1 in b's pixels, inclusive). Without
 * windows, stores the VRAM and returns 0. With windows, draws b's sprites (REDRAW_LAYER_SPRITES, with the equipment
 * on Adol, if equip) into spr, except the windows' (by the game, b->wins; the cursor and the icons: around the shrunk
 * window they showed in the old place), with the masks that touch one rebuilt without it (map_row = b's
 * map lines, motion_map_rows; NULL = only removed), and returns 1; and, if there is stored VRAM, b's background with it
 * (REDRAW_LAYER_BG: the map from before the windows at the current scroll) into bg, and returns 2. bg and spr: b->w x b->h.
 * With fix and bg_shown (b's background as it was shown, redraw_bg_only), also fix: where there is a sprite not from the window
 * (alpha 0xFF), the pixel of bg_shown, for inside the shrunk windows; alpha 0 = the frame's pixel */
int redraw_ring(RedrawRing *r, const RedrawFrame *b, int windows, const int (*boxes)[4], Equip *equip, uint32_t *bg,
                uint32_t *spr, const uint32_t *bg_shown, uint32_t *fix, const uint8_t *map_row);
/* Only b's background, without the sprites, opaque, into out (b->w x b->h): as it was shown (end = 0) or with the VRAM at the
 * end of the frame (end = 1, all its writes). Inside the shrunk windows (redraw_ring: bg_shown), the background without
 * the characters Ys draws over the border on the 1st frame */
void redraw_bg_only(const RedrawFrame *b, int end, uint32_t *out);
/* b at its own positions (as the console showed it, with what b->rows says), into out: b->w x b->h */
void redraw_frame(Redraw *s, const RedrawFrame *b, uint32_t *out);

/* n background pixels from (x, y) of the map (9-bit color: palette << 4 | index), like the VDC's render_background;
 * with mask (1 per map entry), the pixels of the marked tiles get 0x400 (app/gpu_draw.h) */
void redraw_bg_row(const uint16_t *vram, int reg, int x, int y, int n, uint16_t *out, const uint8_t *mask);
/* Game windows at the start of b's image (b->wins, ys_windows_cells) in the map cells of size reg, and the
 * sprites that stay in front of them (keep[64]: the windows', b->wins.own, in the frame's list or the previous one's).
 * Returns the mask (1 entry per map cell, in s->win; whoever draws passes the game's writes to the map to it via
 * ys_windows_write), or NULL if there is no window. */
const uint8_t *redraw_windows(Redraw *s, const RedrawFrame *b, int reg, uint8_t *keep);

/* Map size (in tiles) for MWR value reg, like the VDC */
int redraw_tiles_y(int reg);
int redraw_tiles_x(int reg);

#endif
