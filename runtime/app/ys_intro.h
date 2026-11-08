/*
 * Which intro screen the game is on and whether it accepts RUN, read from the intro program (docs/ys_notes.md,
 * "Intro"). Read only: the console does not change.
 *
 * The intro is disc program $04 (app/ys_screen.h), a script in its own code that the loop at $5A98 reads (pointer
 * at $62/$63). Each screen starts with the script's fade operation (code $18): the hook at $5AA4 recognizes the three by
 * their address and bytes. The main menu is routine $494C, the only one the script registers to wait for Beginning/
 * Continue.
 *
 * Accepting buttons is routine $48A1, every frame: with $3A = 0, it reads the buttons at $2228, ANDs them with the mask $2EBF and, if
 * anything is left, sets bit 0 of $24 (the "hurry": the game's scheduler, $4288, stops counting the waits). The mask is 0 until
 * the script registers routine $48C8, shortly before the copyright notice (which is why RUN does nothing on NEC and the logo);
 * $3A counts the frames in which the game does not accept ($FF = until further notice, $1E = 30 frames).
 */
#ifndef PCE_APP_YS_INTRO_H
#define PCE_APP_YS_INTRO_H

#include "app/intro_skip.h"
#include "pce/pce.h"

typedef struct YsIntro {
    IntroScreen screen; /* the last screen the script started (only moves forward) */
} YsIntro;

void ys_intro_init(YsIntro *s);
/* Before each instruction (on_instruction, after = 0); program = the disc program running (YsScreen.program) */
void ys_intro_hook(YsIntro *s, Pce *pce, int program);
/* Would the game take RUN if it were pressed now? (also 0 if it already took it and has not handled it yet) */
int ys_intro_run_accepted(const Pce *pce, int program);

#endif
