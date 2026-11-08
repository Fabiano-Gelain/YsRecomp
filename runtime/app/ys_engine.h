/*
 * Ys engine read by the PC: at each sprite list the game builds, the camera, the characters (position
 * on screen, bits), the owner of each sprite and the "walkers" (8-pixel steps). Addresses and routines:
 * docs/ys_notes.md, "Engine memory map" (engine memory map; the same hooks as tools/trace/ys_actors.c).
 *
 * Hooks on instructions (Pce.on_instruction, which also runs in the recompiled code): when the game goes through the
 * known addresses of bank $80 (checked against the code bytes), it notes what it is building. Read only: the
 * console does not change.
 *
 * Late frame: the logic overran the frame, the VDC copied the previous list again and the late list never
 * appears (the next one already comes out on time: the cave's "2-0-4"). At the end of that frame it is already ready:
 * ys_engine_frame returns it in place of the repeated one (YS_LIST_LATE) and motion_fix_late (app/motion.h) redraws the
 * frame with it.
 */
#ifndef PCE_APP_YS_ENGINE_H
#define PCE_APP_YS_ENGINE_H

#include <stdint.h>

#include "pce/pce.h"

#define YS_ACTORS 64   /* characters (index X < $40) = VDC sprites */
#define YS_WALKERS 48  /* walkers (tables of $30 bytes) */
#define YS_NO_ACTOR 0xFF

/* Front cells of the map (docs/ys_notes.md): map tiles base..base+count-1 (12 bits of the entry; palettes
 * 14 and 15 never) may stay in front of the characters. For each one (only the first 256 count): the bits (0-2 =
 * height, 4 = see game_fixes.h) and the 8x8 drawing of the front pixels (bit 7 = leftmost pixel) */
typedef struct YsFront {
    uint16_t base, count; /* $5F/$60 and $5D/$5E; count = 0: no table */
    uint8_t attr[256];    /* ($52/$53) in bank $2C52 */
    uint8_t bits[256 * 8]; /* ($2C55/$2C56) in bank $2C57 */
} YsFront;

/* A list built by the game (routine $80:4D58), with the state at the end of the build */
typedef struct YsList {
    long seq;                    /* list number (1, 2, ...) */
    uint16_t base;               /* address of the table in VRAM (R13) */
    uint16_t cam_x, cam_y;       /* the list's camera ($2C33/$2C35; X 0-511, Y 0-255 in the map ring) */
    int count;                   /* sprites written ($2216) */
    uint8_t drawn[YS_ACTORS];    /* character went through the build */
    int16_t ax[YS_ACTORS], ay[YS_ACTORS]; /* position on screen ($2F78/$2FB8, $2FF8/$3038) */
    uint8_t flags[YS_ACTORS];    /* $2F38: 3 = hidden, 5 = pinned to the map */
    uint8_t pose[YS_ACTORS];     /* $2EF8 */
    uint8_t state[YS_ACTORS];    /* $2EB6: $8x = active; $50 = rebuilt when opening/closing windows (app/ys_masks.h) */
    uint8_t owner[YS_ACTORS];    /* character of each sprite (YS_NO_ACTOR = no owner) */
    uint16_t sat[YS_ACTORS * 4]; /* the built table */
    /* Walkers: character ($2736), speed ($2A66), accumulator ($2ADF), pixels left in the step
     * ($2B3F); unit ($2092) */
    uint8_t w_actor[YS_WALKERS], w_vel[YS_WALKERS], w_acc[YS_WALKERS], w_left[YS_WALKERS];
    uint8_t unit;
    uint8_t w_mask[YS_WALKERS], w_next[YS_WALKERS]; /* the walker's masks: current position ($2B9F), next step ($2BCF) */
    uint8_t w_hp[YS_WALKERS], w_hpmax[YS_WALKERS];  /* the walker's HP ($2A36) and max HP ($28E6) */
    uint16_t exp, gold;                             /* the player's ($2ABB, $2C41) */
    YsFront front;
    /* Position copied by the game from another character's since the previous list (YS_NO_ACTOR = none): the light of the mine of
     * Ys I ($86:7108, mine area in window $6000: the light character, stored at $7673, receives Adol's position) */
    uint8_t copy_of[YS_ACTORS];
    uint8_t hero;              /* Adol's character (YsEngine.hero); YS_NO_ACTOR = not born yet */
} YsList;

/* Character that only exists because of the windows: state $50 ($2EB6), created by the area program when it opens
 * the 1st window ($81:6148, far call to $7F03) and deleted when it closes the last one ($81:6157, $7F06); on open/close,
 * $80:4F78 has the area program rebuild it from the map ($7F00). In the Ys I mine ($86:7322, $86:737B): its patterns
 * are opaque in the cells that are not the lit floor (the window's), behind the background and in front of the light mask: that is
 * what lets the window appear over the mask. docs/ys_notes.md, "Ys I mine light". */
static inline int ys_list_window_actor(const YsList *l, int a)
{
    return a < YS_ACTORS && l->drawn[a] && l->state[a] == 0x50;
}

enum { YS_LIST_NONE, YS_LIST_SHOWN, YS_LIST_LATE };

/* The list of one console frame (copied at the end of the frame) */
typedef struct YsFrameList {
    int kind;      /* YS_LIST_*: none (outside the engine), the one the VDC showed, or the late one in place of the repeated one */
    YsList list;
    /* The list the VDC showed (shown_ok = 1 if there is one), the one of the frame's sprite table. With kind = YS_LIST_LATE,
     * list is another one; if motion_fix_late cannot swap it, kind becomes YS_LIST_NONE and only the ring around the windows uses
     * this one (ys_frame_shown_list; smooth motion goes on without a list in that frame) */
    int shown_ok;
    YsList shown;
    uint16_t shown_cam_x, shown_cam_y; /* camera of the list the VDC showed (= the map scroll in the frame) */
} YsFrameList;

/* The list that matches the frame's sprite table; NULL = outside the engine */
static inline const YsList *ys_frame_shown_list(const YsFrameList *e)
{
    return e->kind != YS_LIST_NONE ? &e->list : e->shown_ok ? &e->shown : NULL;
}

typedef struct YsEngine {
    int building;
    int cur_actor;
    YsList cur;
    YsList done[4];  /* last finished lists (ring) */
    long done_count;
    YsList shown;    /* the list the VDC copied last */
    int shown_valid;
    uint32_t sat_copies; /* sprite table copies seen (Vdc.sat_copies) */
    long prev_shown;   /* list the VDC showed in the previous frame */
    long shown_seq;    /* list returned in the previous frame (the VDC's or the late one) */
    long late_frames;  /* late frames with the list swapped */
    uint8_t copy_of[YS_ACTORS]; /* (YsList.copy_of) since the last finished list */
    long copies;       /* copies seen (pce_trace) */
    uint8_t hero;      /* Adol's character, noted when the game creates it (docs/ys_notes.md, "Equipment on Adol") */
} YsEngine;

void ys_engine_init(YsEngine *e);
/* Instruction hook (Pce.on_instruction); uses g_ys_engine */
void ys_engine_hook(Pce *pce, int after);
/* End of a console frame: its list in out (kind = YS_LIST_NONE outside the engine) */
void ys_engine_frame(YsEngine *e, YsFrameList *out);


extern YsEngine *g_ys_engine;

#endif
