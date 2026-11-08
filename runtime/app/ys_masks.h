/*
 * The walkers' masks by the game's rule (docs/ys_notes.md, "How the game builds the masks").
 *
 * To hide a character behind roofs, walls and trees, the game places mask sprites (black, behind the background,
 * before it in the list) built from the map cells around it. Each walker moves in 8-pixel steps and has two:
 * the one at the start of the step ($2B9F, the checked position P) and the one at the end ($2BCF, P + 8 in the step's direction, $81:7DFE/$7E07);
 * at the end of the step the start one goes away and the other stays (until the next decision). Type 1 block (the only one accepted: the one of
 * Adol and the villagers, drawing $40-$5F): 16 x 32 pixels, sprite at (P.x, P.y + 8); 2 cells x 4 lines, the top one with
 * limit 3 and each one below with 1 less (the feet one, 0). Front cell = tile of the area's table with height >= the
 * limit (its 8x8 drawing) or tile outside the table / of palette 14-15 (full). Bit 4 rule ($70): starts at $FF,
 * a full cell clears bit 4, a front cell ANDs with its bits; if bit 4 is left, the game hides the mask.
 *
 * In the images between two frames (A and B) the character appears off the game's position (up to one frame's step behind):
 * the game's masks in B are those of B's step. Here, in each image, the ones that count are those of the step the character is at
 * in it (motion.c); the game's ones for that walker leave the image. The frames do not change. The walker and its masks
 * come from the tables the game itself stores when building them (Ys I: `JSR $81:7A13 / STA $2B9F,X` at the start of the step,
 * `JSR $81:7A15 / STA $2BCF,X` at the decision; Ys II: $81:7A75/$7A77), and the pixels of each block are the ones the game
 * drew: the mask sprite in B at the same checked position, otherwise A's (the step that ended between A and B); only if the
 * game showed none in either, the rule with B's map (counted). The game builds each mask with the map of the
 * moment (in the dark cave the lit floor changes later; with a window open, its cells are full): that is why the
 * pixels are read from the sprites, not rebuilt.
 *
 * Around the shrunk windows (fill): where the console shows the window, the PC window shows the map from before
 * it (redraw_ring). The type 1 masks that touch a window (built with its full cells) go away and in come those
 * of the same rule with that map, at the same position: the character stays as it would without the window (behind the roof, in the
 * dark). ys_masks_unwindowed.
 */
#ifndef PCE_APP_YS_MASKS_H
#define PCE_APP_YS_MASKS_H

#include <stdint.h>

#include "app/redraw.h"
#include "app/ys_engine.h"

#define YS_MASK_WALKERS 8
#define YS_MASK_BLOCKS REDRAW_BEHIND
#define YS_MASK_H 32 /* type 1 block: 16 x 32 pixels */

/* A mask the game showed: of character actor, at the checked position (x, y) (game screen in B; the sprite is
 * at (x, y + 8)), the pixels of its sprites (none: hidden by the game or off screen) */
typedef struct YsGameMask {
    int actor, x, y;
    int age; /* frames before B in which the game showed it (0 = in B) */
    uint32_t rows[YS_MASK_H];
} YsGameMask;
#define YS_MASK_AGE 8 /* the ones the game showed up to 8 frames before B (the constant speed puts the character up to
                       * ~3 pixels behind the game's position: in the cell of the previous step, shown a few frames earlier) */
#define YS_MASK_HIST (YS_MASK_WALKERS * 2 * YS_MASK_AGE)

typedef struct YsMaskWalker {
    int actor;               /* hidden character (engine index) */
    int px, py;              /* its position in B (game screen) */
    int gx, gy;              /* a point of the step grid: the checked position of the step-start mask in B */
    int dx, dy;              /* step direction (-1, 0, 1 on each axis; 0, 0 = no known step): only on the diagonal */
    int has_next, nx, ny;    /* the step-end mask the game already shows in B (step decided, before the
                              * character moves): goes in all the frame's images, as in B */
    int before;              /* the masks go in the order before this sprite of B (the 1st of the game's ones or of it) */
    uint8_t skip[64];        /* B's sprites of this walker's game masks */
} YsMaskWalker;

typedef struct YsMasks {
    int nwalkers;
    YsMaskWalker walker[YS_MASK_WALKERS];
    int nblocks;
    RedrawMask block[YS_MASK_BLOCKS]; /* at B's positions */
    int bx[YS_MASK_BLOCKS], by[YS_MASK_BLOCKS], bw[YS_MASK_BLOCKS]; /* checked position and walker of each block */
    int row_at[1024];        /* for each VDC line, the line of B's map that gives the scroll (the nearest; -1 = no map) */
    /* Last step the game showed of each walker (the character and the direction), from frame to frame */
    uint8_t last_actor[YS_WALKERS];
    int8_t last_dx[YS_WALKERS], last_dy[YS_WALKERS];
    /* The walkers' masks the game showed: in B and in the YS_MASK_AGE frames before (pinned to the map: positions already
     * on B's screen; for each character and position, the newest) */
    int ngame, nhist;
    YsGameMask game[YS_MASK_WALKERS * 2];
    YsGameMask hist[YS_MASK_HIST];
    long game_seq; /* frame of game (-1 = none) */
    /* Counters (pce_trace): hidden walkers; blocks with B's pixels, from before B and by the rule (the game did not
     * show any there) */
    long walkers, no_slot, from_b, from_a, by_rule;
    uint8_t rule_made[YS_MASK_BLOCKS]; /* the block came from the rule (the game showed none there in those frames) */
    long rule_images;        /* images that used one of those (motion_layout) */
} YsMasks;

/* New frame b (map_row: 1 per line of b's image; cdx, cdy: how much the camera moved since the previous frame): the
 * walkers hidden by the game in b (from its tables), with the direction of b's step or, without the step-end
 * mask, that of the last step the game showed of the walker, and the masks the game showed in b and in the frames before
 * (keep = 0: the camera jumped, the earlier ones go away).
 * Clears the blocks. */
void ys_masks_frame(YsMasks *ms, const RedrawFrame *b, const uint8_t *map_row, int cdx, int cdy, int keep);

/* Around the shrunk windows: b's type 1 masks (engine list) with a sprite touching one of the windows
 * boxes (n; frame pixels, x0 y0 x1 y1 inclusive) go away (skip[i] = 1 on their sprites, 64) and in come the rule's ones
 * with the map of VRAM vram (the one from before the windows; NULL = b's) at the same position, in out (up to max; x/y/xq/yq in
 * b's pixels). map_row: the lines of b's map (motion_map_rows). Returns how many (-1 = b without the engine list) */
int ys_masks_unwindowed(const RedrawFrame *b, const uint16_t *vram, const uint8_t *map_row, const int (*boxes)[4],
                        int n, uint8_t *skip, RedrawMask *out, int max);

/* mark[i] = 1 (64) on b's sprites of the characters that only exist because of the windows (ys_list_window_actor: in the mine,
 * the one that pierces the light mask in the window's cells; around the shrunk window, with the map from before it, it would pierce the
 * mask over the lit floor). Returns how many sprites it marked (0 = b without the engine list) */
int ys_masks_window_actors(const RedrawFrame *b, uint8_t *mark);

/* The block of walker w at the checked position (x, y) (game screen in B), built with b's map; creates it if it does not
 * exist yet. Returns its number in ms->block, or -1 with no room */
int ys_masks_block(YsMasks *ms, const RedrawFrame *b, int w, int x, int y);

/* The already built block of walker w at the checked position (x, y), or -1 */
int ys_masks_find(const YsMasks *ms, int w, int x, int y);

/* The checked positions of the blocks around position (x, y) of walker w (game screen in B), as the game places them
 * during a step: on each axis, the two grid points around it (only one if it is on the grid); off the grid
 * on both axes, in a diagonal step the two corners of its direction, otherwise the 4. Returns how many (1 to 4) */
int ys_masks_step(const YsMaskWalker *w, double x, double y, int *bx, int *by);

#endif
