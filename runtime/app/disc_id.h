/*
 * Identification of the Ys Book I & II (USA) disc in the folder the player chose.
 *
 * The recompiled game only works with this disc: the code read from the CD is checked against what was
 * recompiled, and the timing of each track is part of the game's pacing. That is why the image is
 * checked before running:
 *  - table of contents (TOC): number of tracks, type, start and size of each, as the simulated
 *    drive sees them (runtime/cd/cdimage.h) — catches a .bin that is missing, truncated or from another copy;
 *  - CRC32 of the 2048 user bytes of each sector of the data track (~17 MB, read in ~0.1 s) — catches
 *    another version of the game or damaged data. The music tracks are not read in full (that would be
 *    ~600 MB every time the game opens); only their size is checked.
 * The title stored in the 2nd sector of the data track ("Ancient Ys Vanished") tells "another game" apart from
 * "Ys, but another version or a damaged copy", so the message to the player is more useful.
 */
#ifndef PCE_APP_DISC_ID_H
#define PCE_APP_DISC_ID_H

#include <stddef.h>
#include <stdint.h>

typedef enum DiscCheck {
    DISC_OK = 0,
    DISC_NO_CUE,        /* folder without any .cue */
    DISC_OPEN_FAILED,   /* invalid .cue or a .bin that is missing/won't open (the detail says which) */
    DISC_NOT_YS,        /* another game (or a disc with no data track) */
    DISC_LAYOUT_DIFF,   /* it is Ys, but the tracks have a different count/size */
    DISC_DATA_DIFF      /* it is Ys with the right tracks, but the data does not match */
} DiscCheck;

typedef struct DiscFingerprint {
    int track_count;
    uint32_t toc_crc;   /* CRC32 of (type, start, sectors) of each track */
    uint32_t data_crc;  /* CRC32 of the user data of the 1st data track */
    int is_ys;          /* title "Ancient Ys Vanished" in the 2nd sector of the data track */
} DiscFingerprint;

/* Opens the .cue and measures the image. 0 = ok; -1 = did not open (msg says why). */
int disc_fingerprint(const char *cue_path, DiscFingerprint *fp, char *msg, size_t msg_size);

/* Checks a .cue. On error, msg receives an explanation for the player (in English). */
DiscCheck disc_check_cue(const char *cue_path, char *msg, size_t msg_size);

/* Looks in the folder for the Ys Book I & II (USA) .cue. With several .cue files, keeps the first one that
 * passes; if none passes, returns the error of the one closest to Ys. cue_out receives the path. */
DiscCheck disc_find_in_folder(const char *folder, char *cue_out, size_t cue_size, char *msg, size_t msg_size);

#endif
