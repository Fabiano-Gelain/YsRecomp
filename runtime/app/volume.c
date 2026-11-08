#include "app/volume.h"

static int load_one(Config *cfg, const char *key, int *out)
{
    *out = config_get_int(cfg, key, 100, 0, 100);
    if (config_get(cfg, key, NULL))
        return 0;
    config_set_int(cfg, key, *out);
    return 1;
}

int volume_load(Config *cfg, AudioVolumes *vol)
{
    int added = load_one(cfg, "volume_master", &vol->master);
    added |= load_one(cfg, "volume_music", &vol->music);
    added |= load_one(cfg, "volume_chip_music", &vol->chip_music);
    added |= load_one(cfg, "volume_effects", &vol->effects);
    added |= load_one(cfg, "volume_voices", &vol->voices);
    /* chip_filter=1 (default): filtered chip sound, without the treble hiss */
    vol->chip_filter = config_get_int(cfg, "chip_filter", 1, 0, 1);
    if (!config_get(cfg, "chip_filter", NULL)) {
        config_set_int(cfg, "chip_filter", vol->chip_filter);
        added = 1;
    }
    return added;
}
