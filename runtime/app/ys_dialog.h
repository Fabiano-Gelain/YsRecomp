/*
 * Instant dialog (docs/ys_notes.md, "Text program"). The text of the speech windows is written by a
 * text program of the game (a task, index X): every frame it adds the speed ($27F6,X) to an accumulator
 * ($2826,X) and writes one letter each time the accumulator goes past the limit ($92); with speed 0 it writes everything
 * at once, up to the next wait code (button, pause). Ordinary lines already come with speed 0; the slow ones
 * (e.g. the beginning of Ys I) with $0C. The text script changes the speed (code $12 n; $1B/$1D = 4, $1E = 0).
 *
 * With the option on, at the speed test (LDA $27F6,X; BEQ: $80:58D6 in Ys II, $80:5BD0 in Ys I, $80:5BD5 in
 * another program; checked against the code bytes) the task's speed becomes 0: the text comes out whole as in the
 * ordinary lines, through the game's own path. The script's pauses and waits stay the same.
 *
 * This changes the console (the 1st option that changes the game, not just the image): it lives in Pce.game_patch (every
 * copy of the console, the lockstep reference too); the session records the option ("# dialog <frame> <0|1>",
 * input_script.h) and whoever replays it turns it on and off at the same point. With it on, the check against Geargrafx is not valid.
 */
#ifndef PCE_APP_YS_DIALOG_H
#define PCE_APP_YS_DIALOG_H
#include "pce/pce.h"

/* 1 if the game's text program is at the speed test (the next instruction is the LDA $27F6,X) */
int ys_dialog_at(const Pce *pce);
/* 1 if the text program is about to wait for the next frame for the next letter (the JSR $402A of slow text) */
int ys_dialog_wait_at(const Pce *pce);

/* The option's Pce.game_patch: at the speed test, speed 0 */
void ys_dialog_patch(Pce *pce);

/* Turns the option on (on = 1) or off in the console */
void ys_dialog_set(Pce *pce, int on);

#endif
