#include "app/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/u8file.h"

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

int config_load(Config *cfg, const char *path)
{
    cfg->count = 0;
    FILE *f = u8_fopen(path, "rb");
    if (!f)
        return -1;
    char line[CONFIG_KEY_LEN + CONFIG_VALUE_LEN + 16];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
            p += 3; /* UTF-8 byte order mark that Notepad may put at the start */
        first = 0;
        p = trim(p);
        if (!*p || *p == ';' || *p == '#')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        config_set(cfg, trim(p), trim(eq + 1));
    }
    fclose(f);
    return 0;
}

int config_save(const Config *cfg, const char *path)
{
    FILE *f = u8_fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "; YsRecomp settings\n");
    for (int i = 0; i < cfg->count; i++)
        fprintf(f, "%s=%s\n", cfg->entries[i].key, cfg->entries[i].value);
    return fclose(f) == 0 ? 0 : -1;
}

const char *config_get(const Config *cfg, const char *key, const char *def)
{
    for (int i = 0; i < cfg->count; i++)
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    return def;
}

int config_set(Config *cfg, const char *key, const char *value)
{
    if (!*key || strlen(key) >= CONFIG_KEY_LEN || strlen(value) >= CONFIG_VALUE_LEN ||
        strchr(key, '=') || strpbrk(value, "\r\n"))
        return -1;
    ConfigEntry *e = NULL;
    for (int i = 0; i < cfg->count && !e; i++)
        if (strcmp(cfg->entries[i].key, key) == 0)
            e = &cfg->entries[i];
    if (!e) {
        if (cfg->count >= CONFIG_MAX_ENTRIES)
            return -1;
        e = &cfg->entries[cfg->count++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->value, sizeof(e->value), "%s", value);
    return 0;
}

int config_get_int(const Config *cfg, const char *key, int def, int min, int max)
{
    const char *v = config_get(cfg, key, NULL);
    if (!v || !*v)
        return def;
    char *end;
    long n = strtol(v, &end, 10);
    while (*end == ' ' || *end == '%')
        end++;
    if (*end)
        return def;
    return n < min ? min : n > max ? max : (int)n;
}

int config_set_int(Config *cfg, const char *key, int value)
{
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    return config_set(cfg, key, text);
}
