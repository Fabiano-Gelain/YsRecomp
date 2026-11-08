/*
 * PC version settings: a "key=value" text file (YsRecomp.ini, next to the
 * executable — portable version). Lines starting with ';' or '#' are comments. Unknown
 * keys are kept when saving, so newer versions do not erase what older ones do not
 * understand. Paths and text in UTF-8.
 */
#ifndef PCE_APP_CONFIG_H
#define PCE_APP_CONFIG_H

#define CONFIG_MAX_ENTRIES 64
#define CONFIG_KEY_LEN 64
#define CONFIG_VALUE_LEN 1024

typedef struct ConfigEntry {
    char key[CONFIG_KEY_LEN];
    char value[CONFIG_VALUE_LEN];
} ConfigEntry;

typedef struct Config {
    int count;
    ConfigEntry entries[CONFIG_MAX_ENTRIES];
} Config;

/* Reads the file. 0 = read; -1 = does not exist or could not be opened (cfg stays empty). */
int config_load(Config *cfg, const char *path);
/* Writes all keys. 0 = ok. */
int config_save(const Config *cfg, const char *path);

/* Value of the key, or def if it does not exist. */
const char *config_get(const Config *cfg, const char *key, const char *def);
/* Creates or replaces. -1 if the key/value does not fit or there is no more room. */
int config_set(Config *cfg, const char *key, const char *value);

/* Integer value of the key, clamped to [min, max]; def if the key does not exist or is not a number. */
int config_get_int(const Config *cfg, const char *key, int def, int min, int max);
/* Writes an integer. */
int config_set_int(Config *cfg, const char *key, int value);

#endif
