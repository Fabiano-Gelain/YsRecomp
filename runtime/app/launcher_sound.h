/*
 * Start menu sound. Nothing from the game goes in the package: the effects come from the player's disc.
 *
 * Music: the one from the approved mock-up (user's choice, instead of a CD track), made here: calm arpeggio in
 * A minor, triangle waves that fade slowly (each note 2.2 steps of 0.24 s; a bass one octave lower every
 * 8 notes, for 7 steps), in a 32-note loop; fades in and out slowly.
 *
 * Effects: Ys's own, played by the simulated sound chip. Their data is only in memory after the game loads the
 * playable part, so a hidden console (interpreter only: the recompiled code keeps a global cache and does not run
 * on two consoles at the same time) runs the disc on its own thread, pressing RUN like the player (intro,
 * "Beginning", prologue) until the sound driver receives the game's tables (~frame 3700, ~6 s). Then it requests
 * each effect the way the game does (the BIOS driver's "play" function: $22E1 = number, $22E2 = 0, bit 7 of $20E7
 * cleared) and records only the effect channels (Psg.sfx_mask). Numbers (log of the calls in a session, pce_trace
 * --log-calls E0D8): $13 = cursor, $12 = confirm/open window; the game's other effects: $14-$17, $1A, $1B, $25. The
 * result goes into launcher_sfx.bin next to YsRecomp.ini (like the menu font), for the next times; the copy of the
 * VRAM at the end also serves the menu font.
 */
#ifndef PCE_APP_LAUNCHER_SOUND_H
#define PCE_APP_LAUNCHER_SOUND_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "app/launcher.h"
#include "pce/mix.h"

#define LAUNCHER_SFX_MAX 64
#define LAUNCHER_RATE 44100

extern const uint8_t launcher_sfx_numbers[3]; /* LAUNCHER_SFX_MOVE, _OK, _NO */

/* Hidden console that records the effects */
typedef struct LauncherCapture {
    SDL_Thread *thread;
    SDL_AtomicInt done, cancel;
    char cue[1024];
    int count;
    uint8_t numbers[LAUNCHER_SFX_MAX];
    int16_t *pcm[LAUNCHER_SFX_MAX]; /* interleaved stereo */
    int frames[LAUNCHER_SFX_MAX];   /* stereo samples */
    uint16_t *vram;                 /* VRAM at the end (32768 words), for the menu font */
    long console_frames;            /* frames the hidden console ran */
    int ok;                         /* reached the game's tables */
} LauncherCapture;

/* Runs everything right here (tests, --launcher-wav). 0 = ok */
int launcher_capture_run(LauncherCapture *c, const char *cue, const uint8_t *numbers, int count);
/* On its own thread; launcher_capture_done says when it finished */
int launcher_capture_start(LauncherCapture *c, const char *cue, const uint8_t *numbers, int count);
int launcher_capture_done(LauncherCapture *c);
/* Stops (if still running), waits and frees everything */
void launcher_capture_free(LauncherCapture *c);

typedef struct LauncherSound {
    long music_pos; /* samples since the start of the music */
    float music_gain, sfx_gain;
    float level;      /* music volume now (fades in and out slowly) */
    int fading;       /* fading out */
    int16_t *sfx[3];  /* Ys effects (NULL = not available yet) */
    int sfx_frames[3];
    int playing, play_pos; /* effect playing (-1 = none) */
} LauncherSound;

/* Player volumes (the same as the game's: CD music and effects, times master) */
void launcher_sound_init(LauncherSound *s, const AudioVolumes *vol);
/* Volumes changed in the options menu */
void launcher_sound_volumes(LauncherSound *s, const AudioVolumes *vol);
/* Recorded effects (copied). which = LAUNCHER_SFX_* */
void launcher_sound_set_sfx(LauncherSound *s, int which, const int16_t *pcm, int frames);
void launcher_sound_play(LauncherSound *s, int which);
void launcher_sound_fade_out(LauncherSound *s);
/* Next frames stereo samples (music + effect) */
void launcher_sound_mix(LauncherSound *s, int16_t *out, int frames);
void launcher_sound_free(LauncherSound *s);

/* launcher_sfx.bin: the 3 effects (numbers checked on read). 0 = ok */
int launcher_sfx_save(const char *path, const LauncherCapture *c);
int launcher_sfx_load(const char *path, LauncherSound *s);
/* 16-bit stereo 44100 Hz WAV. 0 = ok */
int launcher_write_wav(const char *path, const int16_t *pcm, int frames);

#endif
