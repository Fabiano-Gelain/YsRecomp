/*
 * Tests of the PC version (runtime/app): configuration file and disc identification.
 *
 *   app_tests config DIR              configuration: write, read, unknown keys, path with accents
 *   app_tests menu DIR                options menu: keys, volumes, exit confirmation and images;
 *                                     skipping the intro with synthetic images
 *   app_tests input DIR               controls: defaults, rebinding from the menu (duplicates refused), reading and
 *                                     writing the configuration, SDL virtual gamepad (buttons and analog)
 *   app_tests video DIR               screen: pacing by refresh rate, fullscreen mode choice, lists,
 *                                     8:7 framing, .ini options
 *   app_tests volume DIR              volumes: ratio between chip, ADPCM and music at default and with
 *                                     each volume changed; Geargrafx mix unchanged; .ini keys;
 *                                     PC-version ADPCM voice without the hiss
 *   app_tests disc YS_CUE DIR         identifies Ys in the folder chosen by the player (DIR = work
 *                                     area; copies ~40 MB of the disc). Without the disc: code 77 (skipped)
 *   app_tests --fingerprint CUE       shows the measurements of a .cue (to fill in runtime/app/disc_id.c)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app/battle.h"
#include "app/config.h"
#include "cd/adpcm.h"
#include "cd/cdrom.h"
#include "cpu/huc6280.h"
#include "app/disc_id.h"
#include "app/fade.h"
#include "app/ys_fade.h"
#include "app/input.h"
#include "app/intro_skip.h"
#include "app/launcher.h"
#include "app/launcher_sound.h"
#include "app/menu.h"
#include "app/menu_font.h"
#include "app/present.h"
#include "app/game_fixes.h"
#include "app/redraw.h"
#include "app/pacing.h"
#include "app/video.h"
#include "app/volume.h"
#include "pce/pce.h"
#include "pce/psg.h"
#include "util/u8file.h"

static int g_failures;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            g_failures++;                                                                                    \
            printf("FAILED (line %d): ", __LINE__);                                                          \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while (0)

static int write_text(const char *path, const char *text)
{
    FILE *f = u8_fopen(path, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

/* Copies up to max_bytes from src to dst; flip_at >= 0 inverts the bits of that byte. */
static int copy_file(const char *src, const char *dst, long long max_bytes, long long flip_at)
{
    FILE *in = u8_fopen(src, "rb"), *out = u8_fopen(dst, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return -1;
    }
    static unsigned char buf[1 << 16];
    long long pos = 0;
    size_t n;
    while ((max_bytes < 0 || pos < max_bytes) && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (max_bytes >= 0 && pos + (long long)n > max_bytes)
            n = (size_t)(max_bytes - pos);
        if (flip_at >= pos && flip_at < pos + (long long)n)
            buf[flip_at - pos] ^= 0xFF;
        fwrite(buf, 1, n, out);
        pos += (long long)n;
    }
    fclose(in);
    return fclose(out);
}

/* ---- configuration ---- */

static void test_config(const char *work)
{
    char dir[1024], path[1024];
    snprintf(dir, sizeof(dir), "%s/configura\xC3\xA7\xC3\xA3o \xE6\x97\xA5\xE6\x9C\xAC", work); /* "configuração 日本" */
    SDL_CreateDirectory(dir);
    snprintf(path, sizeof(path), "%s/YsRecomp.ini", dir);

    Config cfg;
    SDL_RemovePath(path);
    CHECK(config_load(&cfg, path) != 0 && cfg.count == 0, "missing file should return -1 and stay empty");
    const char *folder = "D:\\Jogos\\Ys Book I & II (USA) \xC3\xA7\xC3\xA3o";
    CHECK(config_set(&cfg, "game_folder", folder) == 0, "config_set");
    CHECK(config_set(&cfg, "game_folder", folder) == 0 && cfg.count == 1, "repeated key became another entry");
    CHECK(config_set(&cfg, "bad", "a\nb") != 0, "accepted a value with a line break");
    CHECK(config_set(&cfg, "a=b", "x") != 0, "accepted a key with '='");
    CHECK(config_save(&cfg, path) == 0, "did not write %s", path);

    Config back;
    CHECK(config_load(&back, path) == 0, "did not read it back");
    CHECK(strcmp(config_get(&back, "game_folder", ""), folder) == 0, "value read: %s", config_get(&back, "game_folder", ""));
    CHECK(strcmp(config_get(&back, "nao_existe", "padrao"), "padrao") == 0, "default");

    /* Hand-edited file: Notepad UTF-8 mark, comments, spaces, CRLF, unknown key */
    CHECK(write_text(path, "\xEF\xBB\xBF; comentario\r\n# outro\r\n  game_folder =  C:\\Ys  \r\n"
                           "chave_futura=42\r\nlinha sem igual\r\n") == 0, "write ini");
    CHECK(config_load(&back, path) == 0, "read edited ini");
    CHECK(strcmp(config_get(&back, "game_folder", ""), "C:\\Ys") == 0, "game_folder = '%s'", config_get(&back, "game_folder", ""));
    config_set(&back, "game_folder", "E:\\Outro");
    CHECK(config_save(&back, path) == 0, "rewrite");
    Config again;
    config_load(&again, path);
    CHECK(strcmp(config_get(&again, "chave_futura", ""), "42") == 0, "unknown key lost on save");
    CHECK(strcmp(config_get(&again, "game_folder", ""), "E:\\Outro") == 0, "changed value");
    printf("configuration: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- volumes ---- */

/* Output with a single source playing a constant value: 0 = chip channel with the music, 1 = chip channel
 * with a sound effect, 2 = ADPCM, 3 = CD music. The chip goes through the PSG end of frame (which halves it,
 * OUTPUT_SCALE); the 1st sample counts, before the high-pass filter acts. */
static int mix_one(int source, int16_t value, const AudioMix *mix)
{
    static Psg psg;
    int16_t buf[8], other[8];
    psg_reset(&psg);
    psg.buffer_index = 8;
    for (int i = 0; i < 8; i++) {
        psg.ch[0].output[i] = source <= 1 ? value : 0;
        other[i] = value;
    }
    for (int i = 0; i < 4; i++)
        psg.sfx_mask[i] = source == 1 ? 0x01 : 0x00;
    int n = mix && mix->on ? psg_end_frame_split(&psg, buf, mix->psg_music, mix->psg_sfx) : psg_end_frame(&psg, buf);
    if (n != 8)
        return -99999;
    audio_mix_frame(buf, 8, other, source == 2 ? 8 : 0, other, source == 3 ? 8 : 0, mix);
    return buf[0];
}

/* Voice without the hiss: three tones (300, 1100 and 2700 Hz, like a voice) in ADPCM at 16050 Hz, played with the
 * clock chopped like the CPU's (chunks of 7 to 30 cycles, cut at the 44100 Hz samples as in psg.c).
 * Returns the signal-to-noise ratio in dB of the output against the ideal of the same decoded values (the noise of
 * the 4-bit ADPCM itself does not count: it is the same in both). */
#define TEST_PI 3.14159265358979323846

static double adpcm_tone_snr(int pc_filter)
{
    static const int8_t shift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    const double rate = 32100.0 / 2.0;
    static int decoded[2 * 16050];
    static Adpcm a;
    static CdRom cd;
    static HuC6280 cpu;
    cd.cpu = &cpu; /* receives the half-way and end interrupts */
    adpcm_init(&a, &cd);
    adpcm_reset(&a);
    /* Simple encoder: the nibble that brings the decoder closest to the tone */
    int cur = 2048, idx = 0, nibbles = 2 * 16050;
    for (int i = 0; i < nibbles; i++) {
        double want = 2048.0 + 500.0 * sin(2.0 * TEST_PI * 300.0 * i / rate) +
                      300.0 * sin(2.0 * TEST_PI * 1100.0 * i / rate) + 150.0 * sin(2.0 * TEST_PI * 2700.0 * i / rate);
        int best = 0, best_cur = cur;
        double best_err = 1e9;
        for (int n = 0; n < 16; n++) {
            int d = a.step_delta[(idx << 3) + (n & 7)], c = (cur + ((n & 8) ? -d : d)) & 0xFFF;
            if (fabs(c - want) < best_err) {
                best_err = fabs(c - want);
                best = n;
                best_cur = c;
            }
        }
        cur = decoded[i] = best_cur;
        idx += shift[best & 7];
        idx = idx < 0 ? 0 : idx > 48 ? 48 : idx;
        a.ram[i >> 1] |= (uint8_t)((i & 1) ? best : best << 4);
    }
    adpcm_write(&a, 0x0E, 0x0E);
    a.length = (uint32_t)(nibbles / 2);
    a.control = 0x20;
    a.play_pending = 1;
    a.pc_filter = pc_filter;

    static int16_t frame[2048];
    static double out[44100 * 2];
    static uint64_t tau[44100 * 2]; /* cycle of each output sample */
    int n_out = 0, n_tau = 0;
    uint64_t counter = 0, now = 0;
    unsigned seed = 12345;
    while (n_out < 44100 * 19 / 10) {
        unsigned chunk = 7 + (seed = seed * 1103515245u + 12345u) / 65536u % 24u;
        while (chunk > 0) {
            uint64_t to_sample = (21477273u - counter + 44100 - 1) / 44100;
            unsigned step = chunk < to_sample ? chunk : (unsigned)to_sample;
            adpcm_clock(&a, step);
            counter += (uint64_t)step * 44100;
            now += step;
            chunk -= step;
            while (counter >= 21477273u) {
                counter -= 21477273u;
                tau[n_tau++] = now;
                adpcm_sample(&a);
                if (a.buffer_index >= 1400) {
                    int n = adpcm_end_frame(&a, frame);
                    for (int k = 0; k < n; k += 2)
                        out[n_out++] = frame[k];
                }
            }
        }
    }
    /* Ideal: the same decoded values, each in its exact interval ((n+1)P to (n+2)P cycles), band-limited
     * at 20 kHz (sinc with a Blackman window, in steps of 8 cycles) and then the same digital filters
     * of the output; the PC version averages the interval, so its ideal sits in the middle of the interval. */
    enum { L = 1024, G = 8 };
    static double step_resp[2 * L + 1];
    double acc = 0.0;
    for (int i = -L; i <= L; i++) {
        double x = 2.0 * 20000.0 * G / 21477273.0, w = 0.42 + 0.5 * cos(TEST_PI * i / L) + 0.08 * cos(2 * TEST_PI * i / L);
        acc += x * (i ? sin(TEST_PI * x * i) / (TEST_PI * x * i) : 1.0) * w;
        step_resp[i + L] = acc;
    }
    double period = a.cycles_per_sample / 65536.0, sig = 0.0, err = 0.0;
    double dc_x = 0.0, dc_y = 0.0, lp = 0.0, y1 = 0.0, y2 = 0.0;
    for (int k = 0; k < n_out; k++) {
        double t = pc_filter ? (tau[k] + (k ? tau[k - 1] : 0.0)) / 2.0 : (double)tau[k], v = 0.0;
        int n0 = (int)((t - (L + 1) * G) / period) - 2, n1 = (int)((t + (L + 1) * G) / period) + 1;
        for (int n = n0 < 0 ? 0 : n0; n <= n1 && n < nibbles; n++) {
            double sa = floor((t - (n + 1) * period) / G), sb = floor((t - (n + 2) * period) / G);
            double ha = sa < -L ? 0.0 : sa > L ? acc : step_resp[(int)sa + L];
            double hb = sb < -L ? 0.0 : sb > L ? acc : step_resp[(int)sb + L];
            v += (decoded[n] - 2048) * 10.0 * (ha - hb) / acc;
        }
        double y = v - dc_x + 0.997 * dc_y;
        dc_x = v;
        dc_y = y;
        if (pc_filter) {
            lp = a.lp_g * y + a.lp_c1 * y1 + a.lp_c2 * y2;
            y2 = y1;
            y1 = lp;
        } else {
            lp += 0.4 * (y - lp);
        }
        if (k >= 8820) { /* after 0.2 s */
            sig += lp * lp;
            err += (out[k] - lp) * (out[k] - lp);
        }
    }
    return 10.0 * log10(sig / (err + 1e-9));
}

/* Largest drop of the decoder playing only rising nibbles (goes past the 12-bit limit several times) */
static int adpcm_overflow_drop(int pc_filter)
{
    static Adpcm a;
    static CdRom cd;
    static HuC6280 cpu;
    cd.cpu = &cpu;
    adpcm_init(&a, &cd);
    adpcm_reset(&a);
    memset(a.ram, 0x77, 4096);
    adpcm_write(&a, 0x0E, 0x0E);
    a.length = 4096;
    a.control = 0x20;
    a.play_pending = 1;
    a.pc_filter = pc_filter;
    int prev = -1, drop = 0;
    for (int i = 0; i < 200000; i++) {
        adpcm_clock(&a, 16);
        if (a.playing && prev >= 0 && prev - a.sample > drop)
            drop = prev - a.sample;
        if (a.playing)
            prev = a.sample;
    }
    return drop;
}

/* n samples of the music played by the console (cdaudio_sample), into out (2n values) */
static void cd_console_play(CdAudio *a, int16_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        cdaudio_sample(a);
        if (a->buffer_index >= 1024 || i == n - 1)
            out += cdaudio_end_frame(a, out);
    }
}

/* The CD music played with the options menu open (cdaudio_pc_play, console stopped) and then resuming from where
 * it stopped comes out the same as the track played without stopping: in the looped stretch (going through the
 * wrap) and in the one that stops at the end (silence afterwards). The console does not run during the menu and a
 * music command clears the difference. */
static void test_cd_menu_music(const char *cue)
{
    static CdImage img;
    static CdRom cd;
    static HuC6280 cpu;
    static uint64_t master;
    if (cd_open(&img, cue) != 0) {
        CHECK(0, "could not open the disc %s", cue);
        return;
    }
    int t = 0;
    while (t < img.track_count && img.tracks[t].type != CD_TRACK_AUDIO)
        t++;
    CHECK(t < img.track_count, "disc without an audio track");
    if (t >= img.track_count)
        return;
    uint32_t lba = img.tracks[t].start_lba + 75 * 10; /* 10 s after the start: music, not silence */
    static int16_t whole[6000 * 2], part[6000 * 2];
    for (int loop = 1; loop >= 0; loop--) {
        uint32_t stop = lba + (loop ? 3 : 1); /* 4 sectors (2352 samples) looped; 2 sectors (1176) and stop */
        uint32_t pos_lba = 0, pos_sample = 0;
        for (int run = 0; run < 2; run++) {
            cdrom_init(&cd, &img, &cpu, &master);
            cdaudio_reset(&cd.audio);
            cdaudio_start(&cd.audio, lba, 0);
            cd.audio.seek_cycles = cd.audio.playback_delay_cycles = 0;
            cdaudio_set_stop_lba(&cd.audio, stop, loop ? CD_AUDIO_STOP_EVENT_LOOP : CD_AUDIO_STOP_EVENT_STOP);
            if (run == 0) {
                cd_console_play(&cd.audio, whole, 6000);
                continue;
            }
            cd_console_play(&cd.audio, part, 1000);
            pos_lba = cd.audio.current_lba;
            pos_sample = cd.audio.current_sample;
            cdaudio_pc_play(&cd.audio, part + 1000 * 2, 2500); /* menu open: goes past the wrap / the end */
            CHECK(cd.audio.current_lba == pos_lba && cd.audio.current_sample == pos_sample,
                  "the console ran with the menu open");
            cd_console_play(&cd.audio, part + 3500 * 2, 2500);
        }
        int diff = -1, silent_from = -1;
        for (int i = 0; i < 6000 * 2 && diff < 0; i++)
            if (whole[i] != part[i])
                diff = i / 2;
        for (int i = 0; i < 6000 && silent_from < 0; i++)
            if (!whole[i * 2] && !whole[i * 2 + 1] && !whole[i * 2 + 2] && !whole[i * 2 + 3] && i < 5999)
                silent_from = i;
        printf("CD music with the menu (%s): %s; silence from sample %d\n", loop ? "looping" : "stopping",
               diff < 0 ? "same as played without stopping" : "DIFFERENT", silent_from);
        CHECK(diff < 0, "music with the menu (%s) differs from the one played without stopping from sample %d",
              loop ? "looping" : "stopping", diff);
        CHECK(loop ? silent_from < 0 : silent_from >= 1170 && silent_from <= 1176,
              "silence in the wrong place (sample %d)", silent_from);
        cdaudio_start(&cd.audio, lba, 0);
        CHECK(cd.audio.pc_skip == 0, "a music command should clear the difference");
    }
    cd_close(&img);
}

static void test_adpcm_voice(void)
{
    double gg = adpcm_tone_snr(0), pc = adpcm_tone_snr(1);
    printf("ADPCM at 16 kHz against the ideal: signal/noise %.1f dB like Geargrafx, %.1f dB in the PC version\n", gg, pc);
    CHECK(pc >= 36.0 && pc >= gg + 6.0, "PC-version voice should have much less noise (%.1f vs %.1f dB)", pc, gg);
    int gg_drop = adpcm_overflow_drop(0), pc_drop = adpcm_overflow_drop(1);
    printf("ADPCM past the limit: max drop %d like Geargrafx (wraps around), %d in the PC version\n", gg_drop,
           pc_drop);
    CHECK(gg_drop > 2048 && pc_drop == 0, "PC-version decoder should clamp at 4095 (drops %d and %d)",
          gg_drop, pc_drop);
}

static void test_volume(const char *work)
{
    /* Geargrafx mix (off): plain sum, including the 16-bit clipping and short sources */
    AudioMix off = {0};
    int16_t buf[4] = { 1000, -20000, 30000, 5 }, adpcm[4] = { 7, -20000, 3000, 9 }, cdda[2] = { -3, -1 };
    audio_mix_frame(buf, 4, adpcm, 4, cdda, 2, &off);
    CHECK(buf[0] == 1004 && buf[1] == -32768 && buf[2] == 32767 && buf[3] == 14,
          "Geargrafx mix: %d %d %d %d", buf[0], buf[1], buf[2], buf[3]);
    for (int s = 0; s < 4; s++) {
        int expect = s <= 1 ? 1000 : 2000;
        CHECK(mix_one(s, 2000, &off) == expect && mix_one(s, 2000, NULL) == expect, "Geargrafx, source %d: %d", s,
              mix_one(s, 2000, &off));
    }

    /* Default (all 100%): Mednafen balance (chip at 20000 -> 10000 at the PSG output) */
    AudioVolumes vol = { 100, 100, 100, 100, 100 };
    AudioMix mix;
    audio_mix_mednafen(&mix, &vol);
    int pm = mix_one(0, 20000, &mix), ps = mix_one(1, 20000, &mix), adp = mix_one(2, 10000, &mix),
        cd = mix_one(3, 10000, &mix);
    printf("default: chip music %d, chip effect %d, ADPCM %d, CD music %d (of 10000); CD/chip = %.3f\n", pm, ps,
           adp, cd, (double)cd / pm);
    CHECK(abs(pm - 6780) <= 1 && abs(ps - 6780) <= 1 && adp == 6838 && cd == 5000, "Mednafen balance: %d %d %d %d", pm, ps, adp,
          cd);

    /* Each volume affects only its own source; the master one, all of them */
    static const struct {
        int master, music, chip_music, effects, voices;
        int chip_music_out, chip_sfx, adpcm, cdda;
    } cases[] = {
        { 100, 50, 100, 100, 100, 6780, 6780, 6838, 2500 },
        { 100, 100, 50, 100, 100, 3390, 6780, 6838, 5000 },
        { 100, 100, 100, 50, 100, 6780, 3390, 6838, 5000 },
        { 100, 100, 100, 100, 25, 6780, 6780, 1709, 5000 },
        { 50, 100, 100, 100, 100, 3390, 3390, 3419, 2500 },
        { 100, 0, 100, 100, 100, 6780, 6780, 6838, 0 },
        { 100, 100, 0, 100, 100, 0, 6780, 6838, 5000 },
        { 100, 100, 100, 0, 100, 6780, 0, 6838, 5000 },
        { 0, 100, 100, 100, 100, 0, 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AudioVolumes v = { cases[i].master, cases[i].music, cases[i].chip_music, cases[i].effects, cases[i].voices };
        audio_mix_mednafen(&mix, &v);
        int m0 = mix_one(0, 20000, &mix), m1 = mix_one(1, 20000, &mix), a = mix_one(2, 10000, &mix),
            c = mix_one(3, 10000, &mix);
        CHECK(abs(m0 - cases[i].chip_music_out) <= 1 && abs(m1 - cases[i].chip_sfx) <= 1 &&
                  abs(a - cases[i].adpcm) <= 1 && abs(c - cases[i].cdda) <= 1,
              "volumes %d/%d/%d/%d/%d: chip music %d, chip effect %d, ADPCM %d, CD %d", v.master, v.music,
              v.chip_music, v.effects, v.voices, m0, m1, a, c);
    }

    /* Configuration: missing keys come in at 100; values outside 0-100 are clamped */
    char path[1024];
    snprintf(path, sizeof(path), "%s/volumes.ini", work);
    CHECK(write_text(path, "game_folder=C:\\Ys\nvolume_music=40\nvolume_effects=150\nvolume_voices=abc\n") == 0,
          "write ini");
    Config cfg;
    config_load(&cfg, path);
    AudioVolumes got;
    CHECK(volume_load(&cfg, &got) == 1, "should add volume_master and volume_chip_music");
    CHECK(got.master == 100 && got.music == 40 && got.chip_music == 100 && got.effects == 100 && got.voices == 100,
          "read: %d %d %d %d", got.master, got.music, got.effects, got.voices);
    CHECK(strcmp(config_get(&cfg, "volume_master", ""), "100") == 0 &&
              strcmp(config_get(&cfg, "volume_chip_music", ""), "100") == 0, "missing keys not added");
    config_save(&cfg, path);
    Config back;
    config_load(&back, path);
    CHECK(volume_load(&back, &got) == 0 && got.music == 40, "second read should not add anything");
    CHECK(strcmp(config_get(&back, "game_folder", ""), "C:\\Ys") == 0, "game_folder lost");
    test_adpcm_voice();
    printf("volumes: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- options menu ---- */

/* Draws the menu on a blue background image (to see the window border), writes DIR/name.ppm and returns
 * the CRC of the pixels. */
static Uint32 menu_shot(const Menu *m, int w, int h, int blink, const char *dir, const char *name)
{
    static uint32_t px[400 * 242];
    for (int i = 0; i < w * h; i++)
        px[i] = 0xFF2040A0u;
    menu_draw(m, px, w, h, w, blink);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = u8_fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            unsigned char rgb[3] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i] };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    return SDL_crc32(0, px, (size_t)w * h * sizeof(px[0]));
}

static InputMap g_map; /* default keys and buttons */

/* Menu font: without the game's font (it cannot be in the test), the checksum refuses other tiles and the wrong
 * file, and the fallback glyphs exist */
static void test_menu_font(void)
{
    static uint16_t vram[32768];
    menu_font_forget();
    for (int i = 0; i < 32768; i++)
        vram[i] = (uint16_t)(i * 2654435761u >> 16);
    CHECK(!menu_font_capture(vram) && !menu_font_ready(), "tiles that are not the game's font: refused");
    char path[1024];
    snprintf(path, sizeof(path), "%s/app_menu_font_errada.bin", SDL_GetBasePath());
    FILE *f = u8_fopen(path, "wb");
    if (f) {
        fwrite(vram, 2, MENU_FONT_WORDS, f);
        fclose(f);
    }
    CHECK(menu_font_load(path) != 0 && !menu_font_ready(), "file with another font: refused");
    CHECK(menu_font_save(path) != 0, "without the font: nothing to write");
    remove(path);
    uint8_t rows[8], lower[8];
    static const char k_chars[] = "ABCMXYZ0123456789:/%-.+<>?!,'()[]=;_";
    int all = 1;
    for (const char *c = k_chars; *c; c++) {
        int lit = 0;
        all &= menu_font_glyph(*c, rows);
        for (int r = 0; r < 8; r++)
            lit |= rows[r];
        all &= lit != 0 && rows[7] == (*c == ',' ? rows[7] : 0); /* the 8th row empty (except the comma) */
    }
    CHECK(all, "letters, digits and symbols drawn, with the 8th row empty");
    menu_font_glyph('a', lower);
    menu_font_glyph('A', rows);
    CHECK(memcmp(rows, lower, 8) == 0 && !menu_font_glyph(' ', rows), "lowercase = uppercase; space empty");
    menu_font_arrow(rows);
    CHECK(rows[0] == 0x20 && rows[3] == 0x3C && rows[7] == 0, "fallback arrow: 4x7 triangle");
}

/* menufont tool: the menu pages with the game's font already loaded (no CRC check: the font comes from the
 * player's disc) */
static Uint32 menu_shot(const Menu *m, int w, int h, int blink, const char *dir, const char *name);
static void menu_font_shots(const char *dir)
{
    static const struct { int page, item; const char *name; } pages[] = {
        { MENU_PAGE_MAIN, MENU_ITEM_RESUME, "jogo_principal" }, { MENU_PAGE_VIDEO, MENU_VIDEO_REFRESH, "jogo_video" },
        { MENU_PAGE_IMAGE, MENU_IMAGE_CROP, "jogo_image" },     { MENU_PAGE_AUDIO, MENU_AUDIO_MUSIC, "jogo_audio" },
        { MENU_PAGE_GAMEPLAY, MENU_GAMEPLAY_PAUSE_BG, "jogo_gameplay" },
        { MENU_PAGE_KEYBOARD, PAD_FN_I, "jogo_teclado" },
    };
    AudioVolumes vol = { 100, 80, 100, 100, 100 };
    VideoSettings video;
    video_defaults(&video);
    input_default_keys(&g_map);
    input_default_buttons(&g_map);
    Menu m;
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        menu_open(&m, &vol, 0, 1, &g_map, &video, NULL);
        m.page = pages[i].page;
        if (m.page == MENU_PAGE_MAIN)
            m.cursor = pages[i].item;
        else
            m.item = pages[i].item;
        if (m.page == MENU_PAGE_KEYBOARD)
            m.cursor = MENU_ITEM_CONTROLS;
        snprintf(m.video_info, sizeof(m.video_info), "Screen 2560x1440 120 Hz");
        menu_shot(&m, 341, 242, 1, dir, pages[i].name);
    }
    printf("images in %s\n", dir);
}

/* Fake monitor: 2560x1440 at 144 Hz in Windows; has 120 and 60 Hz at that resolution and 1920x1080 at 144/60 */
static VideoModes g_modes = { { 2560, 1440, 143.98f },
                              { { 2560, 1440, 143.98f }, { 2560, 1440, 119.98f }, { 2560, 1440, 59.95f },
                                { 1920, 1080, 144.0f }, { 1920, 1080, 60.0f }, { 1920, 1080, 59.94f } },
                              6 };

static void test_menu(const char *work)
{
    Menu m;
    AudioVolumes vol = { 100, 100, 100, 100, 100 };
    VideoSettings video;
    video_defaults(&video);
    CHECK(video.crop == CROP_FILL && video.hud_bg == CROP_HUD_SHADOW && video.crt == VIDEO_CRT_OFF,
          "shipping defaults: Fill, HUD Shadow, no CRT");
    video.crop = CROP_FRAME; /* the walk below starts at the border and the black background */
    video.hud_bg = CROP_HUD_BLACK;
    input_default_keys(&g_map);
    input_default_buttons(&g_map);
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    CHECK(m.open && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_RESUME, "open: cursor on Resume");
    CHECK(menu_key(&m, MENU_KEY_UP) == 0 && m.cursor == MENU_ITEM_QUIT, "up from the 1st goes to the last");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == 0, "left on Quit does nothing");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.confirm_quit && !m.yes, "Quit asks for confirmation, with No selected");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && !m.confirm_quit && m.open, "No: back to the menu");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && !m.confirm_quit, "Esc on the confirmation: back to the menu");
    menu_key(&m, MENU_KEY_OK);
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.yes && menu_key(&m, MENU_KEY_OK) == MENU_QUIT, "Yes: quit");

    /* Ys sound effects, as in the equipment window: open and confirm = confirm; move, change, back = cursor */
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    CHECK(m.sfx == MENU_SFX_OK, "open: confirm effect");
    m.sfx = MENU_SFX_NONE;
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.sfx == MENU_SFX_MOVE, "move: cursor effect");
    menu_key(&m, MENU_KEY_OK);
    CHECK(m.sfx == MENU_SFX_OK, "enter a page: confirm effect");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.sfx == MENU_SFX_MOVE, "change a value: cursor effect");
    menu_key(&m, MENU_KEY_BACK);
    CHECK(m.sfx == MENU_SFX_MOVE, "back: cursor effect");

    /* Audio */
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_AUDIO && m.item == MENU_AUDIO_MUSIC, "opens Audio");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0, "music already at 100%%: nothing changes");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VOLUME && m.vol.music == 90, "left: music 90%%");
    for (int i = 0; i < 12; i++)
        menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.vol.music == 0, "music does not go below 0%%: %d", m.vol.music);
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VOLUME && m.vol.chip_music == 90 && m.vol.music == 0,
          "chip music 90%%, CD music 0%%");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VOLUME && m.vol.effects == 90, "effects 90%%");
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_LEFT);
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_LEFT);
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.vol.voices == 90 && m.vol.master == 80, "voices %d, master %d", m.vol.voices, m.vol.master);
    CHECK(menu_key(&m, MENU_KEY_UP) == 0 && m.item == MENU_AUDIO_VOICES, "up");
    m.item = MENU_AUDIO_FILTER;
    int filter = m.vol.chip_filter;
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VOLUME && m.vol.chip_filter == !filter, "chip filter: right");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VOLUME && m.vol.chip_filter == filter, "chip filter: OK");
    m.item = MENU_AUDIO_MUSIC;
    CHECK(menu_key(&m, MENU_KEY_UP) == 0 && m.item == MENU_AUDIO_BACK, "up from the 1st goes to Back");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == 0 && m.page == MENU_PAGE_AUDIO, "left on Back does nothing");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_AUDIO && m.open,
          "Back: returns to the main page, cursor on Audio");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_MAIN && m.open, "Esc on Audio: back");
    CHECK(menu_key(&m, MENU_KEY_BACK) == MENU_CLOSED && !m.open, "Esc on the main page closes");
    CHECK(menu_key(&m, MENU_KEY_DOWN) == 0, "closed: keys ignored");

    /* Controls -> Keyboard -> Back returns to Controls */
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    m.cursor = MENU_ITEM_CONTROLS;
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_CONTROLS, "opens Controls");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_KEYBOARD && m.item == PAD_FN_UP, "opens Keyboard");
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_CONTROLS && m.item == MENU_CONTROLS_KEYBOARD,
          "Esc on Keyboard: back to Controls");
    m.item = MENU_CONTROLS_BACK;
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_CONTROLS, "Back");

    /* Video */
    m.cursor = MENU_ITEM_VIDEO;
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_VIDEO && m.item == MENU_VIDEO_FULLSCREEN, "opens Video");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && !m.pending.fullscreen, "Fullscreen: Off");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == 0 && m.pending.fullscreen, "Fullscreen: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0 && m.pending.res_w == 2560 && m.pending.res_h == 1440,
          "Resolution: Desktop -> 2560x1440");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.res_w == 1920 && m.pending.res_h == 1080, "Resolution: 1920x1080");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.res_w == 0, "Resolution: back to Desktop");
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.pending.res_w == 1920, "Resolution: left wraps around");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0 && m.pending.refresh == 144, "Refresh: 144 at 1920x1080");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.refresh == 60, "Refresh: 60 (59.94 and 60 are one)");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.refresh == 0, "Refresh: Auto");
    menu_key(&m, MENU_KEY_RIGHT);
    m.item = MENU_VIDEO_RESOLUTION;
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.res_w == 0 && m.pending.refresh == 0, "changing the resolution resets the refresh rate to Auto");
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.pending.refresh == 60, "Refresh at Desktop (2560x1440): left = the lowest, 60");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && !m.pending.vsync, "V-Sync: Off");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0 && m.pending.window_scale == 4, "Window size 4x");
    menu_key(&m, MENU_KEY_RIGHT);
    menu_key(&m, MENU_KEY_RIGHT);
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.pending.window_scale == 2, "Window size wraps around: 6x -> 2x");
    CHECK(m.video.vsync && m.video.window_scale == video.window_scale, "nothing applies before Apply");
    /* Apply: takes effect and asks for confirmation; timing out returns to the previous mode */
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.item == MENU_VIDEO_APPLY && menu_key(&m, MENU_KEY_RIGHT) == 0 && !m.confirm_video,
          "Apply: right does not apply");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.confirm_video && !m.video.vsync &&
              m.video.window_scale == 2,
          "Apply: the chosen mode takes effect and asks for confirmation");
    CHECK(menu_tick(&m, 1000) == 0 && menu_tick(&m, 6000) == 0 && m.confirm_left_ms == MENU_CONFIRM_MS - 5000,
          "confirmation: counts the time");
    CHECK(menu_tick(&m, 11000) == MENU_CHANGED_VIDEO && !m.confirm_video && m.video.vsync && m.pending.vsync &&
              m.video.window_scale == video.window_scale,
          "confirmation: timeout returns to the previous mode");
    m.item = MENU_VIDEO_VSYNC;
    menu_key(&m, MENU_KEY_OK);
    m.item = MENU_VIDEO_APPLY;
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_DOWN) == 0 && !m.keep && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO &&
              m.video.vsync,
          "confirmation: No reverts");
    m.item = MENU_VIDEO_VSYNC;
    menu_key(&m, MENU_KEY_OK);
    m.item = MENU_VIDEO_APPLY;
    menu_key(&m, MENU_KEY_OK);
    CHECK(m.confirm_video && m.keep && menu_key(&m, MENU_KEY_OK) == 0 && !m.confirm_video && !m.video.vsync,
          "confirmation: Yes keeps");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && !m.confirm_video, "Apply without changes: nothing");
    m.item = MENU_VIDEO_VSYNC;
    menu_key(&m, MENU_KEY_OK);
    m.item = MENU_VIDEO_BACK;
    menu_key(&m, MENU_KEY_OK);
    CHECK(m.page == MENU_PAGE_MAIN && !m.video.vsync && !m.pending.vsync, "leaving without Apply discards");
    menu_key(&m, MENU_KEY_OK);
    m.item = MENU_VIDEO_SMOOTH;
    CHECK(m.video.smooth_motion && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && !m.video.smooth_motion,
          "Smooth motion: Off");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.smooth_motion, "Smooth motion: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.item == MENU_VIDEO_FADES && m.video.smooth_fades && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO &&
              !m.video.smooth_fades && !m.pending.smooth_fades,
          "Smooth fades: Off");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VIDEO && m.video.smooth_fades, "Smooth fades: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.item == MENU_VIDEO_BACK && menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_MAIN &&
              m.cursor == MENU_ITEM_VIDEO,
          "Video ends at Back (Image left the page)");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.cursor == MENU_ITEM_IMAGE && menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_IMAGE &&
              m.item == MENU_IMAGE_INTEGER,
          "Image on the main menu, below Video");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.integer_scale, "Integer scale: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VIDEO && m.video.smooth, "Filter: Smooth");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.crt == VIDEO_CRT_SCANLINES, "CRT: Scanlines");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.crt == VIDEO_CRT_MASK, "CRT: Lines+mask");
    for (int i = 0; i < 5; i++)
        menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.crt == VIDEO_CRT_GLOW_SLOT, "CRT: Glow+slot (the last)");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.crt == VIDEO_CRT_OFF, "CRT: wraps around");
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.video.crt == VIDEO_CRT_GLOW_SLOT, "CRT: left");
    for (int i = 0; i < 5; i++)
        menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.video.crt == VIDEO_CRT_MASK, "CRT: left down to Lines+mask");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(!m.video.crt_bright && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.crt_bright,
          "CRT brightness: Bright");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.aspect == VIDEO_ASPECT_SQUARE, "Pixel aspect: Square");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.aspect == VIDEO_ASPECT_STRETCH, "Pixel aspect: Stretch");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.aspect == VIDEO_ASPECT_TV, "Pixel aspect: wraps around");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.video.crop == CROP_FRAME, "Crop border: default Frame");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.crop == CROP_FULL, "Crop border: Full");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.crop == CROP_COMPACT, "Crop border: Compact");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.crop == CROP_WIDE, "Crop border: Wide");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.crop == CROP_FILL, "Crop border: Fill");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.crop == CROP_FRAME,
          "Crop border: wraps around (no Off)");
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.video.crop == CROP_FILL, "Crop border: left wraps around");
    menu_key(&m, MENU_KEY_LEFT);
    menu_key(&m, MENU_KEY_LEFT);
    menu_key(&m, MENU_KEY_LEFT);
    CHECK(m.video.crop == CROP_FULL, "Crop border: left");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.hud_bg == CROP_HUD_CHARCOAL,
          "HUD background: after black, the near-black gray (no flat colors)");
    for (int k = 0; k < 8; k++)
        menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.hud_bg == CROP_HUD_LEATHER, "HUD background: Slate ... Leather");
    for (int k = 0; k < 5; k++)
        menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.hud_bg == CROP_HUD_SHADOW, "HUD background: Walnut ... Shadow");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.hud_bg == CROP_HUD_BLACK, "HUD background: wraps around");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.video.equipment == 1 && menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.equipment == 0,
          "Visual equipment: on by default, turns off");
    menu_key(&m, MENU_KEY_RIGHT);
    CHECK(m.video.equipment == 1, "Visual equipment: turns on again");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_IMAGE,
          "Back from Image: returns to the main page, cursor on Image");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_IMAGE, "Esc on Image");
    m.cursor = MENU_ITEM_VIDEO;
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_VIDEO, "Esc on Video");
    m.cursor = MENU_ITEM_RESUME;
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CLOSED && !m.open, "Resume closes");
    menu_open(&m, &vol, 1, 1, &g_map, &video, NULL);
    m.page = MENU_PAGE_VIDEO;
    m.item = MENU_VIDEO_RESOLUTION;
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0 && m.video.res_w == 0, "no mode list: Desktop only");

    /* Gameplay: Skip intro left the main page; Pause in background */
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    m.cursor = MENU_ITEM_GAMEPLAY;
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == 0 && m.page == MENU_PAGE_MAIN, "Gameplay: right does nothing");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_GAMEPLAY && m.item == MENU_GAMEPLAY_SKIP_INTRO,
          "Gameplay opens the page");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_SETTING && !m.skip_intro, "Skip intro: right turns off");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_SETTING && m.skip_intro, "Skip intro: Enter turns on");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_SETTING && !m.skip_intro, "Skip intro: left turns off");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_SETTING && !m.pause_bg && !m.skip_intro,
          "Pause in background: Off");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_SETTING && m.pause_bg, "Pause in background: On");
    /* HP bars and reward popups, stored with the screen options */
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.video.enemy_bars && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && !m.video.enemy_bars,
          "Enemy HP bars: Off");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.enemy_bars, "Enemy HP bars: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.video.reward_popups && menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_VIDEO && !m.video.reward_popups &&
              m.video.enemy_bars,
          "Reward popups: Off");
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO && m.video.reward_popups, "Reward popups: On");
    /* Instant dialog (changes the game), on by default, stored with the screen options */
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.item == MENU_GAMEPLAY_DIALOG && m.video.instant_dialog && menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_VIDEO &&
              !m.video.instant_dialog,
          "Instant dialog: Off");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_VIDEO && m.video.instant_dialog, "Instant dialog: On");
    menu_key(&m, MENU_KEY_DOWN);
    CHECK(m.item == MENU_GAMEPLAY_BACK && menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_MAIN &&
              m.cursor == MENU_ITEM_GAMEPLAY,
          "Back from Gameplay: returns to the main page, cursor on Gameplay");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_MAIN && m.cursor == MENU_ITEM_GAMEPLAY,
          "Esc on Gameplay");

    /* Images (checked by eye the first time; from then on by CRC). 256 = intro width,
     * 341 = game width. */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/menu", work);
    SDL_CreateDirectory(dir);
    static const struct { int w, page, cursor, music, confirm, yes, blink; const char *name; Uint32 crc; } shots[] = {
        { 256, MENU_PAGE_MAIN, MENU_ITEM_RESUME, 100, 0, 0, 1, "menu_256", 0x98E9A147 },
        { 341, MENU_PAGE_MAIN, MENU_ITEM_RESUME, 100, 0, 0, 1, "menu_341", 0xAD1C9433 },
        { 341, MENU_PAGE_MAIN, MENU_ITEM_RESUME, 100, 0, 0, 0, "menu_341_apagada", 0xDFC64E5D },
        { 341, MENU_PAGE_AUDIO, MENU_AUDIO_MUSIC, 40, 0, 0, 1, "audio_341", 0xCC8F28EF },
        { 341, MENU_PAGE_VIDEO, MENU_VIDEO_REFRESH, 100, 0, 0, 1, "video_341", 0x0243E82D },
        { 256, MENU_PAGE_VIDEO, MENU_VIDEO_FULLSCREEN, 100, 0, 0, 1, "video_256", 0x113F556F },
        { 341, MENU_PAGE_IMAGE, MENU_IMAGE_CRT, 100, 0, 0, 1, "image_341", 0x2DF563BA },
        { 341, MENU_PAGE_CONTROLS, MENU_CONTROLS_CONTROLLER, 100, 0, 0, 1, "controls_341", 0x400CEB77 },
        { 341, MENU_PAGE_MAIN, MENU_ITEM_QUIT, 100, 1, 0, 1, "sair_no", 0xFF1C7CE3 },
        { 341, MENU_PAGE_MAIN, MENU_ITEM_QUIT, 100, 1, 1, 1, "sair_yes", 0x1086C17C },
        { 341, MENU_PAGE_GAMEPLAY, MENU_GAMEPLAY_SKIP_INTRO, 100, 0, 0, 1, "gameplay_skip_off", 0x1C626952 },
    };
    for (size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
        m.page = shots[i].page;
        if (m.page == MENU_PAGE_MAIN)
            m.cursor = shots[i].cursor;
        else
            m.item = shots[i].cursor;
        m.vol.music = shots[i].music;
        m.confirm_quit = shots[i].confirm;
        m.yes = shots[i].yes;
        m.skip_intro = strcmp(shots[i].name, "gameplay_skip_off") != 0;
        m.video.res_w = 1920;
        m.video.res_h = 1080;
        m.video.refresh = 144;
        m.pending = m.video;
        snprintf(m.video_info, sizeof(m.video_info), "Screen 1920x1080 144 Hz");
        Uint32 crc = menu_shot(&m, shots[i].w, 240, shots[i].blink, dir, shots[i].name);
        printf("image %s: CRC %08X\n", shots[i].name, (unsigned)crc);
        CHECK(!shots[i].crc || crc == shots[i].crc, "image %s changed (CRC %08X, expected %08X)", shots[i].name,
              (unsigned)crc, (unsigned)shots[i].crc);
    }
    /* "Keep these settings?" with 7 s left */
    menu_open(&m, &vol, 1, 1, &g_map, &video, &g_modes);
    m.page = MENU_PAGE_VIDEO;
    m.item = MENU_VIDEO_VSYNC;
    menu_key(&m, MENU_KEY_OK);
    m.item = MENU_VIDEO_APPLY;
    menu_key(&m, MENU_KEY_OK);
    menu_tick(&m, 1);
    menu_tick(&m, 3001);
    {
        Uint32 crc = menu_shot(&m, 341, 240, 1, dir, "video_confirm");
        printf("image video_confirm: CRC %08X\n", (unsigned)crc);
        CHECK(crc == 0x8A79A7DE, "image video_confirm changed (CRC %08X)", (unsigned)crc);
    }
    printf("menu: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- screen ---- */

/* Single clock (app/pacing.h) on a simulated screen. With V-Sync, the screen refreshes on a grid and the delivered
 * image (Present) appears on the first refresh at which the GPU has finished (0.3 ms); with variable refresh, it
 * appears right away, never before the minimum period after the previous one. The console is released when the window
 * takes a frame and is ready 1 to 5 ms later; the window waits for the missing frame like main.c; drawing takes 0 to
 * draw_ms (random). */
typedef struct PaceSim {
    double speed_min, speed_max; /* shown speed: tau per second of screen, between consecutive images */
    long images, frames, late, holds, jumps, skips, same_refresh;
    double fps; /* game frames per second */
} PaceSim;

static double pace_rand(unsigned *s)
{
    *s = *s * 1103515245u + 12345u;
    return (double)((*s >> 8) & 0xFFFF) / 65535.0;
}

/* vsync: 0 = no V-Sync; 1 = V-Sync, the image goes straight to the screen (flips on the 1st refresh after delivery);
 * 2 = V-Sync with Windows compositing the screen (recording or overlay; it takes the image up to ~0.7 ms after each
 * refresh and shows it on the next one) */
static PaceSim pace_simulate(double hz, int vsync, int smooth, double draw_ms, double seconds)
{
    PaceSim r = { 1e9, 0.0 };
    PaceClock c = { 0 };
    pace_setup(&c, vsync != 0, hz, smooth);
    const double T = 1e9 / hz, phase = 3.1e6, warm = 1e9;
    unsigned seed = 1234u + (unsigned)hz;
    long latest = -1, accepted = -1, frames0 = 0;
    int emu_busy = 1;
    double ready_at = 2e6, now = 0.0, last_shown = -1.0, last_tau = 0.0;
    PaceClock at_warm = c;
    int warmed = 0;
    while (now < seconds * 1e9) {
        if (emu_busy && now >= ready_at) {
            latest++;
            emu_busy = 0;
        }
        uint64_t vblank = vsync ? (uint64_t)(phase + ceil((now - phase) / T) * T) : 0;
        pace_plan(&c, (uint64_t)now, vblank, accepted);
        double deadline = (double)c.present_at; /* like main.c */
        if (latest < pace_need(&c) && emu_busy && ready_at <= deadline - c.draw_ns) {
            now = now > ready_at ? now : ready_at;
            latest++;
            emu_busy = 0;
        }
        double f;
        long b = pace_sync(&c, latest, accepted, &f);
        int heavy = b >= 0 && b != accepted;
        if (heavy) {
            if (accepted >= 0 && b != accepted + 1)
                r.skips++;
            accepted = b;
            r.frames++;
            emu_busy = 1; /* released for the next frame */
            ready_at = now + 1e6 + 4e6 * pace_rand(&seed);
        }
        /* The image that takes the new frame needs 0 to draw_ms; the others, up to a third of that. draw_ms < 0:
         * the new frame's in 3-4 ms and, 1 time in 20, a 7-10 ms spike (Windows giving the turn to another program) */
        double draw = draw_ms * 1e6 * pace_rand(&seed) * (heavy ? 1.0 : 1.0 / 3.0);
        if (draw_ms < 0.0)
            draw = !heavy ? 0.1e6 : pace_rand(&seed) < 0.05 ? 7e6 + 3e6 * pace_rand(&seed) : 3e6 + 1e6 * pace_rand(&seed);
        double done = now + draw;
        double call = done > (double)c.present_at ? done : (double)c.present_at, shown;
        pace_presented(&c, (uint64_t)call, (uint64_t)draw, heavy);
        if (vsync == 2)
            shown = phase + ceil((call - 0.7e6 - phase) / T) * T + T;
        else if (vsync)
            shown = phase + ceil((call + 0.3e6 - phase) / T) * T;
        else
            shown = last_shown >= 0.0 && call < last_shown + T ? last_shown + T : call;
        if (!warmed && now > warm) {
            warmed = 1;
            at_warm = c;
            frames0 = r.frames;
        }
        if (warmed && last_shown >= 0.0) {
            r.images++;
            if (shown <= last_shown + 1.0)
                r.same_refresh++;
            else {
                double speed = (c.tau - last_tau) / (shown - last_shown) * 1e9;
                r.speed_min = speed < r.speed_min ? speed : r.speed_min;
                r.speed_max = speed > r.speed_max ? speed : r.speed_max;
            }
        }
        last_shown = shown;
        last_tau = c.tau;
        now = call + 0.05e6; /* Present returns */
    }
    r.late = c.late - at_warm.late;
    r.holds = c.holds - at_warm.holds;
    r.jumps = c.jumps - at_warm.jumps;
    r.fps = (r.frames - frames0) / (seconds - warm / 1e9);
    return r;
}

static void test_pacing(void)
{
    static const double rates[] = { 60.0, 75.0, 100.0, 119.996, 120.0, 144.0, 165.0, 180.0, 240.0 };
    static const char *modes[3] = { "variable refresh", "V-Sync", "composited V-Sync" };
    for (int vsync = 2; vsync >= 0; vsync--)
        for (int smooth = 1; smooth >= 0; smooth--)
            for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
                for (int d = 0; d <= 8; d += 8) {
                    PaceSim s = pace_simulate(rates[i], vsync, smooth, d, 20.0);
                    double var = s.speed_max / s.speed_min - 1.0;
                    printf("clock %s%s %.2f Hz, drawing 0-%d ms: %ld images, %.2f frames/s, step %.3f-%.3f "
                           "(%.2f%%), late %ld, without the frame %ld, jumps %ld/%ld, same refresh %ld\n",
                           modes[vsync], smooth ? "" : " (not smooth)", rates[i], d, s.images,
                           s.fps, s.speed_min, s.speed_max, var * 100.0, s.late, s.holds, s.jumps, s.skips,
                           s.same_refresh);
                    CHECK(var < 0.02 && s.late == 0 && s.holds == 0 && s.jumps == 0 && s.skips == 0 &&
                              s.same_refresh == 0 && fabs(s.fps - 60.0) < 0.6,
                          "clock %s%s at %.2f Hz, drawing 0-%d ms: step varying %.2f%% or frame skipped/repeated",
                          modes[vsync], smooth ? "" : " not smooth", rates[i], d, var * 100.0);
                }
    /* At 2560x1440 and 180 Hz the new frame's image took up to ~6.5 ms and the others ~2: with a single estimate,
     * all of them went one refresh later and the frame got fewer than 3 images. With drawing finishing before
     * delivery, up to ~5 ms fits (measured later: 2-3.5 ms, rarely more than 5); also with a composited screen
     * (before: 26-37% of the frames off the pacing in the temple) */
    for (int vsync = 1; vsync <= 2; vsync++) {
        PaceSim s = pace_simulate(180.0, vsync, 1, 4.5, 20.0);
        double per = s.images / (s.fps * 19.0);
        printf("clock %s 180 Hz, new frame 0-4.5 ms and the others 0-1.5 ms: %.2f images per frame\n", modes[vsync],
               per);
        CHECK(per > 2.95, "clock %s at 180 Hz with a heavy new frame: %.2f images per frame (expected 3)",
              modes[vsync], per);
    }
    /* Isolated spikes do not delay the following images (with the 2nd largest as the estimate: 2.52 per frame) */
    PaceSim s = pace_simulate(180.0, 2, 1, -1.0, 20.0);
    double per = s.images / (s.fps * 19.0);
    printf("clock composited V-Sync 180 Hz, new frame 3-4 ms with 5%% of 7-10 ms spikes: %.2f images per frame\n",
           per);
    CHECK(per > 2.8, "composited clock at 180 Hz with spikes: %.2f images per frame (expected ~2.9)", per);
}

static void test_video(const char *work)
{
    /* Pacing: 60/120/180/240 Hz (and 59.94/119.88) smooth; 144, 165, 75, 100 not */
    static const struct { float hz; int n; } pace[] = {
        { 60.0f, 1 }, { 59.94f, 1 }, { 60.05f, 1 }, { 120.0f, 2 }, { 119.88f, 2 }, { 180.0f, 3 }, { 240.0f, 4 },
        { 239.76f, 4 }, { 144.0f, 0 }, { 143.98f, 0 }, { 165.0f, 0 }, { 75.0f, 0 }, { 100.0f, 0 }, { 50.0f, 0 },
        { 360.0f, 0 }, { 0.0f, 0 },
    };
    for (size_t i = 0; i < sizeof(pace) / sizeof(pace[0]); i++)
        CHECK(video_refreshes_per_frame(pace[i].hz) == pace[i].n, "pacing at %.2f Hz: %d (expected %d)", pace[i].hz,
              video_refreshes_per_frame(pace[i].hz), pace[i].n);

    test_pacing();

    /* The compositor's refresh rate fluctuates (measured: 119.1-120.5 Hz, 50 ms dips to 99-117 Hz, which recover);
     * none of that redoes the pacing; a real change (120 -> 60 Hz) redoes it once, 1 s later */
    PaceRateWatch watch = { 0 };
    uint32_t seed = 12345;
    int changes = 0;
    double t = 1e9, in_use = 120.0;
    for (int i = 0; i < 120 * 30; i++, t += 1e9 / 120.0) {
        seed = seed * 1103515245u + 12345u;
        double hz = 119.1 + (seed >> 16) % 141 / 100.0;
        if (i % 600 >= 300 && i % 600 < 306)
            hz = 99.0 + (seed >> 8) % 19; /* 50 ms dip every 5 s */
        if (i >= 1200 && i < 1440 && i % 2)
            hz = 100.0 + (seed >> 8) % 17; /* 2 s with half of the readings at 100-117 Hz */
        changes += pace_rate_changed(&watch, hz, in_use, (uint64_t)t);
    }
    CHECK(changes == 0, "fluctuating refresh rate: %d pacing changes (expected 0)", changes);
    int at = -1;
    for (int i = 0; i < 240 && at < 0; i++, t += 1e9 / 60.0)
        if (pace_rate_changed(&watch, 60.0, in_use, (uint64_t)t))
            at = i;
    /* The highest reading holds for 0.5-1 s, then 1 s off */
    CHECK(at >= 89 && at <= 121, "120 -> 60 Hz: pacing redone at reading %d (expected 90-120, 1.5-2 s)", at);
    CHECK(fabs(pace_rate_hz(&watch) - 60.0) < 0.01, "120 -> 60 Hz: pacing redone with %.2f Hz", pace_rate_hz(&watch));
    CHECK(!pace_rate_changed(&watch, 0.0, in_use, (uint64_t)t), "no reading: does not redo");

    /* 180 Hz with G-Sync (measured: 144-180 Hz, the real one shows up every ~0.1-0.25 s; stretches of more than 1 s
     * entirely below 1% used to redo the pacing at 175.77 Hz); nothing redoes it. A real 180 -> 120 Hz redoes it once,
     * at 120. */
    PaceRateWatch gsync = { 0 };
    changes = 0;
    in_use = 179.97;
    for (int i = 0; i < 180 * 30; i++, t += 1e9 / 180.0) {
        seed = seed * 1103515245u + 12345u;
        double hz = (seed >> 16) % 20 ? 144.0 + (seed >> 8) % 3300 / 100.0 : 179.85 + (seed >> 12) % 16 / 100.0;
        changes += pace_rate_changed(&gsync, hz, in_use, (uint64_t)t);
    }
    CHECK(changes == 0, "G-Sync at 180 Hz: %d pacing changes (expected 0)", changes);
    CHECK(pace_rate_hz(&gsync) > 179.8, "G-Sync at 180 Hz: highest reading %.2f Hz", pace_rate_hz(&gsync));
    at = -1;
    for (int i = 0; i < 480 && at < 0; i++, t += 1e9 / 120.0)
        if (pace_rate_changed(&gsync, 118.0 + (i % 5) * 0.5, in_use, (uint64_t)t))
            at = i;
    CHECK(at >= 179 && at <= 241 && fabs(pace_rate_hz(&gsync) - 120.0) < 0.01,
          "180 -> 120 Hz: pacing redone at reading %d at %.2f Hz (expected 180-240, 120)", at, pace_rate_hz(&gsync));

    /* Fullscreen: auto = the Windows one (without changing the monitor mode) */
    VideoSettings v;
    video_defaults(&v);
    int excl = -1;
    VideoMode m = video_fullscreen_mode(&g_modes, &v, &excl);
    CHECK(m.w == 2560 && m.h == 1440 && (int)(m.hz + 0.5f) == 144 && !excl, "auto at 144 Hz: stays 144, borderless");
    v.refresh = 144;
    m = video_fullscreen_mode(&g_modes, &v, &excl);
    CHECK((int)(m.hz + 0.5f) == 144 && !excl, "144 chosen = the Windows one: borderless (%.2f, %d)", m.hz, excl);
    v.refresh = 0;
    v.res_w = 1920;
    v.res_h = 1080;
    m = video_fullscreen_mode(&g_modes, &v, &excl);
    CHECK(m.w == 1920 && (int)(m.hz + 0.5f) == 144 && excl, "1920x1080 auto: 144 Hz (the closest to the Windows one)");
    v.res_w = 1280;
    v.res_h = 720;
    m = video_fullscreen_mode(&g_modes, &v, &excl);
    CHECK(m.w == 2560 && (int)(m.hz + 0.5f) == 144 && !excl, "resolution the monitor does not have: the Windows one");
    VideoModes only144 = { { 1920, 1080, 144.0f }, { { 1920, 1080, 144.0f }, { 1920, 1080, 100.0f } }, 2 };
    video_defaults(&v);
    m = video_fullscreen_mode(&only144, &v, &excl);
    CHECK((int)m.hz == 144 && !excl, "keeps the Windows one, borderless");
    v.refresh = 100;
    m = video_fullscreen_mode(&only144, &v, &excl);
    CHECK((int)m.hz == 100 && excl, "refresh rate chosen in the menu: changes the mode");
    v.refresh = 0;
    VideoModes at60 = { { 1920, 1080, 60.0f }, { { 1920, 1080, 240.0f }, { 1920, 1080, 60.0f } }, 2 };
    m = video_fullscreen_mode(&at60, &v, &excl);
    CHECK((int)m.hz == 60 && !excl, "Windows already at 60 Hz: no change");
    VideoModes none = { { 1600, 900, 75.0f }, { { 0 } }, 0 };
    m = video_fullscreen_mode(&none, &v, &excl);
    CHECK(m.w == 1600 && !excl, "no list: the Windows one");

    VideoMode res[8];
    int rates[8];
    CHECK(video_resolutions(&g_modes, res, 8) == 2 && res[0].w == 2560 && res[1].w == 1920, "resolution list");
    int n = video_rates(&g_modes, 1920, 1080, rates, 8);
    CHECK(n == 2 && rates[0] == 144 && rates[1] == 60, "refresh rates of 1920x1080");

    /* Framing: 8:7 pixels filling the screen */
    static const struct { int ow, oh, sw, div, x, y, w, h, kx, ky; } lay[] = {
        { 1920, 1080, 256, 4, 307, 0, 1306, 1080, 6, 5 },  /* 1080/242 = 4.46: scales up 6x5 and down */
        { 2560, 1440, 256, 4, 409, 0, 1741, 1440, 7, 6 },
        { 3840, 2160, 256, 4, 614, 0, 2611, 2160, 11, 9 },
        { 878, 726, 256, 4, 0, 0, 878, 726, 4, 3 },         /* 3x window: exact */
        { 1000, 300, 341, 3, 319, 0, 362, 300, 2, 2 },      /* 341 pixels of 3 cycles: almost the same width */
        { 600, 1000, 256, 4, 0, 252, 600, 496, 3, 3 },      /* narrow window: bars on top and bottom */
    };
    for (size_t i = 0; i < sizeof(lay) / sizeof(lay[0]); i++) {
        VideoRect d;
        int kx, ky;
        video_layout(lay[i].ow, lay[i].oh, lay[i].sw, 242, lay[i].div, NULL, &d, &kx, &ky);
        CHECK(d.x == lay[i].x && d.y == lay[i].y && d.w == lay[i].w && d.h == lay[i].h && kx == lay[i].kx &&
                  ky == lay[i].ky,
              "framing %dx%d: %d,%d %dx%d k %dx%d", lay[i].ow, lay[i].oh, d.x, d.y, d.w, d.h, kx, ky);
    }
    int ww, wh;
    video_defaults(&v);
    video_window_size(&v, &ww, &wh);
    CHECK(ww == 878 && wh == 726, "3x window: %dx%d", ww, wh);

    /* Configuration: default written, reading, invalid values */
    char path[1024];
    snprintf(path, sizeof(path), "%s/video.ini", work);
    write_text(path, "game_folder=x\n");
    Config cfg;
    config_load(&cfg, path);
    CHECK(video_load(&cfg, &v) == 1 && v.fullscreen && !v.res_w && !v.refresh && v.vsync && v.window_scale == 3,
          "default: fullscreen, desktop, auto, vsync, 3x");
    CHECK(strcmp(config_get(&cfg, "resolution", ""), "desktop") == 0 &&
              strcmp(config_get(&cfg, "refresh_rate", ""), "auto") == 0,
          "default written to the .ini");
    v.fullscreen = 0;
    v.res_w = 1920;
    v.res_h = 1080;
    v.refresh = 120;
    v.vsync = 0;
    v.window_scale = 5;
    v.integer_scale = 1;
    v.smooth = 1;
    v.crt = VIDEO_CRT_MASK;
    v.aspect = VIDEO_ASPECT_STRETCH;
    v.crop = CROP_FULL;
    v.hud_bg = CROP_HUD_OXBLOOD;
    v.crt_bright = 1;
    video_store(&cfg, &v);
    CHECK(strcmp(config_get(&cfg, "filter", ""), "smooth") == 0 && strcmp(config_get(&cfg, "crt", ""), "mask") == 0 &&
              strcmp(config_get(&cfg, "aspect", ""), "stretch") == 0 && strcmp(config_get(&cfg, "crop", ""), "full") == 0 &&
              strcmp(config_get(&cfg, "hud_background", ""), "oxblood") == 0 &&
              strcmp(config_get(&cfg, "crt_brightness", ""), "bright") == 0,
          "image options written by name");
    config_save(&cfg, path);
    VideoSettings back;
    config_load(&cfg, path);
    CHECK(video_load(&cfg, &back) == 0 && memcmp(&back, &v, sizeof(v)) == 0, "reads what it wrote");
    config_set(&cfg, "crop", "hud");
    video_load(&cfg, &back);
    CHECK(back.crop == CROP_COMPACT, "crop=hud (the old name) becomes compact");
    config_set(&cfg, "hud_background", "parchment");
    video_load(&cfg, &back);
    CHECK(back.hud_bg == CROP_HUD_LEATHER, "hud_background=parchment (removed flat color) becomes leather");
    config_set(&cfg, "hud_background", "moondoria");
    video_load(&cfg, &back);
    CHECK(back.hud_bg == CROP_HUD_MIDNIGHT, "hud_background=moondoria (light in the center, removed) becomes midnight");
    config_set(&cfg, "hud_background", "noltia");
    video_load(&cfg, &back);
    CHECK(back.hud_bg == CROP_HUD_DEEPSEA, "hud_background=noltia (light, removed) becomes deepsea");
    {
        /* CRT values: each one written by name and read back */
        static const char *const names[VIDEO_CRT_COUNT] = { "off",    "scanlines", "mask",      "slot",
                                                            "shadow", "glow",      "glow_mask", "glow_slot" };
        int crt_ok = 0;
        for (int i = 0; i < VIDEO_CRT_COUNT; i++) {
            VideoSettings c = v;
            c.crt = i;
            video_store(&cfg, &c);
            video_load(&cfg, &back);
            crt_ok += strcmp(config_get(&cfg, "crt", ""), names[i]) == 0 && back.crt == i;
        }
        CHECK(crt_ok == VIDEO_CRT_COUNT, "crt: %d of %d names written and read", crt_ok, VIDEO_CRT_COUNT);
        video_store(&cfg, &v);
    }
    {
        /* Stretched: the whole output; with integer scale, the largest integer in each direction, centered */
        VideoRect d;
        int kx, ky;
        VideoSettings st;
        video_defaults(&st);
        st.aspect = VIDEO_ASPECT_STRETCH;
        video_layout(2560, 1440, 288, 166, 3, &st, &d, &kx, &ky);
        CHECK(d.x == 0 && d.y == 0 && d.w == 2560 && d.h == 1440, "Stretch: the whole screen (%d %d %d %d)", d.x, d.y,
              d.w, d.h);
        st.integer_scale = 1;
        video_layout(2560, 1440, 288, 166, 3, &st, &d, &kx, &ky);
        CHECK(kx == 8 && ky == 8 && d.w == 2304 && d.h == 1328 && d.x == 128 && d.y == 56,
              "Stretch with integer scale: %dx%d (%d %d %d %d)", kx, ky, d.x, d.y, d.w, d.h);
    }
    write_text(path, "fullscreen=7\nresolution=abc\nrefresh_rate=-5\nvsync=x\nwindow_scale=99\n"
                     "integer_scale=3\nfilter=blur\ncrt=tv\naspect=wide\ncrop=all\nhud_background=pink\n");
    config_load(&cfg, path);
    video_load(&cfg, &back);
    CHECK(back.fullscreen == 1 && !back.res_w && back.refresh == 24 && back.vsync == 1 && back.window_scale == 6,
          "invalid: %d %d %d %d %d", back.fullscreen, back.res_w, back.refresh, back.vsync, back.window_scale);
    CHECK(back.integer_scale == 1 && !back.smooth && back.crt == VIDEO_CRT_OFF && back.aspect == VIDEO_ASPECT_TV && back.crop == CROP_FRAME &&
              back.hud_bg == CROP_HUD_BLACK, /* unknown value: the 1st of the list (the default applies only without the key) */
          "invalid image: %d %d %d %d %d %d", back.integer_scale, back.smooth, back.crt, back.aspect, back.crop,
          back.hud_bg);
    CHECK(video_same_display(&back, &back), "same mode");
    v = back;
    v.crt = VIDEO_CRT_SCANLINES;
    v.smooth = 1;
    CHECK(video_same_display(&back, &v), "image options do not change the screen mode");
    v.vsync = 0;
    CHECK(!video_same_display(&back, &v), "V-Sync changes it");
    printf("video: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- image options ---- */

#define IMG_W 256
#define IMG_H 242

/* Test image: color bars on top; 1-pixel checkerboard (left) and gradient (right) in the middle;
 * at the bottom flat white (left, to measure the CRT) and pixels of random colors (right). */
static void image_source(uint32_t *px)
{
    static const uint32_t bars[8] = { 0xFFFFFFFFu, 0xFFFFFF00u, 0xFF00FFFFu, 0xFF00FF00u,
                                      0xFFFF00FFu, 0xFFFF0000u, 0xFF0000FFu, 0xFF000000u };
    uint32_t seed = 12345;
    for (int y = 0; y < IMG_H; y++)
        for (int x = 0; x < IMG_W; x++) {
            uint32_t c;
            if (y < 60)
                c = bars[x * 8 / IMG_W];
            else if (y < 120 && x < 128)
                c = (x + y) & 1 ? 0xFFFFFFFFu : 0xFF000000u;
            else if (y < 120) {
                uint32_t g = (uint32_t)(x - 128) * 2;
                c = 0xFF000000u | g << 16 | g << 8 | g;
            } else if (x < 128)
                c = 0xFFFFFFFFu;
            else {
                seed = seed * 1103515245u + 12345u;
                c = 0xFF000000u | (seed >> 8 & 0xFFFFFF);
            }
            px[y * IMG_W + x] = c;
        }
}

static Uint32 out_px(const SDL_Surface *s, int x, int y)
{
    return ((const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch))[x] | 0xFF000000u;
}

static int channel_diff(Uint32 a, Uint32 b)
{
    int d = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int k = abs((int)(a >> sh & 0xFF) - (int)(b >> sh & 0xFF));
        d = k > d ? k : d;
    }
    return d;
}

/* Draws with options v on an output the size of the surface; writes a BMP and returns the CRC */
static Uint32 image_shot(SDL_Surface *s, SDL_Renderer *r, SDL_Texture *t, Presenter *p, const VideoSettings *v,
                         const char *dir, const char *name)
{
    present_draw(p, r, t, IMG_W, IMG_H, 1, 4, v);
    SDL_FlushRenderer(r);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    SDL_SaveBMP(s, path);
    Uint32 crc = 0;
    for (int y = 0; y < s->h; y++)
        crc = SDL_crc32(crc, (const Uint8 *)s->pixels + y * s->pitch, (size_t)s->w * 4);
    return crc;
}

/* Pixels that differ from the original at the center of each image pixel (with tolerance tol) */
static int center_mismatches(const SDL_Surface *s, const VideoRect *d, const uint32_t *src, int tol)
{
    int bad = 0;
    for (int sy = 0; sy < IMG_H; sy++)
        for (int sx = 0; sx < IMG_W; sx++) {
            int x = d->x + (int)((sx + 0.5) * d->w / IMG_W), y = d->y + (int)((sy + 0.5) * d->h / IMG_H);
            bad += channel_diff(out_px(s, x, y), src[sy * IMG_W + sx]) > tol;
        }
    return bad;
}

/* On the checkerboard: screen pixels that are neither black nor white (smoothing halftones) */
static int checker_halftones(const SDL_Surface *s, const VideoRect *d)
{
    int n = 0;
    for (int y = d->y + d->h * 62 / IMG_H; y < d->y + d->h * 118 / IMG_H; y++)
        for (int x = d->x + d->w * 2 / IMG_W; x < d->x + d->w * 126 / IMG_W; x++) {
            Uint32 c = out_px(s, x, y) & 0xFFFFFF;
            n += c != 0 && c != 0xFFFFFF;
        }
    return n;
}

/* Dominant color of a pixel (0 red, 1 green, 2 blue; -1 none exceeds the others by more than 20) */
static int strong_channel(Uint32 c)
{
    int ch[3] = { (int)(c >> 16 & 0xFF), (int)(c >> 8 & 0xFF), (int)(c & 0xFF) };
    for (int i = 0; i < 3; i++)
        if (ch[i] > ch[(i + 1) % 3] + 20 && ch[i] > ch[(i + 2) % 3] + 20)
            return i;
    return -1;
}

/* New CRT values on a 2560x1440 output (the grille with 2 screen pixels per color; shadow still with 1):
 * aperture, slot (staggered bricks), shadow (dots in a triangle) and the diffuse glow */
static void test_image_crt_big(const uint32_t *src, const char *work)
{
    SDL_Surface *s = SDL_CreateSurface(2560, 1440, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    SDL_Texture *t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IMG_W, IMG_H) : NULL;
    CHECK(t != NULL, "CRT 1440: renderer");
    if (!t) {
        if (r)
            SDL_DestroyRenderer(r);
        if (s)
            SDL_DestroySurface(s);
        return;
    }
    SDL_UpdateTexture(t, NULL, src, IMG_W * (int)sizeof(uint32_t));
    Presenter p;
    SDL_zero(p);
    VideoSettings v;
    video_defaults(&v);
    VideoRect d;
    int kx, ky;
    video_layout(2560, 1440, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    int white_x = d.x + d.w * 40 / IMG_W, white_y = d.y + (int)((180 + 0.5) * d.h / IMG_H);
    Uint32 crc[6];

    /* Lines+mask: 1-pixel columns, repeating every 3, as at any other resolution */
    v.crt = VIDEO_CRT_MASK;
    crc[0] = image_shot(s, r, t, &p, &v, work, "9_crt_mask_1440");
    int ok = 0;
    for (int x = white_x; x < white_x + 300; x++) {
        Uint32 c = out_px(s, x, white_y);
        ok += strong_channel(c) >= 0 && c == out_px(s, x + 3, white_y) &&
              strong_channel(out_px(s, x + 1, white_y)) != strong_channel(c) &&
              strong_channel(out_px(s, x - 1, white_y)) != strong_channel(c);
    }
    CHECK(ok == 300, "1440 Lines+mask: %d of 300 1-pixel columns, repeating every 3", ok);

    /* Slot: repeats every 12 columns; the gap of the 1st triad and that of the 2nd on different lines */
    v.crt = VIDEO_CRT_SLOT;
    crc[1] = image_shot(s, r, t, &p, &v, work, "9_crt_slot_1440");
    int period_bad = 0, gap_a = 0, gap_b = 0, top = d.y + d.h * 140 / IMG_H, bottom = d.y + d.h * 220 / IMG_H;
    int xa = d.x + 12 * ((white_x - d.x) / 12), xb = xa + 6; /* red of the 1st and the 2nd triad */
    for (int y = top; y < bottom; y++) {
        for (int x = white_x; x < white_x + 200; x++)
            period_bad += out_px(s, x, y) != out_px(s, x + 12, y);
        int ra = (int)(out_px(s, xa, y) >> 16 & 0xFF), rb = (int)(out_px(s, xb, y) >> 16 & 0xFF);
        gap_a += ra + 10 < rb;
        gap_b += rb + 10 < ra;
    }
    CHECK(period_bad == 0 && gap_a > 20 && gap_b > 20 && strong_channel(out_px(s, xa, white_y)) == 0,
          "1440 Slot: %d pixels off the period of 12, gap in the 1st triad on %d lines, in the 2nd on %d", period_bad, gap_a,
          gap_b);

    /* Shadow: 2-column colors, repeating every 6; 2 lines below, half a triad (3 columns) ahead */
    v.crt = VIDEO_CRT_SHADOW;
    crc[2] = image_shot(s, r, t, &p, &v, work, "9_crt_shadow_1440");
    int tri = 0, n = 0;
    for (int y = top; y < top + 40; y++)
        for (int x = white_x; x < white_x + 120; x++, n++) {
            int c = strong_channel(out_px(s, x, y));
            tri += c >= 0 && c == strong_channel(out_px(s, x + 6, y)) && c == strong_channel(out_px(s, x + 3, y + 2)) &&
                   c != strong_channel(out_px(s, x, y + 2)) && c != strong_channel(out_px(s, x + 2, y));
        }
    CHECK(tri == n, "1440 Shadow: %d of %d pixels in the triangle", tri, n);

    /* Glow: the bright gets brighter and bleeds into the dark next to it (the black bar beside the blue), but not far */
    v.crt = VIDEO_CRT_SCANLINES;
    image_shot(s, r, t, &p, &v, work, "9_crt_scanlines_1440");
    int bar_y = d.y + (int)((30 + 0.5) * d.h / IMG_H), near_x = d.x + d.w * 225 / IMG_W, far_x = d.x + d.w * 250 / IMG_W;
    int near_lines = (int)(out_px(s, near_x, bar_y) & 0xFF);
    double white_lines = 0, white_glow = 0;
    for (int y = top; y < bottom; y++)
        white_lines += out_px(s, white_x, y) >> 8 & 0xFF;
    v.crt = VIDEO_CRT_GLOW;
    crc[3] = image_shot(s, r, t, &p, &v, work, "9_crt_glow_1440");
    for (int y = top; y < bottom; y++)
        white_glow += out_px(s, white_x, y) >> 8 & 0xFF;
    white_lines /= bottom - top;
    white_glow /= bottom - top;
    int near_glow = (int)(out_px(s, near_x, bar_y) & 0xFF), far_glow = (int)(out_px(s, far_x, bar_y) & 0xFF);
    int border = (int)(out_px(s, d.x / 2, white_y) & 0xFFFFFF);
    printf("CRT glow 1440: white %.0f (lines only %.0f); black beside the blue %d (lines only %d), far %d\n", white_glow,
           white_lines, near_glow, near_lines, far_glow);
    int white_dark = 255; /* the darkest line of the white: the lines stay visible */
    for (int y = top; y < bottom; y++)
        white_dark = SDL_min(white_dark, (int)(out_px(s, white_x, y) >> 8 & 0xFF));
    printf("CRT glow 1440: darkest line of the white %d\n", white_dark);
    CHECK(white_glow > white_lines + 10 && white_dark < 230 && near_lines == 0 && near_glow >= 5 && far_glow <= 2 &&
              border == 0,
          "1440 Glow: white %.0f (lines %.0f), beside %d, far %d, border %06X", white_glow, white_lines, near_glow,
          far_glow, (unsigned)border);
    v.crt = VIDEO_CRT_GLOW_MASK;
    crc[4] = image_shot(s, r, t, &p, &v, work, "9_crt_glow_mask_1440");
    v.crt = VIDEO_CRT_GLOW_SLOT;
    crc[5] = image_shot(s, r, t, &p, &v, work, "9_crt_glow_slot_1440");

    static const struct { const char *name; Uint32 want; } crcs[6] = {
        { "mask_1440", 0x3A2D96C3 }, { "slot_1440", 0x0ED7ED1C },      { "shadow_1440", 0xB2F836C6 },
        { "glow_1440", 0xAA16B665 }, { "glow_mask_1440", 0xC0F9F7A0 }, { "glow_slot_1440", 0xE218176D },
    };
    for (int i = 0; i < 6; i++) {
        printf("image %s: CRC %08X\n", crcs[i].name, (unsigned)crc[i]);
        CHECK(!crcs[i].want || crc[i] == crcs[i].want, "image %s changed (CRC %08X, expected %08X)", crcs[i].name,
              (unsigned)crc[i], (unsigned)crcs[i].want);
    }
    present_free(&p);
    SDL_DestroyTexture(t);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(s);
}

static void test_image(const char *work)
{
    SDL_Surface *s = SDL_CreateSurface(1920, 1080, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    SDL_Texture *t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, IMG_H) : NULL;
    if (!t) {
        CHECK(0, "software renderer: %s", SDL_GetError());
        return;
    }
    static uint32_t src[IMG_W * IMG_H];
    image_source(src);
    SDL_Rect area = { 0, 0, IMG_W, IMG_H };
    SDL_UpdateTexture(t, &area, src, IMG_W * (int)sizeof(uint32_t));
    Presenter p = { 0 };
    VideoSettings v;
    VideoRect d;
    int kx, ky;

    /* Default (sharp, 8:7): the center of each pixel has the exact color; few halftones */
    video_defaults(&v);
    Uint32 crc_sharp = image_shot(s, r, t, &p, &v, work, "1_sharp");
    video_layout(1920, 1080, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    CHECK(center_mismatches(s, &d, src, 0) == 0, "sharp: pixel centers with the exact color (%d different)",
          center_mismatches(s, &d, src, 0));
    int half_sharp = checker_halftones(s, &d);

    /* Smooth: close to the original at the center, many more halftones */
    v.smooth = 1;
    Uint32 crc_smooth = image_shot(s, r, t, &p, &v, work, "2_smooth");
    int half_smooth = checker_halftones(s, &d);
    CHECK(center_mismatches(s, &d, src, 64) < IMG_W * IMG_H / 50, "smooth: center close to the original (%d far)",
          center_mismatches(s, &d, src, 64));
    CHECK(half_smooth > 3 * half_sharp && half_smooth > d.w * d.h / 20, "smooth: halftones %d (sharp %d)",
          half_smooth, half_sharp);

    /* Integer scale: 5x4 screen pixels per pixel (1280x968), all exact; outside, black */
    video_defaults(&v);
    v.integer_scale = 1;
    Uint32 crc_integer = image_shot(s, r, t, &p, &v, work, "3_integer");
    video_layout(1920, 1080, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    CHECK(kx == 5 && ky == 4 && d.w == 1280 && d.h == 968 && d.x == 320 && d.y == 56, "integer: %dx%d at %d,%d k %dx%d",
          d.w, d.h, d.x, d.y, kx, ky);
    int bad = 0;
    for (int y = 0; y < 1080; y++)
        for (int x = 0; x < 1920; x++) {
            int in = x >= d.x && x < d.x + d.w && y >= d.y && y < d.y + d.h;
            Uint32 want = in ? src[(y - d.y) / ky * IMG_W + (x - d.x) / kx] : 0xFF000000u;
            bad += out_px(s, x, y) != want;
        }
    CHECK(bad == 0, "integer: %d screen pixels out of place", bad);

    /* Square pixels: 1142x1080 (1080 * 256 / 242 = 1142.5); with integer scale, 4x4 */
    video_defaults(&v);
    v.aspect = VIDEO_ASPECT_SQUARE;
    Uint32 crc_square = image_shot(s, r, t, &p, &v, work, "4_square");
    video_layout(1920, 1080, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    CHECK(d.w == 1142 && d.h == 1080 && d.x == 389,"square: %dx%d at %d", d.w, d.h, d.x);
    CHECK(center_mismatches(s, &d, src, 0) == 0, "square: exact center");
    v.integer_scale = 1;
    video_layout(1920, 1080, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    CHECK(kx == 4 && ky == 4 && d.w == 1024 && d.h == 968, "square integer: %dx%d k %dx%d", d.w, d.h, kx, ky);

    /* CRT, scanlines: on flat white, each line bright in the middle and dark at the edges */
    video_defaults(&v);
    v.crt = VIDEO_CRT_SCANLINES;
    Uint32 crc_lines = image_shot(s, r, t, &p, &v, work, "5_crt_scanlines");
    video_layout(1920, 1080, IMG_W, IMG_H, 4, &v, &d, &kx, &ky);
    int x = d.x + d.w * 50 / IMG_W, lines_ok = 0;
    for (int sy = 130; sy < 230; sy++) {
        int top = d.y + (int)((double)sy * d.h / IMG_H), mid = d.y + (int)((sy + 0.5) * d.h / IMG_H);
        int b_top = (int)(out_px(s, x, top) & 0xFF), b_mid = (int)(out_px(s, x, mid) & 0xFF);
        lines_ok += b_mid >= 240 && b_top <= 200 && out_px(s, x, mid) == out_px(s, x + 1, mid);
    }
    CHECK(lines_ok == 100, "scanlines: %d of 100 lines bright in the middle and dark at the edge", lines_ok);

    /* CRT, lines + grille: screen columns alternating red, green, blue (every 3) */
    v.crt = VIDEO_CRT_MASK;
    Uint32 crc_mask = image_shot(s, r, t, &p, &v, work, "6_crt_mask");
    int y = d.y + (int)((180 + 0.5) * d.h / IMG_H), mask_ok = 0;
    for (x = d.x + 30; x < d.x + 330; x++) {
        Uint32 c = out_px(s, x, y);
        int rr = c >> 16 & 0xFF, gg = c >> 8 & 0xFF, bb = c & 0xFF;
        int one_strong = (rr > gg + 20 && rr > bb + 20) + (gg > rr + 20 && gg > bb + 20) + (bb > rr + 20 && bb > gg + 20);
        mask_ok += one_strong == 1 && c == out_px(s, x + 3, y) && c != out_px(s, x + 1, y);
    }
    CHECK(mask_ok == 300, "grille: %d of 300 columns with one dominant color, repeating every 3", mask_ok);
    /* Bright CRT (crt_brightness=bright): on flat white, much brighter than normal, lines still visible */
    {
        double sum[2] = { 0, 0 };
        int lines_vis = 0;
        for (int b = 0; b < 2; b++) {
            v.crt_bright = b;
            image_shot(s, r, t, &p, &v, work, b ? "6b_crt_mask_bright" : "6_crt_mask");
            for (int yy = d.y + d.h * 140 / IMG_H; yy < d.y + d.h * 220 / IMG_H; yy++)
                for (int xx = d.x + d.w * 20 / IMG_W; xx < d.x + d.w * 100 / IMG_W; xx++) {
                    Uint32 c = out_px(s, xx, yy);
                    sum[b] += ((c >> 16 & 0xFF) + (c >> 8 & 0xFF) + (c & 0xFF)) / 3.0;
                }
        }
        int xx = d.x + d.w * 50 / IMG_W;
        for (int sy = 140; sy < 220; sy++) {
            int top = d.y + (int)((double)sy * d.h / IMG_H), mid = d.y + (int)((sy + 0.5) * d.h / IMG_H);
            lines_vis += (out_px(s, xx, mid) & 0xFF00) > (out_px(s, xx, top) & 0xFF00);
        }
        int n = (d.h * 80 / IMG_H) * (d.w * 80 / IMG_W);
        double avg0 = sum[0] / n, avg1 = sum[1] / n;
        printf("CRT on white: normal %.0f, bright %.0f (of 255)\n", avg0, avg1);
        CHECK(avg1 > avg0 + 30 && avg1 > 215 && lines_vis >= 70, "bright CRT: average %.0f (normal %.0f), lines %d of 80",
              avg1, avg0, lines_vis);
        v.crt_bright = 0;
    }

    /* All together (to check by eye) */
    v.integer_scale = 1;
    v.smooth = 1;
    Uint32 crc_all = image_shot(s, r, t, &p, &v, work, "7_integer_smooth_mask");

    /* Checked by eye the first time (.bmp files in build/app_output/image); from then on by CRC */
    static const struct { const char *name; Uint32 want; } crcs[] = {
        { "sharp", 0x9D3A485A }, { "smooth", 0x56AFFCC9 }, { "integer", 0x2AA436D8 }, { "square", 0xDEE5155A },
        { "crt_scanlines", 0xE484D77D }, { "crt_mask", 0x394A3AA6 }, { "integer_smooth_mask", 0x371955C5 },
    };
    Uint32 got[7] = { crc_sharp, crc_smooth, crc_integer, crc_square, crc_lines, crc_mask, crc_all };
    for (int i = 0; i < 7; i++) {
        printf("image %s: CRC %08X\n", crcs[i].name, (unsigned)got[i]);
        CHECK(!crcs[i].want || got[i] == crcs[i].want, "image %s changed (CRC %08X, expected %08X)", crcs[i].name,
              (unsigned)got[i], (unsigned)crcs[i].want);
    }
    present_free(&p);
    SDL_DestroyTexture(t);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(s);

    /* 2x window (484 lines, 2 screen pixels per line): no CRT lines, white stays white */
    s = SDL_CreateSurface(586, 484, SDL_PIXELFORMAT_ARGB8888);
    r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IMG_W, IMG_H) : NULL;
    if (t) {
        SDL_UpdateTexture(t, NULL, src, IMG_W * (int)sizeof(uint32_t));
        video_defaults(&v);
        v.crt = VIDEO_CRT_SCANLINES;
        image_shot(s, r, t, &p, &v, work, "8_crt_janela_2x");
        int dark = 0;
        for (y = 130 * 2; y < 230 * 2; y++)
            dark += (out_px(s, 100, y) & 0xFFFFFF) != 0xFFFFFF;
        CHECK(dark == 0, "2x window: %d darkened pixels on the white (expected none)", dark);
        present_free(&p);
        SDL_DestroyTexture(t);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(s);
    test_image_crt_big(src, work);
    printf("image: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- border crop ---- */

#define CROP_W 341
#define CROP_H 242

/* Fake image with the shape of the Ys border (measurements from app/crop.h): outlines of color c, border
 * ornament, black line around the map, map, panel and the overscan repeating the top. Every pixel that is not
 * black has a color tied to its position (to check where each one ended up). */
static void crop_source(uint32_t *px, uint32_t c)
{
    for (int y = 0; y < CROP_H; y++)
        for (int x = 0; x < CROP_W; x++) {
            uint32_t pos = 0xFF000000u | (uint32_t)(x * 0x0101 + y * 0x10300) | 0x10;
            int frame = x >= 8 && x <= 327 && y >= 11 && y <= 170;
            int map = x >= 24 && x <= 311 && y >= 27 && y <= 154;
            int gap = frame && !map && x >= 23 && x <= 312 && y >= 26 && y <= 155;
            int outline = frame && (y == 11 || y == 170 || x == 8 || x == 327 ||
                                    ((y == 25 || y == 156) && x >= 23 && x <= 312));
            uint32_t v = 0xFF000000u;
            if (outline && !((x == 8 || x == 327) && (y == 11 || y == 170)))
                v = 0xFF000000u | c;
            else if (map || (frame && !gap && !outline))
                v = pos;
            else if (x >= 8 && x <= 327 && y >= 179 && y <= 219 && (x + y) % 3)
                v = pos;
            px[y * CROP_W + x] = v;
        }
    for (int y = 235; y < CROP_H; y++)
        memcpy(px + y * CROP_W, px + (y - 224) * CROP_W, CROP_W * sizeof(uint32_t));
}

/* The panel cells as the game writes them (app/ys_panel.h) for a test panel drawn in img (width CROP_W): n labels
 * with the lit columns it[i] = { l0, l1, v0, v1 } (v0 < 0 = no number) and the 1st letter kind[i]; the 1st number
 * with "/" if slash; the numbers in palette num_pal; the two bars (sides at cells 7 and 39); the lit pixels are the
 * image's */
static void test_panel(YsPanel *p, const uint32_t *img, const int (*it)[4], const int *kind, int n, int slash,
                       int num_pal)
{
    memset(p, 0, sizeof(*p));
    p->valid = 1;
    for (int r = 0; r < YS_PANEL_ROWS; r++)
        for (int c = 0; c < YS_PANEL_COLS; c++)
            p->cell[r][c] = 0xF000 | 256;
    for (int i = 0; i < n; i++) {
        for (int c = (it[i][0] - 8) / 8; c <= (it[i][1] - 8) / 8; c++)
            p->cell[0][c] = (uint16_t)(0xF000 | (c == (it[i][0] - 8) / 8 ? kind[i] : 258));
        if (it[i][2] < 0)
            continue;
        int c0 = (it[i][2] - 8) / 8, c1 = (it[i][3] - 8) / 8;
        for (int c = c0; c <= c1; c++)
            p->cell[0][c] = (uint16_t)(num_pal << 12 | (i == 0 && slash && c == (c0 + c1) / 2 ? YS_TILE_SLASH : 449));
    }
    for (int r = 2; r <= 4; r += 2)
        p->cell[r][7] = p->cell[r][39] = 0xF000 | YS_TILE_BAR_SIDE;
    for (int y = 0; y < YS_PANEL_H; y++)
        for (int x = 0; x < YS_PANEL_W; x++)
            p->pix[y][x] = (img[(YS_PANEL_Y + y) * CROP_W + YS_PANEL_X + x] & 0xFFFFFF) != 0;
}

static void test_crop(const char *work)
{
    static uint32_t src[CROP_W * CROP_H], img[CROP_W * CROP_H], big[CROP_W * 4 * CROP_H * 4];
    static uint32_t out[CROP_W * CROP_H], out4[CROP_W * 4 * CROP_H * 4];
    crop_source(src, 0xB69100);
    CHECK(crop_detect(src, CROP_W, CROP_H) == 1, "border recognized");
    memcpy(img, src, sizeof(img));
    for (int i = 0; i < CROP_W * CROP_H; i++)
        if ((img[i] & 0xFFFFFF) == 0xB69100)
            img[i] = 0xFF482400;
    CHECK(crop_detect(img, CROP_W, CROP_H) == 1, "darkened border (another outline color) recognized");
    static const struct { int x, y; const char *what; } breaks[] = {
        { 100, 11, "top outline" }, { 327, 90, "right outline" }, { 200, 156, "map bottom outline" },
        { 150, 26, "black line above the map" }, { 312, 100, "black line right of the map" },
        { 100, 175, "black between the border and the panel" }, { 3, 120, "black on the left" }, { 335, 230, "black on the right" },
        { 170, 5, "black on top" },
    };
    for (int i = 0; i < (int)(sizeof(breaks) / sizeof(breaks[0])); i++) {
        memcpy(img, src, sizeof(img));
        img[breaks[i].y * CROP_W + breaks[i].x] ^= 0x00204080u;
        if ((img[breaks[i].y * CROP_W + breaks[i].x] & 0xFFFFFF) == 0)
            img[breaks[i].y * CROP_W + breaks[i].x] = 0xFF204080u;
        CHECK(crop_detect(img, CROP_W, CROP_H) == 0, "no border with 1 pixel changed: %s", breaks[i].what);
    }
    memset(img, 0, sizeof(img));
    CHECK(crop_detect(img, CROP_W, CROP_H) == -1, "black image");
    CHECK(crop_detect(src, 256, CROP_H) == 0, "another width");

    /* Load game screen: border down to line 234, no panel; crops the same size as the game's */
    {
        static uint32_t ld[CROP_W * CROP_H], ldo[CROP_W * CROP_H];
        for (int y = 0; y < CROP_H; y++)
            for (int x = 0; x < CROP_W; x++) {
                int frame = x >= 8 && x <= 327 && y >= 11 && y <= 234;
                int inner = x >= 24 && x <= 311 && y >= 27 && y <= 218;
                int gap = frame && !inner && x >= 23 && x <= 312 && y >= 26 && y <= 219;
                int outline = frame && (y == 11 || y == 234 || ((x == 8 || x == 327) && y > 11 && y < 234) ||
                                        ((y == 25 || y == 220) && x >= 23 && x <= 312));
                uint32_t v = 0xFF000000u;
                if (outline)
                    v = 0xFFB69100u;
                else if (frame && !gap && !inner)
                    v = 0xFF006D48u;
                else if (inner && y >= 45 && y <= 209 && (x * 7 + y) % 5 == 0)
                    v = 0xFF000000u | (uint32_t)(x * 0x0101 + y * 0x10300) | 0x10;
                ld[y * CROP_W + x] = v;
            }
        CHECK(crop_detect(ld, CROP_W, CROP_H) == CROP_SCREEN_LOAD && crop_detect(src, CROP_W, CROP_H) == CROP_SCREEN_GAME,
              "load screen recognized (and the game not mistaken for it)");
        CropView lv, gv;
        CHECK(crop_view(CROP_FRAME, CROP_SCREEN_LOAD, CROP_W, CROP_H, &lv) &&
                  crop_view(CROP_FRAME, CROP_SCREEN_GAME, CROP_W, CROP_H, &gv) && lv.w == gv.w && lv.h == gv.h,
              "load, Frame: the same size as the game (%dx%d)", lv.w, lv.h);
        /* Whole border: top edge (11-26), the middle (37-217) and the bottom one (219-234) */
        crop_compose(ld, CROP_W, 1, &lv, ldo);
        int fbad = 0;
        for (int y = 0; y < lv.h; y++) {
            int sy = y < 16 ? 11 + y : y < 16 + 181 ? 37 + y - 16 : 219 + y - 16 - 181;
            for (int x = 0; x < lv.w; x++)
                fbad += ldo[y * lv.w + x] != ld[sy * CROP_W + 8 + x];
        }
        CHECK(fbad == 0 && (ldo[(lv.h - 1) * lv.w + 100] & 0xFFFFFF) == 0xB69100 && (ldo[100] & 0xFFFFFF) == 0xB69100,
              "load, Frame: whole border, top and bottom (%d pixels out of place)", fbad);
        /* In the borderless modes, the load screen also keeps it (the Frame crop) */
        for (int m = CROP_FULL; m <= CROP_FILL; m++) {
            CropView mv;
            CHECK(crop_view(m, CROP_SCREEN_LOAD, CROP_W, CROP_H, &mv) && !mv.band && !mv.hud && mv.w == lv.w &&
                      mv.h == lv.h && mv.count == 3 && !memcmp(mv.piece, lv.piece, sizeof(lv.piece)),
                  "load, mode %d: with the border, as in Frame (%dx%d)", m, mv.w, mv.h);
        }
    }

    /* Flash (end of Ys I): the whole screen gains one level (of 7) per channel at each step (the levels come from
     * the game, crop_set_fade). The crop continues with whiten = the levels, the panel with the flash undone comes
     * out equal to the earlier one in the pixels that did not go past 7 (the black around becomes black again) and,
     * brightened again, equal to the frame's; all white turns it off at once */
    {
        static uint32_t pal[CROP_W * CROP_H], wh[CROP_W * CROP_H];
        static CropState ws;
        memset(&ws, 0, sizeof(ws));
        crop_set_screen(&ws, CROP_SCREEN_GAME);
        for (int i = 0; i < CROP_W * CROP_H; i++) { /* in the console's colors: 8 levels per channel */
            uint32_t p = 0xFF000000u;
            for (int s = 0; s < 24; s += 8)
                p |= (uint32_t)((((src[i] >> s & 0xFF) * 7 + 127) / 255 * 255 + 3) / 7) << s;
            pal[i] = p;
        }
        crop_frame(&ws, pal, CROP_W, CROP_H);
        int wbad = 0, pbad = 0, rebad = 0;
        for (int k = 1; k <= 6; k++) {
            for (int i = 0; i < CROP_W * CROP_H; i++) {
                uint32_t p = 0xFF000000u;
                for (int s = 0; s < 24; s += 8) {
                    int l = ((pal[i] >> s & 0xFF) * 7 + 127) / 255 + k;
                    p |= (uint32_t)(((l > 7 ? 7 : l) * 255 + 3) / 7) << s;
                }
                wh[i] = p;
            }
            crop_set_fade(&ws, 7, k, NULL); /* the levels the game added */
            for (int f = 0; f < 6; f++)
                crop_frame(&ws, wh, CROP_W, CROP_H);
            wbad += !ws.active || ws.whiten != k;
            const uint32_t *un = crop_unwhiten_panel(wh, CROP_W, k);
            for (int y = 179; y < 220; y++) /* the panel */
                for (int x = 0; x < CROP_W; x++) {
                    uint32_t u = un[y * CROP_W + x], o = pal[y * CROP_W + x], again = u;
                    int sat = 0;
                    for (int s = 0; s < 24; s += 8)
                        sat |= ((o >> s & 0xFF) * 7 + 127) / 255 + k >= 7;
                    pbad += !sat && (u & 0xFFFFFF) != (o & 0xFFFFFF);
                    crop_whiten(&again, 1, 1, 1, k);
                    rebad += (again & 0xFFFFFF) != (wh[y * CROP_W + x] & 0xFFFFFF);
                }
        }
        CHECK(wbad == 0 && pbad == 0 && rebad == 0,
              "flash: crop kept with the right levels (%d errors); panel undone (%d different pixels) and "
              "brightened again (%d)", wbad, pbad, rebad);
        CropView wv;
        CHECK(crop_view_state(CROP_FILL, &ws, CROP_W, CROP_H, &wv) && wv.whiten == 6, "flash: the cropped image brightens too");
        for (int i = 0; i < CROP_W * CROP_H; i++)
            wh[i] = 0xFFFFFFFFu;
        crop_set_fade(&ws, 7, 7, NULL);
        crop_frame(&ws, wh, CROP_W, CROP_H);
        CHECK(!ws.active && ws.whiten == 0, "flash: all white turns the crop off at once");
        crop_set_fade(&ws, 7, 0, NULL);
        crop_frame(&ws, pal, CROP_W, CROP_H);
        int still_off = !ws.active;
        crop_set_screen(&ws, CROP_SCREEN_NONE); /* the game switches program (the ending scene) */
        crop_frame(&ws, wh, CROP_W, CROP_H);
        crop_set_screen(&ws, CROP_SCREEN_GAME);
        crop_frame(&ws, pal, CROP_W, CROP_H);
        CHECK(still_off && ws.active == CROP_SCREEN_GAME,
              "flash: after the white, no crop until the game switches program (%d %d)", still_off, ws.active);
        memset(&ws, 0, sizeof(ws));
        crop_set_screen(&ws, CROP_SCREEN_GAME);
        crop_frame(&ws, pal, CROP_W, CROP_H);
        crop_frame(&ws, wh, CROP_W, CROP_H);
        CHECK(ws.active, "a sudden all-white frame (without a game flash) does not turn it off");
    }

    /* The screen comes from the game's program (crop_set_screen), not from the image: black, another image or the
     * border being drawn (on entering the game, Ys first draws only the side bars) in the game's program stay the
     * game, with the darkening of the game's fade; outside it, nothing, even with the border shape in the image */
    {
        CropState st = { 0 };
        uint32_t *other = out; /* any image without a border */
        for (int i = 0; i < CROP_W * CROP_H; i++)
            other[i] = 0xFF000000u | (uint32_t)i * 2654435761u >> 8;
        crop_frame(&st, src, CROP_W, CROP_H);
        int off_first = !st.active;
        crop_set_screen(&st, CROP_SCREEN_LOAD);
        crop_frame(&st, other, CROP_W, CROP_H);
        int load = st.active == CROP_SCREEN_LOAD;
        crop_set_screen(&st, CROP_SCREEN_GAME);
        crop_set_fade(&st, 0, 0, NULL); /* the game with the map colors at level 0 */
        memset(img, 0, sizeof(img));
        crop_frame(&st, img, CROP_W, CROP_H);
        int black = st.active == CROP_SCREEN_GAME && st.darken == 7;
        crop_frame(&st, other, CROP_W, CROP_H);
        int any = st.active == CROP_SCREEN_GAME;
        crop_set_fade(&st, 7, 0, NULL);
        crop_frame(&st, src, CROP_W, CROP_H);
        int lit = st.active == CROP_SCREEN_GAME && st.darken == 0;
        crop_set_screen(&st, CROP_SCREEN_NONE);
        crop_frame(&st, src, CROP_W, CROP_H);
        CHECK(off_first && load && black && any && lit && !st.active && st.frames == 6 && st.framed == 3,
              "screen from the game: %d %d %d %d %d %d (%ld %ld)", off_first, load, black, any, lit, st.active, st.frames,
              st.framed);
    }

    /* Crops: size and every pixel in place */
    CropView cv;
    CHECK(!crop_view(CROP_OFF, 1, CROP_W, CROP_H, &cv) && !crop_view(CROP_FULL, 0, CROP_W, CROP_H, &cv) &&
              !crop_view(CROP_FULL, 1, 256, CROP_H, &cv),
          "no crop: off, no border or another width");
    CHECK(crop_view(CROP_FRAME, 1, CROP_W, CROP_H, &cv) && cv.w == 320 && cv.h == CROP_FRAME_VIEW_H, "Frame: %dx%d", cv.w, cv.h);
    crop_compose(src, CROP_W, 1, &cv, out);
    int bad = 0;
    for (int y = 0; y < cv.h; y++)
        for (int x = 0; x < cv.w; x++)
            bad += out[y * cv.w + x] != src[(11 + y) * CROP_W + 8 + x];
    CHECK(bad == 0, "Frame: %d pixels out of place", bad);
    CHECK(crop_view(CROP_FULL, 1, CROP_W, CROP_H, &cv) && cv.w == 288 && cv.h == 128 + CROP_HUD_H,
          "Full: %dx%d (the map width)", cv.w, cv.h);
    crop_compose(src, CROP_W, 1, &cv, out);
    bad = 0;
    int gold = 0, foreign = 0;
    for (int y = 0; y < cv.h; y++)
        for (int x = 0; x < cv.w; x++) {
            uint32_t got = out[y * cv.w + x];
            if (y < 128) {
                bad += got != src[(27 + y) * CROP_W + 24 + x];
            } else if ((got & 0xFFFFFF) != 0) { /* panel: only colors that are in the game's panel */
                int found = 0;
                for (int py = 179; py <= 219 && !found; py++)
                    for (int px = 8; px <= 327 && !found; px++)
                        found = (src[py * CROP_W + px] & 0xFFFFFF) == (got & 0xFFFFFF);
                foreign += !found;
            }
            gold += (got & 0xFFFFFF) == 0xB69100;
        }
    CHECK(bad == 0 && gold == 0 && foreign == 0, "Full: %d map pixels out of place, %d outline ones, %d foreign colors",
          bad, gold, foreign);
    /* Smooth motion: the cropped 4x image is the cropped one scaled up */
    for (int y = 0; y < CROP_H * 4; y++)
        for (int x = 0; x < CROP_W * 4; x++)
            big[y * CROP_W * 4 + x] = src[(y / 4) * CROP_W + x / 4];
    crop_compose(big, CROP_W, 4, &cv, out4);
    bad = 0;
    for (int y = 0; y < cv.h * 4; y++)
        for (int x = 0; x < cv.w * 4; x++)
            bad += out4[y * cv.w * 4 + x] != out[(y / 4) * cv.w + x / 4];
    CHECK(bad == 0, "Full 4x: %d pixels different from the scaled-up one", bad);

    /* Fade on screen changes (the level comes from the game, crop_set_fade; it used to be guessed from the map
     * pixels): the darkening outside the map is 7 - the level, in the same frame, whatever the image (a dark dungeon
     * scrolling darkens nothing; a fade in a dark room darkens everything); outside the game screen, nothing */
    {
        static uint32_t fr[CROP_W * CROP_H];
        static const uint32_t lv[8] = { 0, 0x24, 0x48, 0x6D, 0x91, 0xB6, 0xDA, 0xFF };
        uint8_t table[64]; /* a test table (the game's comes from it): channel * level / 7 */
        for (int c = 0; c < 8; c++)
            for (int l = 0; l < 8; l++)
                table[c * 8 + l] = (uint8_t)(c * l / 7);
        static const int levels[] = { 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7 };
        CropState fs = { 0 };
        crop_set_screen(&fs, CROP_SCREEN_GAME);
        int bad_dark = 0;
        for (int k = 0; k < (int)(sizeof(levels) / sizeof(levels[0])); k++) {
            memcpy(fr, src, sizeof(fr));
            for (int y = 27; y <= 154; y++) /* dark dungeon scrolling: the scenery changes every frame */
                for (int x = 24; x <= 311; x++) {
                    int xs = x - 24 + 2 * k, l = xs % 40 < 10 ? 5 : xs % 40 >= 20 && xs % 40 < 30 ? 2 : 1;
                    fr[y * CROP_W + x] = 0xFF000000u | lv[l] << 16 | lv[l > 1 ? l - 1 : 0];
                }
            crop_set_fade(&fs, levels[k], 0, table);
            crop_frame(&fs, fr, CROP_W, CROP_H);
            bad_dark += fs.darken != 7 - levels[k] || fs.darken_q != fs.darken * FADE_Q;
        }
        CHECK(bad_dark == 0, "fade: the darkening is the game's level, in the same frame (%d errors)", bad_dark);
        crop_set_fade(&fs, 3, 0, table);
        crop_set_screen(&fs, CROP_SCREEN_NONE); /* another program: the game screen goes away */
        crop_frame(&fs, out, CROP_W, CROP_H);
        CHECK(!fs.active && fs.darken == 0, "fade: outside the game screen, nothing darkens");
        /* Smooth (smooth_fades): the 1-level step goes continuously, over the 4 frames between the game's steps */
        CropState ss = { 0 };
        crop_set_screen(&ss, CROP_SCREEN_GAME);
        ss.smooth_fades = 1;
        int mono = 1, prev = 0, last = 0;
        for (int k = 0; k < 40; k++) {
            int level = k < 8 ? 7 : k < 36 ? 7 - (k - 8) / 4 - 1 : 0;
            crop_set_fade(&ss, level, 0, table);
            crop_frame(&ss, src, CROP_W, CROP_H);
            mono &= ss.darken_q >= prev;
            prev = ss.darken_q;
            if (k == 13)
                last = ss.darken_q; /* one frame after the 2nd step: between 1 and 2 */
        }
        CHECK(mono && prev == 7 * FADE_Q && last > FADE_Q && last < 2 * FADE_Q,
              "smooth fade: only rises, ends at black, in between the steps in the middle (%d)", last);

        /* Darken: outside the map, each channel through the game's table at level 7 - steps; the map stays as is */
        memcpy(fr, src, sizeof(fr));
        uint32_t map_before = fr[100 * CROP_W + 100], frame_px = fr[11 * CROP_W + 100];
        crop_darken(fr, CROP_W, CROP_W, CROP_H, 1, CROP_MAP_X, CROP_MAP_Y, 2 * FADE_Q);
        int lvl = ((int)(frame_px >> 16 & 0xFF) * 7 + 127) / 255;
        uint32_t want_r = lv[table[lvl * 8 + 5]];
        CHECK(fr[100 * CROP_W + 100] == map_before && (fr[11 * CROP_W + 100] >> 16 & 0xFF) == want_r,
              "darken: map intact, border %02X (expected %02X, from the table)",
              (unsigned)(fr[11 * CROP_W + 100] >> 16 & 0xFF), (unsigned)want_r);
        crop_darken(fr, CROP_W, CROP_W, CROP_H, 1, CROP_MAP_X, CROP_MAP_Y, 7 * FADE_Q);
        CHECK((fr[11 * CROP_W + 100] & 0xFFFFFF) == 0 && fr[100 * CROP_W + 100] == map_before, "darken 7: black");
    }


    /* Game panel, on a panel similar to the original: text and two bars (white outline 71-327; the ENEMY one
     * without the bottom-left corner, as in the game) */
    {
        static uint32_t pn[CROP_W * CROP_H], pbig[CROP_W * 4 * CROP_H * 4];
        static uint32_t panel[CROP_PANEL_W * CROP_PANEL_H], panel4[CROP_PANEL_W * CROP_PANEL_H];
        memcpy(pn, src, sizeof(pn));
        for (int y = 171; y < 235; y++)
            for (int x = 0; x < CROP_W; x++)
                pn[y * CROP_W + x] = 0xFF000000u;
        for (int y = 179; y <= 186; y++)
            for (int x = 8; x < 328; x++)
                if ((x / 8) % 5 != 3 && (x + y) % 3 == 0)
                    pn[y * CROP_W + x] = (x / 40) % 2 ? 0xFFFFFFFFu : 0xFFDADA00u; /* text */
        for (int b = 0; b < 2; b++) {
            int y0 = b ? 210 : 194, fill = b ? 30 : 60;
            for (int y = y0; y <= y0 + 9; y++) {
                for (int x = 8; x < 56; x++)
                    if ((x + y) % 2 && y > y0 && y < y0 + 9)
                        pn[y * CROP_W + x] = b ? 0xFFB600DAu : 0xFFFF9100u; /* label */
                for (int x = 71; x <= 327; x++)
                    pn[y * CROP_W + x] = y == y0 || y == y0 + 9 || x == 71 || x == 327 ? 0xFFFFFFFFu
                                         : x < 72 + fill                                ? 0xFFFFB600u
                                                                                        : 0xFF000000u;
            }
            if (b)
                pn[(y0 + 9) * CROP_W + 71] = 0xFF000000u; /* the corner missing in the game */
        }
        CHECK(crop_detect(pn, CROP_W, CROP_H) == CROP_SCREEN_GAME, "test panel: border recognized");
        static YsPanel ppn; /* the bars from the panel cells */
        test_panel(&ppn, pn, NULL, NULL, 0, 0, 15);
        crop_panel(pn, CROP_W, 1, &ppn, panel, CROP_PANEL_W);
        int moved = 0, bg_bad = 0, inside_bad = 0;
        for (int y = 0; y < CROP_PANEL_H; y++)
            for (int x = 0; x < CROP_PANEL_W; x++) {
                uint32_t s = pn[(179 + y) * CROP_W + 8 + x] & 0xFFFFFF, g = panel[y * CROP_PANEL_W + x] & 0xFFFFFF;
                int in_bar = x > 63 && x < 319 && ((y > 15 && y < 24) || (y > 31 && y < 40));
                if (s)
                    moved += g != s;
                else if (in_bar)
                    inside_bad += g != 0;
                else if (!(x == 63 && y == 40))
                    bg_bad += panel[y * CROP_PANEL_W + x] != 0; /* transparent */
            }
        CHECK(moved == 0 && bg_bad == 0 && inside_bad == 0,
              "panel: %d lit pixels changed, %d without the background, %d swapped inside the bars", moved, bg_bad, inside_bad);
        CHECK((panel[40 * CROP_PANEL_W + 63] & 0xFFFFFF) == 0xFFFFFF, "panel: ENEMY bar corner complete");
        for (int y = 0; y < CROP_H * 4; y++)
            for (int x = 0; x < CROP_W * 4; x++)
                pbig[y * CROP_W * 4 + x] = pn[(y / 4) * CROP_W + x / 4];
        crop_panel(pbig, CROP_W, 4, &ppn, panel4, CROP_PANEL_W);
        CHECK(memcmp(panel, panel4, sizeof(panel)) == 0, "panel: the same from the 4x image");

        /* Compact, wide, fill: the map (whole, or cut down to the screen aspect) and the HUD band in two lines
         * below, with the panel letters found by color (colored labels, white numbers); ENEMY in the corner */
        {
            int bw, bm;
            crop_band_size(CROP_COMPACT, 0.0, 0.0, &bw, &bm, NULL);
            CHECK(bw == 288 && bm == 128, "Compact: whole map (%dx%d)", bw, bm);
            int bh;
            crop_band_size(CROP_FILL, 0.0, 0.0, &bw, &bm, &bh);
            CHECK(bw == 288 && bm == 120 && bh == CROP_BAND_SMALL_H,
                  "Fill with TV pixels (6:7) at 16:9: 4 lines removed, low band (%dx%d, %d)", bw, bm, bh);
            crop_band_size(CROP_WIDE, 0.0, 0.0, &bw, &bm, NULL);
            CHECK(bw == 288 && bm == 120, "Wide with TV pixels: half (%dx%d)", bw, bm);
            crop_band_size(CROP_FILL, 1.0, 16.0 / 9.0, &bw, &bm, NULL);
            CHECK(bw == 272 && bm == 128, "Fill with square pixels: columns removed (%dx%d)", bw, bm);
            crop_band_size(CROP_WIDE, 1.0, 16.0 / 9.0, &bw, &bm, NULL);
            CHECK(bw == 280 && bm == 128, "Wide with square pixels (%dx%d)", bw, bm);
            crop_band_size(CROP_FILL, 0.0, -1.0, &bw, &bm, NULL);
            CHECK(bw == 288 && bm == 128, "stretched: no cut (%dx%d)", bw, bm);
            crop_band_size(CROP_FILL, 0.0, 21.0 / 9.0, &bw, &bm, NULL);
            CHECK(bm == CROP_BAND_MIN_MAP_H, "very wide screen: the cut stops at %d lines", bm);
            CropView tv;
            crop_view(CROP_FILL, CROP_SCREEN_GAME, CROP_W, CROP_H, &tv);
            double aspect = tv.w * (6.0 / 7.0) / tv.h;
            CHECK(aspect > 1.76 && aspect < 1.80, "Fill (TV): the image has the screen aspect (%.3f)", aspect);
        }
        static const struct {
            int l0, l1, v0, v1;
        } k_ys1[3] = { { 8, 31, 40, 94 }, { 112, 135, 144, 230 }, { 248, 279, 288, 326 } },
          k_ys2[4] = { { 8, 31, 40, 62 }, { 80, 103, 112, 166 }, { 184, 207, 216, 255 }, { 264, 295, 304, 326 } };
        static uint32_t bp[CROP_W * CROP_H], bbig[CROP_W * 4 * CROP_H * 4], bo[CROP_W * CROP_H],
            bo4[CROP_W * 4 * CROP_H * 4];
        for (int game = 1; game <= 2; game++) {
            memcpy(bp, pn, sizeof(bp));
            int n = game == 1 ? 3 : 4;
            for (int y = 179; y <= 186; y++)
                for (int x = 8; x < 328; x++)
                    bp[y * CROP_W + x] = 0xFF000000u;
            for (int i = 0; i < n; i++) {
                int l0 = game == 1 ? k_ys1[i].l0 : k_ys2[i].l0, l1 = game == 1 ? k_ys1[i].l1 : k_ys2[i].l1;
                int v0 = game == 1 ? k_ys1[i].v0 : k_ys2[i].v0, v1 = game == 1 ? k_ys1[i].v1 : k_ys2[i].v1;
                for (int y = 179; y <= 186; y++) {
                    for (int x = l0; x <= l1; x++) /* yellow label, with 3 empty columns in the middle (H.P) */
                        if ((x + y) % 2 == 0 && !(x >= l0 + 9 && x <= l0 + 11))
                            bp[y * CROP_W + x] = 0xFFDADA00u;
                    for (int x = v0; x <= v1; x++)
                        if ((x + y) % 2 == 0)
                            bp[y * CROP_W + x] = 0xFFFFFFFFu;
                }
            }
            for (int i = 0; i < CROP_W * 4 * CROP_H * 4; i++)
                bbig[i] = bp[(i / (CROP_W * 4) / 4) * CROP_W + (i % (CROP_W * 4)) / 4];
            /* The panel cells (Ys I: the H.P with the maximum, "/"; Ys II: without) */
            static YsPanel bpn;
            static const int k_kind1[3] = { YS_TILE_H, YS_TILE_E, YS_TILE_G },
                             k_kind2[4] = { YS_TILE_H, YS_TILE_M, YS_TILE_E, YS_TILE_G };
            int itv[4][4];
            for (int i = 0; i < n; i++) {
                itv[i][0] = game == 1 ? k_ys1[i].l0 : k_ys2[i].l0;
                itv[i][1] = game == 1 ? k_ys1[i].l1 : k_ys2[i].l1;
                itv[i][2] = game == 1 ? k_ys1[i].v0 : k_ys2[i].v0;
                itv[i][3] = game == 1 ? k_ys1[i].v1 : k_ys2[i].v1;
            }
            test_panel(&bpn, bp, (const int (*)[4])itv, game == 1 ? k_kind1 : k_kind2, n, game == 1, 15);
            CropView bv;
            for (int sq = 0; sq < 2; sq++) /* TV pixels (cuts lines) and square ones (cuts columns) */
                for (int mode = CROP_FULL; mode <= CROP_FILL; mode++) {
                    CropState cs = { 0 };
                    crop_set_screen(&cs, CROP_SCREEN_GAME);
                    cs.active = CROP_SCREEN_GAME;
                    cs.next_on = 1; /* NEXT (from RAM, crop_next_exp) in place of the panel's EXP */
                    cs.next = 154;
                    cs.panel = bpn;
                    cs.par = sq ? 1.0 : 6.0 / 7.0;
                    cs.screen = 16.0 / 9.0;
                    int bw, bm, bh;
                    crop_band_size(mode, cs.par, cs.screen, &bw, &bm, &bh);
                    int c0 = (288 - bw) / 2, r0 = (128 - bm) / 2;
                    CHECK(crop_view_state(mode, &cs, CROP_W, CROP_H, &bv) && bv.w == bw && bv.h == bm + bh &&
                              bv.band && !bv.hud && bv.band_y == bm && bv.piece[0].sx == 24 + c0 &&
                              bv.piece[0].sy == 27 + r0 && bv.map_x == -c0 && bv.map_y == -r0,
                          "Ys %d mode %d pixels %d: %dx%d, map from %d,%d", game, mode, sq, bv.w, bv.h,
                          bv.piece[0].sx - 24, bv.piece[0].sy - 27);
                    bv.hud_bg = CROP_HUD_BLACK;
                    /* Same in both games: on top, H.P (margin of 8), bar and number; below it, the M.P (Ys II); on
                     * the right, NEXT (74 pixels: label, 4, 5 digits) above GOLD, against the margin.
                     * (x + y) even lit in the panel: H.P (font column 9), GOLD and M.P (2nd column of the label) */
                    int gold_w = game == 1 ? 32 + 4 + 39 : 32 + 4 + 23, next_w = 31 + 4 + 39;
                    if (bv.hud_q) {
                        /* Fill with TV pixels: low band, HUD in 3/4 pixels (4x output, 3 output pixels per HUD
                         * pixel) */
                        int os = crop_out_sub(&bv), k = os * bv.hud_q / 4, tw = bw * os / k;
                        int th = bh * os / k, r1 = (th - 18) * 2 / 3, r2 = r1 + 10, ow = bw * os;
                        crop_compose_sub(bp, CROP_W, 1, &bv, bo4, os);
#define HUD_AT(dx, dy) bo4[(bm * os + (dy) * k) * ow + (dx) * k]
                        int map_bad = 0;
                        for (int y = 0; y < bm; y++)
                            for (int x = 0; x < bw; x++)
                                map_bad += bo4[(y * os) * ow + x * os] != bp[(27 + r0 + y) * CROP_W + 24 + c0 + x];
                        int hp_ok = HUD_AT(9, r1) == 0xFFDADA00u;
                        int exp_ok = 0, next_digits = 0;
                        for (int y = 0; y < 8; y++) {
                            exp_ok |= HUD_AT(tw - 8 - next_w, r1 + y) == 0xFFDADA00u ||
                                      HUD_AT(tw - 8 - next_w + 1, r1 + y) == 0xFFDADA00u;
                            for (int x = tw - 8 - 39; x < tw - 8; x++)
                                next_digits += HUD_AT(x, r1 + y) == 0xFFFFFFFFu;
                        }
                        exp_ok = exp_ok && next_digits > 40;
                        int gold_ok = HUD_AT(tw - 8 - gold_w + 1, r2) == 0xFFDADA00u;
                        int mp_ok = 1;
                        if (game == 2)
                            mp_ok = HUD_AT(8, r2 + 1) == 0xFFDADA00u;
#undef HUD_AT
                        /* Without the ENEMY in the corner (the whole map, there too) */
                        CHECK(bm == 120 && map_bad == 0 && hp_ok && exp_ok && gold_ok && mp_ok,
                              "Ys %d fill (TV): map %d lines (%d wrong), H.P %d, NEXT on top %d, GOLD below %d, "
                              "M.P %d", game, bm, map_bad, hp_ok, exp_ok, gold_ok, mp_ok);
                        static uint32_t small1[CROP_W * 4 * CROP_H * 4];
                        crop_compose_sub(bbig, CROP_W, 4, &bv, small1, os);
                        CHECK(memcmp(small1, bo4, (size_t)ow * bv.h * os * sizeof(uint32_t)) == 0,
                              "Ys %d fill (TV): the same from the 4x image", game);
                        continue;
                    }
                    crop_compose(bp, CROP_W, 1, &bv, bo);
                    /* the two lines in the band, with 2/3 of the spare room on top; the whole map, without the ENEMY in the corner */
                    int map_bad = 0, label_bad = 0, r1 = bm + (bh - 19) * 2 / 3, r2 = r1 + 11;
                    for (int y = 0; y < bm; y++)
                        for (int x = 0; x < bw; x++)
                            map_bad += bo[y * bw + x] != bp[(27 + r0 + y) * CROP_W + 24 + c0 + x];
                    /* on top: the 1st label at column 8, the bar (80, full = max HP) and the number */
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 24; x++) {
                            uint32_t sp = bp[(179 + y) * CROP_W + 8 + x];
                            label_bad += (sp & 0xFFFFFF) && bo[(r1 + y) * bw + 8 + x] != sp;
                        }
                    int vx = 8 + 24 + 4 + 80 + 4, v_lit = 0, max_lit = 0;
                    for (int y = 0; y < 8; y++) {
                        v_lit += bo[(r1 + y) * bw + vx] == 0xFFFFFFFFu;
                        for (int x = vx + 24; x < vx + 55; x++) /* Ys II: "/" and the maximum, in the game's font */
                            max_lit += bo[(r1 + y) * bw + x] == 0xFFFFFFFFu;
                    }
                    CHECK(map_bad == 0 && label_bad == 0 && bo[r1 * bw + 36] == 0xFFFFFFFFu &&
                              bo[r1 * bw + 115] == 0xFFFFFFFFu && bo[(r1 + 3) * bw + 37] == 0xFFFFB600u &&
                              bo[(r1 + 3) * bw + 114] == 0xFFFFB600u && v_lit == 4 && (game == 1 || max_lit > 30),
                          "Ys %d mode %d: map (%d wrong), H.P (%d wrong), full bar, number at column %d (%d), "
                          "maximum (%d)", game, mode, map_bad, label_bad, vx, v_lit, max_lit);
                    /* on the right: NEXT on top, GOLD below; Ys II: M.P below the H.P */
                    int gx = bw - 8 - gold_w, nx = bw - 8 - next_w, next_lab = 0, next_digits = 0;
                    for (int y = 0; y < 8; y++) {
                        next_lab |= bo[(r1 + y) * bw + nx] == 0xFFDADA00u || bo[(r1 + y) * bw + nx + 1] == 0xFFDADA00u;
                        for (int x = bw - 8 - 39; x < bw - 8; x++)
                            next_digits += bo[(r1 + y) * bw + x] == 0xFFFFFFFFu;
                    }
                    int gold_ok = bo[r2 * bw + gx] == 0xFFDADA00u || bo[(r2 + 1) * bw + gx] == 0xFFDADA00u;
                    int mp_ok = 1;
                    if (game == 2)
                        mp_ok = bo[r2 * bw + 8] == 0xFFDADA00u || bo[(r2 + 1) * bw + 8] == 0xFFDADA00u;
                    CHECK(next_lab && next_digits > 40 && gold_ok && mp_ok,
                          "Ys %d mode %d: NEXT at column %d (%d, %d), GOLD at %d, M.P below the H.P (%d %d)", game, mode,
                          nx, next_lab, next_digits, gx, gold_ok, mp_ok);
                    /* the same from the 4x image (smooth motion) */
                    crop_compose(bbig, CROP_W, 4, &bv, bo4);
                    int big_bad = 0;
                    for (int y = 0; y < bv.h * 4; y++)
                        for (int x = 0; x < bw * 4; x++)
                            big_bad += bo4[y * bw * 4 + x] != bo[(y / 4) * bw + x / 4];
                    CHECK(big_bad == 0, "Ys %d mode %d: the same from the 4x image (%d wrong)", game, mode, big_bad);
                }
            if (game == 1) {
                /* enemy without HP: the corner keeps the map */
                for (int y = 211; y < 219; y++)
                    for (int x = 72; x < 327; x++)
                        bp[y * CROP_W + x] = 0xFF000000u;
                crop_view(CROP_FILL, CROP_SCREEN_GAME, CROP_W, CROP_H, &bv);
                crop_compose(bp, CROP_W, 1, &bv, bo);
                int corner = 0;
                for (int y = 0; y < 14; y++)
                    for (int x = 140; x < bv.w; x++)
                        corner += bo[y * bv.w + x] != bp[(27 - bv.map_y + y) * CROP_W + 24 + x];
                /* the enemy bar from the panel cells: without HP, nothing lit inside it */
                CropHudLayout hl_dead, hl_live;
                test_panel(&bpn, bp, (const int (*)[4])itv, k_kind1, n, 1, 15);
                crop_hud_layout(&bpn, &hl_dead);
                crop_hud_layout(&ppn, &hl_live);
                CHECK(corner == 0 && hl_dead.bars == 2 && hl_dead.bar_len[1] == 0 && hl_live.bars == 2 &&
                          hl_live.bar_len[1] == 30 && hl_live.bar_len[0] == 60 && hl_dead.n == 3 && !hl_dead.save,
                      "without HP, the ENEMY disappears (%d pixels changed in the corner)", corner);
                /* save screen: the game writes the numbers of the chosen save in palette 5 (greens); the labels and
                 * numbers are still found from the cells and nothing gets mixed */
                for (int y = 179; y <= 186; y++)
                    for (int x = 8; x < 328; x++)
                        if (bp[y * CROP_W + x] == 0xFFFFFFFFu)
                            bp[y * CROP_W + x] = 0xFF00FF00u;
                test_panel(&bpn, bp, (const int (*)[4])itv, k_kind1, n, 1, 5);
                CropHudLayout hl_save;
                crop_hud_layout(&bpn, &hl_save);
                CHECK(hl_save.save && hl_save.n == 3, "save: numbers in another palette");
                bv.panel = &bpn;
                crop_compose(bp, CROP_W, 1, &bv, bo);
                /* without NEXT (the panel's EXP, 115 pixels, top right): the bar shortens to 62 */
                int r1 = bv.band_y + (bv.h - bv.band_y - 19 + 1) / 2, r2 = r1 + 11, gx = bv.w - 8 - (32 + 4 + 39);
                int green = 0;
                for (int y = 0; y < 8; y++)
                    green += bo[(r1 + y) * bv.w + 8 + 24 + 4 + 62 + 4] == 0xFF00FF00u;
                int gold_ok = bo[r2 * bv.w + gx] == 0xFFDADA00u || bo[(r2 + 1) * bv.w + gx] == 0xFFDADA00u;
                CHECK(green == 4 && gold_ok, "save: green numbers in place (%d), GOLD at column %d (%d)", green, gx, gold_ok);
                /* NEXT from the EXP written in the panel (not from RAM, which the game runs the saves through when listing
                 * them) and the next level from RAM; above it, 0 */
                static const int k_exp_digits[5] = { 0, 0, 9, 8, 8 };
                int exp_c0 = (itv[1][2] - 8) / 8;
                for (int c = 0; c < 5; c++)
                    bpn.cell[0][exp_c0 + c] = (uint16_t)(5 << 12 | (YS_TILE_DIGIT_FIRST + k_exp_digits[c]));
                bpn.cell[0][exp_c0 + 5] = 5 << 12 | YS_TILE_SLASH;
                for (int c = 1; c < 5; c++) /* after the "/", the maximum (Ys I) is not part of the EXP */
                    bpn.cell[0][exp_c0 + 5 + c] = (uint16_t)(5 << 12 | (YS_TILE_DIGIT_FIRST + 7));
                bpn.next_level = 1100;
                CHECK(crop_next_exp(&bpn) == 112, "NEXT from the panel EXP: %d", crop_next_exp(&bpn));
                bpn.next_level = 988;
                CHECK(crop_next_exp(&bpn) == 0, "NEXT at max level: %d", crop_next_exp(&bpn));
                bpn.next_level = 70000; /* the next level has 3 bytes: at the end of Ys II it goes past 65535 */
                CHECK(crop_next_exp(&bpn) == 69012, "NEXT with a 3-byte next level: %d", crop_next_exp(&bpn));
                bpn.next_level = 1100;
                /* The H.P bar shows the HP of the chosen save (24 of 65), in the game's bar colors, even though the
                 * panel bar is the player's; NEXT stays in place */
                int hp_c0 = (itv[0][2] - 8) / 8;
                static const int k_hp_digits[3] = { 0, 2, 4 };
                for (int c = 0; c < 3; c++)
                    bpn.cell[0][hp_c0 + c] = (uint16_t)(5 << 12 | (YS_TILE_DIGIT_FIRST + k_hp_digits[c]));
                bpn.hp_max = 65;
                bpn.bar_yellow = 0xFFB400u;
                bpn.bar_red = 0xB42400u;
                bv.next_on = 1;
                bv.next = crop_next_exp(&bpn);
                crop_compose(bp, CROP_W, 1, &bv, bo);
                /* the bar starts after H.P (24 pixels) and 4 of spacing; inside it, the 3rd line */
                int bx0 = 8 + 24 + 4, yellow = 0, red = 0, inner = 0;
                for (int x = bx0 + 1; x < bv.w && inner < 300; x++) {
                    uint32_t c = bo[(r1 + 3) * bv.w + x] & 0xFFFFFFu;
                    if (c == 0xFFB400u)
                        yellow++;
                    else if (c == 0xB42400u)
                        red++;
                    else
                        break;
                    inner++;
                }
                int want_yellow = (24 * inner + 64) / 65;
                CHECK(red > 0 && yellow > 0 && yellow == want_yellow, "save: bar with the save's HP: %d yellow, %d red (expected %d)",
                      yellow, red, want_yellow);
                /* empty save (no maximum): the bar comes out unlit */
                bpn.hp_max = 0;
                for (int c = 0; c < 3; c++)
                    bpn.cell[0][hp_c0 + c] = 5 << 12 | YS_TILE_DIGIT_FIRST;
                crop_compose(bp, CROP_W, 1, &bv, bo);
                CHECK((bo[(r1 + 3) * bv.w + bx0 + 2] & 0xFFFFFFu) == 0, "empty save: bar unlit");
                /* Ys II: the panel has no "/" in the H.P; on the save screen, the maximum is the save's, not the player's bar */
                bpn.hp_max = 65;
                for (int c = hp_c0; c <= (itv[0][3] - 8) / 8; c++)
                    if (YS_TILE(bpn.cell[0][c]) == YS_TILE_SLASH)
                        bpn.cell[0][c] = 5 << 12 | YS_TILE_DIGIT_FIRST;
                CropView bv_max = bv;
                static uint32_t bo_max[CROP_W * CROP_H];
                crop_compose(bp, CROP_W, 1, &bv_max, bo_max);
                bpn.hp_max = 77;
                crop_compose(bp, CROP_W, 1, &bv_max, bo);
                int max_diff = 0;
                for (int y = 0; y < 8; y++)
                    for (int x = bx0 + 40; x < bv.w / 2 + 60; x++)
                        max_diff += bo[(r1 + y) * bv.w + x] != bo_max[(r1 + y) * bv.w + x];
                CHECK(max_diff > 0, "save: the written maximum comes from the save (%d pixels change)", max_diff);
            }
        }
        /* Backgrounds: flat black; gradient from the top color to the bottom one, dithered; the last line (the strip
         * below the image) in the bottom color */
        CHECK(crop_hud_pixel(CROP_HUD_BLACK, 5, 7, 288, 26) == 0xFF000000u &&
                  crop_hud_pixel(CROP_HUD_NAVY, 0, 0, 288, 26) == 0xFF00006Du &&
                  crop_hud_pixel(CROP_HUD_NAVY, 1, 25, 288, 26) == 0xFF000024u &&
                  crop_hud_bottom(CROP_HUD_NAVY, 100, 288) == 0xFF000024u,
              "backgrounds: black, gradient from top to bottom, bottom color");
        CHECK(crop_hud_pixel(CROP_HUD_CHARCOAL, 0, 0, 288, 26) == 0xFF484848u &&
                  crop_hud_pixel(CROP_HUD_CHARCOAL, 0, 25, 288, 26) == 0xFF000000u &&
                  crop_hud_bottom(CROP_HUD_CHARCOAL, 0, 288) == 0xFF000000u,
              "backgrounds: near-black gray, from top to bottom");
        {
            int mixed = 0;
            for (int x = 0; x < 2; x++)
                for (int y = 5; y < 7; y++)
                    mixed += crop_hud_pixel(CROP_HUD_CHARCOAL, x, y, 288, 26) != crop_hud_pixel(CROP_HUD_CHARCOAL, 0, 5, 288, 26);
            CHECK(mixed > 0, "dithered gradient between two shades");
        }
        /* Game windows in wide/fill: one that goes outside the shown part (SELECT menu, map columns 9-102) makes the
         * image slide until it fits; one against the edge (scene with a portrait) shows the whole map without the HUD */
        {
            static uint32_t wp[CROP_W * CROP_H];
            CropState ws = { 0 };
            crop_set_screen(&ws, CROP_SCREEN_GAME);
            ws.par = 1.0;
            ws.screen = 240.5 / 154.0; /* square pixels: fill shows 240 columns */
            /* window from x0-x1 (on the map), with the top edge at map line y0 and 20 lines */
#define DRAW_WINDOW(x0, x1, y0)                                                                                    \
    do {                                                                                                           \
        memcpy(wp, pn, sizeof(wp));                                                                                \
        for (int x = 24 + (x0); x <= 24 + (x1); x++) {                                                             \
            wp[(27 + (y0)) * CROP_W + x] = 0xFFFFFFFFu;                                                            \
            wp[(28 + (y0)) * CROP_W + x] = 0xFF000000u;                                                            \
        }                                                                                                          \
        for (int y = 27 + (y0); y < 27 + (y0) + 20; y++)                                                           \
            wp[y * CROP_W + 24 + (x0)] = wp[y * CROP_W + 24 + (x1)] = 0xFFFFFFFFu;                                 \
    } while (0)
            /* The game window (crop_set_windows) with its frame drawn at map columns x0-x1 and lines y0-y1
             * (x0 < 0: none); its cell box goes 1 pixel beyond on each side */
#define SET_WIN(st, x0, x1, y0, y1, scene)                                                                         \
    do {                                                                                                           \
        int box_[1][4] = { { 24 + (x0) - 1, 27 + (y0) - 1, 24 + (x1) + 1, 27 + (y1) + 1 } };                      \
        crop_set_windows((st), (const int (*)[4])box_, (x0) >= 0, (scene));                                        \
    } while (0)
            SET_WIN(&ws, 9, 102, 13, 32, 0);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            crop_pan_step(&ws, CROP_FILL);
            CHECK(ws.window && ws.win_x0 == 9 && ws.win_x1 == 102 && ws.win_y0 == 13 && ws.win_y1 == 32 &&
                      ws.pan_q == -8 && ws.pan_qy == 0 && !ws.scene,
                  "fill: game window at map columns %d-%d, lines %d-%d; starts sliding (%d)", ws.win_x0,
                  ws.win_x1, ws.win_y0, ws.win_y1, ws.pan_q);
            for (int k = 0; k < 20; k++)
                crop_pan_step(&ws, CROP_FILL);
            CropView sv;
            CHECK(ws.pan_q == -68 && crop_view_state(CROP_FILL, &ws, CROP_W, CROP_H, &sv) && sv.band && sv.w == 240 &&
                      sv.piece[0].sx == 24 + 7 && sv.map_x == -7 && sv.pan_q == 0,
                  "fill: SELECT menu fits with 2 pixels of margin (slide %d, map from %d)", ws.pan_q,
                  sv.piece[0].sx - 24);
            ws.pan_q = -6;
            crop_view_state(CROP_FILL, &ws, CROP_W, CROP_H, &sv);
            CHECK(sv.piece[0].sx == 24 + 24 - 2 && sv.pan_q == 2, "slide of 1/4 pixel: %d + %d/4", sv.piece[0].sx,
                  sv.pan_q);
            ws.pan_q = -68;
            crop_pan_step(&ws, CROP_COMPACT);
            CHECK(ws.pan_q == 0 && !ws.scene, "compact: no slide");
            SET_WIN(&ws, -1, 0, 0, 0, 0);
            for (int k = 0; k < CROP_WINDOW_OFF_FRAMES; k++) {
                crop_frame(&ws, pn, CROP_W, CROP_H);
                crop_pan_step(&ws, CROP_WIDE);
            }
            CHECK(!ws.window, "window closed: %d frames later", CROP_WINDOW_OFF_FRAMES);
            /* TV pixels: fill cuts 4 lines on top and 4 at the bottom (it used to be 2 and 2); a window from line 4 already fits */
            ws.par = 6.0 / 7.0;
            ws.screen = 16.0 / 9.0;
            SET_WIN(&ws, 60, 200, 4, 23, 0);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            for (int k = 0; k < 10; k++)
                crop_pan_step(&ws, CROP_FILL);
            CHECK(ws.pan_q == 0 && ws.pan_qy == 0 && crop_view_state(CROP_FILL, &ws, CROP_W, CROP_H, &sv) &&
                      sv.piece[0].sy == 27 + 4 && sv.map_y == -4,
                  "fill (TV): window near the top fits (%d, map from line %d)", ws.pan_qy,
                  sv.piece[0].sy - 27);
            /* SELECT menu (lines 9-28) already fits in TV fill (from line 8): no slide (sliding 1 pixel and back when
             * opening the inventory made the menu shake) */
            SET_WIN(&ws, -1, 0, 0, 0, 0);
            for (int k = 0; k < CROP_WINDOW_OFF_FRAMES; k++)
                crop_frame(&ws, pn, CROP_W, CROP_H);
            SET_WIN(&ws, 9, 102, 9, 28, 0);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            for (int k = 0; k < 10; k++)
                crop_pan_step(&ws, CROP_FILL);
            CHECK(ws.window && !ws.scene && ws.pan_q == 0 && ws.pan_qy == 0, "fill (TV): SELECT menu does not slide (%d, %d)",
                  ws.pan_q, ws.pan_qy);
            /* fade: the whole HUD band darkens (the cut map does not extend over it); the map stays */
            {
                static uint32_t dv[CROP_W * CROP_H];
                CropView fv2;
                crop_view(CROP_FILL, CROP_SCREEN_GAME, CROP_W, CROP_H, &fv2);
                crop_compose(pn, CROP_W, 1, &fv2, dv);
                uint32_t map_px = dv[50 * fv2.w + 100];
                crop_darken_view(dv, fv2.w, &fv2, 1, 7 * FADE_Q);
                int band_lit = 0;
                for (int i = fv2.band_y * fv2.w; i < fv2.w * fv2.h; i++)
                    band_lit += (dv[i] & 0xFFFFFF) != 0;
                CHECK(band_lit == 0 && dv[50 * fv2.w + 100] == map_px,
                      "fill (TV): on fade the whole band darkens (%d lit pixels), the map stays", band_lit);
            }
            /* inventory (columns 49-230, lines 9-118): fits exactly in the 112 lines, without becoming a scene */
            SET_WIN(&ws, -1, 0, 0, 0, 0);
            for (int k = 0; k < CROP_WINDOW_OFF_FRAMES; k++)
                crop_frame(&ws, pn, CROP_W, CROP_H);
            SET_WIN(&ws, 49, 230, 9, 118, 0);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            for (int k = 0; k < 10; k++)
                crop_pan_step(&ws, CROP_FILL);
            CHECK(!ws.scene && ws.win_y0 == 9 && ws.win_y1 == 118 && ws.pan_qy == 0,
                  "fill (TV): inventory fits (lines %d-%d, scene %d, slide %d)", ws.win_y0, ws.win_y1, ws.scene,
                  ws.pan_qy);
            /* window against the right edge of the map (GAME OVER, wide lists): whole map and the HUD */
            SET_WIN(&ws, 129, 286, 13, 32, 0);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            crop_pan_step(&ws, CROP_WIDE);
            CHECK(ws.scene && ws.pan_q == 0 && ws.pan_qy == 0 && crop_view_state(CROP_WIDE, &ws, CROP_W, CROP_H, &sv) &&
                      sv.w == 288 && sv.h == 128 + CROP_BAND_H && sv.band && sv.band_y == 128 && sv.hud_q == 0 &&
                      sv.piece[0].sx == 24 && sv.piece[0].sy == 27 && sv.piece[0].h == 128 && sv.map_y == 0,
                  "window at the edge: whole map and the HUD (%dx%d)", sv.w, sv.h);
            CHECK(crop_view_state(CROP_FILL, &ws, CROP_W, CROP_H, &sv) && sv.w == 288 && sv.h == 128 + CROP_BAND_SMALL_H && sv.band &&
                      sv.band_y == 128 && sv.hud_q == CROP_HUD_SMALL_Q && sv.piece[0].h == 128,
                  "scene in fill (TV): whole map and the low band (%dx%d)", sv.w, sv.h);
            /* Scene with a portrait = game window without a saved background; Feena's book on top of it (with a
             * background) does not end it */
            SET_WIN(&ws, -1, 0, 0, 0, 1);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            crop_pan_step(&ws, CROP_WIDE);
            CHECK(ws.scene && ws.scene_hold, "scene: window without a saved background");
            SET_WIN(&ws, 33, 254, 17, 36, 1);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            crop_pan_step(&ws, CROP_WIDE);
            CHECK(ws.scene && ws.win_x0 == 33, "scene continues with another window on top (book)");
            {
                /* In the scene the windows do not shrink: none goes to the redraw_ring (which removed the masks
                 * touching a window, and the shopkeeper's drawing ended up on top of the choice window) */
                int rb[CROP_WINDOWS_MAX][4];
                CHECK(ws.grp_n > 0 && crop_ring_boxes(&ws, rb) == 0, "scene: no windows for the redraw_ring (%d)", ws.grp_n);
            }
            /* the portrait window closes and the portrait stays drawn: the scene continues until the game darkens the screen */
            SET_WIN(&ws, -1, 0, 0, 0, 0);
            for (int k = 0; k < CROP_WINDOW_OFF_FRAMES + 20; k++) {
                crop_frame(&ws, pn, CROP_W, CROP_H);
                crop_pan_step(&ws, CROP_WIDE);
            }
            CHECK(ws.scene && !ws.window, "scene holds after the window closes (without darkening)");
            {
                static uint32_t dk[CROP_W * CROP_H];
                memcpy(dk, pn, sizeof(dk));
                for (int y = 27; y < 27 + 128; y++)
                    for (int x = 24; x < 24 + 288; x++)
                        dk[y * CROP_W + x] = 0xFF000000u;
                /* the scene only ends with the image all black (also the smooth fade's): at the start of the fade the
                 * switch to the band looked like a zoom (user report; the start of the fade is checked in windows_crop_ys).
                 * The game at level 0. With colors lit outside the fade (the GAME OVER text), not yet */
                crop_set_fade(&ws, 0, 0, NULL);
                crop_set_fade_stray(&ws, 8);
                crop_frame(&ws, pn, CROP_W, CROP_H);
                crop_pan_step(&ws, CROP_WIDE);
                CHECK(ws.scene && ws.scene_hold && ws.darken == 7, "level 0 with colors lit outside: the scene holds");
                crop_set_fade_stray(&ws, 0);
                crop_frame(&ws, dk, CROP_W, CROP_H);
                crop_pan_step(&ws, CROP_WIDE);
                CHECK(!ws.scene && ws.darken == 7 && ws.darken_q == 7 * FADE_Q &&
                          crop_view_state(CROP_WIDE, &ws, CROP_W, CROP_H, &sv) && sv.h == 146 && sv.band,
                      "scene ended with the black image: wide returns (darkened %d)", ws.darken_q);
            }
            /* without darkening, it ends after CROP_SCENE_HOLD_MAX frames */
            crop_set_fade(&ws, 7, 0, NULL);
            SET_WIN(&ws, -1, 0, 0, 0, 1);
            crop_frame(&ws, pn, CROP_W, CROP_H);
            SET_WIN(&ws, -1, 0, 0, 0, 0);
            int held = 0;
            for (int k = 0; k <= CROP_SCENE_HOLD_MAX && ws.scene_hold; k++, held++)
                crop_frame(&ws, pn, CROP_W, CROP_H);
            crop_pan_step(&ws, CROP_WIDE);
            CHECK(!ws.scene && held == CROP_SCENE_HOLD_MAX + 1, "scene without darkening ends in %d frames", held);
            /* fill with the HUD at 3/4: the Ys II ending lines (one window per line, columns 33-262 and lines 1-62, at
             * the top of the map): between one line and the next the box of the closed window is kept, but the view
             * does not follow it at full size (the whole screen went down and up at each line) */
            {
                static CropState fs;
                memset(&fs, 0, sizeof(fs));
                crop_set_screen(&fs, CROP_SCREEN_GAME);
                fs.par = 6.0 / 7.0;
                fs.screen = 16.0 / 9.0;
                for (int k = 0; k < 3; k++)
                    crop_frame(&fs, pn, CROP_W, CROP_H);
                SET_WIN(&fs, 33, 262, 1, 62, 0);
                crop_frame(&fs, pn, CROP_W, CROP_H);
                crop_pan_step(&fs, CROP_FILL);
                int open_ok = fs.grp_n > 0 && !fs.scene && fs.pan_q == 0 && fs.pan_qy == 0;
                SET_WIN(&fs, -1, 0, 0, 0, 0);
                int slid = 0;
                for (int k = 0; k < CROP_WINDOW_OFF_FRAMES - 1; k++) {
                    crop_frame(&fs, pn, CROP_W, CROP_H);
                    crop_pan_step(&fs, CROP_FILL);
                    slid |= fs.pan_q != 0 || fs.pan_qy != 0;
                }
                CHECK(open_ok && fs.window && !slid,
                      "fill (TV): ending line at the top, open and between two lines, no slide (%d, %d)", fs.pan_q,
                      fs.pan_qy);
            }
            /* Fill with the HUD at 3/4 (TV pixels, 16:9): the inventory (columns 49-230, lines 9-118) comes out at 3/4
             * around the middle (140, 64); what it covered shows the map from 2 frames without the window before */
            {
                static CropState ks;
                static uint32_t ko[CROP_W * 4 * CROP_H * 4];
                memset(&ks, 0, sizeof(ks));
                crop_set_screen(&ks, CROP_SCREEN_GAME);
                ks.par = 6.0 / 7.0;
                ks.screen = 16.0 / 9.0;
                CHECK(crop_small_hud(CROP_FILL, ks.par, ks.screen) && !crop_small_hud(CROP_WIDE, ks.par, ks.screen) &&
                          !crop_small_hud(CROP_FILL, 1.0, 240.5 / 154.0),
                      "HUD at 3/4 only in fill with TV pixels");
                for (int k = 0; k < 3; k++)
                    crop_frame(&ks, pn, CROP_W, CROP_H);
                DRAW_WINDOW(49, 230, 9);
                for (int y = 27 + 9; y <= 27 + 118; y++) {
                    wp[y * CROP_W + 24 + 49] = wp[y * CROP_W + 24 + 230] = 0xFFFFFFFFu;
                    for (int x = 24 + 50; x < 24 + 230 && y > 28 + 9; x++)
                        wp[y * CROP_W + x] = 0xFF000000u | (uint32_t)(x << 11) | (uint32_t)y; /* the window text */
                }
                SET_WIN(&ks, 49, 230, 9, 118, 0);
                crop_frame(&ks, wp, CROP_W, CROP_H);
                crop_pan_step(&ks, CROP_FILL);
                CHECK(crop_view_state(CROP_FILL, &ks, CROP_W, CROP_H, &sv) && sv.shrink_n == 1 && sv.shrink_cx == 140 &&
                          sv.shrink_cy == 64 && !sv.shrink_map && !ks.scene && ks.pan_q == 0 && ks.pan_qy == 0,
                      "fill (TV): window at 3/4 (%d windows, middle %d,%d, map without crop_ring %d, slide %d,%d)", sv.shrink_n,
                      sv.shrink_cx, sv.shrink_cy, sv.shrink_map != NULL, ks.pan_q, ks.pan_qy);
                int os = crop_out_sub(&sv), kw = sv.w * os, mx = (sv.piece[0].sx - 24) * os, my = (sv.piece[0].sy - 27) * os;
                crop_compose_sub(wp, CROP_W, 1, &sv, ko, os);
#define KO(x4, y4) ko[((y4) - my) * kw + (x4) - mx] /* (x4, y4) = the map pixel in quarter pixels */
                /* window pixel (160, 74): 20, 10 from the middle -> 15, 7.5 (3 quarter pixels each) */
                int in_ok = 1, white = 0;
                for (int j = 0; j < 3; j++)
                    in_ok &= KO(140 * 4 + 60 + j, 64 * 4 + 30 + j) == (wp[(27 + 74) * CROP_W + 24 + 160] | 0xFF000000u);
                in_ok &= KO(140 * 4 + 63, 64 * 4 + 30) == (wp[(27 + 74) * CROP_W + 24 + 161] | 0xFF000000u);
                /* top edge: line 9 -> 22.75 (quarters 91-93); columns 49-230 -> 182 pixels x 3 */
                for (int x = 0; x < 288 * 4; x++)
                    white += KO(x, 92) == 0xFFFFFFFFu;
                CHECK(in_ok && white == 182 * 3 && KO(287, 92) == 0xFFFFFFFFu && KO(286, 92) != 0xFFFFFFFFu,
                      "fill (TV): window at 3/4 (right pixels %d, top edge with %d quarters)", in_ok, white);
                /* crop_ring: the map redrawn at the current scroll (here, shifted 1 pixel) and the current sprites on
                 * top: an NPC walking with the menu open (in front of the background) appears; one behind the opaque
                 * background does not */
                {
                    static uint32_t rbg[CROP_W * CROP_H], rspr[CROP_W * CROP_H];
                    for (int i = 0; i < CROP_W * CROP_H; i++) {
                        rbg[i] = pn[i];
                        rspr[i] = 0;
                    }
                    for (int y = 27; y < 27 + 128; y++)
                        for (int x = 24; x < 24 + 287; x++)
                            rbg[y * CROP_W + x] = (pn[y * CROP_W + x + 1] & 0xFFFFFFu) | 0xFF000000u;
                    rspr[(27 + 12) * CROP_W + 24 + 52] = 0xFF123456u; /* in front */
                    rspr[(27 + 13) * CROP_W + 24 + 52] = 0x80654321u; /* behind the opaque background */
                    crop_ring(&ks, rbg, rspr);
                    crop_view_state(CROP_FILL, &ks, CROP_W, CROP_H, &sv);
                    crop_compose_sub(wp, CROP_W, 1, &sv, ko, os);
                    CHECK(ks.bg_px == rbg && ks.spr_px == rspr && KO(52 * 4 + 1, 12 * 4 + 1) == 0xFF123456u &&
                              KO(52 * 4 + 1, 13 * 4 + 1) == rbg[(27 + 13) * CROP_W + 24 + 52] &&
                              KO(53 * 4 + 1, 14 * 4 + 1) == rbg[(27 + 14) * CROP_W + 24 + 53],
                          "fill (TV): around it, the map at the current scroll and the current sprite in front");
                    crop_frame(&ks, wp, CROP_W, CROP_H);
                    CHECK(!ks.bg_px && !ks.spr_px, "fill (TV): crop_ring holds until the next frame");
                }
#undef KO
                /* outside fill with the HUD at 3/4, the window stays as the game draws it */
                CHECK(crop_view_state(CROP_WIDE, &ks, CROP_W, CROP_H, &sv) && sv.shrink_n == 0, "wide: whole window");
                ks.par = 1.0;
                ks.screen = 240.5 / 154.0;
                CHECK(crop_view_state(CROP_FILL, &ks, CROP_W, CROP_H, &sv) && sv.shrink_n == 0,
                      "fill (square pixels, normal HUD): whole window");
                /* rewards on kill (+EXP/+GOLD) at 3/4, anchored to the same top middle; the HP bar does not change */
                static Battle kb;
                static double kx[2] = { 100.0, 100.0 }, ky[2] = { 60.0, 80.0 };
                memset(&kb, 0, sizeof(kb));
                kb.count = 2;
                for (int i = 0; i < 2; i++) {
                    kb.st[i].kind = i ? BATTLE_BAR : BATTLE_POPUP;
                    kb.st[i].w = 20;
                    kb.st[i].h = 10;
                    kb.st[i].alpha = 255;
                    for (int p = 0; p < 200; p++)
                        kb.st[i].px[p] = 0xFFFFFFFFu;
                }
                int counts[2][2], left[2] = { 0, 0 };
                for (int sm = 0; sm < 2; sm++) {
                    kb.small = sm;
                    memset(ko, 0, sizeof(ko));
                    battle_draw(&kb, kx, ky, ko, CROP_W, CROP_H, 4, CROP_W * 4);
                    counts[sm][0] = counts[sm][1] = 0;
                    left[sm] = CROP_W * 4;
                    for (int y = 0; y < CROP_H * 4; y++)
                        for (int x = 0; x < CROP_W * 4; x++)
                            if (ko[y * CROP_W * 4 + x]) {
                                counts[sm][y >= 80 * 4]++;
                                left[sm] = y < 80 * 4 && x < left[sm] ? x : left[sm];
                            }
                }
                CHECK(counts[0][0] == 80 * 40 && counts[1][0] == 60 * 30 && left[1] == 400 + 10 &&
                          counts[0][1] == 80 * 40 && counts[1][1] == 80 * 40,
                      "rewards at 3/4 (%d -> %d quarters, from %d), bar the same (%d, %d)", counts[0][0], counts[1][0],
                      left[1], counts[0][1], counts[1][1]);
            }
#undef DRAW_WINDOW
#undef SET_WIN
        }
        CropView hv;
        CHECK(crop_view(CROP_FILL, CROP_SCREEN_LOAD, CROP_W, CROP_H, &hv) && hv.w == 320 && hv.h == CROP_FRAME_VIEW_H &&
                  !hv.band,
              "Fill: load screen with the border, as in Frame");

        /* Frame: 320 x 213, the border and the panel, 4 black spacing lines, bar corner complete */
        CropView fv;
        CHECK(crop_view(CROP_FRAME, CROP_SCREEN_GAME, CROP_W, CROP_H, &fv) && fv.w == 320 && fv.h == CROP_FRAME_VIEW_H,
              "Frame: %dx%d", fv.w, fv.h);
        fv.panel = &ppn; /* the bars from the panel cells */
        crop_compose(pn, CROP_W, 1, &fv, out);
        int fbad = 0, below = 0;
        for (int y = 0; y < fv.h; y++)
            for (int x = 0; x < fv.w; x++) {
                uint32_t g = out[y * fv.w + x];
                if (y + 11 > 219)
                    below += (g & 0xFFFFFF) != 0;
                else if (!(x == 63 && y == 219 - 11))
                    fbad += g != pn[(11 + y) * CROP_W + 8 + x];
            }
        CHECK(fbad == 0 && below == 0 && (out[(219 - 11) * fv.w + 63] & 0xFFFFFF) == 0xFFFFFF,
              "Frame: %d pixels out of place, %d lit in the spacing, bar corner complete", fbad, below);

        /* Full: the band below the map in the background color, with the two-line HUD (lines 12-30; 2/3 of the spare room on top) */
        CropView uv;
        crop_view(CROP_FULL, CROP_SCREEN_GAME, CROP_W, CROP_H, &uv);
        uv.hud_bg = CROP_HUD_PLUM;
        crop_compose(pn, CROP_W, 1, &uv, out);
        int strip_bad = 0;
        for (int y = CROP_MAP_H; y < uv.h; y++)
            for (int x = 0; x < uv.w; x++)
                if (y < CROP_MAP_H + 12 || y >= CROP_MAP_H + 31)
                    strip_bad += out[y * uv.w + x] != crop_hud_pixel(CROP_HUD_PLUM, x, y - CROP_MAP_H, uv.w, CROP_HUD_H);
        CHECK(uv.band && !uv.hud && uv.w == 288 && uv.h == CROP_MAP_H + CROP_HUD_H && strip_bad == 0, "Full: %dx%d, band %d wrong",
              uv.w, uv.h, strip_bad);
        static const struct { int w, h, integer, square; } screens[] = {
            { 2560, 1440, 0, 0 }, { 1920, 1080, 0, 0 }, { 2560, 1440, 1, 0 }, { 1920, 1080, 0, 1 }, { 1024, 768, 0, 0 },
        };
        for (int k = 0; k < 5; k++) {
            VideoSettings vs;
            video_defaults(&vs);
            vs.integer_scale = screens[k].integer;
            vs.aspect = screens[k].square ? VIDEO_ASPECT_SQUARE : VIDEO_ASPECT_TV;
            VideoRect full, fr, pr;
            int kx, ky;
            video_layout(screens[k].w, screens[k].h, uv.w, uv.h, 3, &vs, &full, &kx, &ky);
            video_layout(screens[k].w, screens[k].h, 320, CROP_FRAME_VIEW_H, 3, &vs, &fr, &kx, &ky);
            crop_hud_place(screens[k].w, screens[k].h, 3, &vs, &full, &pr);
            int strip_y = full.y + full.h * CROP_MAP_H / uv.h;
            int same = pr.w == fr.w && pr.h == fr.h * CROP_PANEL_H / CROP_FRAME_VIEW_H;
            int inside = pr.x >= full.x && pr.x + pr.w <= full.x + full.w && pr.y >= strip_y &&
                         pr.y + pr.h <= full.y + full.h;
            int centered = abs((pr.x - full.x) - (full.x + full.w - pr.x - pr.w)) <= 1 &&
                           abs((pr.y - strip_y) - (full.y + full.h - pr.y - pr.h)) <= 1;
            printf("panel in full %dx%d (integer %d, square %d): %dx%d at %d,%d; image %dx%d at %d,%d\n",
                   screens[k].w, screens[k].h, screens[k].integer, screens[k].square, pr.w, pr.h, pr.x, pr.y, full.w,
                   full.h, full.x, full.y);
            CHECK(inside && centered && (same || k == 4), "panel in full %dx%d: inside %d, centered %d, frame size %d",
                  screens[k].w, screens[k].h, inside, centered, same);
            if (screens[k].integer)
                CHECK(pr.w % 320 == 0 && pr.h % CROP_PANEL_H == 0, "integer scale: panel %dx%d in whole pixels",
                      pr.w, pr.h);
        }
    }

    /* On screen (1920x1080, 8:7 pixels of the 341-pixel mode: divider 3) */
    SDL_Surface *s = SDL_CreateSurface(1920, 1080, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    SDL_Texture *t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CROP_W, CROP_H) : NULL;
    if (!t) {
        CHECK(0, "software renderer: %s", SDL_GetError());
        return;
    }
    Presenter p = { 0 };
    VideoSettings v;
    VideoRect d;
    int kx, ky;
    Uint32 crcs[4];
    static const char *const names[4] = { "1_off", "2_frame", "3_full", "4_full_integer" };
    for (int k = 0; k < 4; k++) {
        video_defaults(&v);
        v.integer_scale = k == 3;
        int mode = k == 0 ? CROP_OFF : k == 1 ? CROP_FRAME : CROP_FULL;
        int w = CROP_W, h = CROP_H;
        const uint32_t *shown = src;
        if (crop_view(mode, 1, CROP_W, CROP_H, &cv)) {
            crop_compose(src, CROP_W, 1, &cv, out);
            w = cv.w;
            h = cv.h;
            shown = out;
        }
        SDL_Rect area = { 0, 0, w, h };
        SDL_UpdateTexture(t, &area, shown, w * (int)sizeof(uint32_t));
        present_draw(&p, r, t, w, h, 1, 3, &v);
        SDL_FlushRenderer(r);
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.bmp", work, names[k]);
        SDL_SaveBMP(s, path);
        crcs[k] = 0;
        for (int y = 0; y < s->h; y++)
            crcs[k] = SDL_crc32(crcs[k], (const Uint8 *)s->pixels + y * s->pitch, (size_t)s->w * 4);
        video_layout(1920, 1080, w, h, 3, &v, &d, &kx, &ky);
        /* Center of each pixel with the exact color (sharp); outside the image, black */
        bad = 0;
        for (int sy = 0; sy < h; sy++)
            for (int sx = 0; sx < w; sx++) {
                int x = d.x + (int)((sx + 0.5) * d.w / w), y = d.y + (int)((sy + 0.5) * d.h / h);
                bad += out_px(s, x, y) != (shown[sy * w + sx] | 0xFF000000u);
            }
        int outside = 0;
        for (int y = 0; y < 1080; y += 7)
            for (int x = 0; x < 1920; x += 7)
                if (x < d.x || x >= d.x + d.w || y < d.y || y >= d.y + d.h)
                    outside += (out_px(s, x, y) & 0xFFFFFF) != 0;
        CHECK(bad == 0 && outside == 0, "%s: %d pixels with another color, %d lit outside", names[k], bad, outside);
        printf("crop %s: %dx%d on screen at %d,%d (image %dx%d)\n", names[k], d.w, d.h, d.x, d.y, w, h);
        if (k == 0)
            CHECK(d.h == 1080 && d.w == 1304, "whole: %dx%d", d.w, d.h);
        if (k == 1)
            CHECK(d.h == 1080 && d.w == 1391, "Frame: %dx%d (14%% larger than whole)", d.w, d.h);
        if (k == 2)
            CHECK(d.w == 1606 && d.h == 1080, "Full: %dx%d (46%% larger, the map width)", d.w, d.h);
        if (k == 3)
            CHECK(kx == 5 && ky == 6 && d.w == 1440 && d.h == 996, "Full integer: %dx%d k %dx%d", d.w, d.h, kx, ky);
    }
    static const Uint32 want[4] = { 0x06CE5840, 0x820BA470, 0xCE7DC15E, 0x42E637CB }; /* BMPs in build/app_output/crop */
    for (int k = 0; k < 4; k++) {
        printf("crop %s: CRC %08X\n", names[k], (unsigned)crcs[k]);
        CHECK(!want[k] || crcs[k] == want[k], "crop %s changed (CRC %08X, expected %08X)", names[k],
              (unsigned)crcs[k], (unsigned)want[k]);
    }
    /* Full with the panel on top (like the window: present_overlay instead of crop_hud_place): sharp (the center
     * of each panel pixel with the exact color) and the map image underneath intact */
    {
        static uint32_t pnl[CROP_PANEL_W * CROP_PANEL_H];
        for (int y = 0; y < CROP_PANEL_H; y++)
            for (int x = 0; x < CROP_PANEL_W; x++)
                pnl[y * CROP_PANEL_W + x] = (x + y) % 2 ? 0xFFFFFFFFu : 0xFF004848u;
        SDL_Texture *pt = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CROP_PANEL_W,
                                            CROP_PANEL_H);
        crop_view(CROP_FULL, 1, CROP_W, CROP_H, &cv);
        crop_compose(src, CROP_W, 1, &cv, out);
        SDL_Rect area = { 0, 0, cv.w, cv.h };
        SDL_UpdateTexture(t, &area, out, cv.w * (int)sizeof(uint32_t));
        SDL_UpdateTexture(pt, NULL, pnl, CROP_PANEL_W * (int)sizeof(uint32_t));
        video_defaults(&v);
        present_draw(&p, r, t, cv.w, cv.h, 1, 3, &v);
        VideoRect full, pr;
        video_layout(1920, 1080, cv.w, cv.h, 3, &v, &full, &kx, &ky);
        crop_hud_place(1920, 1080, 3, &v, &full, &pr);
        present_overlay(&p, r, pt, CROP_PANEL_W, CROP_PANEL_H, &pr, &v);
        SDL_FlushRenderer(r);
        char path[1024];
        snprintf(path, sizeof(path), "%s/5_full_painel.bmp", work);
        SDL_SaveBMP(s, path);
        int pbad = 0, mbad = 0;
        for (int sy = 0; sy < CROP_PANEL_H; sy++)
            for (int sx = 0; sx < CROP_PANEL_W; sx++) {
                int x = pr.x + (int)((sx + 0.5) * pr.w / CROP_PANEL_W), y = pr.y + (int)((sy + 0.5) * pr.h / CROP_PANEL_H);
                pbad += out_px(s, x, y) != pnl[sy * CROP_PANEL_W + sx];
            }
        for (int sy = 0; sy < CROP_MAP_H; sy++)
            for (int sx = 0; sx < cv.w; sx++) {
                int x = full.x + (int)((sx + 0.5) * full.w / cv.w), y = full.y + (int)((sy + 0.5) * full.h / cv.h);
                mbad += out_px(s, x, y) != (out[sy * cv.w + sx] | 0xFF000000u);
            }
        CHECK(pbad == 0 && mbad == 0, "Full with panel: %d panel pixels and %d map pixels with another color", pbad, mbad);
        /* CRT on the panel too: each of its lines bright in the middle and dark at the edges, and the color grille in
         * the same phase as the image (the column has the same dominant color inside the panel and in the band just
         * above it) */
        for (int i = 0; i < CROP_PANEL_W * CROP_PANEL_H; i++)
            pnl[i] = 0xFFFFFFFFu;
        SDL_UpdateTexture(pt, NULL, pnl, CROP_PANEL_W * (int)sizeof(uint32_t));
        for (int i = 0; i < cv.w * cv.h; i++)
            out[i] = 0xFFFFFFFFu;
        SDL_UpdateTexture(t, &area, out, cv.w * (int)sizeof(uint32_t));
        v.crt = VIDEO_CRT_MASK;
        present_draw(&p, r, t, cv.w, cv.h, 1, 3, &v);
        present_overlay(&p, r, pt, CROP_PANEL_W, CROP_PANEL_H, &pr, &v);
        SDL_FlushRenderer(r);
        int lines_ok = 0, phase_ok = 0;
        int lx = pr.x + pr.w / 2;
        for (int sy = 2; sy < CROP_PANEL_H - 2; sy++) {
            int top = pr.y + (int)((double)sy * pr.h / CROP_PANEL_H), mid = pr.y + (int)((sy + 0.5) * pr.h / CROP_PANEL_H);
            Uint32 ct = out_px(s, lx, top), cm = out_px(s, lx, mid);
            int bt = (int)(ct >> 16 & 0xFF) + (int)(ct >> 8 & 0xFF) + (int)(ct & 0xFF);
            int bm = (int)(cm >> 16 & 0xFF) + (int)(cm >> 8 & 0xFF) + (int)(cm & 0xFF);
            lines_ok += bm > bt + 60;
        }
        for (int x = pr.x + 10; x < pr.x + 40; x++) {
            int y_in = pr.y + (int)(20.5 * pr.h / CROP_PANEL_H), y_up = pr.y - 6;
            Uint32 a1 = out_px(s, x, y_in), a2 = out_px(s, x, y_up);
            int strong1 = (a1 >> 16 & 0xFF) > (a1 & 0xFF) + 20 ? 0 : (a1 >> 8 & 0xFF) > (a1 & 0xFF) + 20 ? 1 : 2;
            int strong2 = (a2 >> 16 & 0xFF) > (a2 & 0xFF) + 20 ? 0 : (a2 >> 8 & 0xFF) > (a2 & 0xFF) + 20 ? 1 : 2;
            phase_ok += strong1 == strong2;
        }
        CHECK(lines_ok == CROP_PANEL_H - 4 && phase_ok == 30, "CRT on the panel: %d of %d lines, grille %d of 30 columns",
              lines_ok, CROP_PANEL_H - 4, phase_ok);
        SDL_DestroyTexture(pt);
    }
    /* Transparent layer (the options menu at the scale of the cropped image, present_layer), with the CRT, sharp
     * and smooth: where it is transparent the screen stays exactly as without it (the CRT is not applied twice);
     * its red block appears, with the CRT lines */
    {
        static uint32_t lay[CROP_W * CROP_H], ref[1920 * 1080];
        SDL_Texture *lt = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CROP_W, CROP_H);
        crop_view(CROP_FULL, 1, CROP_W, CROP_H, &cv);
        crop_compose(src, CROP_W, 1, &cv, out);
        SDL_Rect area = { 0, 0, cv.w, cv.h };
        SDL_UpdateTexture(t, &area, out, cv.w * (int)sizeof(uint32_t));
        memset(lay, 0, sizeof(lay));
        for (int y = 40; y < 60; y++)
            for (int x = 100; x < 150; x++)
                lay[y * cv.w + x] = 0xFFFF0000u;
        SDL_UpdateTexture(lt, &area, lay, cv.w * (int)sizeof(uint32_t));
        for (int k = 0; k < 2; k++) {
            video_defaults(&v);
            v.crt = VIDEO_CRT_MASK;
            v.smooth = k == 1;
            present_draw(&p, r, t, cv.w, cv.h, 1, 3, &v);
            SDL_FlushRenderer(r);
            for (int y = 0; y < 1080; y++)
                memcpy(&ref[y * 1920], (const Uint8 *)s->pixels + y * s->pitch, 1920 * 4);
            present_draw(&p, r, t, cv.w, cv.h, 1, 3, &v);
            VideoRect lr;
            video_layout(1920, 1080, cv.w, cv.h, 3, &v, &lr, &kx, &ky);
            present_layer(&p, r, lt, cv.w, cv.h, &lr, &v);
            SDL_FlushRenderer(r);
            int mx = k ? lr.w / cv.w + 2 : 2, my = k ? lr.h / cv.h + 2 : 2; /* smooth blurs 1 pixel around */
            int bx0 = lr.x + 100 * lr.w / cv.w - mx, bx1 = lr.x + 150 * lr.w / cv.w + mx;
            int by0 = lr.y + 40 * lr.h / cv.h - my, by1 = lr.y + 60 * lr.h / cv.h + my;
            int changed = 0, red = 0, dark_mid = 0;
            for (int y = 0; y < 1080; y++)
                for (int x = 0; x < 1920; x++) {
                    Uint32 c = out_px(s, x, y);
                    if (x < bx0 || x > bx1 || y < by0 || y > by1)
                        changed += c != (ref[y * 1920 + x] | 0xFF000000u);
                    else if (x > bx0 + 6 && x < bx1 - 6 && y > by0 + 6 && y < by1 - 6)
                        red += (c >> 16 & 0xFF) > 2 * ((c >> 8 & 0xFF) + (c & 0xFF)) + 40;
                }
            /* CRT lines on the block: at the edge of each image line, darker than in the middle */
            for (int sy = 42; sy < 58; sy++) {
                int top = lr.y + (int)((double)sy * lr.h / cv.h), mid = lr.y + (int)((sy + 0.5) * lr.h / cv.h);
                int xm = lr.x + 125 * lr.w / cv.w;
                dark_mid += (out_px(s, xm, mid) >> 16 & 0xFF) > (out_px(s, xm, top) >> 16 & 0xFF) + 20;
            }
            CHECK(changed == 0 && red > 0 && dark_mid == 16,
                  "layer %s: %d pixels changed outside the block, %d red, CRT lines on %d of 16",
                  k ? "smooth" : "sharp", changed, red, dark_mid);
        }
        SDL_DestroyTexture(lt);
    }
    present_free(&p);
    SDL_DestroyTexture(t);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(s);
    printf("crop: %s\n", g_failures ? "with failures" : "ok");
}
/* ---- controls ---- */

/* Updates the virtual gamepad and passes the events through the fake window (connect/disconnect, menu by analog stick) */
static int pump_pads(InputPads *pads, int *menu_key_out)
{
    SDL_UpdateGamepads();
    SDL_Event ev;
    int n = 0;
    while (SDL_PollEvent(&ev)) {
        input_pads_event(pads, &ev);
        int k = input_stick_to_menu(pads, &ev);
        if (k >= 0 && menu_key_out) {
            *menu_key_out = k;
            n++;
        }
    }
    return n;
}

static void test_input(const char *work)
{
    InputMap map;
    input_default_keys(&map);
    input_default_buttons(&map);
    CHECK(map.key[PAD_FN_I] == SDL_SCANCODE_X && map.key[PAD_FN_II] == SDL_SCANCODE_Z &&
              map.key[PAD_FN_SELECT] == SDL_SCANCODE_SPACE && map.key[PAD_FN_RUN] == SDL_SCANCODE_RETURN,
          "default keys");
    CHECK(map.button[PAD_FN_I] == SDL_GAMEPAD_BUTTON_SOUTH && map.button[PAD_FN_II] == SDL_GAMEPAD_BUTTON_EAST &&
              map.button[PAD_FN_SELECT] == SDL_GAMEPAD_BUTTON_NORTH && map.button[PAD_FN_RUN] == SDL_GAMEPAD_BUTTON_WEST,
          "default buttons: A = I, B = II, Y = SELECT, X = RUN");

    /* Rebinding: another function's binding swaps places, reserved refused, the rest accepted */
    int other = -1;
    CHECK(input_set_key(&map, PAD_FN_I, SDL_SCANCODE_Z, &other) == INPUT_SET_SWAPPED && other == PAD_FN_II &&
              map.key[PAD_FN_I] == SDL_SCANCODE_Z && map.key[PAD_FN_II] == SDL_SCANCODE_X,
          "II's key on I: the two swap");
    CHECK(input_set_key(&map, PAD_FN_I, SDL_SCANCODE_X, &other) == INPUT_SET_SWAPPED && map.key[PAD_FN_I] == SDL_SCANCODE_X &&
              map.key[PAD_FN_II] == SDL_SCANCODE_Z,
          "and swap back");
    CHECK(input_set_key(&map, PAD_FN_I, SDL_SCANCODE_ESCAPE, NULL) == INPUT_SET_RESERVED, "Esc: reserved for the menu");
    CHECK(input_set_key(&map, PAD_FN_I, SDL_SCANCODE_X, NULL) == INPUT_SET_OK, "the same key again: accepted");
    CHECK(input_set_key(&map, PAD_FN_I, SDL_SCANCODE_C, NULL) == INPUT_SET_OK && map.key[PAD_FN_I] == SDL_SCANCODE_C,
          "C on I");
    CHECK(input_set_button(&map, PAD_FN_RUN, SDL_GAMEPAD_BUTTON_SOUTH, &other) == INPUT_SET_SWAPPED && other == PAD_FN_I &&
              map.button[PAD_FN_I] == SDL_GAMEPAD_BUTTON_WEST && map.button[PAD_FN_RUN] == SDL_GAMEPAD_BUTTON_SOUTH,
          "I's button (A) on RUN: I gets X");
    CHECK(input_set_button(&map, PAD_FN_RUN, SDL_GAMEPAD_BUTTON_START, NULL) == INPUT_SET_RESERVED &&
              input_set_button(&map, PAD_FN_RUN, SDL_GAMEPAD_BUTTON_GUIDE, NULL) == INPUT_SET_RESERVED,
          "Menu and Xbox: reserved");
    CHECK(input_set_button(&map, PAD_FN_RUN, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, NULL) == INPUT_SET_OK, "LB on RUN");

    /* Keyboard -> PC-Engine buttons */
    static bool keys[SDL_SCANCODE_COUNT];
    keys[SDL_SCANCODE_X] = keys[SDL_SCANCODE_UP] = true;
    CHECK(input_keyboard_bits(&map, keys) == PCE_PAD_UP, "X is no longer I");
    keys[SDL_SCANCODE_C] = keys[SDL_SCANCODE_RETURN] = true;
    CHECK(input_keyboard_bits(&map, keys) == (PCE_PAD_UP | PCE_PAD_I | PCE_PAD_RUN), "C = I, Enter = RUN");
    CHECK(input_key_to_menu(&map, SDL_SCANCODE_C) == MENU_KEY_OK && input_key_to_menu(&map, SDL_SCANCODE_Z) == MENU_KEY_BACK &&
              input_key_to_menu(&map, SDL_SCANCODE_ESCAPE) == MENU_KEY_BACK && input_key_to_menu(&map, SDL_SCANCODE_Q) < 0,
          "keys in the menu");

    /* Configuration: write and read; missing, invalid or repeated returns to the default */
    char path[1024];
    snprintf(path, sizeof(path), "%s/controles.ini", work);
    static Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    InputMap got;
    CHECK(input_load(&cfg, &got) == 1 && got.key[PAD_FN_RUN] == SDL_SCANCODE_RETURN &&
              strcmp(config_get(&cfg, "pad_i", ""), "a") == 0 && strcmp(config_get(&cfg, "key_run", ""), "Return") == 0,
          "no keys: default, added to the configuration (pad_i=%s key_run=%s)", config_get(&cfg, "pad_i", ""),
          config_get(&cfg, "key_run", ""));
    input_store(&cfg, &map);
    CHECK(config_save(&cfg, path) == 0, "write %s", path);
    memset(&cfg, 0, sizeof(cfg));
    CHECK(config_load(&cfg, path) == 0, "read %s", path);
    CHECK(input_load(&cfg, &got) == 0 && memcmp(&got, &map, sizeof(map)) == 0, "read = written (key_i=%s pad_run=%s)",
          config_get(&cfg, "key_i", ""), config_get(&cfg, "pad_run", ""));
    config_set(&cfg, "key_ii", "Nada");
    config_set(&cfg, "key_select", "C"); /* C is already I */
    config_set(&cfg, "pad_up", "start");
    CHECK(input_load(&cfg, &got) == 1 && got.key[PAD_FN_II] == SDL_SCANCODE_Z && got.key[PAD_FN_I] == SDL_SCANCODE_C &&
              got.key[PAD_FN_SELECT] == SDL_SCANCODE_SPACE && got.button[PAD_FN_UP] == SDL_GAMEPAD_BUTTON_DPAD_UP &&
              strcmp(config_get(&cfg, "key_select", ""), "Space") == 0,
          "invalid/repeated/reserved: returns to the default and fixes the configuration");

    /* SDL virtual gamepad (the same path as an Xbox controller via XInput) */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        CHECK(0, "SDL_Init(GAMEPAD): %s", SDL_GetError());
        return;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Virtual gamepad";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick *joy = id ? SDL_OpenJoystick(id) : NULL;
    CHECK(joy != NULL, "virtual gamepad: %s", SDL_GetError());
    if (!joy)
        return;
    static InputPads pads;
    pump_pads(&pads, NULL);
    int slot = -1, others = 0;
    for (int p = 0; p < INPUT_MAX_PADS; p++)
        if (pads.pad[p] && SDL_GetGamepadID(pads.pad[p]) == id)
            slot = p;
        else if (pads.pad[p])
            others++;
    CHECK(slot >= 0, "gamepad connected by the event");
    if (others) {
        /* Real gamepad connected to the PC: left out of the count (only the virtual one is pressed) */
        printf("(%d real gamepad(s) connected: left out)\n", others);
        for (int p = 0; p < INPUT_MAX_PADS; p++)
            if (p != slot && pads.pad[p]) {
                SDL_CloseGamepad(pads.pad[p]);
                pads.pad[p] = NULL;
            }
    }
    input_default_buttons(&map);
    static const struct { int button; uint8_t bits; const char *what; } presses[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, PCE_PAD_I, "A = I" },
        { SDL_GAMEPAD_BUTTON_EAST, PCE_PAD_II, "B = II" },
        { SDL_GAMEPAD_BUTTON_NORTH, PCE_PAD_SELECT, "Y = SELECT" },
        { SDL_GAMEPAD_BUTTON_WEST, PCE_PAD_RUN, "X = RUN" },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, PCE_PAD_UP, "d-pad up" },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, PCE_PAD_DOWN, "d-pad down" },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, PCE_PAD_LEFT, "d-pad left" },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PCE_PAD_RIGHT, "d-pad right" },
        { SDL_GAMEPAD_BUTTON_BACK, 0, "View: unbound" },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0, "LB: unbound" },
        { SDL_GAMEPAD_BUTTON_START, 0, "Menu: does not reach the game" },
    };
    for (size_t i = 0; i < sizeof(presses) / sizeof(presses[0]); i++) {
        SDL_SetJoystickVirtualButton(joy, presses[i].button, true);
        pump_pads(&pads, NULL);
        uint8_t bits = input_pads_bits(&pads, &map);
        CHECK(bits == presses[i].bits, "%s: the console receives %02X (expected %02X)", presses[i].what, bits, presses[i].bits);
        SDL_SetJoystickVirtualButton(joy, presses[i].button, false);
    }
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, true);
    pump_pads(&pads, NULL);
    CHECK(input_pads_menu_pressed(&pads), "Menu button pressed");
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, false);
    pump_pads(&pads, NULL);
    CHECK(input_pads_bits(&pads, &map) == 0 && !input_pads_menu_pressed(&pads), "all released");
    CHECK(input_button_to_menu(&map, SDL_GAMEPAD_BUTTON_SOUTH) == MENU_KEY_OK &&
              input_button_to_menu(&map, SDL_GAMEPAD_BUTTON_EAST) == MENU_KEY_BACK &&
              input_button_to_menu(&map, SDL_GAMEPAD_BUTTON_START) == MENU_KEY_BACK &&
              input_button_to_menu(&map, SDL_GAMEPAD_BUTTON_DPAD_DOWN) == MENU_KEY_DOWN &&
              input_button_to_menu(&map, SDL_GAMEPAD_BUTTON_BACK) < 0,
          "buttons in the menu");

    /* Left stick: d-pad with a 50% dead zone */
    static const struct { int x, y; uint8_t bits; const char *what; } sticks[] = {
        { 10000, 0, 0, "slightly right: dead zone" },
        { 20000, 0, PCE_PAD_RIGHT, "right" },
        { -32768, 0, PCE_PAD_LEFT, "left" },
        { 0, -20000, PCE_PAD_UP, "up" },
        { 0, 32767, PCE_PAD_DOWN, "down" },
        { 23170, -23170, PCE_PAD_RIGHT | PCE_PAD_UP, "45-degree diagonal" },
        { 30000, -8000, PCE_PAD_RIGHT, "almost horizontal: right only" },
    };
    for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)sticks[i].x);
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)sticks[i].y);
        pump_pads(&pads, NULL);
        uint8_t bits = input_pads_bits(&pads, &map);
        CHECK(bits == sticks[i].bits, "analog %s: %02X (expected %02X)", sticks[i].what, bits, sticks[i].bits);
    }
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 0);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    pump_pads(&pads, NULL);
    int mk = -1;
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 30000);
    int n = pump_pads(&pads, &mk);
    CHECK(n == 1 && mk == MENU_KEY_DOWN, "analog in the menu: one step down (%d, key %d)", n, mk);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 31000);
    CHECK(pump_pads(&pads, &mk) == 0, "holding: does not repeat");
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    pump_pads(&pads, NULL);

    /* Disconnect the gamepad */
    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(id);
    pump_pads(&pads, NULL);
    CHECK(slot >= 0 && pads.pad[slot] == NULL, "gamepad disconnected by the event");
    input_pads_close(&pads);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);

    /* Through the menu: Keyboard -> I -> repeated key (refused), new key, Esc cancels, Defaults */
    Menu m;
    AudioVolumes vol = { 100, 100, 100, 100, 100 };
    input_default_keys(&map);
    input_default_buttons(&map);
    VideoSettings video;
    video_defaults(&video);
    menu_open(&m, &vol, 1, 1, &map, &video, NULL);
    while (m.cursor != MENU_ITEM_CONTROLS)
        menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_KEYBOARD && m.item == PAD_FN_UP, "opens Keyboard");
    for (int i = 0; i < PAD_FN_I; i++)
        menu_key(&m, MENU_KEY_DOWN);
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && menu_waiting_key(&m) && !menu_waiting_button(&m), "I: waits for the key");
    CHECK(menu_capture_key(&m, SDL_SCANCODE_Z) == MENU_CHANGED_INPUT && m.notice == INPUT_SET_SWAPPED &&
              m.swapped_with == PAD_FN_II && m.input.key[PAD_FN_I] == SDL_SCANCODE_Z &&
              m.input.key[PAD_FN_II] == SDL_SCANCODE_X && !m.waiting,
          "Z (was II's): I and II swap");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_capture_key(&m, SDL_SCANCODE_C) == MENU_CHANGED_INPUT && m.input.key[PAD_FN_I] == SDL_SCANCODE_C &&
              !m.waiting && !m.notice,
          "C: accepted");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_capture_key(&m, SDL_SCANCODE_ESCAPE) == 0 && !m.waiting && m.input.key[PAD_FN_I] == SDL_SCANCODE_C,
          "Esc cancels");
    m.item = MENU_PAGE_DEFAULTS;
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_INPUT && m.input.key[PAD_FN_I] == SDL_SCANCODE_X, "Defaults");
    CHECK(menu_key(&m, MENU_KEY_OK) == 0, "Defaults again: nothing changes");
    CHECK(menu_key(&m, MENU_KEY_BACK) == 0 && m.page == MENU_PAGE_CONTROLS && m.item == MENU_CONTROLS_KEYBOARD && m.open,
          "Esc on the page: back to Controls");
    menu_key(&m, MENU_KEY_DOWN);
    menu_key(&m, MENU_KEY_OK);
    CHECK(m.page == MENU_PAGE_CONTROLLER, "opens Controller");
    m.item = PAD_FN_RUN;
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_waiting_button(&m) && menu_capture_key(&m, SDL_SCANCODE_C) == 0, "Controller waits for a button, not a key");
    CHECK(menu_capture_button(&m, SDL_GAMEPAD_BUTTON_GUIDE) == 0 && m.notice == INPUT_SET_RESERVED && !m.waiting,
          "Xbox button: not allowed");
    menu_key(&m, MENU_KEY_OK);
    CHECK(menu_capture_button(&m, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == MENU_CHANGED_INPUT &&
              m.input.button[PAD_FN_RUN] == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
          "LB on RUN");
    CHECK(input_layout(&m.input) == INPUT_LAYOUT_CUSTOM, "button changed by hand: Custom layout");
    m.item = MENU_PAGE_DEFAULTS;
    CHECK(menu_key(&m, MENU_KEY_OK) == MENU_CHANGED_INPUT && input_layout(&m.input) == INPUT_LAYOUT_MODERN &&
              m.input.button[PAD_FN_RUN] == SDL_GAMEPAD_BUTTON_WEST,
          "Layout: from Custom to Modern");
    CHECK(menu_key(&m, MENU_KEY_RIGHT) == MENU_CHANGED_INPUT && input_layout(&m.input) == INPUT_LAYOUT_ORIGINAL &&
              m.input.button[PAD_FN_I] == SDL_GAMEPAD_BUTTON_EAST && m.input.button[PAD_FN_II] == SDL_GAMEPAD_BUTTON_SOUTH,
          "Layout Original: B = I, A = II");
    CHECK(input_button_to_menu(&m.input, SDL_GAMEPAD_BUTTON_SOUTH) == MENU_KEY_OK &&
              input_button_to_menu(&m.input, SDL_GAMEPAD_BUTTON_EAST) == MENU_KEY_BACK,
          "in the menu, A confirms and B goes back even with the Original layout");
    CHECK(menu_key(&m, MENU_KEY_LEFT) == MENU_CHANGED_INPUT && input_layout(&m.input) == INPUT_LAYOUT_MODERN,
          "Layout: back to Modern");
    m.item = MENU_PAGE_BACK;
    CHECK(menu_key(&m, MENU_KEY_OK) == 0 && m.page == MENU_PAGE_CONTROLS && m.item == MENU_CONTROLS_CONTROLLER, "Back");

    /* Page images */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/menu", work);
    SDL_CreateDirectory(dir);
    static const struct { int page, item, waiting, notice, pad_mode; const char *name; Uint32 crc; } shots[] = {
        { MENU_PAGE_KEYBOARD, PAD_FN_UP, 0, 0, 0, "teclado", 0xE2F26ACA },
        { MENU_PAGE_CONTROLLER, PAD_FN_RUN, 1, 0, 1, "controle_espera", 0x077D5767 },
        { MENU_PAGE_CONTROLLER, MENU_PAGE_DEFAULTS, 0, 0, 1, "controle_layout", 0xA8593F8B },
        { MENU_PAGE_KEYBOARD, PAD_FN_I, 0, INPUT_SET_SWAPPED, 0, "teclado_troca", 0xAE4708CE },
        { MENU_PAGE_MAIN, 0, 0, 0, 1, "menu_controle", 0x3645991A },
    };
    for (size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        menu_open(&m, &vol, 1, 1, &map, &video, NULL);
        m.page = shots[i].page;
        m.item = shots[i].item;
        m.waiting = shots[i].waiting;
        m.notice = shots[i].notice;
        m.swapped_with = PAD_FN_II;
        m.pad_mode = shots[i].pad_mode;
        Uint32 crc = menu_shot(&m, 341, 240, 1, dir, shots[i].name);
        printf("image %s: CRC %08X\n", shots[i].name, (unsigned)crc);
        CHECK(!shots[i].crc || crc == shots[i].crc, "image %s changed (CRC %08X, expected %08X)", shots[i].name,
              (unsigned)crc, (unsigned)shots[i].crc);
    }
    printf("controls: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- skip the intro ---- */

/* The screen the game reports on each frame: loading, "from NEC" at 10, logo at 20, copyright at 40 */
static IntroScreen intro_screen_at(int f)
{
    return f >= 40 ? INTRO_SCREEN_COPYRIGHT : f >= 20 ? INTRO_SCREEN_LOGO : f >= 10 ? INTRO_SCREEN_NEC : INTRO_SCREEN_LOADING;
}

/* Synthetic intro. Returns the frame at which fast-forward stopped (or -1); skip_at = frame of the request. */
static int intro_run(IntroSkip *s, int skip_at, int *done_at)
{
    int stop = -1;
    *done_at = -1;
    for (int f = 0; f < 50; f++) {
        if (f == skip_at)
            intro_skip(s);
        if (intro_frame(s, intro_screen_at(f)) && stop < 0)
            stop = f;
        if (!intro_active(s) && *done_at < 0)
            *done_at = f;
    }
    return stop;
}

static void test_intro(void)
{
    IntroSkip s;
    int done;
    intro_init(&s, 1, 0);
    CHECK(intro_run(&s, -1, &done) == -1 && done == 40, "no request: intro ends at the copyright (%d)", done);
    intro_init(&s, 1, 0);
    CHECK(intro_run(&s, 3, &done) == 10, "request while loading: stops at the 1st NEC frame");
    intro_init(&s, 1, 0);
    CHECK(intro_run(&s, 12, &done) == 20, "request at NEC: stops at the 1st logo frame");
    intro_init(&s, 1, 0);
    CHECK(intro_run(&s, 25, &done) == 40, "request at the logo: stops at the copyright");
    intro_init(&s, 1, 0);
    CHECK(intro_run(&s, 45, &done) == -1 && !s.fast, "after the intro: request ignored (Enter goes to the game)");
    intro_init(&s, 0, 0);
    CHECK(!intro_active(&s) && intro_run(&s, 3, &done) == -1, "no disc/with --input: nothing happens");

    /* Straight to the menu, with a fake "game": accepts RUN from frame 30 (before the copyright, like the real one);
     * each RUN taken locks the buttons for 20 frames; 15 frames after the 2nd, the main menu */
    intro_init(&s, 1, 1);
    CHECK(s.fast && intro_active(&s), "straight to the menu: fast-forward from the 1st frame");
    int runs = 0, run_frames[3] = { -1, -1, -1 }, stop = -1, unlock = 30, menu_at = -1;
    for (int f = 0; f < 200 && stop < 0; f++) {
        if (intro_auto_pad(&s, f >= unlock) & PCE_PAD_RUN) {
            if (runs < 3)
                run_frames[runs] = f;
            runs++;
            unlock = f + 20;
            if (runs == 2)
                menu_at = f + 15;
        }
        IntroScreen screen = menu_at >= 0 && f >= menu_at ? INTRO_SCREEN_MENU : intro_screen_at(f);
        if (intro_frame(&s, screen))
            stop = f;
    }
    CHECK(runs == 2 && run_frames[0] == 41 && run_frames[1] == 61, "RUN only from the copyright on, when accepted (%d times: %d and %d)",
          runs, run_frames[0], run_frames[1]);
    CHECK(stop == 76 && !s.fast && !intro_active(&s), "stopped at the 1st frame of the main menu routine (%d)", stop);
    CHECK(!(intro_auto_pad(&s, 1) & PCE_PAD_RUN), "in the menu RUN belongs to the player again");

    /* The screen the game reports never changes (another disc): both modes give up instead of holding the buttons forever */
    for (int to_menu = 0; to_menu < 2; to_menu++) {
        intro_init(&s, 1, to_menu);
        intro_skip(&s);
        int given_up = -1;
        for (int f = 0; f < INTRO_MAX_FRAMES + 10 && given_up < 0; f++)
            if (intro_frame(&s, INTRO_SCREEN_LOADING))
                given_up = f;
        CHECK(given_up == INTRO_MAX_FRAMES - 1 && !s.fast && !intro_active(&s), "gives up without the game screen (mode %d, frame %d)",
              to_menu, given_up);
    }
    printf("intro: %s\n", g_failures ? "with failures" : "ok");
}

/* ---- disc identification ---- */

static char g_ys_dir[1024];  /* folder of the original .cue */
static char g_ys_bin[43][160];

static void ys_bin_name(int track, char *out, size_t n)
{
    snprintf(out, n, "Ys Book I & II (USA) (Track %02d).bin", track);
}

/* Ys .cue with the .bin files at their original absolute paths, except those in `local`
 * (track -> file name in the .cue's own folder). */
static int write_ys_cue(const char *cue_path, const char *const *local)
{
    /* Same text as the Redump .cue, only changing FILE */
    static const char *tracks[] = {
        "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n",
        "  TRACK 02 MODE1/2352\n    INDEX 00 00:00:00\n    INDEX 01 00:03:06\n",
        "  TRACK 03 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:03\n",
    };
    FILE *f = u8_fopen(cue_path, "wb");
    if (!f)
        return -1;
    for (int t = 1; t <= 43; t++) {
        if (local && local[t])
            fprintf(f, "FILE \"%s\" BINARY\n", local[t]);
        else
            fprintf(f, "FILE \"%s/%s\" BINARY\n", g_ys_dir, g_ys_bin[t - 1]);
        if (t <= 3)
            fputs(tracks[t - 1], f);
        else
            fprintf(f, "  TRACK %02d AUDIO\n    INDEX 01 00:00:00\n", t);
    }
    return fclose(f);
}

static void expect(DiscCheck got, DiscCheck want, const char *msg, const char *what)
{
    CHECK(got == want, "%s: result %d, expected %d (%s)", what, got, want, msg);
    if (got == want)
        printf("  ok: %s -> \"%s\"\n", what, msg[0] ? msg : "(recognized)");
}

static void test_disc(const char *ys_cue, const char *work)
{
    snprintf(g_ys_dir, sizeof(g_ys_dir), "%s", ys_cue);
    char *slash = strrchr(g_ys_dir, '/');
    char *bslash = strrchr(g_ys_dir, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (slash)
        *slash = 0;
    for (int t = 1; t <= 43; t++)
        ys_bin_name(t, g_ys_bin[t - 1], sizeof(g_ys_bin[0]));

    char cue[1024], msg[1024], dir[1024], path[1024], src[1024];
    DiscCheck r;

    /* 1. The original folder */
    r = disc_find_in_folder(g_ys_dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_OK, msg, "original folder");
    CHECK(r != DISC_OK || strstr(cue, "Ys Book I & II (USA).cue"), "chosen cue: %s", cue);

    /* 2. Folder without a .cue */
    snprintf(dir, sizeof(dir), "%s/vazia", work);
    SDL_CreateDirectory(dir);
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_NO_CUE, msg, "folder without .cue");

    /* 3. .cue citing a .bin that is not in the folder */
    snprintf(dir, sizeof(dir), "%s/falta_bin", work);
    SDL_CreateDirectory(dir);
    snprintf(path, sizeof(path), "%s/Ys.cue", dir);
    {
        static const char *local[44];
        local[17] = "Ys Book I & II (USA) (Track 17).bin";
        write_ys_cue(path, local);
    }
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_OPEN_FAILED, msg, ".bin missing");
    CHECK(strstr(msg, "Track 17"), "the message does not say which file is missing: %s", msg);

    /* 4. Another game: small data track, without the Ys title */
    snprintf(dir, sizeof(dir), "%s/outro_jogo", work);
    SDL_CreateDirectory(dir);
    {
        snprintf(path, sizeof(path), "%s/outro.bin", dir);
        FILE *f = u8_fopen(path, "wb");
        static unsigned char sector[2352];
        for (int i = 0; f && i < 300; i++)
            fwrite(sector, 1, sizeof(sector), f);
        if (f)
            fclose(f);
        snprintf(path, sizeof(path), "%s/outro.cue", dir);
        write_text(path, "FILE \"outro.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    }
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_NOT_YS, msg, "another game");

    /* 5. Altered data: copy of track 2 with one byte changed (in sector 100 of the track) */
    snprintf(dir, sizeof(dir), "%s/dados_alterados", work);
    SDL_CreateDirectory(dir);
    snprintf(src, sizeof(src), "%s/%s", g_ys_dir, g_ys_bin[1]);
    snprintf(path, sizeof(path), "%s/%s", dir, g_ys_bin[1]);
    CHECK(copy_file(src, path, -1, (231LL + 100) * 2352 + 16 + 500) == 0, "copy track 2");
    snprintf(path, sizeof(path), "%s/Ys.cue", dir);
    {
        static const char *local[44];
        local[2] = g_ys_bin[1];
        write_ys_cue(path, local);
    }
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_DATA_DIFF, msg, "altered data");

    /* 6. Truncated music track (the last one, with only 100 sectors) */
    snprintf(dir, sizeof(dir), "%s/faixa_cortada", work);
    SDL_CreateDirectory(dir);
    snprintf(src, sizeof(src), "%s/%s", g_ys_dir, g_ys_bin[42]);
    snprintf(path, sizeof(path), "%s/%s", dir, g_ys_bin[42]);
    CHECK(copy_file(src, path, 100LL * 2352, -1) == 0, "copy track 43");
    snprintf(path, sizeof(path), "%s/Ys.cue", dir);
    {
        static const char *local[44];
        local[43] = g_ys_bin[42];
        write_ys_cue(path, local);
    }
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_LAYOUT_DIFF, msg, "truncated music track");

    /* 7. Folder with accents and another alphabet, with an exact copy of track 2 next to the .cue, and a
     *    second .cue (of another game) in the same folder: it must find Ys */
    snprintf(dir, sizeof(dir), "%s/Jogos \xC3\x81" "e\xC3\xA7\xC3\xA3o \xE6\x97\xA5\xE6\x9C\xAC", work);
    SDL_CreateDirectory(dir);
    snprintf(src, sizeof(src), "%s/%s", g_ys_dir, g_ys_bin[1]);
    snprintf(path, sizeof(path), "%s/%s", dir, g_ys_bin[1]);
    CHECK(copy_file(src, path, -1, -1) == 0, "copy track 2 to the folder with accents");
    snprintf(path, sizeof(path), "%s/Ys Book I & II (USA).cue", dir);
    {
        static const char *local[44];
        local[2] = g_ys_bin[1];
        write_ys_cue(path, local);
    }
    snprintf(path, sizeof(path), "%s/a_outro.cue", dir);
    snprintf(src, sizeof(src), "FILE \"%s/outro_jogo/outro.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", work);
    write_text(path, src);
    r = disc_find_in_folder(dir, cue, sizeof(cue), msg, sizeof(msg));
    expect(r, DISC_OK, msg, "folder with accents, two .cue");
    printf("disc identification: %s\n", g_failures ? "with failures" : "ok");
}

static int absolute_path(const char *path, char *out, size_t size)
{
#ifdef _WIN32
    return _fullpath(out, path, size) != NULL;
#else
    return realpath(path, out) != NULL && strlen(out) < size;
#endif
}

/* ---- Frame redraw (app/redraw.h) and late scroll change (app/game_fixes.h) ----
 * Fake 80x16 frame: VDC area of 64 pixels from column 8 (borders of 8), 32x32 map of
 * stripes (solid tile on the even map columns, transparent on the odd ones) and a solid 16x16 sprite. */
#define SM_W 80
#define SM_H 16
#define SM_BG 0xFFFFFFFFu   /* background color 1: 0x1FF */
#define SM_SPR 0xFFFF0000u  /* color 1 of sprite palette 1: 0x038 (red) */
#define SM_BLACK 0xFF000000u

static void sm_frame(RedrawFrame *f, long seq, int bxr, int sx, int sy)
{
    memset(f, 0, sizeof(*f));
    for (int tx = 0; tx < 32; tx++)
        for (int ty = 0; ty < 32; ty++)
            f->vram[ty * 32 + tx] = (uint16_t)(tx % 2 == 0 ? 0x0100 : 0x0101); /* tile 0x100 solid, 0x101 empty */
    for (int y = 0; y < 8; y++)
        f->vram[0x1000 + y] = 0x00FF; /* plane 0 fully lit: index 1 */
    for (int y = 0; y < 16; y++)
        f->vram[0x4000 + y] = 0xFFFF; /* sprite, pattern 0x100: plane 0 lit */
    f->sat[0] = (uint16_t)(sy + 64);
    f->sat[1] = (uint16_t)(sx + 0x20);
    f->sat[2] = 0x100 << 1;
    f->sat[3] = 0x0081; /* 16x16, in front of the background, palette 1 */
    for (int i = 4; i < VDC_SAT_SIZE; i += 4)
        f->sat[i] = 0; /* the others, off screen */
    f->color[1] = 0x1FF;
    f->color[0x111] = 0x038;
    f->color[0x100] = 0;
    f->color[0] = 0;
    for (int r = 0; r < SM_H; r++) {
        VdcLineInfo *l = &f->rows[r];
        l->valid = 1;
        l->cr = 0xC0;
        l->x0 = 8;
        l->width = 64;
        l->bxr = (uint16_t)bxr;
        l->bgy = (uint16_t)r;
        l->raster = (int16_t)r;
        for (int x = 0; x < SM_W; x++) {
            uint32_t c = SM_BLACK;
            if (x >= 8 && x < 72) {
                int mx = (bxr + x - 8) & 255;
                c = (mx / 8) % 2 == 0 ? SM_BG : SM_BLACK;
                if (r >= sy && r < sy + 16 && x - 8 >= sx && x - 8 < sx + 16)
                    c = SM_SPR;
            }
            f->pixels[r * SM_W + x] = c;
        }
    }
    f->w = SM_W;
    f->h = SM_H;
    f->divider = 4;
    f->seq = seq;
    f->exact = -1;
}

/* 1st column (in 1/4 pixel) from `from` at which sub-line y of img (SM_W*4 wide) has color c */
static int sm_find(const uint32_t *img, int y, int from, uint32_t c)
{
    for (int x = from; x < SM_W * 4; x++)
        if (img[y * SM_W * 4 + x] == c)
            return x;
    return -1;
}

/* Scroll changes by the game (fix_scroll): 64x180 frame with the top border (scroll 0, lines 64+), the map from
 * line YS_SCROLL_MAP_ROW + late (scroll x 8, lines map_y+, ring wrap at 256; sprites from line
 * YS_SCROLL_MAP_ROW + late_cr) and the bottom border from YS_SCROLL_LOW_ROW + late_low (scroll 0, lines 201+, no
 * sprites). Map in an 8x8-tile checkerboard and a 16x16 sprite on the 1st map line. */
#define SCR_W 64
#define SCR_H 180
static void scr_frame(Redraw *s, RedrawFrame *f, int map_y, int late, int late_cr, int late_low)
{
    static uint32_t img[SCR_W * SCR_H];
    memset(f, 0, sizeof(*f));
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++)
            f->vram[ty * 32 + tx] = (uint16_t)((tx + ty) % 2 == 0 ? 0x0100 : 0x0101);
    for (int y = 0; y < 8; y++)
        f->vram[0x1000 + y] = 0x00FF;
    for (int y = 0; y < 16; y++)
        f->vram[0x4000 + y] = 0xFFFF;
    f->sat[0] = YS_SCROLL_MAP_ROW + 64;
    f->sat[1] = 30 + 0x20;
    f->sat[2] = 0x100 << 1;
    f->sat[3] = 0x0081;
    f->color[1] = 0x1FF;
    f->color[0x111] = 0x038;
    YsScrollFrame *sc = &f->scroll;
    sc->valid = 1;
    sc->map_x = 8;
    sc->map_y = (uint16_t)map_y;
    sc->wrap = map_y >= 0x81;
    sc->wrap_x = 8;
    sc->low_x = 0;
    sc->low_byr = 200;
    sc->cr = 0x8C;
    int map0 = YS_SCROLL_MAP_ROW + late, low0 = YS_SCROLL_LOW_ROW + late_low;
    for (int r = 0; r < SCR_H; r++) {
        VdcLineInfo *l = &f->rows[r];
        l->valid = 1;
        l->width = SCR_W;
        l->raster = (int16_t)r;
        l->cr = r >= YS_SCROLL_MAP_ROW + late_cr && r < low0 ? 0xC0 : 0x80;
        if (r >= low0) {
            l->bgy = (uint16_t)(201 + r - low0);
        } else if (r >= map0) {
            int y = map_y + r - map0;
            l->bgy = (uint16_t)(y >= 256 ? y - 256 + 512 : y); /* BYR $1FF: the next line is 512 (0 in the ring) */
            l->bxr = 8;
        } else
            l->bgy = (uint16_t)(64 + r);
    }
    f->w = SCR_W;
    f->h = SCR_H;
    f->divider = 4;
    f->seq = 1;
    f->exact = 1;
    redraw_frame(s, f, img);
    memcpy(f->pixels, img, sizeof(img));
}

static void test_fix_scroll(void)
{
    static Redraw s;
    static RedrawFrame on_time, late;
    int mr, lr;
    CHECK(redraw_init(&s) == 0, "redraw_init");
    for (int wrap = 0; wrap < 2; wrap++) {
        int y = wrap ? 200 : 16;
        scr_frame(&s, &on_time, y, 0, 0, 0);
        CHECK(fix_scroll(&s, &on_time, &mr, &lr) == 0 && mr == 0 && lr == 0, "on time (wrap %d): nothing to do", wrap);
        /* Delays: map 2 lines (scroll and sprites), only the sprites 1 line, map 1 and sprites 2, bottom border 1 */
        static const int k_late[4][3] = { { 2, 2, 0 }, { 0, 1, 0 }, { 1, 2, 0 }, { 0, 0, 1 } };
        for (int k = 0; k < 4; k++) {
            scr_frame(&s, &late, y, k_late[k][0], k_late[k][1], k_late[k][2]);
            CHECK(memcmp(on_time.pixels, late.pixels, sizeof(on_time.pixels)) != 0, "delay %d: different image", k);
            CHECK(fix_scroll(&s, &late, &mr, &lr) == 1 && (k_late[k][2] ? mr == 0 && lr > 0 : mr > 0 && lr == 0),
                  "delay %d (wrap %d): fixed (%d+%d lines)", k, wrap, mr, lr);
            CHECK(memcmp(on_time.pixels, late.pixels, sizeof(on_time.pixels)) == 0,
                  "delay %d (wrap %d): image equal to the on-time frame", k, wrap);
            redraw_check(&s, &late);
            CHECK(late.exact == 1, "delay %d: the redraw matches the fixed image", k);
        }
    }
    /* A line with the background turned off by the game (the line's CR) stays as it is, the rest is fixed; background
     * off at the end of the frame (blanked screen): nothing (-1); without the game's changes: nothing */
    scr_frame(&s, &late, 16, 2, 2, 0);
    int off_row = YS_SCROLL_MAP_ROW;
    late.rows[off_row].cr = 0;
    VdcLineInfo before = late.rows[off_row];
    CHECK(fix_scroll(&s, &late, &mr, &lr) == 1 && mr > 0, "line with the background off: the rest fixed (%d)", mr);
    CHECK(memcmp(&late.rows[off_row], &before, sizeof(before)) == 0, "line with the background off: as the game left it");
    scr_frame(&s, &late, 16, 2, 2, 0);
    late.scroll.cr &= 0x7F;
    CHECK(fix_scroll(&s, &late, &mr, &lr) == -1, "background turned off by the game: nothing (-1)");
    scr_frame(&s, &late, 16, 2, 2, 0);
    late.scroll.valid = 0;
    CHECK(fix_scroll(&s, &late, &mr, &lr) == 0, "without the game's changes: nothing");
    redraw_free(&s);
}

/* Smooth fades: a 1-level step goes continuously; larger changes and moving sprites come out at once */
static void test_fade(void)
{
    FadeTrack t = { 0 };
    CHECK(fade_track(&t, 7, 1) == 7 * FADE_Q, "1st frame: the level, no transition");
    for (int i = 0; i < 6; i++)
        fade_track(&t, 7, 1);
    int prev = 7 * FADE_Q, ok = 1;
    for (int i = 0; i < FADE_PERIOD; i++) {
        int v = fade_track(&t, 6, 1);
        ok &= v < prev && v >= 6 * FADE_Q;
        prev = v;
    }
    CHECK(ok && prev == 6 * FADE_Q, "step 7 -> 6: goes down in %d frames (%d)", FADE_PERIOD, prev);
    for (int i = 0; i < 7; i++) /* 8 frames between the steps: the transition lasts 8 */
        fade_track(&t, 6, 1);
    int a = fade_track(&t, 5, 1);
    CHECK(a > 5 * FADE_Q + FADE_Q / 2 && a < 6 * FADE_Q, "step every 8 frames: 1/8 per frame (%d)", a);
    CHECK(fade_track(&t, 2, 1) == 2 * FADE_Q, "3-level change: at once");
    CHECK(fade_track(&t, 3, 0) == 3 * FADE_Q, "off: at once");

    /* The game's math (app/ys_fade.h) with a test table (channel * level / 7; the game's comes from it) */
    uint8_t table[64];
    for (int c = 0; c < 8; c++)
        for (int l = 0; l < 8; l++)
            table[c * 8 + l] = (uint8_t)(c * l / 7);
    CHECK(ys_fade_color(table, 0x1FF, 7, 0) == 0x1FF && ys_fade_color(table, 0x1FF, 0, 0) == 0 &&
              ys_fade_color(table, 0x1C7, 4, 0) == (uint16_t)(4 << 6 | 0 << 3 | 4) &&
              ys_fade_color(table, 0x0C9, 7, 2) == (uint16_t)(5 << 6 | 3 << 3 | 3) &&
              ys_fade_color(table, 0x1FF, 7, 3) == 0x1FF,
          "game math: level from the table, brightening up to 7");
    CHECK(ys_fade_channel(table, 7, 5 * FADE_Q + FADE_Q / 2, 0, FADE_Q) == 5 * FADE_Q + FADE_Q / 2 &&
              ys_fade_channel(table, 4, 7 * FADE_Q, 0, FADE_Q) == 4 * FADE_Q &&
              ys_fade_channel(table, 7, 7 * FADE_Q, FADE_Q, FADE_Q) == 7 * FADE_Q,
          "channel between two levels: the linear transition");

    /* Palette: only the color in which the game took a fade step (YsFadeFrame.step) transitions smoothly; the same
     * 1-level change without the step (an animated color of the area) and the large changes come out at once */
    static RedrawFrame fr;
    static FadePalette fp;
    static YsFadeFrame yf;
    memset(&fp, 0, sizeof(fp));
    memset(&yf, 0, sizeof(yf));
    fr.color[1] = fr.color[2] = fr.color[3] = 0x1FF;
    for (int i = 0; i < 6; i++)
        fade_palette(&fp, &fr, &yf, 1);
    CHECK(fr.rgb[1] == vce_rgb(0x1FF), "static palette: the console's colors");
    fr.color[1] = 0x1B6; /* the 3 channels 7 -> 6, game step */
    fr.color[2] = 0x000; /* large change */
    fr.color[3] = 0x1B6; /* 1 level without the game step */
    yf.step[1] = 1;
    fade_palette(&fp, &fr, &yf, 1);
    yf.step[1] = 0;
    uint32_t mid = fr.rgb[1] & 0xFF;
    CHECK(mid < (vce_rgb(0x1FF) & 0xFF) && mid > (vce_rgb(0x1B6) & 0xFF), "palette: in the middle of the step (%u)", mid);
    CHECK(fr.rgb[2] == vce_rgb(0x000) && fr.rgb[3] == vce_rgb(0x1B6),
          "palette: large change and change without the game step at once");
    for (int i = 0; i < FADE_PERIOD - 1; i++)
        fade_palette(&fp, &fr, &yf, 1);
    CHECK(fr.rgb[1] == vce_rgb(0x1B6), "palette: the step ends in %d frames", FADE_PERIOD);

    /* Pixels (without smooth motion): each pixel by its color (Vce.frame_index); only those of the color in the step
     * transition smoothly, and only if the pixel has the end-of-frame color */
    enum { FW = 64, FH = 64 };
    static FadePixels fx;
    static uint32_t px[FW * FH];
    static uint16_t idx[FW * FH];
    static uint16_t col[512];
    memset(&fx, 0, sizeof(fx));
    memset(&yf, 0, sizeof(yf));
    col[1] = col[2] = 0x1FF;
    for (int i = 0; i < FW * FH; i++)
        idx[i] = i < 10 ? 2 : i == 20 ? VCE_PIXEL_BLACK : 1;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < FW * FH; i++)
            px[i] = i == 20 ? 0xFF000000u : vce_rgb(0x1FF);
        fade_pixels(&fx, px, idx, FW, FH, col, &yf, 1);
    }
    col[1] = col[2] = 0x1B6;
    yf.step[1] = 1; /* only color 1 in the game's fade */
    for (int i = 0; i < FW * FH; i++)
        px[i] = i == 20 ? 0xFF000000u : vce_rgb(0x1B6);
    px[30] = vce_rgb(0x007); /* pixel of color 1 with another color (changed mid-image) */
    fade_pixels(&fx, px, idx, FW, FH, col, &yf, 1);
    yf.step[1] = 0;
    mid = px[100] & 0xFF;
    CHECK(mid < (vce_rgb(0x1FF) & 0xFF) && mid > (vce_rgb(0x1B6) & 0xFF), "pixels: fade in the middle of the step (%u)", mid);
    CHECK(px[0] == vce_rgb(0x1B6) && px[20] == 0xFF000000u && px[30] == vce_rgb(0x007),
          "pixels: the color without the step, the black and the pixel changed mid-image as they came");
    for (int k = 0; k < FADE_PERIOD - 1; k++) {
        for (int i = 0; i < FW * FH; i++)
            px[i] = i == 20 ? 0xFF000000u : vce_rgb(0x1B6);
        fade_pixels(&fx, px, idx, FW, FH, col, &yf, 1);
    }
    CHECK(px[100] == vce_rgb(0x1B6), "pixels: the step ends in %d frames", FADE_PERIOD);
    printf("fade: %s\n", g_failures ? "with failures" : "ok");
}

/* Sprite i of f belongs to the window, per the game (RedrawFrame.wins: the engine list, here equal to the table) */
static void ring_own(RedrawFrame *f, int i)
{
    f->wins.sprites_ok = 1;
    memcpy(f->wins.sat, f->sat, sizeof(f->wins.sat));
    f->wins.own[i] = 1;
}

/* The equipment colors from Adol's palette and the game's fade routine (equip_colors) */
static void test_equip_colors(void)
{
    static EquipTable table;
    static YsFadeFrame f;
    EquipColors c;
    enum { BASE = 256 + 13 * 16 };
    memset(&table, 0, sizeof(table));
    for (int i = 1; i < 16; i++)
        table.pal[i] = (uint16_t)(i * 17 + 3); /* colors different from each other */
    memset(&f, 0, sizeof(f));
    for (int ch = 0; ch < 8; ch++)
        for (int level = 0; level < 8; level++)
            f.table[ch * 8 + level] = (uint8_t)(ch * level / 7);
    f.table_ok = 1;
    /* Full color, no fade routine: the values as they are */
    memcpy(&f.color[BASE], table.pal, sizeof(table.pal));
    equip_colors(&table, &f, &c);
    CHECK(c.kind == EQUIP_COLORS_MAP && c.map[0] == 0 && c.map[5] == 5 && c.map[7] == 7, "full color: %d %d", c.kind, c.map[5]);
    /* In the fade: level from the game's routine, on all values (also those the palette does not use) */
    for (int i = 1; i < 16; i++) {
        f.c[BASE + i] = (YsFadeColor){ .src = table.pal[i], .level = 3, .add = 1, .on = 1 };
        f.color[BASE + i] = ys_fade_color(f.table, table.pal[i], 3, 1);
    }
    equip_colors(&table, &f, &c);
    CHECK(c.kind == EQUIP_COLORS_MAP && c.map[7] == 3 + 1 && c.map[0] == 0 + 1, "fade: %d %d %d", c.kind, c.map[7], c.map[0]);
    /* Without the routine (the hook lost the color) and with other colors: it is a game effect, Adol comes out as the console */
    for (int i = 1; i < 16; i++)
        f.c[BASE + i].on = 0;
    equip_colors(&table, &f, &c);
    CHECK(c.kind == EQUIP_COLORS_NONE, "effect: %d", c.kind);
    /* All colors equal: the single color */
    for (int i = 1; i < 16; i++)
        f.color[BASE + i] = 0x1FF;
    equip_colors(&table, &f, &c);
    CHECK(c.kind == EQUIP_COLORS_FLAT && c.flat == 0x1FF, "single color: %d %03X", c.kind, c.flat);
    /* Without the disc frames: nothing */
    equip_colors(NULL, &f, &c);
    CHECK(c.kind == EQUIP_COLORS_NONE, "no table: %d", c.kind);
}

/* The map cells of the game windows (ys_windows_cells: drawn lines, wrap on the 64x32 map, change mid-image) and the
 * sprites that stay in front of them (redraw_windows: only the windows' own, wins.own) */
static void test_window_cells(void)
{
    static uint8_t cells[128 * 64];
    static RedrawFrame b;
    static Redraw s;
    YsWinFrame *f = &b.wins;
    const YsWindow w = { .col = 2, .row = 3, .w = 4, .h = 5, .page = 1, .bat_x = 62, .bat_y = 30,
                         .state = YS_WIN_OPEN, .vis0 = 3, .vis1 = 8 };
    memset(&b, 0, sizeof(b));
    f->count = f->prev_count = 1;
    f->win[0] = f->prev[0] = w;
    int n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 20 && cells[30 * 64 + 62] == YS_CELL_WIN, "open 4x5 window: %d cells", n);
    CHECK(cells[30 * 64 + 62] && cells[30 * 64 + 1] && !cells[30 * 64 + 2] && !cells[30 * 64 + 61] &&
              cells[2 * 64 + 63] && !cells[3 * 64 + 62] && !cells[29 * 64 + 62],
          "the window in the map corner should wrap around (columns 62-1, lines 30-2)");
    n = ys_windows_cells(f, 64, 64, cells); /* VDC at 64x64: the wrap stays within the game's 32 lines */
    CHECK(n == 20 && cells[1 * 64 + 0] && !cells[33 * 64 + 0], "64x64 map: %d cells", n);
    /* opening at image line 40, frame with 2 lines at the end of the frame: the cells become window when the game
     * writes them, after it starts opening */
    f->prev_count = 0;
    f->win[0].state = YS_WIN_OPENING;
    f->win[0].vis1 = 5;
    f->win[0].chg_row = f->win[0].start_row = 40;
    n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 8 && cells[31 * 64 + 1] == (YS_CELL_FROM | 0), "frame going down: %d cells", n);
    ys_windows_write(f, cells, 64, 32, 30 * 64 + 62, 39); /* before opening: something else */
    ys_windows_write(f, cells, 64, 32, 31 * 64 + 62, 45);
    ys_windows_write(f, cells, 64, 32, 29 * 64 + 62, 45); /* outside the window */
    CHECK(cells[30 * 64 + 62] == (YS_CELL_FROM | 0) && cells[31 * 64 + 62] == YS_CELL_WIN && !cells[29 * 64 + 62],
          "frame going down: written cells %02X %02X", cells[30 * 64 + 62], cells[31 * 64 + 62]);
    f->win[0].chg_row = f->win[0].start_row = 0; /* no change in the frame */
    n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 8 && cells[31 * 64 + 1] == YS_CELL_WIN, "frame stopped: %d cells (2 lines)", n);
    /* closing: started at line 25, background returning from top to bottom; at the end of the frame 2 lines */
    f->prev_count = 1;
    f->prev[0] = w;
    f->win[0].state = YS_WIN_CLOSING;
    f->win[0].vis0 = 6;
    f->win[0].vis1 = 8;
    f->win[0].chg_row = 1;
    f->win[0].start_row = 25;
    n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 20 && cells[30 * 64 + 62] == (YS_CELL_UNTIL | 0) && cells[1 * 64 + 62] == YS_CELL_WIN,
          "background returning: %d cells", n);
    ys_windows_write(f, cells, 64, 32, 30 * 64 + 62, 30);
    ys_windows_write(f, cells, 64, 32, 31 * 64 + 62, 10); /* before it starts closing (the text) */
    CHECK(!cells[30 * 64 + 62] && cells[31 * 64 + 62] == (YS_CELL_UNTIL | 0), "background returning: written cells");
    /* window without a saved background (scene) closed in the frame: whole until the game redraws the scene; the 2nd of the list (k 1) */
    f->count = 1;
    f->win[0] = w;
    f->win[0].bat_y = 10;
    f->gone_n = 1;
    f->gone[0] = w;
    f->gone[0].page = 0;
    f->gone[0].chg_row = 50;
    f->prev_count = 2;
    f->prev[1] = f->gone[0];
    f->prev[1].chg_row = 0;
    n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 40 && cells[30 * 64 + 62] == (YS_CELL_UNTIL | 1) && cells[10 * 64 + 62] == YS_CELL_WIN,
          "scene closing: %d cells", n);
    f->gone[0].vis0 = f->gone[0].vis1; /* with the background already restored before the frame */
    f->prev[1].vis0 = f->prev[1].vis1;
    n = ys_windows_cells(f, 64, 32, cells);
    CHECK(n == 20 && !cells[30 * 64 + 62], "restored window that closed: %d cells", n);
    /* the sprites in front: only the windows' own (by the equal entry in the shown table) */
    f->count = 1;
    f->gone_n = 0;
    f->win[0] = w;
    const uint16_t spr[4] = { 100, 200, 0x40, 0x0080 };
    memcpy(&b.sat[7 * 4], spr, sizeof(spr));
    memcpy(&b.sat[9 * 4], spr, sizeof(spr));
    b.sat[9 * 4 + 2] = 0x42;
    memcpy(&f->sat[3 * 4], spr, sizeof(spr));
    f->own[3] = 1;
    f->sprites_ok = 1;
    uint8_t keep[64];
    const uint8_t *m = redraw_windows(&s, &b, 1, keep); /* reg 1: 64x32 */
    int nk = 0;
    for (int i = 0; i < 64; i++)
        nk += keep[i];
    CHECK(m && m[30 * 64 + 62] && keep[7] && nk == 1, "in front of the window: only sprite 7 (%d)", nk);
    f->sprites_ok = 0;
    m = redraw_windows(&s, &b, 1, keep);
    CHECK(m && !keep[7], "without the engine list, no sprite belongs to the window");
    f->count = f->prev_count = 0;
    CHECK(!redraw_windows(&s, &b, 1, keep), "without a game window, no mask");
}

static void boxes_rows(VdcLineInfo *rows, int from, int to, int bxr, int bgy, int cr)
{
    for (int r = from; r <= to; r++)
        rows[r] = (VdcLineInfo){ .valid = 1, .cr = (uint8_t)cr, .x0 = 8, .width = 336, .bxr = (uint16_t)bxr,
                                 .bgy = (uint16_t)(bgy + r - from) };
}

/* The window box comes from the scroll the image shows, not from the logic camera (bat_x, bat_y): on returning from
 * the save screen they differ for 2 frames, and the window seemed stuck to the map edge (fill showed the whole map: a zoom) */
static void test_windows_boxes(void)
{
    static VdcLineInfo rows[VCE_ACTIVE_LINES];
    static YsWinFrame f;
    int boxes[YS_WIN_MAX][4], scene = 0;
    memset(&f, 0, sizeof(f));
    f.count = f.prev_count = 1;
    /* the cell from the logic camera of each case; the box must not depend on it */
    f.win[0] = (YsWindow){ .col = 3, .row = 1, .w = 12, .h = 8, .page = 0x35, .bat_x = 29, .bat_y = 4,
                           .state = YS_WIN_OPEN, .vis0 = 1, .vis1 = 9 };
    f.prev[0] = f.win[0];
#define CHECK_BOX(what, x0_, y0_, x1_, y1_)                                                                        \
    do {                                                                                                           \
        memset(boxes, 0, sizeof(boxes));                                                                           \
        int n_ = ys_windows_boxes(&f, rows, VCE_ACTIVE_LINES, boxes, YS_WIN_MAX, &scene);                          \
        CHECK(n_ == 1 && boxes[0][0] == (x0_) && boxes[0][1] == (y0_) && boxes[0][2] == (x1_) &&                   \
                  boxes[0][3] == (y1_),                                                                            \
              "%s: %d box(es), %d,%d-%d,%d", what, n_, boxes[0][0], boxes[0][1], boxes[0][2], boxes[0][3]);         \
    } while (0)
    /* normal image: border, map (camera 208,24) and panel; only the map with the screen and the sprites on */
    memset(rows, 0, sizeof(rows));
    boxes_rows(rows, 3, 26, 0, 328, 0x00);
    boxes_rows(rows, 27, 154, 208, 24, 0xC0);
    boxes_rows(rows, 155, 241, 0, 256, 0x00);
    CHECK_BOX("normal image", 32, 35, 127, 98);
    /* 1st normal frame after the save: the border and the panel still on, with the scroll landing on the window cells */
    boxes_rows(rows, 3, 26, 0, 328, 0xC0);
    boxes_rows(rows, 155, 241, 0, 256, 0xC0);
    CHECK_BOX("border and panel on", 32, 35, 127, 98);
    /* save screen (a single scroll, camera 0,128) with the logic camera already back at 208,24 */
    boxes_rows(rows, 3, 241, 0, 104, 0xC0);
    CHECK_BOX("scroll behind the logic", 32, 35, 127, 98);
    /* the late scroll change: the map starts 4 lines later and moves down with it */
    boxes_rows(rows, 3, 30, 0, 328, 0x00);
    boxes_rows(rows, 31, 154, 208, 24, 0xC0);
    boxes_rows(rows, 155, 241, 0, 256, 0x00);
    CHECK_BOX("late change", 32, 39, 127, 102);
#undef CHECK_BOX
}

/* The engine list of f, with the sprite as a character's type-1 mask (the rule without map lines gives an empty mask,
 * instead of the one that leaves) */
static void ring_mask_list(RedrawFrame *f, int sprite)
{
    YsList *l = &f->eng.list;
    memset(l, 0, sizeof(*l));
    memset(l->owner, YS_NO_ACTOR, sizeof(l->owner));
    f->eng.kind = YS_LIST_SHOWN;
    f->eng.shown_ok = 0;
    l->front.count = 1;
    l->count = sprite + 1;
    memcpy(l->sat, f->sat, sizeof(l->sat));
    l->owner[sprite] = 1;
    l->drawn[1] = 1;
    l->flags[1] = 0x22;
    l->pose[1] = 0x40;
}

/* Around the shrunk windows (redraw_ring), the windows' sprites per the game (cursor, icon) disappear; the others
 * stay, except the masks that touch a window */
static void test_redraw_ring(void)
{
    static uint8_t map_rows[VCE_ACTIVE_LINES]; /* no map lines */
    static RedrawRing ring;
    static RedrawFrame fr;
    static uint32_t bg[SM_W * SM_H], spr[SM_W * SM_H];
    static const struct { int box[4], own, drawn; const char *what; } k_case[] = {
        { { 20, 0, 50, 15 }, 1, 0, "window sprite (per the game) disappears" },
        { { 20, 0, 50, 15 }, 0, 1, "whole character under the window, without a mask, stays" },
        { { 30, 0, 50, 15 }, 0, 1, "sprite that goes past the window edge stays" },
        { { 20, 0, 50, 10 }, 0, 1, "sprite that goes below the window stays" },
    };
    for (int i = 0; i < 4; i++) {
        sm_frame(&fr, 1, 0, 20, 0); /* sprite at frame columns 28-43, lines 0-15 */
        if (k_case[i].own)
            ring_own(&fr, 0);
        memset(&ring, 0, sizeof(ring));
        int got = redraw_ring(&ring, &fr, 1, &k_case[i].box, NULL, bg, spr, NULL, NULL, NULL);
        CHECK(got == 1, "redraw_ring without saved VRAM: only the sprites (%d)", got);
        CHECK(((spr[4 * SM_W + 30] >> 24) == 0xFF) == k_case[i].drawn, "%s (alfa %02X)", k_case[i].what,
              spr[4 * SM_W + 30] >> 24);
    }
    /* the engine list late (another order): the window sprite is found in the table by the equal entry */
    sm_frame(&fr, 1, 0, 20, 0);
    fr.wins.sprites_ok = 1;
    memcpy(&fr.wins.sat[5 * 4], fr.sat, 4 * sizeof(uint16_t));
    fr.wins.own[5] = 1;
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &fr, 1, &k_case[0].box, NULL, bg, spr, NULL, NULL, NULL);
    CHECK(ring.own[0] && (spr[4 * SM_W + 30] >> 24) != 0xFF, "window sprite by the equal list entry (%d)",
          ring.own[0]);
    /* Mask (sprite 0 behind the background in front of sprite 1, as Ys places it with the window open) touching the
     * window: around the shrunk window, the sprite under it appears */
    static RedrawFrame two;
    sm_frame(&two, 1, 0, 20, 0);
    memcpy(two.sat + 4, two.sat, 4 * sizeof(uint16_t));
    two.sat[3] = 0x0001; /* behind the background */
    ring_mask_list(&two, 0);
    static const int edge_box[1][4] = { { 40, 0, 60, 15 } };
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, edge_box, NULL, bg, spr, NULL, NULL, map_rows);
    CHECK((spr[4 * SM_W + 30] >> 24) == 0xFF, "mask touching the window is left out (alpha %02X)",
          spr[4 * SM_W + 30] >> 24);
    static const int over_box[1][4] = { { 20, 0, 50, 15 } };
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, over_box, NULL, bg, spr, NULL, NULL, map_rows);
    CHECK((spr[4 * SM_W + 30] >> 24) == 0xFF, "whole character under the window, with a mask, appears around it");
    static const int far_box[1][4] = { { 60, 0, 70, 15 } };
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, far_box, NULL, bg, spr, NULL, NULL, map_rows);
    CHECK((spr[4 * SM_W + 30] >> 24) != 0xFF, "mask far from the window still applies");
    /* The late frame that could not switch lists (no list for smooth motion): the one the console showed says which
     * one is a mask */
    two.eng.shown = two.eng.list;
    two.eng.shown_ok = 1;
    two.eng.kind = YS_LIST_NONE;
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, over_box, NULL, bg, spr, NULL, NULL, map_rows);
    CHECK(ring.skip[0] && !ring.no_list && (spr[4 * SM_W + 30] >> 24) == 0xFF,
          "without the frame's list, the mask leaves by the list the console showed (%d, no list %ld)", ring.skip[0],
          ring.no_list);
    /* Without any list it is unknown which one is a mask: the game's sprites stay */
    two.eng.shown_ok = 0;
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, over_box, NULL, bg, spr, NULL, NULL, map_rows);
    CHECK(!ring.skip[0] && ring.no_list == 1 && (spr[4 * SM_W + 30] >> 24) != 0xFF,
          "without any list, the game's mask stays (%d, no list %ld, alpha %02X)", ring.skip[0], ring.no_list,
          spr[4 * SM_W + 30] >> 24);
    ring_mask_list(&two, 0);
    /* Inside the window (fix): the character with a mask leaves (the shown background goes in); the cursor (the window's) stays */
    static uint32_t shown[SM_W * SM_H], fix[SM_W * SM_H];
    for (int i = 0; i < SM_W * SM_H; i++)
        shown[i] = 0xFF00FF00u;
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &two, 1, over_box, NULL, bg, spr, shown, fix, map_rows);
    CHECK(fix[4 * SM_W + 30] == 0xFF00FF00u, "inside the window, the character with a mask becomes the background (%08X)",
          fix[4 * SM_W + 30]);
    sm_frame(&fr, 1, 0, 20, 0);
    ring_own(&fr, 0);
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &fr, 1, &k_case[0].box, NULL, bg, spr, shown, fix, NULL);
    CHECK((fix[4 * SM_W + 30] >> 24) == 0, "inside the window, the cursor stays (%08X)", fix[4 * SM_W + 30]);
    /* Equipment frame (the window's, per the game) touching the character with a mask under the window */
    static RedrawFrame three;
    sm_frame(&three, 1, 0, 20, 0);
    memcpy(three.sat + 4, three.sat, 4 * sizeof(uint16_t));
    memcpy(three.sat + 8, three.sat, 4 * sizeof(uint16_t));
    /* in the order of the Ys table: the window's, the masks, the characters */
    three.sat[1] = (uint16_t)(20 + 0x20);  /* 0: the 16x16 frame at the same corner as them */
    three.sat[3] = 0x0082;                 /* palette 2 */
    three.sat[7] = 0x1001;                 /* 1: the mask, 16x32 */
    three.sat[11] = 0x1081;                /* 2: the character, 16x32 */
    ring_own(&three, 0);
    ring_mask_list(&three, 1);
    static const int tall_box[1][4] = { { 10, 0, 60, 40 } };
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &three, 1, tall_box, NULL, bg, spr, shown, fix, map_rows);
    CHECK(ring.skip[0] && ring.skip[1] && !ring.skip[2],
          "window frame and mask left out, the character around (%d %d %d)", ring.skip[0], ring.skip[1],
          ring.skip[2]);
    CHECK((fix[4 * SM_W + 30] >> 24) == 0, "frame in front of the hidden character does not become the background (%08X)",
          fix[4 * SM_W + 30]);
    /* On closing, Ys leaves the window sprites (the pink equipment frames) 2 more frames: the identical ones stay left
     * out for RING_GHOST_FRAMES; one that changed does not */
    sm_frame(&fr, 1, 0, 20, 0);
    ring_own(&fr, 0);
    fr.seq = 100;
    memset(&ring, 0, sizeof(ring));
    redraw_ring(&ring, &fr, 1, &k_case[0].box, NULL, bg, spr, NULL, NULL, NULL);
    fr.wins.own[0] = 0;
    fr.seq = 101;
    redraw_ring(&ring, &fr, 0, NULL, NULL, bg, spr, NULL, NULL, NULL);
    CHECK(ring.skip_seq == 101 && ring.skip[0], "window sprite that lingers after it closes leaves (%ld %d)",
          ring.skip_seq, ring.skip[0]);
    fr.sat[1]++;
    fr.seq = 102;
    redraw_ring(&ring, &fr, 0, NULL, NULL, bg, spr, NULL, NULL, NULL);
    CHECK(ring.skip_seq == -1, "sprite that moved after the window closed stays");
    fr.sat[1]--;
    fr.seq = 100 + RING_GHOST_FRAMES + 1;
    redraw_ring(&ring, &fr, 0, NULL, NULL, bg, spr, NULL, NULL, NULL);
    CHECK(ring.skip_seq == -1, "identical sprite long after the window closed stays");
    fr.seq = 200; /* closes the equipment with SELECT, far away, still open */
    ring_own(&fr, 0);
    redraw_ring(&ring, &fr, 1, &k_case[0].box, NULL, bg, spr, NULL, NULL, NULL);
    fr.wins.own[0] = 0;
    fr.seq = 201;
    static const int select_box[1][4] = { { 60, 0, 70, 15 } };
    redraw_ring(&ring, &fr, 1, select_box, NULL, bg, spr, NULL, NULL, NULL);
    CHECK(ring.skip[0] && (spr[4 * SM_W + 30] >> 24) != 0xFF,
          "sprite of the closed window leaves with another window open (alpha %02X)", spr[4 * SM_W + 30] >> 24);
}


static void test_redraw(void)
{
    static Redraw s;
    static RedrawFrame fr;
    static uint32_t img[SM_W * 4 * SM_H * 4];
    static int bx[SM_H], lines[SM_H * 4], sx[64], sy[64];
    CHECK(redraw_init(&s) == 0, "redraw_init");
    sm_frame(&fr, 1, 0, 20, 4);
    CHECK(fr.color[1] == 0x1FF, "background color");
    redraw_check(&s, &fr);
    CHECK(fr.exact == 1, "redraw equal to the image (%d)", fr.exact);
    /* With 1/4 pixel (like smooth motion on the CPU): the scroll and the sprite move in quarters */
    static const struct { int bx_q, sx_q, bg_edge, spr_edge; } k_pos[] = {
        { 0, 20 * 4, (8 + 8) * 4, (8 + 20) * 4 },          /* end of the 1st stripe on the map (x 8) */
        { 2, 20 * 4 + 2, (8 + 8) * 4 - 2, (8 + 20) * 4 + 2 }, /* scroll 0.5: 2 quarters to the left */
        { 8, 22 * 4, (8 + 8) * 4 - 8, (8 + 22) * 4 },
    };
    for (int i = 0; i < 3; i++) {
        for (int r = 0; r < SM_H; r++) {
            bx[r] = k_pos[i].bx_q;
            for (int j = 0; j < 4; j++)
                lines[r * 4 + j] = r;
        }
        for (int k = 0; k < 64; k++) {
            sx[k] = ((fr.sat[k * 4 + 1] & 0x3FF) - 0x20) * 4;
            sy[k] = ((fr.sat[k * 4] & 0x3FF) - 64) * 4;
        }
        sx[0] = k_pos[i].sx_q;
        redraw_draw(&s, &fr, REDRAW_SUB_SHIFT, bx, lines, sx, sy, img);
        int bg = sm_find(img, 1, 8 * 4, SM_BLACK), spr = sm_find(img, 4 * 4 + 1, 0, SM_SPR);
        CHECK(bg == k_pos[i].bg_edge, "scroll %d/4: stripe edge at %d (expected %d)", k_pos[i].bx_q, bg,
              k_pos[i].bg_edge);
        CHECK(spr == k_pos[i].spr_edge, "sprite at %d/4: at %d (expected %d)", k_pos[i].sx_q, spr, k_pos[i].spr_edge);
        CHECK(img[1 * SM_W * 4 + 3] == SM_BLACK && img[1 * SM_W * 4 + 8 * 4] == SM_BG, "border and 1st column");
        if (i == 0) { /* at the frame positions, the real image scaled up */
            int same = 1;
            for (int y = 0; y < SM_H * 4; y++)
                for (int x = 0; x < SM_W * 4; x++)
                    same &= img[y * SM_W * 4 + x] == fr.pixels[(y / 4) * SM_W + x / 4];
            CHECK(same, "frame positions: real image scaled up");
        }
    }
    /* Frame the redraw does not reproduce (VRAM changed mid-frame): marked, goes as it came */
    sm_frame(&fr, 2, 2, 22, 4);
    fr.pixels[5 * SM_W + 30] = 0xFF00FF00u;
    redraw_check(&s, &fr);
    CHECK(fr.exact == 0, "image different from the redraw: marked");
    CHECK(s.frames == 2 && s.exact_frames == 1, "count: %ld of %ld equal", s.exact_frames, s.frames);
    redraw_free(&s);
    printf("redraw: %s\n", g_failures ? "with failures" : "ok");
}

/* Launcher (start menu): background in the console palette, image crop, color/gray covers,
 * selection and messages, texts, sounds and the effects file */
static int on_palette(uint32_t c)
{
    static const uint8_t lv[8] = { 0x00, 0x24, 0x48, 0x6D, 0x91, 0xB6, 0xDA, 0xFF };
    for (int ch = 0; ch < 3; ch++) {
        uint8_t v = (uint8_t)(c >> (ch * 8)), ok = 0;
        for (int i = 0; i < 8; i++)
            ok |= v == lv[i];
        if (!ok)
            return 0;
    }
    return 1;
}

static void test_launcher(const char *dir)
{
    static Launcher L;
    launcher_init(&L);
    int bad = 0;
    for (int i = 0; i < LAUNCHER_W * LAUNCHER_H; i++)
        bad += !on_palette(L.bg[i]);
    CHECK(bad == 0, "background: %d pixels outside the console palette", bad);
    CHECK(L.star_count > 150 && L.star_count <= LAUNCHER_STARS, "stars: %d", L.star_count);

    /* Fake image: 200x200 colored box at (100, 50) on a black background with a stray noise pixel */
    int w = 400, h = 300;
    uint32_t *img = calloc((size_t)w * h, sizeof(uint32_t));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            img[y * w + x] = x >= 100 && x < 300 && y >= 50 && y < 250 ? 0xFF000000u | (uint32_t)(x & 0xFF) << 16 | 0x8040
                                                                         : 0xFF000000u;
    img[10 * w + 10] = 0xFFFFFFFFu;
    int x0, y0, cw, ch;
    CHECK(launcher_crop_box(img, w, h, &x0, &y0, &cw, &ch) == 0 && x0 == 100 && y0 == 50 && cw == 200 && ch == 200,
          "outline: %d,%d %dx%d (expected 100,50 200x200)", x0, y0, cw, ch);
    CHECK(launcher_set_image(&L, 1, img, w, h) == 0 && L.have_cover[0], "cover 1 did not go in");
    CHECK(!L.have_cover[1] && !L.have_cover[2], "covers without an image marked");
    int not_gray = 0, off_pal = 0, n = LAUNCHER_COVER_W * 2 * LAUNCHER_COVER_H * 2;
    for (int i = 0; i < n; i++) {
        uint32_t g = L.gray[0][i];
        not_gray += ((g >> 16) & 0xFF) != (g & 0xFF) || ((g >> 8) & 0xFF) != (g & 0xFF);
        off_pal += !on_palette(L.cover[0][i]) + !on_palette(g);
    }
    CHECK(not_gray == 0 && off_pal == 0, "cover: %d colored pixels in the gray one, %d outside the palette", not_gray, off_pal);
    CHECK(launcher_set_image(&L, 0, img, w, h) == 0 && L.logo_w == 200 && L.logo_h == 160,
          "logo: %dx%d (expected 200x160, without the bottom line)", L.logo_w, L.logo_h);
    int lh = 0;
    uint32_t *logo = launcher_logo(&L, 100, &lh);
    CHECK(logo && lh == 80 && (logo[0] >> 24) > 0, "scaled-down logo: height %d", lh);
    free(logo);
    /* Logo effects: margin around; in the middle, the logo (opaque); just above it, the black outline; below, the
     * brown relief; farther, the golden glow (partly transparent); in the corners, nothing */
    LauncherLogoFx fx = { 0 };
    CHECK(launcher_logo_fx_build(&fx, &L, 100, 1080) == 0 && fx.w == 100 && fx.h == 80 && fx.pad > 20 &&
              fx.fw == 100 + 2 * fx.pad && fx.fh == 80 + 2 * fx.pad,
          "logo effects: %dx%d, margin %d", fx.fw, fx.fh, fx.pad);
    const uint32_t *fxo = launcher_logo_fx_render(&fx, 0);
    if (fxo) {
        uint32_t mid = fxo[(fx.pad + 40) * fx.fw + fx.pad + 50], above = fxo[(fx.pad - 2) * fx.fw + fx.pad + 50];
        uint32_t below = fxo[(fx.pad + 80 + 3) * fx.fw + fx.pad + 50], glow = fxo[(fx.pad - 12) * fx.fw + fx.pad + 50];
        CHECK(mid >> 24 == 255 && (mid >> 16 & 0xFF) > 200 && above >> 24 > 200 && (above & 0xFFFFFF) == 0 &&
                  below >> 24 > 200 && (below & 0xFFFFFF) == 0x3A1C00 && glow >> 24 > 0 && glow >> 24 < 200 &&
                  (glow >> 16 & 0xFF) == 255 && fxo[0] == 0,
              "logo effects: middle %08X, outline %08X, relief %08X, glow %08X, corner %08X", mid, above, below, glow,
              fxo[0]);
        /* the shine passes: at some step the middle of the logo gets brighter */
        int brighter = 0;
        for (long t = 0; t < 135 && !brighter; t++)
            brighter = (launcher_logo_fx_render(&fx, t)[(fx.pad + 40) * fx.fw + fx.pad + 50] & 0xFF) > (mid & 0xFF) + 15;
        CHECK(brighter, "logo shine did not pass through the middle");
    }
    launcher_logo_fx_free(&fx);
    uint32_t *black = calloc((size_t)w * h, sizeof(uint32_t));
    CHECK(launcher_set_image(&L, 2, black, w, h) != 0, "all-black image accepted");
    free(black);
    free(img);

    /* Selection: starts on Ys I & II; left wraps around (Ys IV); OK on an unreleased one = "not released" */
    CHECK(L.sel == 0 && L.msg == 0, "start: cover %d", L.sel);
    CHECK(launcher_key(&L, LAUNCHER_KEY_LEFT) == LAUNCHER_SFX_MOVE && L.sel == 2, "left: cover %d", L.sel);
    CHECK(launcher_key(&L, LAUNCHER_KEY_OK) == LAUNCHER_SFX_NO && L.msg == 2 && !launcher_starting(&L),
          "not released: message %d", L.msg);
    int ticks = 0;
    while (ticks < 200 && !launcher_tick(&L))
        ticks++;
    CHECK(ticks == 200, "the game started on an unreleased title");
    CHECK(launcher_key(&L, LAUNCHER_KEY_RIGHT) == LAUNCHER_SFX_MOVE && L.sel == 0 && L.msg == 0, "right: cover %d",
          L.sel);
    CHECK(launcher_key(&L, LAUNCHER_KEY_OK) == LAUNCHER_SFX_OK && launcher_starting(&L), "Ys I & II did not start");
    CHECK(launcher_key(&L, LAUNCHER_KEY_LEFT) == LAUNCHER_SFX_NONE && L.sel == 0, "key accepted while loading");
    ticks = 1;
    while (ticks < 200 && !launcher_tick(&L))
        ticks++;
    /* 37 letters, one per step, and 20 more steps */
    CHECK(ticks == 57, "the game started at step %d (expected 57)", ticks);

    /* Drawing: chosen cover in color with a golden/white frame; the others in gray */
    static uint32_t out[LAUNCHER_IMG_W * LAUNCHER_IMG_H];
    L.sel = 1;
    L.msg = 0;
    launcher_set_image(&L, 2, L.cover[0], LAUNCHER_COVER_W * 2, LAUNCHER_COVER_H * 2);
    launcher_draw(&L, out);
    int cx1 = (LAUNCHER_W - (LAUNCHER_COVER_W * 3 + 40)) / 2 + LAUNCHER_COVER_W + 20;
    uint32_t frame = out[(93 * 2) * LAUNCHER_IMG_W + (cx1 - 2) * 2] & 0xFFFFFF;
    CHECK(frame == 0xFFDA48 || frame == 0xFFFFFF, "frame: %06X", frame);
    uint32_t sel_px = out[(97 * 2 + 60) * LAUNCHER_IMG_W + cx1 * 2 + 100], oth_px = out[(97 * 2 + 60) * LAUNCHER_IMG_W + 100];
    CHECK(sel_px == L.cover[1][(60 + 4) * LAUNCHER_COVER_W * 2 + 100], "chosen cover is not the colored one");
    (void)oth_px;
    static uint32_t txt[640 * 360];
    launcher_draw_text(&L, txt, 640, 360, LAUNCHER_TEXT_MAIN);
    int lit = 0, title_lit = 0, hint_lit = 0;
    for (int i = 0; i < 640 * 360; i++) {
        lit += txt[i] >> 24 != 0;
        title_lit += i / 640 >= 120 && i / 640 < 129 && (txt[i] & 0xFFFFFF) == 0xFFDA48; /* golden */
        hint_lit += i / 640 >= 330 && txt[i] >> 24 != 0;
    }
    CHECK(lit > 1000 && title_lit > 40 && hint_lit == 0 && txt[0] == 0, "texts: %d pixels, title %d, hint %d", lit,
          title_lit, hint_lit);
    /* Project name with a glow around it (half transparent, golden) and, in the middle of the shine (step 74 of 135),
     * brightened letters (outside the gradient) */
    long keep_tick = L.tick;
    L.tick = 74;
    launcher_draw_text(&L, txt, 640, 360, LAUNCHER_TEXT_MAIN);
    L.tick = keep_tick;
    int glow = 0, shine = 0;
    for (int i = 0; i < 640 * 360; i++) {
        uint32_t a = txt[i] >> 24, c = txt[i] & 0xFFFFFF;
        glow += a > 0 && a < 255 && c == 0xFFBA3C;
        shine += i / 640 >= 120 && i / 640 < 129 && a == 255 && (c >> 16) == 0xFF && (c & 0xFF) > 0xB6 &&
                 c != 0xFFFFB6;
    }
    CHECK(glow > 300 && shine > 5, "project name: %d glow pixels, %d brightened by the shine", glow, shine);
    launcher_draw_text(&L, txt, 640, 360, LAUNCHER_TEXT_HINT);
    lit = hint_lit = 0;
    for (int i = 0; i < 640 * 360; i++) {
        lit += txt[i] >> 24 != 0;
        hint_lit += i / 640 >= 343 && i / 640 < 352 && txt[i] >> 24 != 0;
    }
    CHECK(hint_lit > 200 && hint_lit == lit, "hint: %d pixels on the bottom line, %d in total", hint_lit, lit);
    /* On any screen (font rounded to whole pixels), the window texts stay inside it (lines 176-202 of 216, inside
     * the double border) and the hint below it (205 onward) */
    static const int k_area_h[] = { 400, 720, 1080, 1440, 2160 };
    for (size_t a = 0; a < sizeof(k_area_h) / sizeof(k_area_h[0]); a++) {
        int ah = k_area_h[a], aw = ah * 16 / 9;
        int lo[2] = { ah, ah }, hi[2] = { -1, -1 };
        for (int p = 0; p < 2; p++) {
            int k = launcher_text_scale(ah, p), tw = (aw + k - 1) / k, th = (ah + k - 1) / k;
            uint32_t *t = malloc((size_t)tw * th * sizeof(uint32_t));
            launcher_draw_text(&L, t, tw, th, p);
            for (int y = th * 6 / 10; y < th; y++)
                for (int x = 0; x < tw; x++)
                    if (t[y * tw + x] >> 24) {
                        lo[p] = y * k < lo[p] ? y * k : lo[p];
                        hi[p] = (y + 1) * k - 1 > hi[p] ? (y + 1) * k - 1 : hi[p];
                    }
            free(t);
        }
        CHECK(lo[0] * 216 >= 176 * ah && hi[0] * 216 < 202 * ah && lo[1] * 216 >= 205 * ah && hi[1] < ah,
              "screen of %d lines: window %d-%d (inside: %d-%d), hint %d-%d", ah, lo[0], hi[0], 176 * ah / 216,
              202 * ah / 216, lo[1], hi[1]);
    }
    CHECK(launcher_text_scale(1440, LAUNCHER_TEXT_MAIN) == 6 && launcher_text_scale(1080, LAUNCHER_TEXT_MAIN) == 4 &&
              launcher_text_scale(720, LAUNCHER_TEXT_MAIN) == 3 && launcher_text_scale(1440, LAUNCHER_TEXT_HINT) == 4 &&
              launcher_text_scale(1080, LAUNCHER_TEXT_HINT) == 3 && launcher_text_scale(720, LAUNCHER_TEXT_HINT) == 2,
          "font scale: %d %d %d / %d %d %d", launcher_text_scale(1440, 0), launcher_text_scale(1080, 0),
          launcher_text_scale(720, 0), launcher_text_scale(1440, 1), launcher_text_scale(1080, 1),
          launcher_text_scale(720, 1));
    launcher_free(&L);

    /* Effects: file round trip, checking the numbers; effects and master volume */
    static LauncherCapture cap;
    static int16_t pcm[3][200];
    cap.count = 3;
    for (int i = 0; i < 3; i++) {
        cap.numbers[i] = launcher_sfx_numbers[i];
        for (int k = 0; k < 200; k++)
            pcm[i][k] = (int16_t)(1000 * (i + 1) + k);
        cap.pcm[i] = pcm[i];
        cap.frames[i] = 100;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/launcher_sfx.bin", dir);
    CHECK(launcher_sfx_save(path, &cap) == 0, "did not write %s", path);
    /* Simulated music: plays (fades in slowly) and stays at a reasonable level */
    AudioVolumes full = { 100, 100, 100, 100, 100 };
    static LauncherSound snd;
    static int16_t music[LAUNCHER_RATE * 2 * 3];
    launcher_sound_init(&snd, &full);
    launcher_sound_mix(&snd, music, LAUNCHER_RATE * 3);
    int peak_first = 0, peak = 0;
    for (int i = 0; i < LAUNCHER_RATE * 2 * 3; i++) {
        int a = music[i] < 0 ? -music[i] : music[i];
        if (i < LAUNCHER_RATE / 10 * 2 && a > peak_first)
            peak_first = a;
        if (a > peak)
            peak = a;
    }
    CHECK(peak > 3000 && peak < 20000 && peak_first < peak / 4, "music: peak %d (at the start %d)", peak, peak_first);
    launcher_sound_free(&snd);
    AudioVolumes vol = { 50, 0, 100, 100, 100 }; /* without the music: only the effect */
    launcher_sound_init(&snd, &vol);
    CHECK(launcher_sfx_load(path, &snd) == 0 && snd.sfx_frames[2] == 100 && snd.sfx[2][5] == 3005,
          "effects read wrong");
    launcher_sound_play(&snd, LAUNCHER_SFX_OK);
    int16_t mix[200 * 2];
    launcher_sound_mix(&snd, mix, 200);
    /* OK effect = 2000 + k, master volume 50% */
    CHECK(mix[0] == 1000 && mix[1] == 1000 && mix[198] == 1099 && mix[200] == 0 && snd.playing < 0,
          "effect mix: %d %d %d %d", mix[0], mix[1], mix[198], mix[200]);
    launcher_sound_free(&snd);
    cap.numbers[1] = 0x77;
    CHECK(launcher_sfx_save(path, &cap) != 0, "wrote effects with the wrong number");
    FILE *f = u8_fopen(path, "r+b");
    if (f) {
        fseek(f, 8 + 1 + 4 + 400, SEEK_SET);
        fputc(0x77, f); /* number of the 2nd effect */
        fclose(f);
    }
    launcher_sound_init(&snd, &vol);
    CHECK(launcher_sfx_load(path, &snd) != 0 && !snd.sfx[0], "file with another effect accepted");
    launcher_sound_free(&snd);
    snprintf(path, sizeof(path), "%s/efeito.wav", dir);
    f = launcher_write_wav(path, pcm[0], 100) == 0 ? u8_fopen(path, "rb") : NULL;
    long size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == 44 + 400, "WAV with %ld bytes", size);
    printf("launcher: %s\n", g_failures ? "with failures" : "ok");
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--fingerprint") == 0) {
        DiscFingerprint fp;
        char msg[1024];
        if (disc_fingerprint(argv[2], &fp, msg, sizeof(msg)) != 0) {
            printf("%s\n", msg);
            return 1;
        }
        printf("tracks %d\ntoc_crc %08X\ndata_crc %08X\nys_title %d\n", fp.track_count, fp.toc_crc, fp.data_crc, fp.is_ys);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "config") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_config(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "input") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_input(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "volume") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_volume(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "menu") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_menu_font();
        test_menu(argv[2]);
        test_intro();
    } else if (argc == 4 && strcmp(argv[1], "menufont") == 0) {
        /* Tool: the menu pages with the font copied from the game (the menu_font.bin that YsRecomp writes next to
         * the .ini; e.g. build/app_output/recorte/menu_font.bin after app_crop_ys) */
        if (menu_font_load(argv[2]) != 0) {
            printf("%s: not the game's font\n", argv[2]);
            return 1;
        }
        SDL_CreateDirectory(argv[3]);
        menu_font_shots(argv[3]);
    } else if (argc == 3 && strcmp(argv[1], "video") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_video(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "image") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_image(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "crop") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_crop(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "launcher") == 0) {
        SDL_CreateDirectory(argv[2]);
        test_launcher(argv[2]);
    } else if (argc == 2 && strcmp(argv[1], "fade") == 0) {
        test_fade();
    } else if (argc == 2 && strcmp(argv[1], "redraw") == 0) {
        test_redraw();
        test_fix_scroll();
        test_redraw_ring();
        test_window_cells();
        test_windows_boxes();
        test_equip_colors();
    } else if (argc == 4 && strcmp(argv[1], "disc") == 0) {
        FILE *f = u8_fopen(argv[2], "rb");
        if (!f) {
            printf("No Ys disc (%s): test SKIPPED.\n", argv[2]);
            return 77;
        }
        fclose(f);
        SDL_CreateDirectory(argv[3]);
        /* The test .cue files cite the original .bin files by absolute path */
        char cue[1024], work[1024];
        if (!absolute_path(argv[2], cue, sizeof(cue)) || !absolute_path(argv[3], work, sizeof(work))) {
            printf("invalid path\n");
            return 2;
        }
        test_disc(cue, work);
    } else if (argc == 3 && strcmp(argv[1], "cdmenu") == 0) {
        FILE *f = u8_fopen(argv[2], "rb");
        if (!f) {
            printf("No Ys disc (%s): test SKIPPED.\n", argv[2]);
            return 77;
        }
        fclose(f);
        test_cd_menu_music(argv[2]);
    } else {
        printf("usage: app_tests config DIR | volume DIR | disc YS_CUE DIR | --fingerprint CUE\n");
        return 2;
    }
    printf(g_failures ? "%d failure(s)\n" : "all good\n", g_failures);
    return g_failures ? 1 : 0;
}
