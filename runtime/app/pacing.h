/*
 * Single window clock. Each image asks for the game position (tau, in console frames) at the predicted time it will
 * appear on screen, not at the time it was drawn; the image shows frame B = ceil(tau) at fraction f = tau - (B - 1)
 * between the previous frame and it (the fixed 1-frame delay). The window is also the console's clock: when it starts
 * showing frame B, it releases the console to make B + 1 (main.c). Only math (no SDL), for the tests (app_tests video
 * simulates the displays).
 *
 * With V-Sync the display refreshes on a grid (the time of the next refresh comes from Windows, main.c): the image goes
 * to the first refresh that leaves time to draw it (the longest recent draw time) and is presented on the previous
 * refresh, always in the same phase: it appears on the predicted refresh, always with the same delay, whatever the
 * draw time. The drawing finishes before that present: with Windows composing the screen (recording or NVIDIA overlay,
 * Xbox Game Bar), it grabs the image right after each refresh and shows it on the next one; presented later than that
 * (an earlier version let the drawing run until half a period before the predicted refresh, and presented 1 ms after
 * the previous one), it appeared one refresh later: in the temple, 26-37% of frames off pace, 8.6% this way. With the
 * image going straight to the screen (no composition), presenting early only leaves it waiting in the queue. Display at a multiple of 60 (60/120/180/240 Hz): tau advances 1/n per
 * refresh (the game locked to the display, at 59.95 frames/s at 119.9 Hz; the sound adjusts). Others (75, 100, 144, 165):
 * tau advances period / frame per refresh (the game at 60.05; with smooth motion, each image at the position of the
 * time it appears).
 * Without V-Sync (G-Sync/FreeSync): the image appears when it is presented; images at equal intervals, m per game
 * frame, m = the largest multiple of 60 that fits in the display rate (user decision: 120 images on a 144/165 Hz
 * monitor, 180 on a 240 Hz one; on a 119.9 Hz one, 2 with the game 0.2% slower so as not to exceed the limit); without
 * smooth motion, m = 1.
 */
#ifndef PCE_APP_PACING_H
#define PCE_APP_PACING_H

#include <stdint.h>

#define PACE_FRAME_NS (1365.0 * 262 * 1e9 / 21477270.0) /* one console frame */
#define PACE_VRR_MARGIN 1.002                         /* without V-Sync: minimum interval = display period + 0.2% */
/* The estimated draw time used to be the recent maximum decaying 0.2% per image: a 17 ms spike (measured at 180 Hz)
 * took ~9 s to leave, and during that time every image went one refresh later (72% of frames off pace). Now the 2nd
 * largest of the last 32 (~0.2 s at 180 Hz): a lone spike does not count; the new frame (1 image in 3, the heaviest) does. */
#define PACE_DRAW_RECENT 32
/* If the 2nd largest exceeds PACE_DRAW_SPIKE times the PACE_DRAW_RANK-th largest, the largest are isolated spikes
 * (Windows giving the turn to another program: 7-10 ms with the rest at 3-4 ms, ~5% of images in the temple) and the
 * PACE_DRAW_RANK-th largest is used; with the 2nd, each spike delayed ~0.2 s of images by one refresh (with a composed
 * screen, 26% -> 11% off pace). Draw times that vary evenly (0-8 ms) still use the 2nd largest. */
#define PACE_DRAW_RANK 5
#define PACE_DRAW_SPIKE 1.5

typedef struct PaceClock {
    /* configuration (pace_setup) */
    int vsync;        /* 1: display grid; 0: images by the clock (G-Sync/FreeSync) */
    int per_frame;    /* V-Sync: refreshes per frame on a display at a multiple of 60 (0 = it is not); without: images per frame */
    double refresh_ns; /* display period */
    double image_ns;   /* grid interval: the display period (V-Sync) or between images (without) */
    double tau_step;   /* tau per interval */
    /* state */
    uint64_t target;     /* predicted time of the planned image (0 = none yet) */
    uint64_t present_at; /* time to present it (Present) */
    double tau;          /* game position in it */
    double draw_ns;      /* estimated draw time of the planned image: the 2nd largest of the last PACE_DRAW_RECENT, without the spikes */
    /* The image that takes the new frame (heavy: fixes, crop, motion) and the others (light) have separate
     * estimates: with a single one, the heavy one (~5.4 ms at 2560x1440) also delayed the light ones and the frame lost images at 180 Hz */
    double recent[2][PACE_DRAW_RECENT]; /* [0] light, [1] heavy */
    int recent_at[2];
    double draw_est[2];
    int intervals;       /* intervals since the previous image */
    long images, late;   /* planned images; presented after the time (appear later than predicted) */
    long holds, jumps;   /* the console did not have the frame (tau stopped on it); ran ahead (tau jumped) */
} PaceClock;

/* hz = display rate (0 = unknown: 60); smooth = smooth motion on (without V-Sync, more than 1 image per
 * frame). Keeps tau; the next image restarts the grid. */
void pace_setup(PaceClock *c, int vsync, double hz, int smooth);
/* Plans the next image: now = now; vblank = time of a display refresh (from Windows; 0 = unknown: follows the
 * previous image's grid); accepted = the frame the window already has (-1 = none): if the image asks for another, the
 * heavy estimate applies. Returns tau; c->target and c->present_at hold the predicted time and the present time. */
double pace_plan(PaceClock *c, uint64_t now, uint64_t vblank, long accepted);
/* The frame B the planned image asks for: ceil(tau) (integer tau = the whole frame, f = 1) */
long pace_need(const PaceClock *c);
/* Frame to show: latest = the console's last ready frame, accepted = the B the window already has. Adjusts tau: console
 * behind (without frame ceil(tau)) -> tau = latest; ahead (the requested frame was already replaced by another) ->
 * tau = latest - 1. Returns B; if it differs from accepted, the window takes latest (and releases the console). *f = fraction. */
long pace_sync(PaceClock *c, long latest, long accepted, double *f);
/* The planned image was presented at now; drawing took draw_ns; heavy = it took a new frame */
void pace_presented(PaceClock *c, uint64_t now, uint64_t draw_ns, int heavy);

/* Did the display rate change? The Windows compositor's rate fluctuates constantly (measured on the user's monitor,
 * 120 Hz with G-Sync: 119.1-120.5 Hz, and momentarily 94-118 Hz), and each pace change restarts the grid (stutter,
 * missing images): only a difference of more than 1% from the rate in use that lasts PACE_RATE_HOLD_NS without coming
 * back counts.
 * At 180 Hz with G-Sync it drops all the time (144-180 Hz, the monitor follows the game) and sometimes stays below 1%
 * for a whole 1 s: the pace went to a momentary reading (175.77 Hz, 11% of frames with 2 refreshes instead of
 * 3). It only drops, never exceeds the real one: the largest reading of the last PACE_RATE_PEAK_NS to 2 x PACE_RATE_PEAK_NS is used. */
#define PACE_RATE_HOLD_NS 1000000000.0
#define PACE_RATE_PEAK_NS 500000000.0
typedef struct PaceRateWatch {
    uint64_t off_since;    /* since when the read rate has been off (0 = it is within) */
    uint64_t bucket_start; /* start of the current stretch of the largest reading */
    double peak, peak_prev; /* largest reading of the current stretch and of the previous one */
} PaceRateWatch;
/* hz = read now (0 = unknown), in_use = the one of the current pace; 1 = redo the pace (with pace_rate_hz) */
int pace_rate_changed(PaceRateWatch *w, double hz, double in_use, uint64_t now);
/* The rate for the pace: the largest recent reading (0 = none) */
double pace_rate_hz(const PaceRateWatch *w);

#endif
