/*
 * Ys panel read from VRAM (docs/ys_notes.md, "Panel: the cells the game writes"). The game stores the lower
 * frame and the panel in the background map from line 256 on and shows that part below the map (the image from line
 * YS_SCROLL_LOW_ROW on comes from map line $2212 + 1, column $220E: app/ys_scroll.h). In the panel it writes
 * fixed tiles, the same in Ys I and Ys II:
 *   text (image line 179): label letters 257-266 (H . P = 257 258 259, E X P = 260 261 259,
 *         G O L D = 262-265, M . P = 266 258 259), digits 0-9 = 449-458 and "/" = 460, blank = 256; the player's
 *         numbers in palette 15, like the labels; on the save screen (the numbers of the chosen save) in palette 5;
 *   bars (cells of lines 195 and 211): PLAYER 267-272, ENEMY 273-278, sides 282 (pixel lit in the last column),
 *         outlines 279/280 (last line of the upper cell), 284/285 (both), 288 (1st line of the lower one: the cell at the
 *         lower left corner of the ENEMY bar the game leaves blank); inside, 1 pixel per HP point (up to 255):
 *         yellow = what is left, red = what was lost, unlit = beyond the maximum (tiles 506-511: 506 yellow, 507
 *         red, the others rebuilt by the game; with max HP 255 the last cell, 511, is the tip with the side attached,
 *         in place of 282).
 * ys_panel_read stores those cells and the color number of each of their pixels (by tile): the HUD (app/crop.c) finds the
 * labels, numbers and bars through them, not through the image's colors. Read only: the console does not change.
 *
 * Save screen (docs/ys_notes.md, "Save screen"): the game writes on the panel the numbers of the chosen save and leaves
 * that save's next level ($2C4A) and max H.P ($2C48) in RAM, but the bar stays the player's. When listing the saves
 * it passes each one through the player's H.P ($2C43), EXP ($2ABB) and GOLD ($2C41) for a few frames, before writing the
 * list: those are no use for the HUD; its EXP is the panel's.
 */
#ifndef PCE_APP_YS_PANEL_H
#define PCE_APP_YS_PANEL_H
#include <stdint.h>

#define YS_PANEL_X 8    /* 1st column of the panel in the image (the frame's) */
#define YS_PANEL_Y 179  /* 1st line of the panel in the image: the text's */
#define YS_PANEL_ROWS 6 /* 8-line cells: text, outline, player bar, outline, enemy bar, outline */
#define YS_PANEL_COLS 40
#define YS_PANEL_W (YS_PANEL_COLS * 8)
#define YS_PANEL_H (YS_PANEL_ROWS * 8)

#define YS_TILE(e) ((e) & 0xFFF) /* background map word: palette << 12 | tile */
#define YS_PAL(e) ((e) >> 12)
#define YS_TILE_BLANK 256
#define YS_TILE_LABEL_FIRST 257 /* label letters */
#define YS_TILE_LABEL_LAST 266
#define YS_TILE_H 257 /* 1st letter of each label: H.P, E(XP), G(OLD), M.P */
#define YS_TILE_E 260
#define YS_TILE_G 262
#define YS_TILE_M 266
#define YS_TILE_DIGIT_FIRST 449 /* 0-9, ":" (459) and "/" (460) */
#define YS_TILE_DIGIT_LAST 460
#define YS_TILE_SLASH 460
#define YS_TILE_BAR_SIDE 282
#define YS_TILE_BAR_YELLOW 506 /* 8 HP points left */
#define YS_TILE_BAR_RED 507    /* 8 HP points lost */
#define YS_BAR_FIRST_COL 8     /* 1st inner cell of the bar (the left side is at 7) */

typedef struct YsPanel {
    uint8_t valid; /* the panel is on whole map cells ($2212 + 1 and $220E multiples of 8) */
    uint16_t cell[YS_PANEL_ROWS][YS_PANEL_COLS]; /* background map words */
    uint8_t pix[YS_PANEL_H][YS_PANEL_W];         /* color number (0-15) of each pixel by tile; 0 = unlit */
    uint32_t next_level;                         /* EXP of the next level: $2C4A-$2C4C (on the save screen, the chosen save's) */
    uint8_t hp_max;                              /* max H.P: $2C48 (likewise; 1 byte: the panel goes up to 255) */
    uint32_t bar_yellow, bar_red;                /* the bar's inner colors, as they come out in the image (tiles 506, 507) */
} YsPanel;

struct Pce;
/* At the end of the frame: the panel cells the game left in the background map */
void ys_panel_read(const struct Pce *pce, YsPanel *p);
/* Image pixel (x, y) lit in the panel (p may be NULL or invalid: nothing lit) */
int ys_panel_lit(const YsPanel *p, int x, int y);

#endif
