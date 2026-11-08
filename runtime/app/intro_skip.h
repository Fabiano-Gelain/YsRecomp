/*
 * Skipping the Ys intro, one screen at a time, with fast-forward (the screen comes from the intro program,
 * app/ys_intro.h). Does not touch the game: the console runs the same frames it would run with the player watching
 * without pressing anything, only as fast as possible, with no sound and no image shown, until the next screen.
 *
 * Only the first screens need this: from the copyright screen on, the game itself skips with RUN (goes to the goddess
 * and from her to the main menu). The beginning, in the BIOS-less version, with the frame where the script starts each
 * screen (the fade that lights up the image; the 1st lit pixel comes 9 to 15 frames later):
 *   loading (black screen) -> "from NEC" (244) -> "Ys Book I & II" logo (473) -> copyright (1618): here the
 *   intro ends and RUN belongs to the game again.
 * During the beginning, Enter/I do not reach the game (it ignores them until shortly before the copyright). Skipping
 * screen by screen is always on in the player version.
 *
 * "Skip intro" option (straight to the main menu, off by default): fast-forward from the 1st frame. From the copyright
 * on, RUN is pressed every time the game would accept it (ys_intro_run_accepted) and released when it takes it:
 * at the copyright and, again, when the goddess is about to appear. Stops when the game starts the Beginning/Continue
 * menu routine, with the screen already complete.
 * It is the same as the player pressing RUN on those frames (the injected buttons go to --record too).
 */
#ifndef PCE_APP_INTRO_SKIP_H
#define PCE_APP_INTRO_SKIP_H

#include <stdint.h>

typedef enum IntroScreen {
    INTRO_SCREEN_LOADING,
    INTRO_SCREEN_NEC,
    INTRO_SCREEN_LOGO,
    INTRO_SCREEN_COPYRIGHT,
    INTRO_SCREEN_MENU,
} IntroScreen;

/* If the game does not get where it should by here (the screen it reports does not change: not this disc), give up and
 * show whatever is on screen. The intro reaches the menu well before (~2400). */
#define INTRO_MAX_FRAMES 4000

typedef struct IntroSkip {
    int enabled;
    int active;         /* still in the intro */
    int to_menu;        /* "Skip intro" option: straight to the main menu */
    int fast;           /* fast-forward in progress */
    IntroScreen screen; /* the screen on the last frame */
    long frames;        /* frames since the start */
} IntroSkip;

void intro_init(IntroSkip *s, int enabled, int to_menu);
/* Still in the intro (and the option on): Enter/I are for skipping, not for the game. */
int intro_active(const IntroSkip *s);
/* Buttons the intro presses by itself this frame (RUN, in straight-to-menu mode); run_accepted = the game would take it now */
uint8_t intro_auto_pad(const IntroSkip *s, int run_accepted);
/* Skip request. Returns 1 if fast-forward started. */
int intro_skip(IntroSkip *s);
/* Every game frame, with the screen the game is on (app/ys_intro.h). Returns 1 if this frame is the 1st of the next
 * screen and fast-forward stopped on it. */
int intro_frame(IntroSkip *s, IntroScreen screen);

#endif
