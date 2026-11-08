/*
 * Fixes for the game's own defects (formerly in app/smooth.h): only the shown image changes, not the console.
 * They apply with or without smooth motion. They use the frame redraw (app/redraw.h).
 */
#ifndef PCE_APP_GAME_FIXES_H
#define PCE_APP_GAME_FIXES_H

#include "app/redraw.h"

/* Torn frame: background pixels, on the lines drawn before the last VRAM write made during the
 * image, that change if the line is drawn with the VRAM of the end of the image (the game touched what had already gone out;
 * the writes after the last line belong to the next frame and do not count). first_row: 1st such line (-1).
 * E.g. in conversations, swapping the text window lets out a frame with the window half done (also with the real BIOS
 * and in Geargrafx).
 * Inside the game's windows (app/ys_windows.h: opening, open, closing; most of the torn
 * frames: window swapped, text written), fix_torn_window shows the window as it is at the end of the image (the user's
 * decision: the whole new window). Outside them, nothing is done: it is the game redrawing the map at each step, or
 * swapping the already dark screen; hiding the frame (removed) only made the motion stutter.
 * On the VDC log, before redraw_capture (0 if there is no log or it overflowed): returns the torn pixels
 * outside the cells of the windows of wins (NULL = all) and, in *in (or NULL), the ones inside. */
int fix_torn_log(const VdcLineLog *log, const YsWinFrame *wins, int *in, int *first_row);
/* The same on a captured frame, with its windows (b->wins) */
int fix_torn(const RedrawFrame *b, int *in);
/* Map cells (1 per entry, map of size reg) of the windows of wf: the frame's, the ones at the end of the previous one and the ones that
 * closed in it (2 = of one that closes in this frame, 1 = of the others); returns how many */
int torn_window_cells(const YsWinFrame *wf, int reg, uint8_t *cells);
/* With b->torn_win, the image's writes to the window cells (and to the drawings of the tiles only they show)
 * move to its beginning: b (VRAM and writes) and b->pixels show the windows as at the end of the image; in the cells of
 * a window that closes, they leave this frame (the whole window as it was; it disappears in the next one). The redraw stays equal to the
 * image. Returns how many pixels changed. */
int fix_torn_window(Redraw *s, RedrawFrame *b);

/* Scroll changes by the game (app/ys_scroll.h): the lines of the map and of the lower frame as the game wanted them
 * (b->scroll: the values the line interrupt put in the registers). If the change arrived late (the map starts
 * 1-2 lines lower, the ring wrap in the middle of the map is delayed and shows frame lines, the lower frame moves down, only
 * the CR is late, BYR half done), the wrong lines receive the right scroll and CR and are redrawn (only the shown
 * image; the console does not change). Lines with the background turned off by the game (each line's CR) stay as they are; with the
 * background off at the end of the frame (blank screen), nothing changes. Until 2026-11-05 the fix gave up if two check
 * lines did not match (with margins of 10 and 8 lines): in 4 long sessions (~254 thousand frames) that only happened with the
 * background turned off by the game. Returns 1 if it changed, 0 if it was on time, -1 with the blank screen; map_rows / low_rows:
 * lines changed in the map and below. */
int fix_scroll(Redraw *s, RedrawFrame *b, int *map_rows, int *low_rows);

/* Item window in front: Ys draws the "Adol gets ..." window on the background map, and the faces of the
 * conversations are sprites "in front of the background"; in the US version the text got longer and passes under the
 * face (also on the real console: it is the game). With s->window_front, the cells of the
 * game's windows (b->wins, redraw_windows) stay in front of the sprites, except the windows' own ones, in the
 * smooth motion drawings and, through this function, in b->pixels (the shown image; the console does not change). Returns
 * how many pixels changed (0 = nothing done). */
int fix_window(Redraw *s, RedrawFrame *b);

/* Equipment on Adol (app/equip.h): with s->equip, Adol's sprite comes out with the equipment in the
 * smooth motion drawings and, through this function, in b->pixels (the shown image, also without smooth motion; the console does not
 * change). Only the pixels that differ from the redraw without the equipment change. Returns how many (0 = nothing done). */
int fix_equip(Redraw *s, RedrawFrame *b);

#endif
