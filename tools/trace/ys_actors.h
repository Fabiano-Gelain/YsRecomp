/*
 * Ys characters: reads the sprite list the game engine builds and tells which character each sprite belongs
 * to, with the camera and the late frames. Addresses and routines: docs/ys_notes.md, "Engine memory map".
 *
 * Works by instruction hooks (Pce.on_instruction): when the game code passes through the known addresses of
 * the CD RAM (bank $80 in the $4000-$5FFF window, checked by the code bytes), it notes what is being
 * drawn. The list that appears in a frame is the one the VDC copied (SATB, at the start of VBlank, from the
 * R13 address): the newest one finished before the copy. Tools only (pce_trace --actors); the console does
 * not change.
 */
#ifndef YS_ACTORS_H
#define YS_ACTORS_H

#include <stdint.h>
#include <stdio.h>

#include "pce/pce.h"

#define YS_SLOTS 64  /* VDC sprites = characters of the game table (index X < $40) */
#define YS_NO_OWNER 0xFF

/* A sprite list built by the game (routine $80:4D58) */
typedef struct YsBuild {
    long seq;                   /* list number (1, 2, ...) */
    long frame;                 /* frame in which the build finished */
    int line;                   /* VCE line (0-262) where it finished (the frame ends at 256) */
    int work;                   /* lines since the return from the frame wait (the whole logic; -1 = no wait) */
    uint16_t base;              /* table address in VRAM (R13 after ex_sprdma) */
    uint16_t cam_x, cam_y;      /* list camera ($2C33/$2C35 = $2C37/$2C39 copied at the end) */
    int first, count;           /* 1st engine sprite ($2E4A; the earlier ones are from another routine) and total */
    int actors;                 /* characters drawn, in linked-list order */
    uint8_t order[YS_SLOTS];
    int16_t ax[YS_SLOTS], ay[YS_SLOTS]; /* screen position of each character (index = character) */
    uint8_t pose[YS_SLOTS], flags[YS_SLOTS];
    uint8_t owner[YS_SLOTS];    /* character of each sprite */
    int8_t dx[YS_SLOTS], dy[YS_SLOTS]; /* offset of the piece (the drawing) of each sprite */
    uint16_t sat[YS_SLOTS * 4]; /* the built table */
} YsBuild;

typedef struct YsActors {
    FILE *out;                  /* list per frame (NULL = summary only) */
    long frame;                 /* frame in progress (1 = the first) */
    int building, cur_actor;
    YsBuild cur;
    YsBuild done[4];            /* last finished lists (ring) */
    long done_count;
    YsBuild shown;              /* the list in the VDC sprite table (last copy) */
    int shown_valid;
    long prev_seq;              /* list shown in the previous frame */
    int dma_seen;
    int frame_builds, prev_builds;
    long frame_list[4];           /* lists finished in this frame */
    int frame_waits, frame_delta; /* waits for the next frame ($80:4B13) that finished in this frame */
    int vbl_line, vbl_copy;       /* game VBlank ($80:4AA3) in this frame: line and whether it copied the camera */
    /* summary */
    long frames, frames_engine, frames_differ;
    long sprites_owned, sprites_free, sprites_bad, first_bad;
    long repeated, skipped;       /* frames with the previous list; lists never shown */
    long repeated_list[64];
    long waits, wait_hist[4];     /* wait of 1, 2, 3, 4+ frames */
    long builds_hist[4];
    uint64_t wait_master;         /* clock at the return of the last wait */
    long work_hist[4];            /* logic up to 132 lines, 210, 263, more */
    int work_max;
    long work_max_frame;
} YsActors;

void ys_actors_init(YsActors *ya, FILE *out);
/* Instruction hook (Pce.on_instruction) */
void ys_actors_hook(Pce *pce, int after);
/* End of a frame: drawn_sat = the sprite table used to draw the frame (VdcLineLog.sat) */
void ys_actors_frame(YsActors *ya, const uint16_t *drawn_sat);
void ys_actors_summary(const YsActors *ya);

extern YsActors *g_ys_actors; /* what the hook uses */

#endif
