/*
 * New smooth motion, reading the game: more than 60 images per second without guessing anything from the frame.
 *
 * The Ys engine (app/ys_engine.h) tells, every frame, the camera, where each character is and whose each
 * sprite is. The image is ~1 frame behind the game: between the previous frame (A) and the last one (B), at fraction f.
 *
 *   Camera and characters move by the game's position. "Walkers" (Adol, the enemies) move in steps of 8
 *     pixels: on the decision frame they do not move, then move for m frames (accumulator: speed / unit). The
 *     shown position travels the step's path at constant speed, 8 / (m + 1) pixels per frame (SLOW
 *     1.6; FAST 2): it leaves the game's position at the decision and returns to it at the next decision (never overshoots and comes back; while
 *     walking it stays up to one frame step behind). No history, no line fitting, no error limit.
 *   The camera follows whoever it follows (the character standing still on screen while walking in the world): it gets the same correction.
 *     Map-attached ones move with the shown camera; screen-attached ones, by their own position.
 *   Map: only the lines that scroll with the camera move (the frame, the panel and the windows stay). Sprites without an owner (the
 *     frame corners) stay where they are in the frame.
 *   Late frame (the "2-0-4"): motion_fix_late replaces the repeated frame with the drawing of the list the console never
 *     showed (also applies without smooth motion).
 *   Outside the engine (intro, title screens, menus): only the scroll of each image piece moves (from A to B,
 *     without constant speed); the sprites stay where they are.
 *   Jump (camera or character moving more than MOTION_JUMP pixels in one frame: room change): no interpolation.
 *   Map edges: the strip the offset brings in from outside B's map (where the game already put the frame) comes from
 *     A's map (redraw.h, RedrawSides).
 *   Under roofs and walls: the masks that hide a walker (sprites behind the background) come, in each
 *     image, from the game's rule at the walker's shown position (app/ys_masks.h), instead of the game's.
 *
 * Drawing: motion_layout gives the positions of an image; motion_render draws with them on the CPU (redraw_draw,
 * 1/4 pixel) and app/gpu_draw.h on the GPU, in screen pixels.
 */
#ifndef PCE_APP_MOTION_H
#define PCE_APP_MOTION_H

#include "app/redraw.h"
#include "app/ys_engine.h"
#include "app/ys_masks.h"

#define MOTION_JUMP 16
#define MOTION_STEP 8     /* walkers' step, in pixels */
#define MOTION_SAMPLES 24 /* positions stored for one step (the slowest: 1 pixel per frame) */

/* A walker in the current step */
typedef struct MotionWalk {
    int on;              /* step tracked since the decision */
    int actor;
    int t, m;            /* frames walked in the step; frames the step takes walking */
    int n;               /* stored positions */
    int prog[MOTION_SAMPLES];          /* pixels walked (8 - remaining) */
    int wx[MOTION_SAMPLES], wy[MOTION_SAMPLES]; /* world position (screen + unwrapped camera) */
    int sx0, sy0;        /* screen position at the decision */
    int fix_x, fix_y;    /* still on screen on that axis in all frames of the step */
} MotionWalk;

/* Positions of one frame */
typedef struct MotionPose {
    int valid;
    long seq;                  /* engine list */
    int cam_mx, cam_my;        /* game camera (in the ring) */
    int raw_x, raw_y;          /* unwrapped game camera */
    double cam_x, cam_y;       /* shown camera (unwrapped) */
    uint8_t present[YS_ACTORS];
    int sx[YS_ACTORS], sy[YS_ACTORS];       /* game position on screen */
    double dx[YS_ACTORS], dy[YS_ACTORS];    /* shown position on screen */
    uint8_t glued[YS_ACTORS]; /* position copied by the game from another's (the mine light, YsList.copy_of): moves with B's map */
} MotionPose;

typedef struct Motion {
    int constant_speed;  /* 0 = interpolation only (tests: at f = 1 the image is the frame) */
    MotionWalk walk[YS_WALKERS];
    MotionPose pose[2];  /* A, B */
    int lerp;            /* B came right after A and without a jump */
    int engine;          /* B has the engine list and the map lines */
    uint8_t map_row[VCE_ACTIVE_LINES];
    int piece_dx[VCE_ACTIVE_LINES], piece_dy[VCE_ACTIVE_LINES]; /* outside the engine: scroll from A to B */
    /* Last motion_render (tests) */
    double shown_cam_x, shown_cam_y;
    double spr_x[YS_ACTORS], spr_y[YS_ACTORS];
    int follow;          /* character the camera follows in the last step (-1 = none) */
    /* Walkers' masks by the game's rule (app/ys_masks.h): the walkers hidden in B and the blocks that the
     * images between A and B (and, with constant speed, also at f = 1) may use, at B's positions */
    YsMasks ym;
    int behind_off;      /* (pce_trace) only the game's masks, to measure what the rule's change */
    long mask_missing;   /* images where a block was missing (the walker kept the game's; must be 0) */
    /* The masks rebuilt without the shrunk windows (RedrawRing.masks of frame ring_seq; motion_set_ring): they go
     * into ym.block from ring_from on, in all images of the frame (the game's are removed by Redraw.skip) */
    const RedrawMask *ring_masks;
    int ring_n, ring_from;
    long ring_seq;
    RedrawMask draw_mask[REDRAW_DRAW_MASKS]; /* those of the last motion_render */
    /* Map edges (redraw.h: RedrawSides): the strip outside B's map comes from A's map */
    RedrawSides sides;   /* rows = NULL: no edges (outside the engine) */
    uint16_t side_vram[VDC_VRAM_SIZE];
    /* Counters */
    long frames, engine_frames, lerp_frames, late_fixed;
} Motion;

void motion_init(Motion *m, int constant_speed);
void motion_reset(Motion *m);

/* Late frame: b->eng is the list the console never showed (YS_LIST_LATE). Replaces in b the sprite table and
 * the scroll of the map lines with its own and redraws b->pixels (the shown image; the console does not change). Returns 1
 * if it replaced; if the redraw does not reproduce the frame, b->eng becomes YS_LIST_NONE and returns 0. */
int motion_fix_late(Motion *m, Redraw *s, RedrawFrame *b);

/* New frame b (a = the previous one, or NULL), already checked (redraw_check) */
void motion_step(Motion *m, const RedrawFrame *a, const RedrawFrame *b);

/* Adol's rhythm on screen (--walk-rhythm), after motion_step, in frames from..to-1: how much he moved in the world
 * (shown camera + screen position) from one frame end to the next, as smooth motion shows it. "Stop" = a
 * still frame with him walking in the previous and next ones (the jolt the hidden frame used to cause). */
#define MOTION_RHYTHM_BINS 81 /* speed in tenths of a pixel per frame; the last one groups 8 or more */
typedef struct MotionRhythm {
    long from, to; /* to = 0: off */
    int have;      /* wx, wy are valid */
    double wx, wy, s1, s2; /* position at the end of the last frame; speeds of the last two (< 0: none) */
    long walking, stops, first_stop;
    long hist[MOTION_RHYTHM_BINS];
} MotionRhythm;
void motion_rhythm_add(MotionRhythm *r, const Motion *m, long frame);
void motion_rhythm_print(const MotionRhythm *r);

/* The lines of b's image that are the map (1 per line; by the engine list's camera); returns how many (0 without the
 * list) */
int motion_map_rows(const RedrawFrame *b, uint8_t *rows);

/* Before the motion_step of frame seq: the masks rebuilt without the shrunk windows (RedrawRing; n = 0: none) */
void motion_set_ring(Motion *m, const RedrawMask *masks, int n, long seq);

/* dst = src with the inner pointers (edges, rebuilt masks) pointing into dst; the masks go to ring_store
 * (REDRAW_BEHIND). dst no longer depends on src (the prep thread continues with src while the window draws dst) */
void motion_copy(Motion *dst, const Motion *src, RedrawMask *ring_store);

/* Positions of one image, in pixels; whoever draws rounds to their grid: q(v) = floor(v * k + 0.5) */
typedef struct MotionLayout {
    int exact;  /* 0 = the real image (b->exact != 1): nothing moves */
    int sides;  /* engine: on the map lines, the frame goes outside the map columns and the strip outside B's map comes
                 * from A's map (Motion.sides; keep_sides in motion.c) */
    double map_ox, map_oy;                  /* offset of the map lines (engine) */
    double ox[VCE_ACTIVE_LINES], oy[VCE_ACTIVE_LINES]; /* the line shows B's map shifted by (ox, oy) */
    uint8_t ring[VCE_ACTIVE_LINES];         /* the map line wraps around the 256-line ring */
    uint8_t map_row[VCE_ACTIVE_LINES];      /* map line (engine) */
    double sx[YS_ACTORS], sy[YS_ACTORS];    /* sprites (x from the 1st visible pixel, y in VDC lines) */
    uint8_t glued[YS_ACTORS]; /* moves with B's map: q(sx) - q(map_ox), q(sy) - q(map_oy) */
    /* masks by the rule (Motion.ym.block) in this image: those of the step each hidden walker is at in it;
     * they move with B's map. hide: the sprites of those walkers' game masks (removed from the image) */
    uint8_t behind_on[YS_MASK_BLOCKS];
    uint8_t hide[YS_ACTORS];
} MotionLayout;

/* Positions at fraction f (0..1) from A to B (also writes the test fields: shown_cam_*, spr_*) */
void motion_layout(Motion *m, const RedrawFrame *b, double f, MotionLayout *lay);

/* Image at fraction f (0..1) from A to B: s->out, (b->w * REDRAW_SUB) x (b->h * REDRAW_SUB) */
const uint32_t *motion_render(Motion *m, Redraw *s, const RedrawFrame *b, double f);

#endif
