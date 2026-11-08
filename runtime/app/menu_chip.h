/* Chip music while the options menu is open (continues from where it stopped). The console stays paused; in a copy
 * of it (made the 1st time the menu asks for sound), only the BIOS sound driver step ($E6E8, the same one as the
 * timer or VBlank interrupt, depending on $E7) runs, at the pace of the copy's timer, and its PSG goes to the sound
 * card. When the menu closes (menu_chip_finish), the console receives the copy's sound state: the RAM bytes the
 * driver changed (outside the stack) and the PSG registers and channels. So the music continues from where it was in
 * the menu.
 *
 * This changes the console: the recorded session notes the menu ("# menu Q CHUNKS SAMPLES", input_script.h) and
 * whoever replays it calls menu_chip_replay at the same point (after Q frames). The result depends only on the console
 * state and on how many sound chunks of how many samples the menu played. */
#ifndef PCE_APP_MENU_CHIP_H
#define PCE_APP_MENU_CHIP_H

#include <stdint.h>
#include "bios/hle.h"
#include "pce/pce.h"

typedef struct MenuChip {
    Pce clone;
    Hle hle;
    double frac;  /* main clock cycles left over from the last chunk */
    int on;
    long pieces;  /* sound chunks played since the copy */
    int samples;  /* samples per chunk (always the same number) */
} MenuChip;

/* Advances the copy (made from src if there is none yet) by the time of n samples and puts its chip sound in buf
 * (n x 2 values, with the gains and the chip filter from src->mix). */
void menu_chip_play(MenuChip *m, const Pce *src, int16_t *buf, int n);

/* End of the menu: passes the copy's sound state to dst (the console it came from) and throws the copy away.
 * Returns how many chunks were played (0 = nothing changed). */
long menu_chip_finish(MenuChip *m, Pce *dst);

/* Replays a recorded menu: the same as pieces calls of menu_chip_play with n samples and menu_chip_finish. */
void menu_chip_replay(Pce *pce, long pieces, int n);

#endif
