#include "app/launcher_sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"
#include "util/u8file.h"

/* Cursor, confirm and "not released" ($17: short, low and falling, ~166 -> 81 Hz, like the mock-up's) */
const uint8_t launcher_sfx_numbers[3] = { 0x13, 0x12, 0x17 };

/* Hidden console script (frames measured without the BIOS with pce_trace --input): RUN at the copyright,
 * at the goddess and at the main menu ("Beginning"); then RUN every 40 frames skips the prologue until the game
 * gives the tables to the sound driver ($22D0-$22D1 = songs; ~frame 3705) */
#define CAP_FIRST_PULSE 2560
#define CAP_MAX_FRAMES 7000
#define CAP_SETTLE 150    /* frames after the tables: the game music starts, the effects stop */
#define CAP_SFX_MAX 240   /* maximum frames per effect */
#define CAP_SFX_QUIET 20  /* frames of silence = the effect ended */
#define RAM(a) (0x1F0000u + ((a) - 0x2000u)) /* physical address of the work RAM ($2000-$3FFF) */

static int pressed(long f)
{
    static const long k_fixed[3][2] = { { 1644, 1650 }, { 1965, 1971 }, { 2487, 2496 } };
    for (int i = 0; i < 3; i++)
        if (f >= k_fixed[i][0] && f < k_fixed[i][1])
            return 1;
    return f >= CAP_FIRST_PULSE && (f - CAP_FIRST_PULSE) % 40 < 6;
}

static int frame_silent(const Pce *p)
{
    for (int i = 0; i < p->audio_count; i++)
        if (p->audio[i] > 8 || p->audio[i] < -8)
            return 0;
    return 1;
}

static int run_capture(LauncherCapture *c)
{
    Pce *p = calloc(1, sizeof(Pce));
    if (!p)
        return -1;
    if (pce_load_cd(p, NULL, c->cue) != 0) {
        free(p);
        return -1;
    }
    pce_reset(p);
    /* Only the effect channels, at the PC version's chip level (the player volumes come later) */
    p->mix.on = 1;
    p->mix.psg_music = 0.0f;
    p->mix.psg_sfx = MIX_MEDNAFEN_PSG;
    p->mix.adpcm = p->mix.cdda = 0.0f;
    p->mix.chip_filter = 1;
    long f = 0, ready = -1;
    uint8_t tables[2] = { 0, 0 };
    for (; f < CAP_MAX_FRAMES && !SDL_GetAtomicInt(&c->cancel); f++) {
        if (f == CAP_FIRST_PULSE) {
            tables[0] = pce_bus_read(p, RAM(0x22D0));
            tables[1] = pce_bus_read(p, RAM(0x22D1));
        }
        if (ready < 0 && f > CAP_FIRST_PULSE &&
            (pce_bus_read(p, RAM(0x22D0)) != tables[0] || pce_bus_read(p, RAM(0x22D1)) != tables[1]))
            ready = f;
        p->pad_buttons = ready < 0 && pressed(f) ? PCE_PAD_RUN : 0;
        pce_run_frame(p, 10000000);
        if (ready >= 0 && f - ready >= CAP_SETTLE && frame_silent(p))
            break;
    }
    c->ok = ready >= 0 && f < CAP_MAX_FRAMES;
    int16_t *buf = c->ok ? malloc((size_t)CAP_SFX_MAX * PSG_BUFFER_SIZE * sizeof(int16_t)) : NULL;
    for (int i = 0; buf && i < c->count && !SDL_GetAtomicInt(&c->cancel); i++) {
        /* Like the driver's "play" function (hle_psg.c, $0B) */
        pce_bus_write(p, RAM(0x22E1), c->numbers[i]);
        pce_bus_write(p, RAM(0x22E2), 0);
        pce_bus_write(p, RAM(0x20E7), (uint8_t)(pce_bus_read(p, RAM(0x20E7)) & 0x7F));
        int n = 0, quiet = 0, heard = 0;
        for (int k = 0; k < CAP_SFX_MAX && quiet < CAP_SFX_QUIET; k++, f++) {
            pce_run_frame(p, 10000000);
            int silent = frame_silent(p);
            heard |= !silent;
            quiet = silent ? quiet + 1 : 0;
            if (!heard)
                continue; /* the silent start is left out */
            memcpy(buf + n * 2, p->audio, (size_t)p->audio_count * sizeof(int16_t));
            n += p->audio_count / 2;
        }
        n -= quiet * 735; /* the trailing silence (735 samples per frame, to within 1) */
        if (n > 0 && heard) {
            c->pcm[i] = malloc((size_t)n * 2 * sizeof(int16_t));
            if (c->pcm[i]) {
                memcpy(c->pcm[i], buf, (size_t)n * 2 * sizeof(int16_t));
                c->frames[i] = n;
            }
        }
        /* Waits for silence before the next one */
        for (int k = 0; k < 30; k++, f++)
            pce_run_frame(p, 10000000);
    }
    free(buf);
    c->vram = malloc(sizeof(p->vdc.vram));
    if (c->vram)
        memcpy(c->vram, p->vdc.vram, sizeof(p->vdc.vram));
    c->console_frames = f;
    pce_free(p);
    free(p);
    return c->ok ? 0 : -1;
}

static void setup(LauncherCapture *c, const char *cue, const uint8_t *numbers, int count)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->cue, sizeof(c->cue), "%s", cue);
    c->count = count > LAUNCHER_SFX_MAX ? LAUNCHER_SFX_MAX : count;
    memcpy(c->numbers, numbers, (size_t)c->count);
}

int launcher_capture_run(LauncherCapture *c, const char *cue, const uint8_t *numbers, int count)
{
    setup(c, cue, numbers, count);
    int r = run_capture(c);
    SDL_SetAtomicInt(&c->done, 1);
    return r;
}

static int SDLCALL capture_thread(void *data)
{
    LauncherCapture *c = data;
    run_capture(c);
    SDL_SetAtomicInt(&c->done, 1);
    return 0;
}

int launcher_capture_start(LauncherCapture *c, const char *cue, const uint8_t *numbers, int count)
{
    setup(c, cue, numbers, count);
    c->thread = SDL_CreateThread(capture_thread, "sfx", c);
    return c->thread ? 0 : -1;
}

int launcher_capture_done(LauncherCapture *c)
{
    return SDL_GetAtomicInt(&c->done);
}

void launcher_capture_free(LauncherCapture *c)
{
    if (c->thread) {
        SDL_SetAtomicInt(&c->cancel, 1);
        SDL_WaitThread(c->thread, NULL);
        c->thread = NULL;
    }
    for (int i = 0; i < LAUNCHER_SFX_MAX; i++)
        free(c->pcm[i]);
    free(c->vram);
    memset(c, 0, sizeof(*c));
}

void launcher_sound_volumes(LauncherSound *s, const AudioVolumes *vol)
{
    float master = (float)vol->master / 100.0f;
    s->music_gain = (float)vol->music / 100.0f * master;
    s->sfx_gain = (float)vol->effects / 100.0f * master;
}

void launcher_sound_init(LauncherSound *s, const AudioVolumes *vol)
{
    memset(s, 0, sizeof(*s));
    launcher_sound_volumes(s, vol);
    s->playing = -1;
}

/* Mock-up music: MIDI notes of the arpeggio (A minor, F, G, E), 0.24 s step */
static const uint8_t k_notes[32] = { 57, 64, 69, 72, 76, 72, 69, 64, 53, 60, 65, 69, 72, 69, 65, 60,
                                     55, 62, 67, 71, 74, 71, 67, 62, 52, 59, 64, 68, 71, 68, 64, 59 };
#define MUSIC_STEP 0.24
#define MUSIC_LOOP (32 * MUSIC_STEP)

/* A fading triangle note (like the mock-up's exponentialRamp down to 0.0005), age a in seconds */
static float voice(double a, double hz, double dur, float vol)
{
    if (a < 0.0 || a >= dur)
        return 0.0f;
    double ph = a * hz - floor(a * hz);
    float tri = (float)(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
    return tri * vol * powf(0.0005f / vol, (float)(a / dur));
}

static float music_sample(long pos)
{
    double t = (double)pos / LAUNCHER_RATE, tl = fmod(t, MUSIC_LOOP);
    float v = 0.0f;
    /* The loop restarts before the last notes fade: also adds the end of the previous round */
    for (int loop = 0; loop < 2; loop++) {
        double base = loop ? tl + MUSIC_LOOP : tl;
        if (loop && t < MUSIC_LOOP)
            break;
        for (int i = 0; i < 32; i++) {
            double a = base - i * MUSIC_STEP;
            if (a < 0.0 || a >= MUSIC_STEP * 7)
                continue;
            v += voice(a, 440.0 * pow(2.0, (k_notes[i] - 69) / 12.0), MUSIC_STEP * 2.2, 0.07f);
            if (i % 8 == 0)
                v += voice(a, 440.0 * pow(2.0, (k_notes[i] - 81) / 12.0), MUSIC_STEP * 7, 0.05f);
        }
    }
    return v * 32767.0f;
}

void launcher_sound_set_sfx(LauncherSound *s, int which, const int16_t *pcm, int frames)
{
    if (which < 0 || which > 2 || !pcm || frames <= 0)
        return;
    int16_t *copy = malloc((size_t)frames * 2 * sizeof(int16_t));
    if (!copy)
        return;
    memcpy(copy, pcm, (size_t)frames * 2 * sizeof(int16_t));
    if (s->playing == which)
        s->playing = -1;
    free(s->sfx[which]);
    s->sfx[which] = copy;
    s->sfx_frames[which] = frames;
}

void launcher_sound_play(LauncherSound *s, int which)
{
    if (which >= 0 && which <= 2 && s->sfx[which]) {
        s->playing = which;
        s->play_pos = 0;
    }
}

void launcher_sound_fade_out(LauncherSound *s)
{
    s->fading = 1;
}

void launcher_sound_mix(LauncherSound *s, int16_t *out, int frames)
{
    /* Music fades in over ~1 s and out over ~0.7 s */
    const float in_step = 1.0f / LAUNCHER_RATE, out_step = 1.5f / LAUNCHER_RATE;
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        s->level = s->fading ? (s->level > out_step ? s->level - out_step : 0.0f)
                             : (s->level + in_step < 1.0f ? s->level + in_step : 1.0f);
        if (s->music_gain > 0.0f && s->level > 0.0f)
            l = r = music_sample(s->music_pos) * s->music_gain * s->level;
        s->music_pos++;
        if (s->playing >= 0) {
            const int16_t *e = s->sfx[s->playing] + s->play_pos * 2;
            l += e[0] * s->sfx_gain;
            r += e[1] * s->sfx_gain;
            if (++s->play_pos >= s->sfx_frames[s->playing])
                s->playing = -1;
        }
        out[i * 2] = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
        out[i * 2 + 1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
    }
}

void launcher_sound_free(LauncherSound *s)
{
    for (int i = 0; i < 3; i++)
        free(s->sfx[i]);
    memset(s, 0, sizeof(*s));
    s->playing = -1;
}

static const char k_magic[8] = { 'Y', 'S', 'L', 'S', 'F', 'X', '2', 0 }; /* 2: with the chip filter */

int launcher_sfx_save(const char *path, const LauncherCapture *c)
{
    if (c->count != 3)
        return -1;
    for (int i = 0; i < 3; i++)
        if (!c->pcm[i] || c->numbers[i] != launcher_sfx_numbers[i])
            return -1;
    FILE *f = u8_fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(k_magic, 1, 8, f) == 8;
    for (int i = 0; i < 3 && ok; i++) {
        uint32_t n = (uint32_t)c->frames[i];
        ok = fwrite(&c->numbers[i], 1, 1, f) == 1 && fwrite(&n, 4, 1, f) == 1 &&
             fwrite(c->pcm[i], sizeof(int16_t) * 2, n, f) == n;
    }
    return fclose(f) == 0 && ok ? 0 : -1;
}

int launcher_sfx_load(const char *path, LauncherSound *s)
{
    FILE *f = u8_fopen(path, "rb");
    if (!f)
        return -1;
    char magic[8];
    int ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, k_magic, 8) == 0;
    int16_t *pcm[3] = { NULL, NULL, NULL };
    uint32_t n[3] = { 0, 0, 0 };
    for (int i = 0; i < 3 && ok; i++) {
        uint8_t num;
        ok = fread(&num, 1, 1, f) == 1 && num == launcher_sfx_numbers[i] && fread(&n[i], 4, 1, f) == 1 && n[i] > 0 &&
             n[i] < LAUNCHER_RATE * 10;
        if (ok) {
            pcm[i] = malloc((size_t)n[i] * 2 * sizeof(int16_t));
            ok = pcm[i] && fread(pcm[i], sizeof(int16_t) * 2, n[i], f) == n[i];
        }
    }
    fclose(f);
    for (int i = 0; i < 3; i++) {
        if (ok)
            launcher_sound_set_sfx(s, i, pcm[i], (int)n[i]);
        free(pcm[i]);
    }
    return ok ? 0 : -1;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int launcher_write_wav(const char *path, const int16_t *pcm, int frames)
{
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0 };
    uint32_t bytes = (uint32_t)frames * 4;
    put32(h + 4, 36 + bytes);
    put32(h + 24, LAUNCHER_RATE);
    put32(h + 28, LAUNCHER_RATE * 4);
    h[32] = 4;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    put32(h + 40, bytes);
    FILE *f = u8_fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(h, 1, 44, f) == 44 && fwrite(pcm, 4, (size_t)frames, f) == (size_t)frames;
    return fclose(f) == 0 && ok ? 0 : -1;
}
