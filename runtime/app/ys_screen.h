/*
 * Framed screen read from the game (docs/ys_notes.md, "Border and panel on screen"). Ys runs one disc program at
 * a time: the resident loader ($80:40E7, PHX; JSR $4165) receives in A the number of the next one, puts the file at $4800 and jumps
 * there (the program returns the number of the following one; loop at $40E2). The hook at $40E7 (checked against the
 * code bytes, read only: the console does not change) stores that number. Each program has a single screen layout:
 *   $08 (Ys I gameplay) and $30 (Ys II gameplay): the map with the frame and the panel (the game splits the screen with a
 *        line interrupt, app/ys_scroll.h; when changing area it hands the split to the BIOS for ~85 frames, $4B2F/$4B3A, with the
 *        frame still on screen); the portrait scenes, GAME OVER and the full screens of Ys II are its windows
 *        (app/ys_windows.h);
 *   $01 (load game menu): the frame down to line 234, no panel;
 *   the others ($04 intro, $20/$24 end of Ys I, $28 beginning of Ys I, $2C, $52, $55...): no frame.
 * Measured on all recorded sessions (30, 2026-11-03): wherever the image had the frame, the program was always the right one.
 * The 1st program (the boot one, called directly at $40DC JSR $4800, also after a reset; hook there) has no frame.
 */
#ifndef PCE_APP_YS_SCREEN_H
#define PCE_APP_YS_SCREEN_H
#include "pce/pce.h"

typedef struct YsScreen {
    int program; /* the last program loaded ($40E7); -1 = none yet */
    long loads;  /* how many program loads seen */
} YsScreen;

void ys_screen_init(YsScreen *s);
/* Before each instruction (on_instruction, after = 0) */
void ys_screen_hook(YsScreen *s, Pce *pce);
/* The program's screen (CROP_SCREEN_*, app/crop.h) */
int ys_screen_layout(int program);
/* Still in the intro (from boot until leaving the main menu)? It is program $04 (app/ys_intro.h) or the boot before it; with
 * no program seen by the time `frame` passes INTRO_MAX_FRAMES (another disc), it gives up and says no. */
int ys_screen_in_opening(int program, long frame);

#endif
