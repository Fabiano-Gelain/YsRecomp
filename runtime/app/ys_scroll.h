/*
 * Ys's mid-image scroll changes, read from the game (docs/ys_notes.md, "Mid-frame scroll
 * changes"). The game's line interrupt ($80:4AC6, counter at $31C4, cleared at VBlank at $80:4ABA) does:
 *   state 0 ($80:4AE1, JSR $4A5B): start of the map: BXR = $2C2F/$2C30, BYR = $2C31 - 1 (the next line shows
 *            map line $2C31), sprites on (SMB6 $F3); if $2C31 >= $81 the map goes past line 255 of the ring and
 *            state 1 comes next, otherwise 2;
 *   state 1 ($80:4B05, JSR $4A88): ring wrap: BYR = $1FF (the next line shows line 0) and BXR = $2C2F, or 0
 *            with bit 6 of $82;
 *   state 2+ ($80:4AD8, JSR $4A75): end of the map: BXR = $220E/$220F, BYR = $2212/$2213 (frame and panel stored from
 *            VRAM line 256 on), sprites off (RMB6 $F3).
 * CR comes from $F3/$F4 at the end of each interrupt ($80:4A33). Hooks on the three calls (checked against the
 * code bytes, read only: the console does not change) store the values the game set and the image line where it set them; with a lot
 * to do (centipede boss, temple) the interrupt arrives 1-2 lines late and the console's image comes out with the change
 * delayed. The PC redraws the image with the change on the right line (game_fixes.h: fix_scroll).
 */
#ifndef PCE_APP_YS_SCROLL_H
#define PCE_APP_YS_SCROLL_H
#include <stdint.h>
#include "pce/pce.h"

/* Image lines where the map and the lower frame start when the change arrives on time (made on the previous
 * line: 26 and 154 in every on-time frame of the sessions; = CROP_MAP_Y and CROP_MAP_Y + CROP_MAP_H) */
#define YS_SCROLL_MAP_ROW 27
#define YS_SCROLL_LOW_ROW 155

/* The changes of one frame (the one at the start of the map and the one at the end; the ring wrap between them, if any) */
typedef struct YsScrollFrame {
    uint8_t valid;          /* start and end of the map seen, in that order */
    uint8_t wrap;           /* ring wrap seen */
    uint16_t map_x, map_y;  /* the map's BXR and its 1st line in the ring ($2C2F, $2C31) */
    uint16_t wrap_x;        /* BXR after the ring wrap */
    uint16_t low_x, low_byr; /* BXR and BYR (as written) of the lower frame ($220E, $2212) */
    uint8_t cr;             /* $F3 (bits 0x80 background, 0x40 sprites: the game sets 0x40 on the map and clears it below) */
    int16_t row_map, row_wrap, row_low; /* image line where each change was made (-1 = none) */
} YsScrollFrame;

typedef struct YsScroll {
    YsScrollFrame cur; /* the frame being output */
} YsScroll;

void ys_scroll_init(YsScroll *s);
/* Before each instruction (on_instruction, after = 0) */
void ys_scroll_hook(YsScroll *s, Pce *pce);
/* At the end of the frame: its changes in out; starts the next one */
void ys_scroll_frame(YsScroll *s, YsScrollFrame *out);

#endif
