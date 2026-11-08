/*
 * Ys fades read from the game (docs/ys_notes.md, "Game fades"). The game never writes the colors directly to the VCE:
 * it builds the shown colors in RAM ($3C00-$3FFF: 256 background and 256 sprite, 2 bytes each) and asks the BIOS to copy them
 * (ex_colorcmd, $E0E4, called at VBlank at $80:4A1B, or the BIOS interrupt routine, when it is the one handling the
 * VBlank; pointers at $2220/$2223, request at $221F, cleared at the end of the copy). Who darkens and brightens:
 *   - the programs' fade routine (one copy in each program, at different addresses; found by its bytes): input
 *     color in X (low byte) and A (high), level (0 = black, 7 = full color) in a zero-page byte; each
 *     channel comes out through the game's table (8 x 8: table[channel * 8 + level]). In the scenes family (intro, end of Ys I)
 *     the routine also adds a brightening (another zero-page byte) and stops at 7. The caller ($7867 and similar) reads
 *     the color from ($10),Y and stores the result at ($12),Y-1: the hook at the routine's entry knows the source color, the level,
 *     the brightening and the destination;
 *   - the flash at the end of Ys I (area, bank $86): adds 1 level to each channel of all shown colors ($3C00-$3FFF),
 *     stopping at 7, every 6 frames: the hook in the loop knows the destination ($10).
 * So each shown color = per channel min(7, table[source][level] + brightening). Nothing is guessed from the image: at
 * each copy to the VCE, each color carries what the game's routine did to it, and it only counts while the calculation gives exactly
 * the color the console shows (the game changed the color some other way: the color stops being a fade color).
 * The hooks only read: the console does not change.
 */
#ifndef PCE_APP_YS_FADE_H
#define PCE_APP_YS_FADE_H
#include <stdint.h>
#include "pce/pce.h"

#define YS_FADE_FULL 7 /* full color level */

/* Where a shown color came from */
typedef struct YsFadeColor {
    uint16_t src;  /* source color (9 bits: GGGRRRBBB) */
    uint8_t level; /* 0-7 */
    uint8_t add;   /* brightening (0-7) */
    uint8_t on;    /* valid (the calculation gives the shown color) */
    uint32_t when; /* frame (YsFade.frames) in which the game's routine made the color */
} YsFadeColor;

/* The 512 VCE colors of one frame */
typedef struct YsFadeFrame {
    uint16_t color[512];     /* the VCE colors at the end of the frame */
    YsFadeColor c[512];
    uint8_t step[512];       /* the color took a fade step in this frame (same source; level or brightening 1 step) */
    uint8_t table[64];       /* the game's table (table[channel * 8 + level]) */
    int table_ok;
    /* The map fade: the level and brightening of the background colors (with non-black source) of the game's last step (the
     * ones the routine made last; palettes left at a level by an old fade, which the game no longer touches, stay
     * out, and the frame and the panel never enter the fade); with none, YS_FADE_FULL and 0. Level 0 (and brightening
     * 0) with the whole screen black by the game without the fade routine (when starting a program): video off (background and
     * sprites in the VDC's CR) during the whole image, or, with no fade colors, the VCE's 256 background colors black (the
     * frame and the panel are background) */
    int map_level, map_add;
    int map_colors;          /* how many colors there are */
    /* Lit background colors that did not come from the fade routine, in the palettes of the last step: with them, level 0 does not
     * mean a black screen (the GAME OVER text is lit this way, after the map darkens) */
    int map_stray;
} YsFadeFrame;

typedef struct YsFade {
    YsFadeColor ram[512];    /* $3C00-$3FFF, as the game's routine left it */
    YsFadeColor vce[512];    /* what was copied to the VCE */
    YsFadeColor prev[512];   /* the previous frame's (for step) */
    uint8_t table[64];
    int table_ok;
    uint32_t frames;         /* console frames */
    long transforms, flashes, uploads; /* calls seen (pce_trace) */
    long unknown_dest;       /* routine calls from a caller that calls some other way (unknown destination) */
    uint8_t display_on, display_seen; /* CR bits 0x80/0x40 seen during this frame's image; there was a hook */
    uint8_t req;             /* the color request ($221F) at the previous instruction */
} YsFade;

void ys_fade_init(YsFade *f);
/* Instruction hook (Pce.on_instruction, before the instruction) */
void ys_fade_hook(YsFade *f, Pce *pce);
/* End of a console frame: its colors in out (checks each one against the VCE color) */
void ys_fade_frame(YsFade *f, const Pce *pce, YsFadeFrame *out);

/* One channel (0-7) of the color at the level and brightening; level may be fractional in 1/q (0..7*q: between two levels, the
 * linear transition between the two table values), the result in 1/q of a level */
int ys_fade_channel(const uint8_t table[64], int ch, int level_q, int add_q, int q);
/* The color (9 bits) at the integer level and brightening */
uint16_t ys_fade_color(const uint8_t table[64], uint16_t src, int level, int add);

#endif
