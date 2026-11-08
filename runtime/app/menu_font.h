/*
 * Options menu font: Ys's own 8x8 font (the one of the SELECT menu and the HUD numbers), uppercase, bold with
 * serifs. It is not on the disc in direct form (compressed), and cannot go into the code or the package (it comes
 * from the player's disc): it is copied from VRAM when the game loads it, checked by the checksum (CRC32) of the
 * tiles, and saved in a file next to YsRecomp.ini for the next times.
 *
 * Tiles in VRAM (docs/ys_notes.md): 449-458 = 0-9, 459 = ':', 460 = '/', 461-463 = A-C, 464 = solid tile,
 * 465-487 = D-Z; 504 = the game menu's little arrow. Each tile: 16 words (4 planes); lit pixel = color != 0.
 * Symbols the game font lacks (%, -, ., +, <, >, ?, !...) are drawn here in the same style. Before the copy
 * exists (1st time, before the game reaches the screen with the HUD), a hand-drawn fallback font in the same
 * style (A-Z, 0-9, ':', '/').
 */
#ifndef PCE_APP_MENU_FONT_H
#define PCE_APP_MENU_FONT_H

#include <stdint.h>

#define MENU_FONT_FIRST 449 /* tiles 449-487 */
#define MENU_FONT_TILES 39
#define MENU_FONT_ARROW 504
#define MENU_FONT_WORDS ((MENU_FONT_TILES + 1) * 16) /* the 39 tiles and the little arrow */
#define MENU_FONT_CRC 0xAADA7A4Cu /* CRC32 of the MENU_FONT_WORDS words (little-endian), checked in the game */

/* Checks the tiles in VRAM (32768 words); if they are the game font's, copies them and returns 1 */
int menu_font_capture(const uint16_t *vram);
/* The game font has already been copied (or read from the file) */
int menu_font_ready(void);
/* Reads/writes the copy (MENU_FONT_WORDS words). Read only if the checksum matches. 0 = ok. */
int menu_font_load(const char *path);
int menu_font_save(const char *path);
/* Tests: forgets the copy (back to the fallback font) */
void menu_font_forget(void);

/* Drawing of character ch (lowercase becomes uppercase) in 8 rows of 8 pixels (bit 7 = leftmost pixel).
 * 0 = space/no drawing. */
int menu_font_glyph(char ch, uint8_t rows[8]);
/* The menu's little arrow (the game's, or a 4x7 triangle) */
void menu_font_arrow(uint8_t rows[8]);

#endif
