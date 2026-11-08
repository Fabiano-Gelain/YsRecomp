/* Gains on kill and enemy HP bar on the map (simulation approved by the user on 2026-10-27).
 *
 * Window only: the console does not change. Read from each frame's sprite list (app/ys_engine.h: each walker's HP
 * and the player's EXP/GOLD at the end of the build, docs/ys_notes.md "Enemies: HP, EXP and GOLD"):
 *   bar: wounded enemy (0 < HP < max) that took a hit less than BATTLE_BAR_FRAMES frames ago: 20 x 2 pixels
 *     just above the lit pixels of its sprites, yellow = HP left, red = HP lost (the colors of the panel's ENEMY
 *     bar);
 *   gains (bt->popups: 1 = Large, 2 = Small, the small 3x5 font on one line): the reward the game paid for the death
 *     (RedrawFrame.rewards, through the hooks of the hit routine, app/ys_reward.h: the walker and what went up),
 *     "+N EXP" (white) and "+N GOLD" (yellow) in the game's font, pinned to the ground where it was (the box of its lit
 *     pixels in the frame's list or in the last one where it was drawn): they rise 12 pixels,
 *     stay and fade out (BATTLE_POPUP_FRAMES frames).
 * With any game window in the frame (RedrawFrame.wins, opening, open or closing) nothing appears. Everything stays inside
 * the map (CROP_MAP_*). With smooth motion, the bar moves with the enemy's sprite and the text with the map
 * (battle_place). */
#ifndef PCE_APP_BATTLE_H
#define PCE_APP_BATTLE_H

#include <stdint.h>

#include "app/ys_engine.h"

struct RedrawFrame;
struct MotionLayout;

#define BATTLE_BAR_FRAMES 180   /* the bar stays 3 s after the last hit */
#define BATTLE_POPUP_FRAMES 75  /* gains: 1.25 s */
#define BATTLE_POPUPS 8
#define BATTLE_STICKERS 24
#define BATTLE_STICKER_W 96
#define BATTLE_STICKER_H 20

enum { BATTLE_BAR, BATTLE_POPUP };

/* A drawing on top of the image, in console image pixels (ARGB; alpha 0 = transparent) */
typedef struct BattleSticker {
    int kind;
    int actor;          /* bar: the enemy's character (moves with its sprite) */
    int x, y;           /* top left corner, at the frame's positions */
    int w, h;
    int alpha;          /* 0-255 */
    uint32_t px[BATTLE_STICKER_W * BATTLE_STICKER_H];
} BattleSticker;

typedef struct BattlePopup {
    int on, age;
    int wx, wy;         /* top center, on the map (image + unwrapped camera) */
    int exp, gold;
} BattlePopup;

typedef struct Battle {
    int bars, popups;   /* options (video.h: enemy_hp_bars, reward_popups) */
    int small;          /* the gains at 3/4 (fill with the HUD at 3/4, crop_small_hud); the caller sets it */
    long seq;           /* last list seen */
    int valid;
    uint8_t hp[YS_WALKERS], max[YS_WALKERS], actor[YS_WALKERS];
    int hit_age[YS_WALKERS]; /* frames since the last hit (-1 = no hit) */
    int box_x[YS_WALKERS], box_y[YS_WALKERS], box_w[YS_WALKERS]; /* the enemy's lit pixels (top and width) */
    int cam_mx, cam_my;  /* the game's camera (in the ring) */
    long cam_x, cam_y;   /* unwrapped camera */
    BattlePopup pop[BATTLE_POPUPS];
    int window;          /* game window on the map in this frame */
    BattleSticker st[BATTLE_STICKERS];
    int count;           /* drawings of the frame (battle_frame) */
    long popups_made, bars_shown; /* counts (ys_trace --battle) */
    long rewards_unplaced;        /* rewards without the enemy's box (not shown) */
} Battle;

void battle_init(Battle *bt);
/* New frame b (with the engine's list): hits, deaths, gains and the frame's drawings (bt->st, bt->count) */
void battle_frame(Battle *bt, const struct RedrawFrame *b);
/* Position of each drawing in an image: lay = NULL, the frame's; otherwise the smooth motion ones (app/motion.h) in
 * image pixels (between pixels). x0/y0 of each drawing in out_x/out_y. */
void battle_place(const Battle *bt, const struct RedrawFrame *b, const struct MotionLayout *lay, double *out_x,
                  double *out_y);
/* Draws on the image dst (w x h console pixels, sub times larger in each direction, line = stride pixels), only inside
 * the map */
void battle_draw(const Battle *bt, const double *x, const double *y, uint32_t *dst, int w, int h, int sub,
                 int stride);
/* The same, with point (0, 0) of the console image at (ox, oy) of dst, only inside clip (x0, y0, x1, y1 of dst, the end
 * outside) and only the drawings of kinds (1 << BATTLE_BAR, 1 << BATTLE_POPUP). With bt->small and sub a multiple of 4, the
 * gains come out at 3/4 (without smooth motion, the window draws them on the image cropped 4x: crop_map_out) */
void battle_draw_at(const Battle *bt, const double *x, const double *y, uint32_t *dst, int stride, int sub, int ox,
                    int oy, const int clip[4], int kinds);

#endif
