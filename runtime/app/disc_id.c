#include "app/disc_id.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "cd/cdimage.h"

/* Measurements of Ys Book I & II (USA), Redump format (.cue + 43 .bin), taken from the user's disc
 * with `app_tests --fingerprint <cue>`. They are numbers about the disc, not its content. */
#define YS_TRACK_COUNT 43
#define YS_TOC_CRC 0x34B2F40Eu
#define YS_DATA_CRC 0xEC4E1070u

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1])
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        crc_table[i] = c;
    }
}

/* CRC32 in parts: start with crc = 0. */
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static const char *file_name(const char *path)
{
    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\')
            base = c + 1;
    return base;
}

static int open_image(const char *cue_path, CdImage **out, char *msg, size_t msg_size)
{
    CdImage *img = (CdImage *)malloc(sizeof(CdImage));
    if (!img) {
        snprintf(msg, msg_size, "Out of memory.");
        return -1;
    }
    if (cd_open(img, cue_path) != 0) {
        if (img->bad_file[0])
            snprintf(msg, msg_size, "This file is missing or could not be read:\n%s", file_name(img->bad_file));
        else
            snprintf(msg, msg_size, "The file %s is not a valid .cue sheet\n(%s).", file_name(cue_path), img->error);
        free(img);
        return -1;
    }
    *out = img;
    return 0;
}

int disc_fingerprint(const char *cue_path, DiscFingerprint *fp, char *msg, size_t msg_size)
{
    crc_init();
    memset(fp, 0, sizeof(*fp));
    CdImage *img;
    if (open_image(cue_path, &img, msg, msg_size) != 0)
        return -1;

    fp->track_count = img->track_count;
    int data = -1;
    for (int i = 0; i < img->track_count; i++) {
        const CdTrack *t = &img->tracks[i];
        uint8_t rec[9];
        rec[0] = (uint8_t)t->type;
        put32(rec + 1, t->start_lba);
        put32(rec + 5, t->sector_count);
        fp->toc_crc = crc_update(fp->toc_crc, rec, sizeof(rec));
        if (data < 0 && t->type != CD_TRACK_AUDIO)
            data = i;
    }
    if (data >= 0) {
        const CdTrack *t = &img->tracks[data];
        static uint8_t sector[CD_SECTOR_DATA];
        for (uint32_t lba = t->start_lba; lba <= t->end_lba; lba++) {
            if (cd_read_sector(img, lba, sector) != 0)
                memset(sector, 0, sizeof(sector)); /* sector outside the file: the CRC will not match */
            fp->data_crc = crc_update(fp->data_crc, sector, sizeof(sector));
            if (lba == t->start_lba + 1) {
                static const char title[] = "Ancient Ys Vanished";
                for (size_t k = 0; k + sizeof(title) - 1 <= sizeof(sector) && !fp->is_ys; k++)
                    fp->is_ys = memcmp(sector + k, title, sizeof(title) - 1) == 0;
            }
        }
    }
    cd_close(img);
    free(img);
    return 0;
}

DiscCheck disc_check_cue(const char *cue_path, char *msg, size_t msg_size)
{
    DiscFingerprint fp;
    if (disc_fingerprint(cue_path, &fp, msg, msg_size) != 0)
        return DISC_OPEN_FAILED;
    if (!fp.is_ys) {
        snprintf(msg, msg_size, "%s is not Ys Book I & II.", file_name(cue_path));
        return DISC_NOT_YS;
    }
    if (fp.track_count != YS_TRACK_COUNT) {
        snprintf(msg, msg_size,
                 "%s is Ys Book I & II, but it has %d tracks; the supported copy has %d\n"
                 "(Redump format: one .cue and 43 .bin files).",
                 file_name(cue_path), fp.track_count, YS_TRACK_COUNT);
        return DISC_LAYOUT_DIFF;
    }
    if (fp.toc_crc != YS_TOC_CRC) {
        snprintf(msg, msg_size,
                 "%s is Ys Book I & II, but some tracks have a different size from the supported copy.\n"
                 "A .bin file may be incomplete, or this copy was made in another way\n"
                 "(supported: Redump format, one .cue and 43 .bin files).",
                 file_name(cue_path));
        return DISC_LAYOUT_DIFF;
    }
    if (fp.data_crc != YS_DATA_CRC) {
        snprintf(msg, msg_size,
                 "%s is Ys Book I & II, but its data is different from the supported version\n"
                 "(Ys Book I & II, USA). It may be another version or a damaged copy.",
                 file_name(cue_path));
        return DISC_DATA_DIFF;
    }
    msg[0] = 0;
    return DISC_OK;
}

/* How much the error says "it is Ys, almost there": decides which message to show when there are several .cue files. */
static int closeness(DiscCheck r)
{
    switch (r) {
    case DISC_DATA_DIFF: return 4;
    case DISC_LAYOUT_DIFF: return 3;
    case DISC_OPEN_FAILED: return 2;
    case DISC_NOT_YS: return 1;
    default: return 0;
    }
}

DiscCheck disc_find_in_folder(const char *folder, char *cue_out, size_t cue_size, char *msg, size_t msg_size)
{
    int count = 0;
    char **names = SDL_GlobDirectory(folder, "*.cue", SDL_GLOB_CASEINSENSITIVE, &count);
    DiscCheck best = DISC_NO_CUE;
    snprintf(msg, msg_size, "There is no .cue file in this folder:\n%s", folder);
    cue_out[0] = 0;
    for (int i = 0; names && i < count; i++) {
        char path[1024], m[1024];
        size_t n = strlen(folder);
        int sep = n > 0 && (folder[n - 1] == '/' || folder[n - 1] == '\\');
        snprintf(path, sizeof(path), "%s%s%s", folder, sep ? "" : "/", names[i]);
        DiscCheck r = disc_check_cue(path, m, sizeof(m));
        if (r == DISC_OK || closeness(r) > closeness(best)) {
            best = r;
            snprintf(msg, msg_size, "%s", m);
            snprintf(cue_out, cue_size, "%s", path);
        }
        if (r == DISC_OK)
            break;
    }
    SDL_free(names);
    return best;
}
