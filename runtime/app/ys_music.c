/* New music started by the game: see ys_music.h */
#include "app/ys_music.h"

#include "cd/cdrom.h"

#define MUSIC_PC 0x4244    /* the routine (bank $80 in window 2) */
#define MUSIC_FADE_PC 0x4247 /* its JSR to cd_fade */

static const uint8_t k_sig[6] = { 0xDA, 0x48, 0x62, 0x20, 0x2D, 0xE0 }; /* PHX; PHA; CLA; JSR $E02D */

void ys_music_hook(Pce *pce)
{
    if (pce->cpu.pc != MUSIC_FADE_PC || pce->cpu.mpr[2] != 0x80 || !pce->cd)
        return;
    for (int k = 0; k < (int)sizeof(k_sig); k++)
        if (pce_bus_read(pce, 0x80u * 0x2000 + ((MUSIC_PC + k) & 0x1FFF)) != k_sig[k])
            return;
    /* with no fade active the volume does not change: the old track keeps going as it was until the command */
    if (cdrom_fader_enabled(pce->cd, 0) && pce->cd->audio.state == CD_AUDIO_PLAYING) {
        pce->cd->audio.pc_mute = 1;
        pce->cd->audio.pc_mutes++;
    }
}
