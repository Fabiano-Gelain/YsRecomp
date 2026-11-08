/*
 * PC version sound volumes, stored in the settings (YsRecomp.ini), in % from 0 to 100:
 *   volume_master   overall volume
 *   volume_music    CD music
 *   volume_chip_music  music played by the sound chip (PSG)
 *   volume_effects  sound effects from the sound chip
 *   volume_voices   recorded voices and effects (ADPCM)
 *   chip_filter     1 = filtered chip sound (without the treble hiss), 0 = raw as in Geargrafx
 * At 100% the sound has Mednafen's balance (pce/mix.h).
 */
#ifndef PCE_APP_VOLUME_H
#define PCE_APP_VOLUME_H

#include "app/config.h"
#include "pce/mix.h"

/* Reads the volumes (default 100; outside 0-100 is clamped) and adds the missing ones to the settings, so the
 * player sees them in the file. Returns 1 if it added any key (save the settings). */
int volume_load(Config *cfg, AudioVolumes *vol);

#endif
