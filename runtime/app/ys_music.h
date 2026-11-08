/*
 * New music started by the game (docs/ys_notes.md, "CD music"). The resident routine that plays a song ($80:4244, the
 * same in Ys I and Ys II) calls cd_fade(0) and then cd_play, but the CD command only arrives ~40 ms later: with a fade
 * active (at GAME OVER, already in silence), the old track comes back at full volume in that gap. The hook, checked by
 * the code bytes, marks the CD output as muted until the command (CdAudio.pc_mute). The console does not change.
 */
#ifndef PCE_APP_YS_MUSIC_H
#define PCE_APP_YS_MUSIC_H
#include "pce/pce.h"

void ys_music_hook(Pce *pce);

#endif
