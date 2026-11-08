/*
 * Runtime entry point: runs a HuCard game, shows the VCE image in a window and
 * plays the PSG sound. Without a ROM, shows a test pattern.
 *
 * Two executables come out of this file:
 *   pce_runtime  interpreter only (any ROM)
 *   YsRecomp     the recompiled Ys (PCE_RECOMP; falls back to the interpreter for what was not recompiled),
 *                player version (PCE_APP_YS). Without a rom: uses the game folder
 *                saved in YsRecomp.ini (next to the .exe) or asks for one; the save goes to
 *                saves/ next to the .ini (portable version)
 *
 * Usage: pce_runtime [rom] [--frames N] [--coverage FOLDER] [--interpret]
 *   rom          without it (and outside YsRecomp), test pattern
 *   --frames N   closes by itself after N frames (used by the automated test)
 *   --turbo      (tests) the game runs as fast as possible, without sound; the window draws what it can
 *   --coverage FOLDER  writes this session's coverage (what the game executed) to
 *                      FOLDER/session_YYYYMMDD_HHMMSS.txt on exit (see runtime/pce/coverage.h)
 *   --interpret  (recompiled) ignores the recompiled code
 *   --lockstep   (recompiled) runs the interpreter alongside and checks every instruction and every frame;
 *                on the first difference freezes the game and writes lockstep_divergence.txt
 *   --bios FILE  console with CD-ROM²: FILE is the System Card and rom is the CD image (.cue)
 *   --record FOLDER  records the buttons pressed, frame by frame, to FOLDER/session_YYYYMMDD_HHMMSS.txt
 *                   on exit (script to check the session against Geargrafx; tools/trace/check_session.py)
 *   --input FILE replays a recorded session (or a test script) instead of reading the keyboard
 *   --vrr        V-Sync off, for variable refresh rate displays (G-Sync/FreeSync); in YsRecomp it is the
 *                V-Sync menu option (app/video.h). Pacing: single clock (app/pacing.h) with or without V-Sync
 *   --motion-cpu (YsRecomp, test) starts with smooth motion drawn on the CPU (like F6)
 *   --frame-timing  on exit, shows how long each frame stayed on screen (refreshes and ms)
 *   --screen-dump FROM COUNT FOLDER  saves (BMP) each image shown in the window in frames FROM..FROM+COUNT-1
 *   --walk-rhythm FROM TO  (YsRecomp) on exit, Adol's on-screen rhythm in frames FROM..TO-1 (app/motion.h)
 *   --window-size W H  (test) the window with W x H pixels instead of the window_scale size (e.g. 16:9 for
 *                fill with the HUD at 3/4)
 *   --save FILE  (CD) the game's save file; default saves/<.cue name>.brm. It is read on start and
 *                written on exit (the CD-ROM² save memory, 2 KB)
 *   --config FILE (YsRecomp) another settings file instead of YsRecomp.ini next to the .exe
 *   --gg-mix     (CD) sound mixed as in Geargrafx (loud CD music), instead of Mednafen's
 *                balance with the YsRecomp.ini volumes (pce/mix.h, app/volume.h)
 *   --skip-at F1,F2,... (YsRecomp, test) on these game frames simulates an Enter during the intro
 *   --mark-test F (test) on frame F simulates F12, which marks the shown frame in the recorded session
 *   --menu-test F (YsRecomp, test) on game frame F opens the options menu, changes the music volume,
 *                opens and cancels "Quit game" and closes the menu; the game must then go on the same
 *   --stall-window MS  (test) at 3 s, the window stops responding for MS milliseconds, as when
 *                Windows holds it (dragging, maximizing); the game and the sound must keep going
 *   --stall-device MS  (test) the same, and the sound device stops too (as when connecting headphones or a
 *                Bluetooth controller); the game must follow the clock
 *
 * The game runs on its own thread (emu_thread); the window only shows the most recent frame.
 *
 * Environment variable PCE_AUDIO_LOG=1: prints each time the sound queue runs dry or overflows and
 * each frame that took more than 50 ms (the summary is always printed on exit).
 *
 * Keyboard: arrows = d-pad, Z = button II, X = button I, Enter = RUN,
 *          Right Shift or Space = SELECT, Esc = quit (in YsRecomp: opens the options menu, which pauses the
 *          game; in it, arrows, Enter/X = select, Esc/Z = back; the chosen volume goes to the .ini).
 *          Alt+Enter = fullscreen <-> window. Image: 8:7 pixels filling the window (screen_draw; image
 *          options on the Image page, app/present.h).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "cd/cdrom.h"
#include "input_script.h"
#include "profiler.h"
#include "pce/coverage.h"
#include "pce/lockstep.h"
#include "bios/hle.h"
#include "app/menu_chip.h"
#include "pce/pce.h"
#include "util/u8file.h"
#ifdef PCE_RECOMP
#include "cpu/huc6280_recomp.h"
#endif
#ifdef PCE_APP_YS
#include "app/config.h"
#include "app/disc_id.h"
#include "app/fade.h"
#include "app/ys_fade.h"
#include "app/ys_music.h"
#include "app/ys_dialog.h"
#include "app/ys_screen.h"
#include "app/ys_intro.h"
#include "app/input.h"
#include "app/intro_skip.h"
#include "app/launcher.h"
#include "app/launcher_image.h"
#include "app/launcher_sound.h"
#include "app/menu.h"
#include "app/menu_font.h"
#include "app/gpu_draw.h"
#include "app/battle.h"
#include "app/motion.h"
#include "app/game_fixes.h"
#include "app/redraw.h"
#include "app/volume.h"
#endif
#include "app/pacing.h"
#include "app/gpu_time.h"
#include "app/present.h"
#include "app/vblank.h"
#include "app/video.h"

/* One PC-Engine frame: 1365 x 262 cycles of a 21.477270 MHz clock. */
#define FRAME_NS ((Uint64)1365 * 262 * 1000000000ull / 21477270ull)

/* Sound queue: bytes of one frame (~737 16-bit stereo samples); starts with a reserve of
 * silence and is kept around AUDIO_TARGET_FRAMES frames (~67 ms of latency). */
#define AUDIO_BYTES_PER_FRAME (737 * 4)
#define AUDIO_RESERVE_FRAMES 4
#define AUDIO_TARGET_FRAMES 4
/* Above this (217 ms) the console holds the next frame until the queue drops (the last safeguard; before it, the
 * fine adjustment goes up to 2%, emu_run_frame). The sound device sometimes stops consuming for tens of ms (busy
 * GPU, 2560x1440 at 180 Hz) and the queue rose in jumps; the fine adjustment (0.5%) did not drain it in time, it went
 * past 15 frames and was discarded: a sound dropout every ~10 s in the cave */
#define AUDIO_HOLD_FRAMES 13
/* --audio-hiccup MS (test): the sound device stops for MS ms every 5 s, with the game going on (the case above) */
static int g_audio_hiccup_ms;

/* HuCard game, or CD game (.cue): with the System Card (--bios) or, without it, with the synthetic BIOS and the
 * BIOS routines in C (runtime/bios) */
static int load_media(Pce *pce, const char *bios_path, const char *rom_path)
{
    if (!rom_path)
        return -1;
    if (bios_path)
        return pce_load_cd(pce, bios_path, rom_path);
    size_t n = strlen(rom_path);
    if (n > 4 && (strcmp(rom_path + n - 4, ".cue") == 0 || strcmp(rom_path + n - 4, ".CUE") == 0))
        return pce_load_cd(pce, NULL, rom_path);
    return pce_load_rom_file(pce, rom_path);
}

static void queue_silence(SDL_AudioStream *audio, int frames)
{
    static const int16_t zeros[737 * 2] = {0};
    for (int i = 0; i < frames; i++)
        SDL_PutAudioStreamData(audio, zeros, (int)sizeof(zeros));
}

/* Tests with PCE_HIDDEN_WINDOW=1 (ctest) open the window hidden, without fullscreen, without taking focus, without
 * V-Sync (pacing goes by the clock, as with --vrr) and without message boxes: they do not disturb whoever is using the
 * computer. The image is drawn the same way (--screen-dump and the GPU readback still apply). */
static int hidden_window(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("PCE_HIDDEN_WINDOW");
        v = e && e[0] && e[0] != '0';
    }
    return v;
}

static Uint64 g_game_t0; /* time the game thread started ("Game time" at the end) */

#ifdef PCE_APP_YS
#define WINDOW_TITLE "Ys Book I & II"

/* Answer of the Windows "choose folder" dialog (arrives on another thread). */
static SDL_AtomicInt g_dialog_done;
static char g_dialog_folder[1024];

static void SDLCALL folder_chosen(void *userdata, const char *const *list, int filter)
{
    (void)userdata;
    (void)filter;
    g_dialog_folder[0] = 0;
    if (list && list[0])
        snprintf(g_dialog_folder, sizeof(g_dialog_folder), "%s", list[0]);
    SDL_SetAtomicInt(&g_dialog_done, 1);
}

/* Opens the Windows dialog to choose the folder. 0 = chosen (folder), -1 = cancelled. */
static int choose_folder(const char *start, char *folder, size_t size)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                          "Choose the Ys Book I & II (USA) folder (.cue and .bin files)");
    if (start && start[0])
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, start);
    SDL_SetAtomicInt(&g_dialog_done, 0);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, folder_chosen, NULL, props);
    while (!SDL_GetAtomicInt(&g_dialog_done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);
    if (!g_dialog_folder[0])
        return -1;
    snprintf(folder, size, "%s", g_dialog_folder);
    return 0;
}

/* Player version: finds the Ys disc in the folder saved in the settings; if there is no saved
 * folder or it no longer has the game, asks for another (and explains what was wrong). Saves the chosen
 * folder and returns the .cue and the save file (saves/ next to the settings).
 * 0 = ready; -1 = the player gave up. */
static int app_find_game(const char *config_path, char *cue, size_t cue_size, char *save, size_t save_size)
{
    static Config cfg;
    config_load(&cfg, config_path);
    char folder[1024], msg[1024];
    snprintf(folder, sizeof(folder), "%s", config_get(&cfg, "game_folder", ""));
    int from_config = folder[0] != 0;
    for (;;) {
        if (folder[0]) {
            DiscCheck r = disc_find_in_folder(folder, cue, cue_size, msg, sizeof(msg));
            if (r == DISC_OK)
                break;
            printf("Folder %s: %s\n", folder, msg);
            if (hidden_window())
                return -1; /* test: no choose-folder dialog */
            char text[2600];
            snprintf(text, sizeof(text), "%s%s\n\nPlease choose the folder with the .cue and .bin files of\n"
                     "Ys Book I & II (USA).",
                     from_config ? "The game folder saved in the settings can no longer be used.\n\n" : "", msg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, WINDOW_TITLE, text, NULL);
        }
        from_config = 0;
        if (choose_folder(folder, folder, sizeof(folder)) != 0)
            return -1;
    }
    if (strcmp(config_get(&cfg, "game_folder", ""), folder) != 0) {
        config_set(&cfg, "game_folder", folder);
        if (config_save(&cfg, config_path) == 0)
            printf("Game folder saved to %s.\n", config_path);
        else
            printf("Could not write the settings to %s\n", config_path);
    }

    /* saves/<.cue name>.brm, in the settings folder */
    const char *base = cue;
    for (const char *c = cue; *c; c++)
        if (*c == '/' || *c == '\\')
            base = c + 1;
    const char *dot = strrchr(base, '.');
    int dir_len = 0;
    for (int i = 0; config_path[i]; i++)
        if (config_path[i] == '/' || config_path[i] == '\\')
            dir_len = i + 1;
    snprintf(save, save_size, "%.*ssaves/%.*s.brm", dir_len, config_path,
             (int)(dot ? dot - base : (long)strlen(base)), base);
    return 0;
}

/* Options stored in the settings (volumes, intro_to_menu with default 0, keys and buttons, display); adds
 * the missing ones to the file. */
static void app_load_settings(const char *config_path, AudioVolumes *vol, int *skip_intro, int *pause_bg,
                              InputMap *input, VideoSettings *video, int *record_sessions)
{
    static Config cfg;
    config_load(&cfg, config_path);
    int added = volume_load(&cfg, vol);
    added |= input_load(&cfg, input);
    added |= video_load(&cfg, video);
    /* New key (formerly "skip_intro" meant skipping screen by screen, now always on; it is ignored) */
    *skip_intro = config_get_int(&cfg, "intro_to_menu", 0, 0, 1);
    if (!config_get(&cfg, "intro_to_menu", NULL)) {
        config_set_int(&cfg, "intro_to_menu", *skip_intro);
        added = 1;
    }
    /* pause_in_background=1 (default): pauses the game when the window loses focus */
    *pause_bg = config_get_int(&cfg, "pause_in_background", 1, 0, 1);
    if (!config_get(&cfg, "pause_in_background", NULL)) {
        config_set_int(&cfg, "pause_in_background", *pause_bg);
        added = 1;
    }
    /* record_sessions=1: records every session to sessions/, next to the settings (like --record) */
    *record_sessions = config_get_int(&cfg, "record_sessions", 0, 0, 1);
    if (!config_get(&cfg, "record_sessions", NULL)) {
        config_set_int(&cfg, "record_sessions", *record_sessions);
        added = 1;
    }
    if (added && config_save(&cfg, config_path) != 0)
        printf("Could not write the settings to %s\n", config_path);
}

/* Writes the options chosen in the menu (the rest of the settings stays as it is). */
static void app_save_settings(const char *config_path, const AudioVolumes *vol, int skip_intro, int pause_bg,
                              const InputMap *input, const VideoSettings *video)
{
    static Config cfg;
    config_load(&cfg, config_path);
    input_store(&cfg, input);
    video_store(&cfg, video);
    config_set_int(&cfg, "volume_master", vol->master);
    config_set_int(&cfg, "volume_music", vol->music);
    config_set_int(&cfg, "volume_chip_music", vol->chip_music);
    config_set_int(&cfg, "volume_effects", vol->effects);
    config_set_int(&cfg, "volume_voices", vol->voices);
    config_set_int(&cfg, "chip_filter", vol->chip_filter);
    config_set_int(&cfg, "intro_to_menu", skip_intro);
    config_set_int(&cfg, "pause_in_background", pause_bg);
    if (config_save(&cfg, config_path) != 0)
        printf("Could not write the settings to %s\n", config_path);
}
#else
#define WINDOW_TITLE "pce_recomp"
#endif

static Pce g_pce;
static Pce g_ref;           /* lockstep reference (interpreter only) */
static Lockstep g_lockstep;
static uint32_t g_test_pixels[256 * 240];

/* Color bars + a moving square (when there is no ROM). */
static void draw_test_pattern(int frame)
{
    static const uint32_t bars[8] = {
        0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
        0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000,
    };
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++)
            g_test_pixels[y * 256 + x] = bars[x / 32];
    int bx = frame % (256 - 32);
    for (int y = 104; y < 136; y++)
        for (int x = bx; x < bx + 32; x++)
            g_test_pixels[y * 256 + x] = 0xFF808080;
}

/* ---- The game on its own thread ----
 * The window (main thread) only shows the most recent frame and reads the keyboard. This way, when
 * Windows holds the window (dragging, resizing, maximizing), the game and the sound keep going; only the
 * image stops until the window comes back. */
typedef struct Emu {
    SDL_AudioStream *audio;
    SDL_AtomicInt screen_paced; /* paced by the window: it gives the signal (tick) for each frame */
    SDL_Semaphore *tick;
    /* Single clock (app/pacing.h): the window releases a frame (tick) when it starts showing the previous one.
     * started = signals taken by the console; runs_done = frames finished since a signal (or without one, window stuck);
     * published_run = the started of the frame that last changed frame_seq (with frame_lock) */
    SDL_AtomicInt started, runs_done;
    int published_run;
    int pace_log; /* PCE_PACE_LOG: console frames that took more than 10 ms */
    SDL_AtomicInt rate_ppm; /* game speed requested by the window, in millionths of 60.05 frames/s */
    SDL_Semaphore *frame_ready; /* tells the window there is a new frame */
    SDL_Mutex *frame_lock;      /* protects frame/frame_w/frame_h/frame_seq */
    uint32_t frame[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    int frame_w, frame_h;
    int frame_divider; /* master cycles per pixel (pixel aspect on screen) */
    int frame_next;    /* EXP left to the next level in this frame (crop_next_exp; crop HUD) */
    YsPanel frame_panel; /* the panel fields in this frame (ys_panel_read; crop HUD and bars) */
    long frame_seq;
    SDL_AtomicInt pad;          /* buttons read from the keyboard by the window */
    SDL_AtomicInt quit;         /* the window closed, or the game ended (--frames, error) */
    SDL_AtomicInt diverged;     /* lockstep found a difference: game stopped */
    SDL_AtomicInt paused;       /* options menu open: the game runs no frames */
    SDL_AtomicInt menu_music;   /* stopped by the options menu: the CD music goes on (menu_music_feed) */
    SDL_AtomicInt menu_sfx;     /* menu effect requested by the window (MENU_SFX_* + 1; 0 = none) */
    AudioMix mix_pending;       /* new volumes from the menu (with frame_lock), applied on the next frame */
    int mix_dirty;
#ifdef PCE_APP_YS
    /* Intro skipping (app/intro_skip.h); only the game thread touches it */
    IntroSkip intro;
    uint8_t intro_prev, intro_block, intro_auto_prev;
    SDL_AtomicInt opening;      /* still in the intro (ys_screen_in_opening): the options menu does not open */
    long skip_at[8];            /* --skip-at: frames on which an "Enter" is simulated (test) */
    int skip_at_count, skip_at_next;
    Uint64 start_ns; /* start of the 1st frame (to measure "straight to the menu") */
    /* Smooth motion (app/redraw.h): the VDC logs the lines; the finished frame goes to the window (frame_lock),
     * also without smooth motion (fix for the late scroll switch, fix_scroll) */
    SDL_AtomicInt smooth_on;
    /* Instant dialog (app/ys_dialog.h): the settings option (the window sets it), the one in effect on the console
     * and the last one noted in the recorded session */
    SDL_AtomicInt dialog_want;
    int dialog_on, dialog_rec;
    VdcLineLog line_log;
    RedrawFrame smooth_frame;
    YsRewardFrame rewards; /* kill rewards not yet taken by the frame preparation (frame_lock) */
    /* Smooth fades (app/fade.h): the colors shown for each captured frame (smooth_frame.rgb) */
    SDL_AtomicInt smooth_fades;
    FadePalette fade_pal;
    /* The game's fades for each published frame (app/ys_fade.h; the steps of hidden frames go along) and the
     * color number of each pixel (Vce.frame_index: smooth fade without smooth motion) */
    YsFadeFrame fade;
    int ys_screen; /* the game program's screen (CROP_SCREEN_*, app/ys_screen.h) */
    uint16_t frame_index[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    long torn_win_frames; /* torn frames inside the game's windows (fixed when drawing, fix_torn_window) */
#endif
    /* Only the game thread touches these while it runs */
    InputScript *playback, *record;
    int lockstep_on;
    int turbo; /* --turbo: no waiting for the clock or the screen */
#ifdef PCE_APP_YS
    int font_check;      /* the menu font (app/menu_font.h) has not been copied from VRAM yet */
    char font_path[1024]; /* where to store the copy ("" = do not store) */
#endif
    long max_frames;
    long pause_at; /* --menu-test: the console stops by itself on this frame (with --turbo, it does not pass the menu) */
    long frames_done;
    int audio_log, audio_underruns, audio_overflows, audio_holds;
} Emu;

static Emu g_emu;

/* Recorded session (--record or record_sessions=1 in the settings): the buttons frame by frame, in the test
 * script format. The file is rewritten every minute of play (by the game thread, the only one that touches the
 * script) and on exit: if the program crashes, at most that minute is lost. */
#define RECORD_SAVE_FRAMES 3600
typedef struct Recording {
    InputScript script;
    char path[1024];
    char header[2048]; /* without the "# frames:" line */
    char marks[1200];  /* F12 marks the shown frame (defect seen by the player): " 12345 23456" */
    SDL_SpinLock marks_lock; /* F12 on the window thread, saving on the game thread */
} Recording;
static Recording g_rec;

/* F12: notes in the session the frame that is on screen, to find later the defect the player saw */
static void recording_mark(long frame)
{
    SDL_LockSpinlock(&g_rec.marks_lock);
    size_t len = strlen(g_rec.marks);
    if (len < sizeof(g_rec.marks) - 16)
        snprintf(g_rec.marks + len, sizeof(g_rec.marks) - len, " %ld", frame);
    SDL_UnlockSpinlock(&g_rec.marks_lock);
    printf("F12: mark on frame %ld\n", frame);
}

/* Window pacing switches (by the screen <-> by the clock), noted in the recorded session ("# display:") to check
 * stutters reported by the player */
static struct {
    int switches;
    char notes[700];
    int motion_used; /* smooth motions used (bit 0 = off, 1 = the old one; 2 = new on the
                      * GPU, 3 = new on the CPU: "# motion:") */
} g_pace_log;

static PaceClock g_pace; /* the window's single clock (app/pacing.h) */

/* Sound failures noted in the recorded session ("# sound:"), to find the dropouts reported by the player: queue
 * empty, queue full (discarded), console frame that took too long, window stalled, sound device stalled along with it
 * and Windows events that held the window ("eventos<ms>"), with the frame of each one */
static struct {
    char notes[1200];
    long stalls;
} g_audio_rec;

static void audio_note(const char *what, long frame)
{
    static SDL_SpinLock lock; /* called by the game thread and by the window thread */
    SDL_LockSpinlock(&lock);
    size_t len = strlen(g_audio_rec.notes);
    if (len < sizeof(g_audio_rec.notes) - 32)
        snprintf(g_audio_rec.notes + len, sizeof(g_audio_rec.notes) - len, " %s@%ld", what, frame);
    SDL_UnlockSpinlock(&lock);
}

/* n = refreshes per frame (V-Sync, display a multiple of 60; 0 = it is not) or images per frame (without V-Sync) */
static void pace_note(float hz, int vsync, int n, int is_switch)
{
    size_t len = strlen(g_pace_log.notes);
    g_pace_log.switches += is_switch;
    if (len < sizeof(g_pace_log.notes) - 40)
        snprintf(g_pace_log.notes + len, sizeof(g_pace_log.notes) - len, "%s%us %.1fHz %s%d", len ? "; " : "",
                 (unsigned)(SDL_GetTicks() / 1000), hz, vsync ? "screen x" : "free x", n);
}

/* Picks the name (session_YYYYMMDD_HHMMSS.txt). If the game started from a save, it is copied along
 * (session_X.brm), so the session can be replayed. */
static void recording_start(const char *dir, const char *rom_path, const char *bios_path, const uint8_t *bram,
                            size_t bram_size)
{
    char stamp[32], save_line[1100] = "";
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
    snprintf(g_rec.path, sizeof(g_rec.path), "%s/session_%s.txt", dir, stamp);
    SDL_CreateDirectory(dir);
    if (bram) {
        char brm[1024];
        snprintf(brm, sizeof(brm), "%s/session_%s.brm", dir, stamp);
        FILE *f = u8_fopen(brm, "wb");
        if (f) {
            fwrite(bram, 1, bram_size, f);
            fclose(f);
            snprintf(save_line, sizeof(save_line), "# save: %s\n", brm);
        }
    }
    snprintf(g_rec.header, sizeof(g_rec.header),
             "# Session recorded by YsRecomp on %s (see tools/trace/README.md, \"Recorded sessions\").\n"
             "# game: %s\n# bios: %s\n%s",
             stamp, rom_path, bios_path ? bios_path : "-", save_line);
}

/* Writes to a temporary file and swaps: a crash in the middle of writing does not ruin the previous version. */
static int recording_save(long frames_done)
{
    char header[5900], tmp[1100], marks[sizeof(g_rec.marks)];
    int u = g_pace_log.motion_used;
    SDL_LockSpinlock(&g_rec.marks_lock);
    memcpy(marks, g_rec.marks, sizeof(marks));
    SDL_UnlockSpinlock(&g_rec.marks_lock);
    snprintf(header, sizeof(header), "%s# frames: %ld\n# motion:%s%s%s\n# display: %d pacing switches: %s\n"
             "# clock: %ld images, %ld late, %ld without the frame ready, %ld skips\n"
             "# sound: queue empty %d, full %d, waited %d, window stalled %ld:%s\n# marks (F12):%s\n",
             g_rec.header, frames_done, u & 1 ? " off" : "", u & 4 ? " new" : "",
             u & 8 ? " new-cpu" : "", g_pace_log.switches, g_pace_log.notes, g_pace.images, g_pace.late,
             g_pace.holds, g_pace.jumps, g_emu.audio_underruns, g_emu.audio_overflows, g_emu.audio_holds,
             g_audio_rec.stalls,
             g_audio_rec.notes, marks);
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_rec.path);
    if (input_script_save(&g_rec.script, tmp, header) != 0 || !SDL_RenamePath(tmp, g_rec.path))
        return -1;
    return 0;
}

#ifdef PCE_APP_YS
/* The game's windows through the open/close hooks (app/ys_windows.h), along with the engine (without lockstep) */
static YsWindows g_ys_windows;
static int g_ys_windows_on;
static YsWinFrame g_ys_wins; /* those of the last frame (ys_windows_snapshot) */
static SDL_Semaphore *g_prep_wake; /* wakes the preparation thread (Prep.wake) */

static void prep_wake_up(void)
{
    if (g_prep_wake && SDL_GetSemaphoreValue(g_prep_wake) == 0)
        SDL_SignalSemaphore(g_prep_wake);
}
static YsScroll g_ys_scroll; /* the game's scroll switches (app/ys_scroll.h) */
static YsScrollFrame g_ys_scroll_frame; /* those of the last frame */
static YsFade g_ys_fade;     /* the game's fades (app/ys_fade.h) */
static YsScreen g_ys_screen;  /* the game program running: the framed screen (app/ys_screen.h) */
static YsIntro g_ys_intro;    /* the intro screen and the accepted RUN (app/ys_intro.h) */
static YsFadeFrame g_ys_fade_frame; /* those of the last console frame */
static YsReward g_ys_reward;  /* rewards for kills (app/ys_reward.h) */

static void app_instruction_hook(Pce *pce, int after)
{
    ys_engine_hook(pce, after);
    if (!after) {
        ys_windows_hook(&g_ys_windows, pce);
        ys_scroll_hook(&g_ys_scroll, pce);
        ys_fade_hook(&g_ys_fade, pce);
        ys_screen_hook(&g_ys_screen, pce);
        ys_intro_hook(&g_ys_intro, pce, g_ys_screen.program);
        ys_reward_hook(&g_ys_reward, pce);
        ys_music_hook(pce);
    }
}
#endif

static void emu_publish_frame(void)
{
    int w = g_pce.vce.frame_width, h = g_pce.vce.frame_height;
    if (w <= 0 || h <= 0)
        return;
#ifdef PCE_APP_YS
    /* The engine's list for this frame */
    static YsFrameList eng;
    eng.kind = YS_LIST_NONE;
    eng.shown_ok = 0;
    if (g_ys_engine && SDL_GetAtomicInt(&g_emu.smooth_on))
        ys_engine_frame(g_ys_engine, &eng);
    ys_scroll_frame(&g_ys_scroll, &g_ys_scroll_frame);
    /* The game's fades, on every console frame */
    ys_fade_frame(&g_ys_fade, &g_pce, &g_ys_fade_frame);
    if (g_ys_windows_on) { /* on every console frame (the lines drawn when opening and closing) */
        ys_windows_frame(&g_ys_windows, &g_pce, g_emu.frame_seq + 1);
        ys_windows_snapshot(&g_ys_windows, &eng, &g_ys_wins);
    }
    /* The tear inside the game's windows is fixed when drawing (fix_torn_window). Outside them the game only
     * redraws the map: every frame goes to the window */
    int torn_in = 0;
    if (SDL_GetAtomicInt(&g_emu.smooth_on) && g_ys_windows_on)
        fix_torn_log(&g_emu.line_log, &g_ys_wins, &torn_in, NULL);
#endif
    SDL_LockMutex(g_emu.frame_lock);
    memcpy(g_emu.frame, g_pce.vce.frame, (size_t)w * h * sizeof(uint32_t));
    g_emu.frame_w = w;
    g_emu.frame_h = h;
    g_emu.frame_divider = g_pce.vce.clock_divider;
    ys_panel_read(&g_pce, &g_emu.frame_panel);
    g_emu.frame_next = crop_next_exp(&g_emu.frame_panel);
    g_emu.frame_seq++;
    g_emu.published_run = SDL_GetAtomicInt(&g_emu.started);
#ifdef PCE_APP_YS
    g_emu.fade = g_ys_fade_frame;
    g_emu.ys_screen = ys_screen_layout(g_ys_screen.program);
    memcpy(g_emu.frame_index, g_pce.vce.frame_index, (size_t)w * h * sizeof(uint16_t));
    if (SDL_GetAtomicInt(&g_emu.smooth_on)) {
        redraw_capture(&g_emu.smooth_frame, &g_emu.line_log, &g_pce.vce, g_emu.frame_seq);
        fade_palette(&g_emu.fade_pal, &g_emu.smooth_frame, &g_emu.fade, SDL_GetAtomicInt(&g_emu.smooth_fades));
        g_emu.smooth_frame.eng = eng;
        g_emu.smooth_frame.wins = g_ys_wins;
        g_emu.smooth_frame.scroll = g_ys_scroll_frame;
        g_emu.smooth_frame.torn_win = torn_in > 0;
        if (torn_in > 0)
            g_emu.torn_win_frames++;
        equip_read(&g_pce, &g_emu.smooth_frame.equip);
        equip_colors(g_equip_table, &g_emu.fade, &g_emu.smooth_frame.equip_colors);
    }
    /* The rewards, until the preparation takes them */
    ys_reward_take(&g_ys_reward, &g_emu.rewards);
#endif
    SDL_UnlockMutex(g_emu.frame_lock);
    if (SDL_GetSemaphoreValue(g_emu.frame_ready) == 0)
        SDL_SignalSemaphore(g_emu.frame_ready);
#ifdef PCE_APP_YS
    prep_wake_up();
#endif
}

#ifdef PCE_APP_YS
/* Intro: Enter (RUN) or I pressed request fast forward and do not reach the game; those still
 * pressed when the intro ends stay blocked until released. */
static uint8_t intro_buttons(uint8_t pad)
{
    const uint8_t keys = PCE_PAD_RUN | PCE_PAD_I;
    int skip = (pad & keys & ~g_emu.intro_prev) != 0;
    g_emu.intro_prev = pad & keys;
    if (g_emu.skip_at_next < g_emu.skip_at_count && g_emu.frames_done >= g_emu.skip_at[g_emu.skip_at_next]) {
        g_emu.skip_at_next++;
        skip = 1;
    }
    if (intro_active(&g_emu.intro)) {
        if (skip && intro_skip(&g_emu.intro))
            printf("Fast forward: from frame %ld", g_emu.frames_done);
        g_emu.intro_block |= pad & keys;
    }
    g_emu.intro_block &= pad;
    /* Straight to the menu: the RUN the intro presses by itself */
    uint8_t auto_pad = intro_auto_pad(&g_emu.intro, ys_intro_run_accepted(&g_pce, g_ys_screen.program));
    if (auto_pad != g_emu.intro_auto_prev)
        printf("Straight to menu: RUN %s on frame %ld\n", auto_pad ? "pressed" : "released", g_emu.frames_done);
    g_emu.intro_auto_prev = auto_pad;
    return (uint8_t)((pad & ~g_emu.intro_block) | auto_pad);
}
/* During the intro the options menu does not open (user's decision); in the launcher it has its own loop and still opens.
 * --menu-press F[,F...] (test): a real Esc, through the event queue, on these frames */
#define MENU_BLOCKED() SDL_GetAtomicInt(&g_emu.opening)
static long g_menu_press[8];
static int g_menu_press_count, g_menu_press_next, g_menu_press_wait;
#else
#define MENU_BLOCKED() 0
#endif

/* --audio-wav FILE (test): records the sound the game thread sends to the device (game frames and menu
 * music); tells in which samples the options menu stayed open (the first time) */
static struct {
    const char *path;
    int16_t *pcm;
    long n, cap; /* stereo samples */
    long menu_from, menu_to;
} g_wav = { NULL, NULL, 0, 0, -1, -1 };
static void wav_put(const int16_t *s, int values)
{
    if (!g_wav.path || g_wav.n + values / 2 > 44100L * 600)
        return;
    if (g_wav.n + values / 2 > g_wav.cap) {
        long cap = g_wav.cap ? g_wav.cap * 2 : 44100L * 60;
        int16_t *p = realloc(g_wav.pcm, (size_t)cap * 2 * sizeof(int16_t));
        if (!p)
            return;
        g_wav.pcm = p;
        g_wav.cap = cap;
    }
    memcpy(g_wav.pcm + g_wav.n * 2, s, (size_t)values * sizeof(int16_t));
    g_wav.n += values / 2;
}

/* Options menu open: the console stays stopped, but the CD music keeps playing (cdaudio_pc_play, at the menu
 * volumes, which apply right away); effects, voices and the chip music stop. On close, the music resumes where it stopped.
 * The chip music plays on a copy of the console and, on close, the console receives its sound state
 * (app/menu_chip.h): the session notes the menu and the lockstep reference receives the same. */
static MenuChip g_menu_chip;

static void menu_chip_end(void)
{
    long pieces = menu_chip_finish(&g_menu_chip, &g_pce);
    if (!pieces)
        return;
    if (g_emu.lockstep_on)
        menu_chip_replay(&g_ref, pieces, g_menu_chip.samples);
    if (g_emu.record) {
        const long v[2] = { pieces, g_menu_chip.samples };
        input_script_record_event(g_emu.record, "menu", g_emu.frames_done, v, 2); /* "# menu <frame> <pieces> <samples>" */
    }
}

#ifdef PCE_APP_YS
/* Options menu effects: Ys's own, recorded by the launcher (launcher_sfx.bin; without it, no effects),
 * mixed into the menu sound (menu_music_feed) at the effects volume */
static LauncherSound g_menu_snd;
static int g_menu_sfx_play = -1, g_menu_sfx_pos;

static void menu_sfx_mix(int16_t *buf, int n)
{
    int want = SDL_SetAtomicInt(&g_emu.menu_sfx, 0) - 1;
    if (want >= 0 && want < 3 && g_menu_snd.sfx[want]) {
        g_menu_sfx_play = want;
        g_menu_sfx_pos = 0;
    }
    if (g_menu_sfx_play < 0)
        return;
    const int16_t *pcm = g_menu_snd.sfx[g_menu_sfx_play];
    int left = g_menu_snd.sfx_frames[g_menu_sfx_play] - g_menu_sfx_pos;
    for (int i = 0; i < n && i < left; i++)
        for (int c = 0; c < 2; c++) {
            int v = buf[i * 2 + c] + (int)(pcm[(g_menu_sfx_pos + i) * 2 + c] * g_menu_snd.sfx_gain);
            buf[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
    g_menu_sfx_pos += n;
    if (g_menu_sfx_pos >= g_menu_snd.sfx_frames[g_menu_sfx_play])
        g_menu_sfx_play = -1;
}
#endif

static void menu_music_feed(void)
{
    SDL_AudioStream *audio = g_emu.audio;
    if (!audio || !g_pce.cd)
        return;
    SDL_LockMutex(g_emu.frame_lock);
    if (g_emu.mix_dirty) {
        g_pce.mix = g_ref.mix = g_emu.mix_pending;
        g_emu.mix_dirty = 0;
    }
    SDL_UnlockMutex(g_emu.frame_lock);
    static int16_t buf[AUDIO_BYTES_PER_FRAME / 2], cd[AUDIO_BYTES_PER_FRAME / 2];
    int n = AUDIO_BYTES_PER_FRAME / 4;
    if (g_wav.menu_from < 0)
        g_wav.menu_from = g_wav.n;
    while (SDL_GetAudioStreamQueued(audio) <= AUDIO_BYTES_PER_FRAME * AUDIO_TARGET_FRAMES) {
        if (g_emu.lockstep_on && g_ref.cd) /* the reference moves ahead too: the sound of both stays the same */
            cdaudio_pc_play(&g_ref.cd->audio, cd, n);
        cdaudio_pc_play(&g_pce.cd->audio, cd, n);
        menu_chip_play(&g_menu_chip, &g_pce, buf, n);
        audio_mix_frame(buf, n * 2, NULL, 0, cd, n * 2, &g_pce.mix);
#ifdef PCE_APP_YS
        menu_sfx_mix(buf, n);
#endif
        SDL_PutAudioStreamData(audio, buf, (int)sizeof(buf));
        wav_put(buf, n * 2);
    }
}

/* Runs one game frame and sends the sound. 0 = ok; -1 = the game stopped (error or divergence). */
static int emu_run_frame(void)
{
    SDL_AudioStream *audio = g_emu.audio;
    long frames_done = g_emu.frames_done;
#ifdef PCE_APP_YS
    if (!g_emu.start_ns)
        g_emu.start_ns = SDL_GetTicksNS();
#endif
    SDL_LockMutex(g_emu.frame_lock);
    if (g_emu.mix_dirty) {
        g_pce.mix = g_ref.mix = g_emu.mix_pending; /* only the sound coming out of the console changes */
        g_emu.mix_dirty = 0;
    }
    SDL_UnlockMutex(g_emu.frame_lock);
#ifdef PCE_APP_YS
    /* Mutes the old track when the game routine changes music with a fade active (app/ys_music.h) */
    g_pce.mix.hide_cd_pop = g_ref.mix.hide_cd_pop = 1;
#endif
    g_pce.pad_buttons =g_emu.playback ? input_script_update(g_emu.playback, frames_done, g_pce.pad_buttons)
                                       : (uint8_t)SDL_GetAtomicInt(&g_emu.pad);
#ifdef PCE_APP_YS
    if (!g_emu.playback)
        g_pce.pad_buttons = intro_buttons(g_pce.pad_buttons);
#endif
    if (g_emu.record)
        input_script_record(g_emu.record, frames_done, g_pce.pad_buttons);
#ifdef PCE_APP_YS
    /* Instant dialog: changes the game; the session notes each change and the replay follows what it noted (without the
     * line, off, as on the console). The lockstep reference receives the same (pce/lockstep.c). */
    if (g_emu.playback) {
        InputEvent e; /* "# dialog <frame> <0|1>" */
        while (input_script_event(g_emu.playback, "dialog", frames_done, &e))
            if (e.n >= 1)
                g_emu.dialog_on = (int)e.v[0];
    } else
        g_emu.dialog_on = SDL_GetAtomicInt(&g_emu.dialog_want);
    if (g_emu.record && g_emu.dialog_on != g_emu.dialog_rec) {
        const long v[1] = { g_emu.dialog_on != 0 };
        input_script_record_event(g_emu.record, "dialog", frames_done, v, 1);
        g_emu.dialog_rec = g_emu.dialog_on;
    }
    ys_dialog_set(&g_pce, g_emu.dialog_on);
#endif
    InputEvent menu_ev; /* "# menu <frame> <pieces> <samples>" */
    while (g_emu.playback && input_script_event(g_emu.playback, "menu", frames_done, &menu_ev))
        if (menu_ev.n >= 2) {
            menu_chip_replay(&g_pce, menu_ev.v[0], (int)menu_ev.v[1]); /* menu opened in the recorded session */
            if (g_emu.lockstep_on)
                menu_chip_replay(&g_ref, menu_ev.v[0], (int)menu_ev.v[1]);
        }
    if (!pce_run_frame(&g_pce, 10000000)) {
        fprintf(stderr, "The frame did not finish (PC=%04X).\n", g_pce.cpu.pc);
        return -1;
    }
    frames_done = ++g_emu.frames_done;
    if (g_emu.record && frames_done % RECORD_SAVE_FRAMES == 0 && recording_save(frames_done) != 0)
        printf("Could not write the session to %s\n", g_rec.path);
    if (g_emu.lockstep_on && lockstep_end_frame(&g_lockstep) != 0) {
        /* Stops the game (the image stays frozen) and reports what happened. */
        printf("\n%s\n", g_lockstep.report);
        if (lockstep_write_report(&g_lockstep, "lockstep_divergence.txt") == 0)
            printf("Report written to lockstep_divergence.txt\n");
        SDL_SetAtomicInt(&g_emu.diverged, 1);
        return -1;
    }
    if (g_emu.lockstep_on && frames_done % 600 == 0)
        printf("Lockstep: %llu frames, %llu instructions checked, all equal.\n",
               (unsigned long long)g_lockstep.frames, (unsigned long long)g_lockstep.instructions);
#ifdef PCE_APP_YS
    SDL_SetAtomicInt(&g_emu.opening, !g_emu.lockstep_on && g_pce.cd &&
                                         ys_screen_in_opening(g_ys_screen.program, frames_done));
    int intro_stop = intro_frame(&g_emu.intro, g_ys_intro.screen);
    if (g_emu.intro.fast)
        return 0; /* fast forward: no sound and no image until the next screen */
    if (intro_stop) {
        if (g_emu.intro.to_menu)
            printf("Straight to menu: stopped on frame %ld, %.2f s after the 1st frame.\n", frames_done,
                   (SDL_GetTicksNS() - g_emu.start_ns) / 1e9);
        else
            printf(" to frame %ld (next screen).\n", frames_done);
        if (audio) { /* restarts the sound from this frame */
            SDL_ClearAudioStream(audio);
            queue_silence(audio, AUDIO_RESERVE_FRAMES);
        }
    }
#endif
    if (audio && g_pce.audio_count > 0) {
        if (g_audio_hiccup_ms) {
            static Uint64 paused_at;
            if (!paused_at && frames_done % 300 == 150) {
                SDL_PauseAudioStreamDevice(audio);
                paused_at = SDL_GetTicksNS();
            } else if (paused_at && SDL_GetTicksNS() - paused_at >= (Uint64)g_audio_hiccup_ms * 1000000) {
                SDL_ResumeAudioStreamDevice(audio);
                paused_at = 0;
            }
        }
        int queued = SDL_GetAudioStreamQueued(audio);
        if (queued == 0 && frames_done > AUDIO_RESERVE_FRAMES) {
            g_emu.audio_underruns++; /* the sound device had nothing to play: audible dropout */
            audio_note("vazia", frames_done);
            if (g_emu.audio_log)
                printf("[%.1f s] sound: queue empty\n", frames_done / 59.82);
        }
        /* More than 0.25 s in the queue (should not happen with pacing by the sound device):
         * discards it so latency does not build up and restarts with the reserve. */
        if (queued > AUDIO_BYTES_PER_FRAME * 15) {
            g_emu.audio_overflows++;
            audio_note("cheia", frames_done);
            if (g_emu.audio_log)
                printf("[%.1f s] sound: queue full, discarded\n", frames_done / 59.82);
            SDL_ClearAudioStream(audio);
            queue_silence(audio, AUDIO_RESERVE_FRAMES);
        }
        SDL_PutAudioStreamData(audio, g_pce.audio, g_pce.audio_count * (int)sizeof(int16_t));
        if (g_wav.menu_from >= 0 && g_wav.menu_to < 0)
            g_wav.menu_to = g_wav.n;
        wav_put(g_pce.audio, g_pce.audio_count);
        /* The display (e.g. 60.00 Hz) may hold the game slightly below the console's 60.05 frames/s:
         * slightly adjusts the playback speed (at most 0.5%, inaudible)
         * so the queue stays near the target, instead of running dry or overflowing. */
        float error = (float)(queued - AUDIO_BYTES_PER_FRAME * AUDIO_TARGET_FRAMES) /
                      (float)(AUDIO_BYTES_PER_FRAME * AUDIO_TARGET_FRAMES);
        /* starting from the speed the window requests (e.g. 59.95 frames/s on a 119.9 Hz display), so the queue
         * stays at the target and not below it */
        int ppm = SDL_GetAtomicInt(&g_emu.rate_ppm);
        float base = ppm > 0 ? (float)ppm / 1e6f : 1.0f;
        float ratio = base * (1.0f + 0.005f * error);
        ratio = ratio < base * 0.995f ? base * 0.995f : ratio > base * 1.005f ? base * 1.005f : ratio;
        /* With the queue high (more than 8 frames, after the sound device stopped for a moment), 1.5% faster until it
         * comes back: at 0.5% it did not drop in time and went past 15 (discarded: dropout); normal stays at 0.5%, inaudible */
        if (queued > AUDIO_BYTES_PER_FRAME * 8)
            ratio = base * 1.015f;
        SDL_SetAudioStreamFrequencyRatio(audio, ratio);
    }
    emu_publish_frame();
    return 0;
}

static int audio_needs_frame(void)
{
    return SDL_GetAudioStreamQueued(g_emu.audio) <= AUDIO_BYTES_PER_FRAME * AUDIO_TARGET_FRAMES;
}

/* Console speed (~60.05 frames per second). Waits for the window's signal (single clock: one
 * frame released when it starts showing the previous one); if it does not come (window held by Windows), follows
 * the sound device (the next frame only runs when the queue drops to AUDIO_TARGET_FRAMES frames), or the
 * clock without sound, until the window comes back. The sound queue is kept near the target by the fine adjustment of the
 * playback speed (emu_run_frame), without which it would slowly run dry or fill up. */
static int SDLCALL emu_thread(void *data)
{
    (void)data;
    if (!g_emu.turbo)
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH); /* see where it is created */
    Uint64 next_frame = SDL_GetTicksNS();
    Uint64 frame_start = next_frame;
    int window_stalled = 0, audio_stalled = 0;
    Uint64 last_run_ns = 0;
    while (!SDL_GetAtomicInt(&g_emu.quit)) {
        if (SDL_GetAtomicInt(&g_emu.paused)) {
            /* Menu open: the console stays stopped exactly where it was */
            if (SDL_GetAtomicInt(&g_emu.menu_music))
                menu_music_feed();
            SDL_DelayNS(2000000);
            next_frame = frame_start = SDL_GetTicksNS();
            continue;
        }
        menu_chip_end();
#ifdef PCE_APP_YS
        int fast = g_emu.intro.fast || g_emu.turbo; /* intro fast forward: no waiting */
#else
        int fast = g_emu.turbo;
#endif
        if (window_stalled && !SDL_GetAtomicInt(&g_emu.screen_paced))
            window_stalled = 0; /* pacing is no longer by the screen (option changed): clock */
        if (fast) {
            next_frame = SDL_GetTicksNS();
        } else if (SDL_GetAtomicInt(&g_emu.screen_paced) && !window_stalled) {
            /* No signal for ~2 frames: the window is stuck. The wait fits comfortably within the sound
             * reserve (AUDIO_TARGET_FRAMES frames, ~67 ms). */
            if (SDL_WaitSemaphoreTimeout(g_emu.tick, 35)) {
                if (g_emu.audio && SDL_GetAudioStreamQueued(g_emu.audio) > AUDIO_BYTES_PER_FRAME * AUDIO_HOLD_FRAMES) {
                    /* sound ahead (AUDIO_HOLD_FRAMES): waits for the queue to drop, at most 2 frames */
                    Uint64 t0 = SDL_GetTicksNS();
                    g_emu.audio_holds++;
                    while (SDL_GetAudioStreamQueued(g_emu.audio) > AUDIO_BYTES_PER_FRAME * AUDIO_HOLD_FRAMES &&
                           SDL_GetTicksNS() - t0 < 33000000 && !SDL_GetAtomicInt(&g_emu.quit))
                        SDL_DelayNS(1000000);
                }
                SDL_AddAtomicInt(&g_emu.started, 1);
            } else {
                window_stalled = 1;
                if (g_audio_rec.stalls++ == 0 || g_emu.frames_done % 600 == 0)
                    audio_note("window", g_emu.frames_done);
                next_frame = SDL_GetTicksNS();
                if (g_emu.audio_log)
                    printf("[%.1f s] window stalled: pacing by the sound device\n", g_emu.frames_done / 59.82);
                if (g_emu.audio && !audio_needs_frame())
                    continue;
            }
        } else if (g_emu.audio && window_stalled) {
            /* The sound device may also stop (headphones or a Bluetooth controller connecting, Windows switches the
             * sound output while it holds the window): if it does not ask for sound for 3 frames, follows the clock
             * (one frame every 1/60.05 s) until it asks again. */
            int got_tick = 0;
            Uint64 limit = frame_start + (audio_stalled ? FRAME_NS : 3 * FRAME_NS);
            while (!SDL_GetAtomicInt(&g_emu.quit) && !audio_needs_frame()) {
                if (window_stalled && SDL_TryWaitSemaphore(g_emu.tick)) {
                    got_tick = 1; /* the window came back */
                    break;
                }
                Uint64 now = SDL_GetTicksNS();
                if (now >= limit) {
                    if (!audio_stalled)
                        audio_note("device", g_emu.frames_done);
                    audio_stalled = 1;
                    break;
                }
                SDL_DelayNS(limit - now < 1000000 ? limit - now : 1000000);
            }
            if (audio_needs_frame())
                audio_stalled = 0;
            if (window_stalled && (got_tick || SDL_TryWaitSemaphore(g_emu.tick))) {
                window_stalled = 0;
                SDL_AddAtomicInt(&g_emu.started, 1);
            }
        } else {
            if (window_stalled && SDL_TryWaitSemaphore(g_emu.tick)) { /* no sound: the window came back */
                window_stalled = 0;
                SDL_AddAtomicInt(&g_emu.started, 1);
            }
            /* Clock (no sound with the window stuck, or no pacing by the window): one frame every 1/60.05 s, evenly. The sound device asks for sound in blocks; following its queue
             * released frames in pairs (10 and 20 ms). The difference between the clock and the device is handled by
             * the fine adjustment of the sound speed (emu_run_frame). */
            Uint64 now = SDL_GetTicksNS();
            next_frame += FRAME_NS;
            if (next_frame > now)
                SDL_DelayNS(next_frame - now);
            else if (now - next_frame > 100000000ull)
                next_frame = now; /* fell too far behind: does not try to catch up */
        }
        if (SDL_GetAtomicInt(&g_emu.quit))
            break;
        Uint64 now = SDL_GetTicksNS();
        if (g_emu.pace_log && last_run_ns > 10000000ull)
            printf("[frame %ld] console: frame took %.1f ms\n", g_emu.frames_done, last_run_ns / 1e6);
        if (now - frame_start > 50000000ull && !SDL_GetAtomicInt(&g_emu.paused))
            audio_note("slow", g_emu.frames_done);
        if (g_emu.audio_log && now - frame_start > 50000000ull)
            printf("[%.1f s] frame took %.0f ms\n", g_emu.frames_done / 59.82, (now - frame_start) / 1e6);
        frame_start = now;

        if (emu_run_frame() != 0) {
            if (!SDL_GetAtomicInt(&g_emu.diverged))
                SDL_SetAtomicInt(&g_emu.quit, 1); /* error: closes; divergence: the image stays frozen */
            break;
        }
        SDL_SetAtomicInt(&g_emu.runs_done, SDL_GetAtomicInt(&g_emu.started));
        last_run_ns = SDL_GetTicksNS() - frame_start;
        if (g_emu.max_frames >= 0 && g_emu.frames_done >= g_emu.max_frames) {
            SDL_SetAtomicInt(&g_emu.quit, 1);
            break;
        }
        if (g_emu.pause_at >= 0 && g_emu.frames_done >= g_emu.pause_at) {
            g_emu.pause_at = -1;
            SDL_SetAtomicInt(&g_emu.paused, 1);
        }
#ifdef PCE_APP_YS
        /* Menu font: copied from VRAM when the game loads it (the checksum matches) and stored for next time */
        if (g_emu.font_check && g_emu.frames_done % 30 == 0 && menu_font_capture(g_pce.vdc.vram)) {
            g_emu.font_check = 0;
            if (g_emu.font_path[0] && menu_font_save(g_emu.font_path) == 0)
                printf("Menu font copied from the game to %s.\n", g_emu.font_path);
        }
#endif
    }
    SDL_SignalSemaphore(g_emu.frame_ready); /* wakes the window, if it is waiting for a frame */
    return 0;
}

#ifdef PCE_APP_YS
static InputMap input_map; /* keys and buttons */
static InputPads pads;     /* connected controllers */

/* Keyboard and controllers, through the player's mapping */
static uint8_t keyboard_to_pad(void)
{
    return (uint8_t)(input_keyboard_bits(&input_map, SDL_GetKeyboardState(NULL)) | input_pads_bits(&pads, &input_map));
}
#else
static uint8_t keyboard_to_pad(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= PCE_PAD_I;
    if (k[SDL_SCANCODE_Z]) b |= PCE_PAD_II;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_SPACE]) b |= PCE_PAD_SELECT;
    if (k[SDL_SCANCODE_RETURN]) b |= PCE_PAD_RUN;
    if (k[SDL_SCANCODE_UP]) b |= PCE_PAD_UP;
    if (k[SDL_SCANCODE_RIGHT]) b |= PCE_PAD_RIGHT;
    if (k[SDL_SCANCODE_DOWN]) b |= PCE_PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PCE_PAD_LEFT;
    return b;
}
#endif

/* ---- Display: monitor modes, fullscreen, V-Sync, pacing and framing (app/video.h) ---- */
typedef struct Screen {
    SDL_Window *window;
    SDL_Renderer *renderer;
    VideoSettings video;
    VideoModes modes;          /* of the monitor the window is on */
    int vsync_forced_off;      /* --vrr */
    int have_audio;
    int vsync;                 /* V-Sync in use */
    int refreshes_per_frame;   /* V-Sync on a display that is a multiple of 60: refreshes per frame (1..4); 0 = it is not (--frame-timing) */
    float hz;                  /* current monitor refresh rate (the measured one, if the reported one was not confirmed) */
    float hz_reported;         /* the one Windows reports */
    int pace_smooth;           /* smooth motion when the pacing was set up (without V-Sync: images per frame) */
    int paced_once;
    Presenter present;         /* drawing with the image options (app/present.h) */
} Screen;

static Screen g_screen;

static int mode_px(int v, float density)
{
    return (int)(v * (density > 0.0f ? density : 1.0f) + 0.5f);
}

static void screen_query_modes(Screen *s)
{
    VideoModes *m = &s->modes;
    memset(m, 0, sizeof(*m));
    SDL_DisplayID display = SDL_GetDisplayForWindow(s->window);
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(display);
    if (desk) {
        m->desktop.w = mode_px(desk->w, desk->pixel_density);
        m->desktop.h = mode_px(desk->h, desk->pixel_density);
        m->desktop.hz = desk->refresh_rate;
    }
    int n = 0;
    SDL_DisplayMode **list = SDL_GetFullscreenDisplayModes(display, &n);
    for (int i = 0; list && i < n && m->count < VIDEO_MAX_MODES; i++) {
        VideoMode *v = &m->list[m->count++];
        v->w = mode_px(list[i]->w, list[i]->pixel_density);
        v->h = mode_px(list[i]->h, list[i]->pixel_density);
        v->hz = list[i]->refresh_rate;
    }
    SDL_free(list);
}

/* Next display refresh (from Windows) on the SDL_GetTicksNS clock, and the period; 0 = unknown */
static Uint64 screen_vblank(double *period_ns)
{
    uint64_t next, per;
    if (!vblank_query(&next, &per))
        return 0;
    double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 now = SDL_GetTicksNS();
    double ahead = (double)(int64_t)(next - SDL_GetPerformanceCounter()) * 1e9 / freq;
    *period_ns = (double)per * 1e9 / freq;
    return (Uint64)((double)now + ahead);
}

/* Pacing: the window is the game's clock at any refresh rate (app/pacing.h). The rate is the Windows
 * compositor's when it answers (some modes in the list are not what they report: on the user's monitor, "1920x1080 at
 * 180 Hz" refreshes at 120 Hz), otherwise the reported one. peak_hz = the highest recent reading (app/pacing.h; 0 = none):
 * with G-Sync the current one may have dropped; 0 = new mode (forgets the previous one's readings and, if the current one is up to 25%
 * below the reported one, keeps the reported one: G-Sync drop; the 180 mode that refreshes at 120 is 33% below). */
static PaceRateWatch g_rate_watch;

static void screen_update_pacing(Screen *s, double peak_hz)
{
    const SDL_DisplayMode *cur = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s->window));
    s->hz_reported = cur ? cur->refresh_rate : 0.0f;
    double period = 0.0;
    s->hz = screen_vblank(&period) && period > 0.0 ? (float)(1e9 / period) : s->hz_reported;
    if (peak_hz > s->hz)
        s->hz = (float)peak_hz;
    if (peak_hz <= 0.0) {
        memset(&g_rate_watch, 0, sizeof(g_rate_watch));
        if (s->hz < 0.99f * s->hz_reported && s->hz >= 0.75f * s->hz_reported)
            s->hz = s->hz_reported;
    }
    s->pace_smooth = s->video.smooth_motion != 0;
    pace_setup(&g_pace, s->vsync, s->hz, s->pace_smooth);
    s->refreshes_per_frame = s->vsync ? g_pace.per_frame : 0;
    SDL_SetAtomicInt(&g_emu.screen_paced, 1);
    SDL_SetAtomicInt(&g_emu.rate_ppm, (int)(g_pace.tau_step * PACE_FRAME_NS / g_pace.image_ns * 1e6 + 0.5));
    pace_note(s->hz, s->vsync, g_pace.per_frame, s->paced_once++ > 0);
    if (!s->vsync)
        printf("Display at %.2f Hz, V-Sync off (G-Sync/FreeSync): %d image(s) per game frame, %.2f per second.\n",
               s->hz, g_pace.per_frame, 1e9 / g_pace.image_ns);
    else if (g_pace.per_frame)
        printf("Display at %.2f Hz: one game frame every %d display refresh(es).\n", s->hz, g_pace.per_frame);
    else
        printf("Display at %.2f Hz: each image at the game position of the time it appears.\n", s->hz);
}

/* The compositor's rate (the highest recent one) changed more than 1% for 1 s (another mode, another monitor;
 * short fluctuations do not count, app/pacing.h), or smooth motion was turned on or off without V-Sync (changes the images per frame):
 * redoes the pacing */
static void screen_check_pacing(Screen *s, double period_ns)
{
    static double logged_ns;
    if (g_emu.pace_log && period_ns > 0.0 && SDL_fabs(period_ns - logged_ns) > 0.002 * period_ns) {
        printf("[%.2f s, frame %ld] compositor: %.2f Hz\n", SDL_GetTicksNS() / 1e9, g_emu.frames_done, 1e9 / period_ns);
        logged_ns = period_ns;
    }
    if (pace_rate_changed(&g_rate_watch, period_ns > 0.0 ? 1e9 / period_ns : 0.0, s->hz, SDL_GetTicksNS()) ||
        (!s->vsync && (s->video.smooth_motion != 0) != s->pace_smooth))
        screen_update_pacing(s, pace_rate_hz(&g_rate_watch));
}

/* Mode in use, for the menu's Video page (fits in 26 columns) */
static void screen_info(const Screen *s, char *out, size_t size)
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(s->window, &w, &h);
    snprintf(out, size, "%s %dx%d %d Hz", s->video.fullscreen ? "Screen" : "Window", w, h, (int)(s->hz + 0.5f));
}

static int g_window_w, g_window_h; /* --window-size */

/* Applies the display options. resize: in windowed mode, returns to the window_scale size (limited to the monitor's
 * usable area). */
static void screen_apply(Screen *s, int resize)
{
    screen_query_modes(s);
    if (s->video.fullscreen && !hidden_window()) {
        int exclusive = 0;
        VideoMode want = video_fullscreen_mode(&s->modes, &s->video, &exclusive);
        int n = 0;
        SDL_DisplayMode **list = exclusive ? SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(s->window), &n) : NULL;
        const SDL_DisplayMode *mode = NULL;
        for (int i = 0; list && i < n; i++)
            if (mode_px(list[i]->w, list[i]->pixel_density) == want.w &&
                mode_px(list[i]->h, list[i]->pixel_density) == want.h &&
                (!mode || SDL_fabsf(list[i]->refresh_rate - want.hz) < SDL_fabsf(mode->refresh_rate - want.hz)))
                mode = list[i];
        SDL_SetWindowFullscreenMode(s->window, mode); /* NULL = borderless, as Windows is set */
        SDL_SetWindowFullscreen(s->window, true);
        /* The game does not use the mouse: no cursor in fullscreen, and the window in front with focus (Windows
         * sometimes leaves another window focused on open) */
        SDL_HideCursor();
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
        SDL_RaiseWindow(s->window);
        if (mode)
            printf("Fullscreen: %dx%d at %.2f Hz.\n", want.w, want.h, want.hz);
        else
            printf("Borderless fullscreen (%dx%d, %.2f Hz from Windows).\n", s->modes.desktop.w, s->modes.desktop.h,
                   s->modes.desktop.hz);
        SDL_free(list);
    } else {
        SDL_SetWindowFullscreen(s->window, false);
        SDL_ShowCursor();
        if (resize) {
            int w, h;
            video_window_size(&s->video, &w, &h);
            SDL_Rect area;
            if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(s->window), &area)) {
                VideoSettings smaller = s->video;
                while ((w > area.w || h > area.h - 40) && smaller.window_scale > 1) {
                    smaller.window_scale--;
                    video_window_size(&smaller, &w, &h);
                }
            }
            if (g_window_w > 0 && g_window_h > 0) { /* --window-size */
                w = g_window_w;
                h = g_window_h;
            }
            SDL_SetWindowSize(s->window, w, h);
            SDL_SetWindowPosition(s->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    SDL_SyncWindow(s->window);
    s->vsync = s->video.vsync && !s->vsync_forced_off && SDL_SetRenderVSync(s->renderer, 1);
    if (!s->vsync)
        SDL_SetRenderVSync(s->renderer, SDL_RENDERER_VSYNC_DISABLED);
    screen_update_pacing(s, 0.0);
}

/* Draws the texture's w x h image (sub times larger: smooth motion) with the image options
 * (app/present.h), with black borders. */
/* --screen-dump FROM COUNT FOLDER: saves each image shown in the window (BMP) while the game frame is between
 * FROM and FROM+COUNT-1, to check what the player sees (smooth motion, crop, scene); name q<frame>_<n>.bmp */
#define DUMP_HOLD_MAX 4096
#define DUMP_HOLD_BYTES ((size_t)1536 << 20)
static struct {
    const char *dir;
    long from, count, last, n;
    int menu; /* options menu in the image: name m<frame>_<n>.bmp */
    int held; /* images kept in memory, written at the end */
    size_t held_bytes;
    SDL_Surface *shots[DUMP_HOLD_MAX];
    char *paths[DUMP_HOLD_MAX];
} g_dump = { NULL, 0, 0, -1, 0, 0 };
static void screen_dump(SDL_Renderer *r)
{
    long q = g_emu.frame_seq;
    if (!g_dump.dir || q < g_dump.from || q >= g_dump.from + g_dump.count)
        return;
    g_dump.n = q == g_dump.last ? g_dump.n + 1 : 0;
    g_dump.last = q;
    SDL_Surface *shot = SDL_RenderReadPixels(r, NULL);
    if (!shot)
        return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%c%06ld_%ld.bmp", g_dump.dir, g_dump.menu ? 'm' : 'q', q, g_dump.n);
    /* Kept in memory and written at the end (screen_dump_flush): writing to disk in the middle of the game delayed the window,
     * which skipped frames */
    size_t bytes = (size_t)shot->pitch * shot->h;
    if (g_dump.held < DUMP_HOLD_MAX && g_dump.held_bytes + bytes <= DUMP_HOLD_BYTES) {
        g_dump.shots[g_dump.held] = shot;
        g_dump.paths[g_dump.held] = SDL_strdup(path);
        g_dump.held++;
        g_dump.held_bytes += bytes;
        return;
    }
    SDL_SaveBMP(shot, path);
    SDL_DestroySurface(shot);
}

static void screen_dump_flush(void)
{
    for (int i = 0; i < g_dump.held; i++) {
        SDL_SaveBMP(g_dump.shots[i], g_dump.paths[i]);
        SDL_DestroySurface(g_dump.shots[i]);
        SDL_free(g_dump.paths[i]);
    }
    g_dump.held = 0;
    g_dump.held_bytes = 0;
}

/* Image planned by the single clock: the drawing goes to the GPU right away and the delivery (Present) waits for the
 * planned time, right after the previous refresh; the drawing time goes into the estimate for the next ones */
static int g_pace_armed;
static Uint64 g_pace_plan_ns;
static int g_pace_heavy; /* the planned image took a new frame (separate estimate) */

/* Drawing cost (at the end of --frame-timing): [0] before the new frame (crop, HUD), [1] the new frame
 * (motion, GPU, crop layer), [2] the whole image until delivery */
typedef struct DrawCost {
    long n, over;
    Uint64 sum, max;
} DrawCost;
static DrawCost g_draw_cost[14]; /* 8-12: the parts of "crop, fade, texture"; 13: whole preparation */
static long g_heavy_ms[16];      /* the image with the new frame, per whole ms */
static long g_heavy_lead[16], g_heavy_steps[5]; /* it in the plan: ms from the start to the refresh; refreshes */
static long g_present_ms[16];    /* how long SDL_RenderPresent held the window (all images) */
static int g_gpu_time;           /* --frame-timing with Direct3D 11: GPU time of each image (app/gpu_time.h) */

static void draw_cost_add(DrawCost *c, Uint64 ns)
{
    c->n++;
    c->sum += ns;
    c->max = ns > c->max ? ns : c->max;
    c->over += ns > 3000000;
}

/* Marks the end of a piece: the time since *t goes to piece slot */
static void draw_cost_mark(int slot, Uint64 *t)
{
    Uint64 now = SDL_GetTicksNS();
    draw_cost_add(&g_draw_cost[slot], now - *t);
    *t = now;
}

static void screen_present(SDL_Renderer *r)
{
    if (!g_pace_armed) {
        SDL_RenderPresent(r);
        return;
    }
    g_pace_armed = 0;
    if (g_gpu_time)
        gpu_time_mark(r, 2);
    SDL_FlushRenderer(r);
    Uint64 now = SDL_GetTicksNS(), draw = now - g_pace_plan_ns;
    draw_cost_add(&g_draw_cost[2], draw);
    if (g_pace_heavy)
        g_heavy_ms[draw >= 15000000 ? 15 : draw / 1000000]++;
    if (g_pace.present_at > now)
        SDL_DelayPrecise(g_pace.present_at - now);
    Uint64 call = SDL_GetTicksNS();
    SDL_RenderPresent(r);
    Uint64 took = (SDL_GetTicksNS() - call) / 1000000;
    g_present_ms[took > 15 ? 15 : took]++;
    if (g_gpu_time)
        gpu_time_presented(r, g_pace.target);
    pace_presented(&g_pace, call, draw, g_pace_heavy);
}

static void screen_draw(Screen *s, SDL_Texture *texture, int w, int h, int sub, int divider)
{
    if (g_gpu_time)
        gpu_time_mark(s->renderer, 1);
    present_draw(&s->present, s->renderer, texture, w, h, sub, divider, &s->video);
    screen_dump(s->renderer);
    screen_present(s->renderer);
}

#ifdef PCE_APP_YS
/* Uploads the w x h image to the texture (tex_w x tex_h) repeating the last column and the last row right after it:
 * the smooth (bilinear) filter reads one pixel past the edge, and leftovers of previous images would be there (a
 * strip on the bottom edge) */
static void upload_padded(SDL_Texture *t, const uint32_t *px, int w, int h, int tex_w, int tex_h)
{
    static uint32_t buf[(VCE_MAX_WIDTH * REDRAW_SUB + 1) * (VCE_ACTIVE_LINES * REDRAW_SUB + 1)];
    int pw = w < tex_w ? w + 1 : w, ph = h < tex_h ? h + 1 : h;
    for (int y = 0; y < ph; y++) {
        const uint32_t *row = px + (size_t)(y < h ? y : h - 1) * w;
        memcpy(buf + (size_t)y * pw, row, (size_t)w * sizeof(uint32_t));
        if (pw > w)
            buf[(size_t)y * pw + w] = row[w - 1];
    }
    SDL_Rect r = { 0, 0, pw, ph };
    SDL_UpdateTexture(t, &r, buf, pw * (int)sizeof(uint32_t));
}

/* Pixel and screen aspect for wide and fill (crop_band_size): stretched = no cut */
static void crop_target_size(CropState *c, const VideoSettings *v, int ow, int oh, int divider)
{
    c->par = v->aspect == VIDEO_ASPECT_SQUARE ? 1.0 : divider / 3.5;
    c->screen = v->aspect == VIDEO_ASPECT_STRETCH ? -1.0 : oh > 0 ? (double)ow / oh : 0.0;
}

static void crop_target(CropState *c, Screen *s, int divider)
{
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(s->renderer, &ow, &oh);
    crop_target_size(c, &s->video, ow, oh, divider);
}

/* Builds the cropped image of the game frame (without smooth motion) and, in full mode, the panel. With bt->small, the
 * gains of bt (at positions bx, by) go into the cropped image, at 3/4 (outside the console frame) */
/* CPU only (on the preparation thread): the cropped image in crop_pixels and, with has_panel and the panel in the
 * mode, the panel in panel_pixels (*panel = 1). Returns the sub (0 = no crop) */
static int crop_build_cpu(const uint32_t *frame, const CropState *crop, int mode, int hud_bg, int w, int h,
                          int has_crop, uint32_t *crop_pixels, int has_panel, uint32_t *panel_pixels, int *panel,
                          CropView *v, const Battle *bt, const double *bx, const double *by)
{
    *panel = 0;
    if (!has_crop || !crop_view_state(mode, crop, w, h, v))
        return 0;
    v->hud_bg = hud_bg;
    v->darken = crop->darken_q;
    int sub = crop_out_sub(v); /* 4 with the HUD at 3/4 (fill with TV pixels) */
    crop_compose_sub(frame, w, 1, v, crop_pixels, sub);
    if (bt && bt->small && bt->count && v->band && sub % 4 == 0) {
        int ox, oy, clip[4];
        crop_map_out(v, sub, &ox, &oy, clip);
        battle_draw_at(bt, bx, by, crop_pixels, v->w * sub, sub, ox, oy, clip, 1 << BATTLE_POPUP);
    }
    crop_darken_view(crop_pixels, v->w * sub, v, sub, crop->darken_q);
    if (v->hud && has_panel) {
        crop_panel(crop_unwhiten_panel(frame, w, crop->whiten), w, 1, crop->panel.valid ? &crop->panel : NULL,
                   panel_pixels, CROP_PANEL_W);
        crop_darken(panel_pixels, CROP_PANEL_W, CROP_PANEL_W, CROP_PANEL_H, 1, CROP_NO_MAP, CROP_NO_MAP, crop->darken_q);
        crop_whiten(panel_pixels, CROP_PANEL_W, CROP_PANEL_W, CROP_PANEL_H, crop->whiten);
        *panel = 1;
    }
    return sub;
}

static void crop_build_upload(SDL_Texture *crop_tex, const uint32_t *crop_pixels, const CropView *v, int sub,
                              SDL_Texture *panel_tex, const uint32_t *panel_pixels, int panel)
{
    if (!sub)
        return;
    upload_padded(crop_tex, crop_pixels, v->w * sub, v->h * sub, VCE_MAX_WIDTH * REDRAW_SUB,
                  VCE_ACTIVE_LINES * REDRAW_SUB);
    if (panel)
        SDL_UpdateTexture(panel_tex, NULL, panel_pixels, CROP_PANEL_W * (int)sizeof(uint32_t));
}

static int crop_build(const uint32_t *frame, const CropState *crop, int mode, int hud_bg, int w, int h,
                      SDL_Texture *crop_tex, uint32_t *crop_pixels, SDL_Texture *panel_tex, uint32_t *panel_pixels,
                      CropView *v, const Battle *bt, const double *bx, const double *by)
{
    int panel, sub = crop_build_cpu(frame, crop, mode, hud_bg, w, h, crop_tex != NULL, crop_pixels, panel_tex != NULL,
                                    panel_pixels, &panel, v, bt, bx, by);
    crop_build_upload(crop_tex, crop_pixels, v, sub, panel_tex, panel_pixels, panel);
    return sub;
}

/* ---- Preparing the new frame ----
 * Everything a new console frame needs before it appears (game fixes, crop, fades, bars, equipment,
 * smooth motion and the images for the GPU textures) is done on its own thread ("preparation") as soon as the
 * console publishes the frame, while the images of the previous frame are on screen. The window, when the time for the
 * new frame comes, only uploads the ready textures. Each prepared frame goes into a slot (PrepSlot) that has everything
 * its images read (frame B, copies of the motion, the bars and the crop, the texture images): the preparation
 * thread only writes to a slot that is neither the one on screen (shown) nor the last prepared one (last, the A of the next).
 * The state carried from one frame to the next (Prep: fixes, equipment, crop, window ring, smooth fade) is only
 * touched with Prep.lock. Lock order: Prep.lock, frame_lock, Prep.slot_lock.
 * With smooth motion on the CPU (F6 or no GPU) and with --prep-inline, the window itself prepares the frame at the
 * time it shows it (as before); the output is the same (--prep-hashes). */
#define PREP_SLOTS 3
#define PREP_FRAME_PX (VCE_MAX_WIDTH * VCE_ACTIVE_LINES)
#define PREP_BIG_PX (VCE_MAX_WIDTH * REDRAW_SUB * VCE_ACTIVE_LINES * REDRAW_SUB)

typedef struct PrepSlot {
    long seq; /* console frame (-1 = empty) */
    int w, h, divider;
    uint32_t tex[PREP_FRAME_PX];   /* the uncropped texture (with the frame darkening) */
    uint32_t shown[PREP_FRAME_PX]; /* the console image shown (fixes, smooth fade, bars): under the menu */
    int crop_ready, panel;         /* crop_px has the cropped frame (crop_ready times larger); panel_px the panel */
    CropView crop_v;
    uint32_t crop_px[PREP_BIG_PX];
    uint32_t panel_px[CROP_PANEL_W * CROP_PANEL_H];
    int over_sub; /* over_px: the crop of frame B with the map transparent (over_sub times larger); 0 = none */
    CropView over_v;
    uint32_t over_px[PREP_BIG_PX];
    int gpu_on;   /* gpu has frame B prepared for the GPU */
    GpuPrep gpu;
    /* Smooth motion B (frame.seq = seq; -1 = none) and the copies of what its images read */
    RedrawFrame frame;
    Motion motion;
    RedrawMask ring_masks[REDRAW_BEHIND];
    Battle battle;
    CropState crop;
    uint32_t ring_bg[PREP_FRAME_PX], ring_spr[PREP_FRAME_PX], ring_fix[PREP_FRAME_PX], win_bg[PREP_FRAME_PX];
    uint8_t map_skip[64];
    long map_skip_seq;
    int window_front, equip_on; /* Redraw for CPU drawing */
    uint32_t hash; /* --prep-hashes */
} PrepSlot;

typedef struct PrepSettings {
    VideoSettings video;
    int ow, oh;        /* output size (crop_target) */
    int smooth_tex;    /* the smooth motion capture exists */
    int gpu, gpu_ok;   /* drawing on the GPU (no F6); the GPU can assemble */
    int crop_tex, panel_tex, over_tex;
} PrepSettings;

static struct {
    SDL_Mutex *lock, *slot_lock;
    SDL_Semaphore *wake;
    SDL_Thread *thread;
    SDL_AtomicInt quit, parallel;
    PrepSlot *slot[PREP_SLOTS];
    int ready, shown, last; /* slots: ready and not shown yet; on screen; the last prepared (-1) */
    long done_seq;          /* last prepared frame */
    int restart;            /* the window prepares the console frame again (menu closed): no A */
    PrepSettings set;       /* from the window, on every image (slot_lock) */
    /* Input (copy from the console) */
    long seq;
    int w, h, divider, next;
    YsPanel panel;
    uint32_t frame[PREP_FRAME_PX];
    uint16_t frame_index[PREP_FRAME_PX];
    YsFadeFrame fade;
    int ys_screen; /* the game program's screen (CROP_SCREEN_*, app/ys_screen.h) */
    /* State from one frame to the next */
    Redraw redraw;
    Motion motion;
    Equip equip;
    Battle battle;
    CropState crop;
    RedrawRing ring;
    int ring_got, ring_fix_ok;
    uint8_t map_skip[64];
    long map_skip_seq;
    FadePixels fade_px;
    uint32_t over_src[PREP_FRAME_PX];
    double bx[BATTLE_STICKERS], by[BATTLE_STICKERS];
    uint8_t ring_rows[VCE_ACTIVE_LINES];
    /* Counters */
    long late_splits, torn_win_fixed, jobs[2]; /* jobs: on the window, on the preparation thread */
    MotionRhythm rhythm; /* --walk-rhythm */
    FILE *hashes; /* --prep-hashes */
} g_prep;

/* Fast checksum (32-bit FNV-1a over 4-byte words; the remainder byte by byte) */
static uint32_t prep_crc(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p = data;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t v;
        memcpy(&v, p + i, 4);
        crc = (crc ^ v) * 16777619u;
    }
    for (; i < n; i++)
        crc = (crc ^ p[i]) * 16777619u;
    return crc;
}

/* What the window shows from the slot (--prep-hashes): the texture images and what its images read */
static uint32_t prep_slot_hash(const PrepSlot *s)
{
    size_t px = (size_t)s->w * s->h * sizeof(uint32_t);
    uint32_t c = prep_crc(0xFFFFFFFFu, s->tex, px);
    c = prep_crc(c, s->shown, px);
    if (s->crop_ready)
        c = prep_crc(c, s->crop_px, (size_t)s->crop_v.w * s->crop_ready * s->crop_v.h * s->crop_ready * 4);
    if (s->panel)
        c = prep_crc(c, s->panel_px, sizeof(s->panel_px));
    if (s->over_sub)
        c = prep_crc(c, s->over_px, (size_t)s->over_v.w * s->over_sub * s->over_v.h * s->over_sub * 4);
    if (s->frame.seq == s->seq) {
        c = prep_crc(c, s->frame.pixels, px);
        static MotionLayout lay;
        static Motion m;
        static RedrawMask masks[REDRAW_BEHIND];
        motion_copy(&m, &s->motion, masks);
        for (int k = 0; k <= 4; k++) { /* the positions at 5 fractions */
            memset(&lay, 0, sizeof(lay));
            motion_layout(&m, &s->frame, k / 4.0, &lay);
            c = prep_crc(c, &lay, sizeof(lay));
        }
    }
    const GpuFrameInfo *g = &s->gpu.f;
    if (s->gpu_on && g->ready) { /* only what was written (the rest of the slot is from old frames) */
        int sc[6] = { g->w, g->h, g->raster0, g->x0, g->windows, g->nbehind };
        c = prep_crc(c, sc, sizeof(sc));
        c = prep_crc(c, g->piece, (size_t)g->npieces * sizeof(GpuPiece));
        c = prep_crc(c, g->region, (size_t)g->nregions * sizeof(GpuRegion));
        c = prep_crc(c, g->front, sizeof(g->front));
        c = prep_crc(c, g->keep, sizeof(g->keep));
        c = prep_crc(c, g->used, sizeof(g->used));
        c = prep_crc(c, g->spr_w, sizeof(g->spr_w));
        c = prep_crc(c, g->spr_h, sizeof(g->spr_h));
        c = prep_crc(c, s->gpu.base, px);
        c = prep_crc(c, s->gpu.atlas, sizeof(s->gpu.atlas));
        if (g->nbehind)
            c = prep_crc(c, s->gpu.masks, sizeof(s->gpu.masks));
    }
    return ~c;
}

/* Copies the published frame from the console (with frame_lock and Prep.lock). force: even if already prepared. Returns the
 * chosen slot, or -1 */
static int prep_input_locked(int force)
{
    long seq = g_emu.frame_seq;
    int w = g_emu.frame_w, h = g_emu.frame_h;
    if (seq < 0 || w <= 0 || h <= 0 || (!force && seq <= g_prep.done_seq))
        return -1;
    SDL_LockMutex(g_prep.slot_lock);
    int k = 0;
    while (k == g_prep.shown || k == g_prep.last)
        k++;
    SDL_UnlockMutex(g_prep.slot_lock);
    PrepSlot *s = g_prep.slot[k];
    g_prep.seq = seq;
    g_prep.w = w;
    g_prep.h = h;
    g_prep.divider = g_emu.frame_divider ? g_emu.frame_divider : 4;
    g_prep.next = g_emu.frame_next;
    g_prep.panel = g_emu.frame_panel;
    memcpy(g_prep.frame, g_emu.frame, (size_t)w * h * sizeof(uint32_t));
    memcpy(g_prep.frame_index, g_emu.frame_index, (size_t)w * h * sizeof(uint16_t));
    g_prep.fade = g_emu.fade;
    g_prep.ys_screen = g_emu.ys_screen;
    if (g_emu.smooth_frame.seq == seq)
        s->frame = g_emu.smooth_frame;
    else
        s->frame.seq = -1;
    s->frame.rewards = g_emu.rewards; /* those of all frames since the last prepared one */
    g_emu.rewards.n = 0;
    return k;
}

/* Prepares in slot k the frame copied by prep_input_locked (with Prep.lock) */
static void prep_run(int k, const PrepSettings *st)
{
    PrepSlot *S = g_prep.slot[k];
    const VideoSettings *vs = &st->video;
    int w = g_prep.w, h = g_prep.h;
    long seq = g_prep.seq;
    uint32_t *frame = g_prep.frame;
    RedrawFrame *sf = &S->frame;
    Redraw *rd = &g_prep.redraw;
    CropState *crop = &g_prep.crop;
    Battle *battle = &g_prep.battle;
    SDL_LockMutex(g_prep.slot_lock);
    PrepSlot *L = g_prep.last >= 0 ? g_prep.slot[g_prep.last] : NULL; /* the previous B (this one's A) */
    SDL_UnlockMutex(g_prep.slot_lock);
    int smoothing = st->smooth_tex && vs->smooth_motion;
    Uint64 t = SDL_GetTicksNS(), t_all = t, part_t;
    S->seq = seq;
    S->w = w;
    S->h = h;
    S->divider = g_prep.divider;
    int cur = st->smooth_tex && sf->seq == seq && sf->w == w && sf->h == h;
    rd->equip = NULL; /* the late frame and the scrolls check the redraw against the console frame */
    /* Frame torn inside the game's windows: they as at the end of the image. Before the scrolls (which
     * redraw with the frame's VRAM) */
    if (cur && fix_torn_window(rd, sf) > 0) {
        memcpy(frame, sf->pixels, (size_t)w * h * sizeof(uint32_t));
        g_prep.torn_win_fixed++;
    }
    /* The late scroll switch at the start of the map (the centipede boss), at the ring wraparound and at the end (the temple) comes out
     * as the game wanted it, by the values it put in the registers: only the shown image (game_fixes.h). Before
     * the late frame, which changes the scroll to the next camera */
    int late_map, late_low;
    if (cur && fix_scroll(rd, sf, &late_map, &late_low) > 0) {
        memcpy(frame, sf->pixels, (size_t)w * h * sizeof(uint32_t));
        g_prep.late_splits++;
    }
    /* Late frame: instead of the repeated list, the one the console never showed (also without smooth motion:
     * user's decision) */
    if (cur && motion_fix_late(&g_prep.motion, rd, sf))
        memcpy(frame, sf->pixels, (size_t)w * h * sizeof(uint32_t));
    /* Item window in front of the conversation portrait */
    rd->window_front = 1;
    if (cur && fix_window(rd, sf))
        memcpy(frame, sf->pixels, (size_t)w * h * sizeof(uint32_t));
    /* Equipment on Adol, only on the map: on the console image and in the smooth motion drawings */
    rd->equip = vs->equipment && crop->active == CROP_SCREEN_GAME ? &g_prep.equip : NULL;
    if (cur && fix_equip(rd, sf))
        memcpy(frame, sf->pixels, (size_t)w * h * sizeof(uint32_t));
    /* Enemy health bars and gains, only on the map: on the frame (without smooth motion) and, in the smooth motion
     * images, at the positions of each one */
    battle->bars = vs->enemy_bars;
    battle->popups = vs->reward_popups;
    if ((battle->bars || battle->popups) && cur && crop->active == CROP_SCREEN_GAME)
        battle_frame(battle, sf);
    else
        battle_init(battle);
    draw_cost_mark(4, &t);
    part_t = t;
    crop->smooth_fades = vs->smooth_fades;
    /* The game's fade for this frame (app/ys_fade.h) */
    crop_set_fade(crop, g_prep.fade.map_level, g_prep.fade.map_add, g_prep.fade.table_ok ? g_prep.fade.table : NULL);
    crop_set_fade_stray(crop, g_prep.fade.map_stray);
    crop_set_screen(crop, g_prep.ys_screen); /* the framed screen, from the game program */
    {
        /* The game's windows (app/ys_windows.h: through the hooks in the open and close routines), in image
         * pixels through each line's scroll */
        int have_bg = 0;
        if (cur) {
            int yb[CROP_WINDOWS_MAX][4], ysc, yn = ys_windows_boxes(&sf->wins, sf->rows, sf->h, yb, CROP_WINDOWS_MAX,
                                                                     &ysc);
            crop_set_windows(crop, (const int (*)[4])yb, yn, ysc);
            /* inside the shrunk windows, the background without the characters over the border (redraw_ring: fix; only with
             * a window: the redraw costs ~0.4 ms) */
            if (yn && crop_small_hud(vs->crop, crop->par, crop->screen)) {
                redraw_bg_only(sf, 0, S->win_bg);
                have_bg = 1;
            }
        }
        draw_cost_mark(8, &part_t);
        crop_frame(crop, frame, w, h);
        draw_cost_mark(9, &part_t);
        /* In fill with the HUD at 3/4, around the shrunk windows, the map from before them at the current scroll and
         * the current characters (crop_ring); inside them, without the sprites that are not theirs */
        int small = crop->active == CROP_SCREEN_GAME && crop_small_hud(vs->crop, crop->par, crop->screen);
        if (cur && small) {
            int boxes[CROP_WINDOWS_MAX][4], nb = crop_ring_boxes(crop, boxes);
            motion_map_rows(sf, g_prep.ring_rows);
            g_prep.ring_got = redraw_ring(&g_prep.ring, sf, nb, boxes, rd->equip, S->ring_bg, S->ring_spr,
                                          have_bg ? S->win_bg : NULL, S->ring_fix, g_prep.ring_rows);
            g_prep.ring_fix_ok = g_prep.ring_got && have_bg;
            crop_ring(crop, g_prep.ring_got == 2 ? S->ring_bg : NULL, g_prep.ring_got ? S->ring_spr : NULL);
            crop->fix_px = g_prep.ring_fix_ok ? S->ring_fix : NULL;
            memcpy(g_prep.map_skip, g_prep.ring.skip, sizeof(g_prep.map_skip));
            g_prep.map_skip_seq = g_prep.ring.skip_seq;
            /* When drawing the map, the masks that touch a window are also redone without it */
            motion_set_ring(&g_prep.motion, g_prep.ring.masks, g_prep.ring.nmasks, g_prep.ring.skip_seq);
        } else {
            g_prep.ring.keep_ok = g_prep.ring.next_ok = 0;
            g_prep.map_skip_seq = -1;
            motion_set_ring(&g_prep.motion, NULL, 0, -1);
        }
        draw_cost_mark(10, &part_t);
    }
    fade_pixels(&g_prep.fade_px, frame, g_prep.frame_index, w, h, g_prep.fade.color, &g_prep.fade, vs->smooth_fades);
    /* In fill with the HUD at 3/4, the gains too (without smooth motion, on the 4x cropped image: crop_build) */
    battle->small = crop->active == CROP_SCREEN_GAME && crop_small_hud(vs->crop, crop->par, crop->screen);
    if (battle->count) {
        int clip[4] = { 0, 0, w, h };
        battle_place(battle, sf, NULL, g_prep.bx, g_prep.by);
        battle_draw_at(battle, g_prep.bx, g_prep.by, frame, w, 1, 0, 0, clip,
                       1 << BATTLE_BAR | (battle->small ? 0 : 1 << BATTLE_POPUP));
    }
    memcpy(S->shown, frame, (size_t)w * h * sizeof(uint32_t));
    crop->next_on = 1;
    crop->next = g_prep.next;
    crop->panel = g_prep.panel;
    crop_target_size(crop, vs, st->ow, st->oh, g_prep.divider);
    crop_pan_step(crop, vs->crop);
    draw_cost_mark(11, &part_t);
    memcpy(S->tex, frame, (size_t)w * h * sizeof(uint32_t));
    if (crop->darken_q) /* the game's fade also on the border and the panel (app/crop.h) */
        crop_darken(S->tex, w, w, h, 1, CROP_MAP_X, CROP_MAP_Y, crop->darken_q);
    draw_cost_mark(12, &part_t);
    draw_cost_mark(5, &t);
    S->crop_ready = crop_build_cpu(frame, crop, vs->crop, vs->hud_bg, w, h, st->crop_tex, S->crop_px, st->panel_tex,
                                   S->panel_px, &S->panel, &S->crop_v, battle, g_prep.bx, g_prep.by);
    draw_cost_mark(6, &t);
    /* Smooth motion's B: the frame */
    if (!smoothing || sf->seq != seq)
        sf->seq = -1;
    /* Smooth motion and the GPU textures */
    S->over_sub = 0;
    S->gpu_on = 0;
    if (sf->seq == seq) {
        Uint64 t1 = SDL_GetTicksNS();
        redraw_check(rd, sf);
        /* A: the previous B, if it is the previous frame (the same frame prepared again after the menu: none) */
        const RedrawFrame *prev = L && L->frame.seq == seq - 1 ? &L->frame : NULL;
        motion_step(&g_prep.motion, prev, sf);
        motion_rhythm_add(&g_prep.rhythm, &g_prep.motion, seq);
        if (st->gpu) {
            rd->skip = g_prep.map_skip_seq == seq ? g_prep.map_skip : NULL;
            gpu_draw_prepare(&S->gpu, st->gpu_ok, &g_prep.motion, rd, sf, rd->window_front);
            S->gpu_on = 1;
            rd->skip = NULL;
            /* The frame's crop (like crop_build) with the map transparent: the moving map comes from the GPU */
            if (st->over_tex && crop_view_state(vs->crop, crop, w, h, &S->over_v)) {
                S->over_v.hud_bg = vs->hud_bg;
                S->over_v.darken = crop->darken_q;
                /* the image shown (with the smooth fade; the map's bars stay under the transparent map) */
                uint32_t *src = g_prep.over_src;
                memcpy(src, S->shown, (size_t)w * h * sizeof(uint32_t));
                for (int y = CROP_MAP_Y; y < CROP_MAP_Y + CROP_MAP_H && y < h; y++)
                    for (int x = CROP_MAP_X; x < CROP_MAP_X + CROP_MAP_W && x < w; x++)
                        src[y * w + x] &= 0x00FFFFFFu;
                S->over_sub = crop_out_sub(&S->over_v);
                crop_compose_sub(src, w, 1, &S->over_v, S->over_px, S->over_sub);
                crop_darken_view(S->over_px, S->over_v.w * S->over_sub, &S->over_v, S->over_sub, crop->darken_q);
            }
        }
        draw_cost_add(&g_draw_cost[1], SDL_GetTicksNS() - t1);
    }
    /* What this frame's images read, in copies inside the slot */
    motion_copy(&S->motion, &g_prep.motion, S->ring_masks);
    S->battle = *battle;
    S->crop = *crop;
    memcpy(S->map_skip, g_prep.map_skip, sizeof(S->map_skip));
    S->map_skip_seq = g_prep.map_skip_seq;
    S->window_front = rd->window_front;
    S->equip_on = rd->equip != NULL;
    if (g_prep.hashes)
        S->hash = prep_slot_hash(S);
    draw_cost_add(&g_draw_cost[13], SDL_GetTicksNS() - t_all);
}

/* Publishes the prepared slot k (the next one to go to the screen) */
static void prep_publish(int k)
{
    SDL_LockMutex(g_prep.slot_lock);
    g_prep.last = g_prep.ready = k;
    g_prep.done_seq = g_prep.slot[k]->seq;
    SDL_UnlockMutex(g_prep.slot_lock);
    if (g_prep.hashes)
        fprintf(g_prep.hashes, "%ld %08X\n", g_prep.slot[k]->seq, g_prep.slot[k]->hash);
}

/* Newest ready frame (-1 = none) */
static long prep_ready_seq(void)
{
    SDL_LockMutex(g_prep.slot_lock);
    long s = g_prep.last >= 0 ? g_prep.slot[g_prep.last]->seq : -1;
    SDL_UnlockMutex(g_prep.slot_lock);
    return s;
}

static int SDLCALL prep_thread(void *data)
{
    (void)data;
    if (!g_emu.turbo)
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (!SDL_GetAtomicInt(&g_prep.quit)) {
        SDL_WaitSemaphoreTimeout(g_prep.wake, 50);
        if (SDL_GetAtomicInt(&g_prep.quit))
            break;
        if (!SDL_GetAtomicInt(&g_prep.parallel))
            continue;
        SDL_LockMutex(g_prep.lock);
        Uint64 t = SDL_GetTicksNS();
        SDL_LockMutex(g_emu.frame_lock);
        int k = g_prep.restart ? -1 : prep_input_locked(0);
        SDL_UnlockMutex(g_emu.frame_lock);
        if (k >= 0) {
            draw_cost_add(&g_draw_cost[3], SDL_GetTicksNS() - t);
            SDL_LockMutex(g_prep.slot_lock);
            PrepSettings st = g_prep.set;
            SDL_UnlockMutex(g_prep.slot_lock);
            prep_run(k, &st);
            prep_publish(k);
            g_prep.jobs[1]++;
        }
        SDL_UnlockMutex(g_prep.lock);
        if (k >= 0)
            SDL_SignalSemaphore(g_emu.frame_ready);
    }
    return 0;
}

static int prep_start(int parallel, const char *hashes_path)
{
    for (int i = 0; i < PREP_SLOTS; i++) {
        g_prep.slot[i] = calloc(1, sizeof(PrepSlot));
        if (!g_prep.slot[i])
            return -1;
        g_prep.slot[i]->seq = g_prep.slot[i]->frame.seq = -1;
    }
    g_prep.ready = g_prep.shown = g_prep.last = -1;
    g_prep.done_seq = -1;
    g_prep.map_skip_seq = -1;
    g_prep.lock = SDL_CreateMutex();
    g_prep.slot_lock = SDL_CreateMutex();
    g_prep.wake = g_prep_wake = SDL_CreateSemaphore(0);
    if (hashes_path && !(g_prep.hashes = u8_fopen(hashes_path, "w")))
        fprintf(stderr, "--prep-hashes: could not open %s\n", hashes_path);
    SDL_SetAtomicInt(&g_prep.parallel, parallel);
    if (parallel)
        g_prep.thread = SDL_CreateThread(prep_thread, "prep", NULL);
    return 0;
}

static void prep_stop(void)
{
    SDL_SetAtomicInt(&g_prep.quit, 1);
    if (g_prep.wake)
        SDL_SignalSemaphore(g_prep.wake);
    if (g_prep.thread)
        SDL_WaitThread(g_prep.thread, NULL);
    g_prep.thread = NULL;
    if (g_prep.hashes)
        fclose(g_prep.hashes);
    g_prep.hashes = NULL;
}

/* The cropped image (app/crop.h, v->w x v->h, sub times larger) and, on top: in full mode, the panel (panel,
 * transparent outside the text and the bars, at the frame mode's size, centered in the band under the map); with the
 * options menu open, its layer (menu_layer, v->w x menu_h: at the cropped image's scale, like the game's menus;
 * centered on it, taller if the image is short). In the compact and wide modes, the HUD band continues below the
 * image down to the screen edge, in the bottom color of the background (user's decision: above, black). All with the
 * image options (filter, CRT). With canvas_ky > 0, tex is the image assembled on the GPU, sub x canvas_ky
 * pixels per pixel (present_draw_canvas). */
#define MENU_LAYER_MIN_H 160 /* the menu's tallest window fits (wide/fill scene: 128-line image) */

static void crop_screen_draw(Screen *s, SDL_Texture *tex, const CropView *v, int sub, int canvas_ky, int divider,
                             SDL_Texture *panel, SDL_Texture *menu_layer, int menu_h)
{
    if (g_gpu_time)
        gpu_time_mark(s->renderer, 1);
    if (canvas_ky > 0)
        present_draw_canvas(&s->present, s->renderer, tex, v->w, v->h, sub, canvas_ky, divider, &s->video);
    else
        present_draw(&s->present, s->renderer, tex, v->w, v->h, sub, divider, &s->video);
    int ow = 0, oh = 0, kx, ky;
    SDL_GetCurrentRenderOutputSize(s->renderer, &ow, &oh);
    VideoRect d;
    video_layout(ow, oh, v->w, v->h, divider, &s->video, &d, &kx, &ky);
    if (v->band && d.y + d.h < oh && v->hud_bg != CROP_HUD_BLACK) {
        /* in vertical slices, each in the color of the band's last line (the light in the center and the
         * left-to-right gradient change along it), darkened with the fade, like the band */
        enum { SLICES = 32 };
        SDL_Vertex vx[(SLICES + 1) * 2];
        int idx[SLICES * 6];
        for (int i = 0; i <= SLICES; i++) {
            uint32_t c = crop_hud_bottom(v->hud_bg, (v->w - 1) * i / SLICES, v->w);
            crop_darken(&c, 1, 1, 1, 1, CROP_NO_MAP, CROP_NO_MAP, v->darken);
            crop_whiten(&c, 1, 1, 1, v->whiten);
            SDL_FColor fc = { (float)(c >> 16 & 0xFF) / 255.0f, (float)(c >> 8 & 0xFF) / 255.0f, (float)(c & 0xFF) / 255.0f,
                              1.0f };
            float x = (float)d.x + (float)d.w * (float)i / SLICES;
            vx[i * 2] = (SDL_Vertex){ { x, (float)(d.y + d.h) }, fc, { 0, 0 } };
            vx[i * 2 + 1] = (SDL_Vertex){ { x, (float)oh }, fc, { 0, 0 } };
            if (i < SLICES) {
                int *t = &idx[i * 6], k = i * 2;
                t[0] = k, t[1] = k + 1, t[2] = k + 2, t[3] = k + 1, t[4] = k + 3, t[5] = k + 2;
            }
        }
        SDL_RenderGeometry(s->renderer, NULL, vx, (SLICES + 1) * 2, idx, SLICES * 6);
    }
    if (v->hud && panel) {
        VideoRect pr;
        crop_hud_place(ow, oh, divider, &s->video, &d, &pr);
        present_layer(&s->present, s->renderer, panel, CROP_PANEL_W, CROP_PANEL_H, &pr, &s->video);
    }
    if (menu_layer) {
        /* At the image's scale; if it does not fit on the screen (wide/fill with TV pixels: 138-line image, the
         * menu's tallest window has 144), smaller, until it fits */
        VideoRect md = d;
        md.h = (int)((long long)d.h * menu_h / v->h);
        int gap = oh / 20, room = oh - 2 * gap; /* not touching the top and bottom of the screen (user's request) */
        if (md.h > room) {
            md.w = (int)((long long)d.w * room / md.h);
            md.h = room;
            md.x = d.x + (d.w - md.w) / 2;
        }
        md.y = d.y + (d.h - md.h) / 2;
        md.y = md.y < gap ? gap : md.y + md.h > oh - gap ? oh - gap - md.h : md.y;
        present_layer(&s->present, s->renderer, menu_layer, v->w, menu_h, &md, &s->video);
    }
    screen_dump(s->renderer);
    screen_present(s->renderer);
}

/* ---- Launcher (app/launcher.h): before the game ----
 * User's images: launcher/ next to the .exe (the package), otherwise the project's docs/ (development). */
static const char *const k_launcher_images[1 + LAUNCHER_GAMES] = { "Ys Logo.jpg", "YS I II.jpg", "Ys III.jpg",
                                                                   "Ys IV.jpg" };

static void launcher_load_images(Launcher *L)
{
    const char *exe = SDL_GetBasePath();
    for (int i = 0; i <= LAUNCHER_GAMES; i++) {
        char path[1024];
        uint32_t *px = NULL;
        int w = 0, h = 0, ok = 0;
        snprintf(path, sizeof(path), "%slauncher/%s", exe ? exe : "", k_launcher_images[i]);
        ok = launcher_load_jpg(path, &px, &w, &h) == 0;
#ifdef PCE_DOCS_DIR
        if (!ok) {
            snprintf(path, sizeof(path), "%s/%s", PCE_DOCS_DIR, k_launcher_images[i]);
            ok = launcher_load_jpg(path, &px, &w, &h) == 0;
        }
#endif
        if (!ok || launcher_set_image(L, i, px, w, h) != 0)
            printf("Launcher: missing image %s.\n", k_launcher_images[i]);
        free(px);
    }
}

/* The effects recorded by the hidden console: for the sound, for launcher_sfx.bin and the menu font */
static void launcher_take_capture(LauncherCapture *cap, LauncherSound *snd, const char *sfx_path)
{
    int n = 0;
    for (int i = 0; i < cap->count; i++)
        if (cap->pcm[i]) {
            launcher_sound_set_sfx(snd, i, cap->pcm[i], cap->frames[i]);
            n++;
        }
    printf("Launcher: Ys effects recorded by the hidden console: %d of %d (%ld frames).\n", n, cap->count,
           cap->console_frames);
    if (n == cap->count && launcher_sfx_save(sfx_path, cap) == 0)
        printf("Launcher: effects stored in %s.\n", sfx_path);
    if (g_emu.font_check && cap->vram && menu_font_capture(cap->vram)) {
        g_emu.font_check = 0;
        if (g_emu.font_path[0] && menu_font_save(g_emu.font_path) == 0)
            printf("Menu font copied from the game (%s).\n", g_emu.font_path);
    }
    launcher_capture_free(cap);
}

/* Options the launcher's options menu can change (the same as in the game) */
typedef struct LauncherOptions {
    AudioVolumes *vol;
    int *skip_intro, *pause_bg;
    Menu *menu;
    const char *config_path; /* NULL = do not write */
} LauncherOptions;

/* Runs the launcher. dir = settings folder (with the trailing slash); test_dir = --launcher-test (presses
 * the keys by itself and saves screen images). Esc/SELECT (keyboard) or Start/SELECT (controller) open the options
 * menu on top, as in the game. Returns 1 = start the game, 0 = quit. */
static int run_launcher(SDL_AudioStream *audio, LauncherOptions *o, const char *cue, const char *dir,
                        const char *test_dir)
{
    Screen *s = &g_screen;
    Menu *menu = o->menu;
    Launcher *L = calloc(1, sizeof(Launcher));
    static LauncherCapture cap;
    static LauncherSound snd;
    static uint32_t menu_px[LAUNCHER_W * LAUNCHER_H];
    if (!L)
        return 1;
    launcher_init(L);
    launcher_load_images(L);
    launcher_sound_init(&snd, o->vol);
    AudioVolumes start_vol = *o->vol;
    int start_skip = *o->skip_intro, start_pause = *o->pause_bg;
    InputMap start_input = input_map;
    VideoSettings start_video = s->video;
    SDL_Texture *menu_tex = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              LAUNCHER_W, LAUNCHER_H);
    char sfx_path[1024];
    snprintf(sfx_path, sizeof(sfx_path), "%slauncher_sfx.bin", dir);
    int capturing = 0;
    if (launcher_sfx_load(sfx_path, &snd) == 0)
        printf("Launcher: efeitos do Ys lidos de %s.\n", sfx_path);
    else
        capturing = launcher_capture_start(&cap, cue, launcher_sfx_numbers, 3) == 0;

    uint32_t *img = malloc((size_t)LAUNCHER_IMG_W * LAUNCHER_IMG_H * sizeof(uint32_t)), *txt[2] = { NULL, NULL };
    SDL_Texture *img_tex = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             LAUNCHER_IMG_W, LAUNCHER_IMG_H);
    SDL_Texture *txt_tex[2] = { NULL, NULL }, *logo_tex = NULL;
    int txt_w[2] = { 0, 0 }, txt_h[2] = { 0, 0 }, logo_w = 0, logo_ah = 0;
    long logo_tick = -1;
    uint32_t *logo_band = NULL; /* the logo in a band of whole lines out of the background's 216 (so the CRT lines up) */
    int band_h = 0, band_lines = 0, band_y0 = 0, band_off = 0;
    LauncherLogoFx logo_fx = { 0 };
    /* --launcher-test: tick -> key (Ys III, Ys IV, "not released", options menu opened and closed, back to
     * Ys I & II, start). key -1 = opens the options menu, -2 = "back" in the menu */
    static const struct {
        long tick;
        int key;
    } k_script[] = { { 30, LAUNCHER_KEY_RIGHT }, { 45, LAUNCHER_KEY_RIGHT }, { 50, LAUNCHER_KEY_OK }, { 60, -1 },
                     { 80, -2 }, { 120, LAUNCHER_KEY_LEFT }, { 130, LAUNCHER_KEY_LEFT }, { 140, LAUNCHER_KEY_OK } };
    static const long k_shots[] = { 40, 70, 110, 150 };
    int step = 0, result = -1, fading = 0, pad_mode = 0;
    long shot_tick = -1;
    Uint64 tick_ns = 1000000000ull / LAUNCHER_TICK_HZ, next_tick = SDL_GetTicksNS();
    while (result < 0 && img && img_tex) {
        SDL_Event ev;
        int res = 0, opening = 0;
        while (SDL_PollEvent(&ev)) {
            int k = -1;
            if (ev.type == SDL_EVENT_QUIT)
                result = 0;
            else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_RETURN &&
                     (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat) {
                    s->video.fullscreen = !s->video.fullscreen;
                    screen_apply(s, 0);
                    menu->video.fullscreen = menu->pending.fullscreen = s->video.fullscreen;
                    if (o->config_path && !menu->open)
                        app_save_settings(o->config_path, o->vol, *o->skip_intro, *o->pause_bg, &input_map, &s->video);
                }
            } else if (input_pads_event(&pads, &ev))
                ;
            else if (ev.type == SDL_EVENT_KEY_DOWN) {
                /* As in the game (the main loop): new key in the menu, Esc opens, arrows/OK in the open menu */
                int sc = ev.key.scancode;
                pad_mode = 0;
                if (menu_waiting_key(menu) || menu_waiting_button(menu)) {
                    if (!ev.key.repeat && (menu_waiting_key(menu) || sc == SDL_SCANCODE_ESCAPE))
                        res |= menu_waiting_key(menu) ? menu_capture_key(menu, sc) : menu_key(menu, MENU_KEY_BACK);
                } else if (!menu->open && !ev.key.repeat &&
                           (sc == SDL_SCANCODE_ESCAPE || sc == input_map.key[PAD_FN_SELECT]))
                    opening = 1;
                else if (menu->open) {
                    if ((k = input_key_to_menu(&input_map, sc)) >= 0)
                        res |= menu_key(menu, (MenuKey)k);
                    k = -1;
                } else
                    k = input_key_to_menu(&input_map, sc);
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                int b = ev.gbutton.button;
                pad_mode = 1;
                if (menu_waiting_button(menu) || menu_waiting_key(menu)) {
                    if (menu_waiting_button(menu) || b == SDL_GAMEPAD_BUTTON_START)
                        res |= menu_waiting_button(menu) ? menu_capture_button(menu, b) : menu_key(menu, MENU_KEY_BACK);
                } else if (!menu->open && (b == SDL_GAMEPAD_BUTTON_START || b == input_map.button[PAD_FN_SELECT]))
                    opening = 1;
                else if (menu->open) {
                    if ((k = input_button_to_menu(&input_map, b)) >= 0)
                        res |= menu_key(menu, (MenuKey)k);
                    k = -1;
                } else
                    k = input_button_to_menu(&input_map, b);
            } else if (ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                k = input_stick_to_menu(&pads, &ev);
                if (k >= 0)
                    pad_mode = 1;
                if (menu->open) {
                    if (!menu->waiting && k >= 0)
                        res |= menu_key(menu, (MenuKey)k);
                    k = -1;
                }
            }
            if (!test_dir && (k == MENU_KEY_LEFT || k == MENU_KEY_RIGHT || k == MENU_KEY_OK))
                launcher_sound_play(&snd, launcher_key(L, k == MENU_KEY_LEFT    ? LAUNCHER_KEY_LEFT
                                                          : k == MENU_KEY_RIGHT ? LAUNCHER_KEY_RIGHT
                                                                                : LAUNCHER_KEY_OK));
        }
        if (capturing && launcher_capture_done(&cap)) {
            launcher_take_capture(&cap, &snd, sfx_path);
            capturing = 0;
        }
        /* Steps of 1/30 s (the test waits for the effects before starting). In --launcher-test, one step per image:
         * with late drawing the window skipped the step of the saved screens */
        Uint64 now = SDL_GetTicksNS();
        for (int n = 0; now >= next_tick && n < (test_dir ? 1 : 4) && result < 0; n++) {
            next_tick += tick_ns;
            if (test_dir && capturing)
                continue;
            if (test_dir && step < (int)(sizeof(k_script) / sizeof(k_script[0])) && L->tick == k_script[step].tick) {
                int key = k_script[step++].key;
                if (key == -1)
                    opening = 1;
                else if (key == -2)
                    res |= menu_key(menu, MENU_KEY_BACK);
                else
                    launcher_sound_play(&snd, launcher_key(L, (LauncherKey)key));
            }
            if (launcher_tick(L))
                result = 1;
        }
        /* Options menu: the same options as in the game, written on close if they changed */
        res |= menu_tick(menu, SDL_GetTicks());
        if (opening && !menu->open) {
            menu_open(menu, o->vol, *o->skip_intro, *o->pause_bg, &input_map, &s->video, &s->modes);
            printf("Launcher: options menu opened.\n");
        }
        if (menu->open && menu->sfx != MENU_SFX_NONE) { /* the key's sound effect */
            launcher_sound_play(&snd, menu->sfx);
            menu->sfx = MENU_SFX_NONE;
        }
        menu->pad_mode = pad_mode;
        if (res & MENU_CHANGED_VOLUME) {
            *o->vol = menu->vol;
            launcher_sound_volumes(&snd, o->vol);
        }
        if (res & MENU_CHANGED_INPUT)
            input_map = menu->input;
        if (res & MENU_CHANGED_SETTING) {
            *o->skip_intro = menu->skip_intro;
            *o->pause_bg = menu->pause_bg;
        }
        if (res & MENU_CHANGED_VIDEO) {
            int resize = menu->video.window_scale != s->video.window_scale;
            int same = video_same_display(&menu->video, &s->video);
            s->video = menu->video;
            if (!same)
                screen_apply(s, resize);
        }
        if (res & (MENU_CLOSED | MENU_QUIT)) {
            printf("Launcher: options menu closed.\n");
            if (o->config_path &&
                (memcmp(o->vol, &start_vol, sizeof(start_vol)) != 0 || *o->skip_intro != start_skip ||
                 *o->pause_bg != start_pause || memcmp(&input_map, &start_input, sizeof(input_map)) != 0 ||
                 memcmp(&s->video, &start_video, sizeof(s->video)) != 0)) {
                app_save_settings(o->config_path, o->vol, *o->skip_intro, *o->pause_bg, &input_map, &s->video);
                printf("Options saved to %s.\n", o->config_path);
            }
            start_vol = *o->vol;
            start_skip = *o->skip_intro;
            start_pause = *o->pause_bg;
            start_input = input_map;
            start_video = s->video;
        }
        if (res & MENU_QUIT)
            result = 0;
        if (now > next_tick + tick_ns * 4)
            next_tick = now; /* window stuck: does not catch up */
        if (launcher_starting(L) && !fading) {
            launcher_sound_fade_out(&snd);
            fading = 1;
        }
        /* Sound: ~60 ms in the queue */
        while (audio && SDL_GetAudioStreamQueued(audio) < LAUNCHER_RATE * 4 * 60 / 1000) {
            int16_t chunk[735 * 2];
            launcher_sound_mix(&snd, chunk, 735);
            SDL_PutAudioStreamData(audio, chunk, (int)sizeof(chunk));
        }

        /* Drawing: centered 16:9 area; console image with the filters and the CRT at 216 lines; logo and texts at
         * the screen resolution (texts with the filters, like the HUD and the menus) */
        int ow = 0, oh = 0;
        SDL_GetCurrentRenderOutputSize(s->renderer, &ow, &oh);
        VideoRect area = { 0, 0, ow, ow * 9 / 16 };
        if (area.h > oh) {
            area.h = oh;
            area.w = oh * 16 / 9;
        }
        area.x = (ow - area.w) / 2;
        area.y = (oh - area.h) / 2;
        SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, 255);
        SDL_RenderClear(s->renderer);
        if (area.w > 0 && area.h > 0) {
            launcher_draw(L, img);
            SDL_UpdateTexture(img_tex, NULL, img, LAUNCHER_IMG_W * (int)sizeof(uint32_t));
            s->present.mask_x = (float)area.x;
            s->present.mask_y = (float)area.y;
            present_overlay_lines(&s->present, s->renderer, img_tex, LAUNCHER_IMG_W, LAUNCHER_IMG_H, LAUNCHER_H, &area,
                                  &s->video);
            int lx, ly, lw, lh;
            launcher_logo_rect(area.w, area.h, L, &lx, &ly, &lw, &lh);
            /* Golden logo with the effects (outline, emboss, pulsing glow, reflection): rebuilt at every step */
            if (L->logo && (lw != logo_w || area.h != logo_ah)) {
                if (logo_tex)
                    SDL_DestroyTexture(logo_tex);
                logo_tex = NULL;
                free(logo_band);
                logo_band = NULL;
                if (launcher_logo_fx_build(&logo_fx, L, lw, area.h) == 0) {
                    double line = (double)area.h / LAUNCHER_H;
                    int top = ly - logo_fx.pad, l0 = (int)floor(top / line), l1 = (int)ceil((top + logo_fx.fh) / line);
                    band_y0 = (int)lround(l0 * line);
                    band_h = (int)lround(l1 * line) - band_y0;
                    band_lines = l1 - l0;
                    band_off = top - band_y0;
                    logo_band = calloc((size_t)logo_fx.fw * band_h, sizeof(uint32_t));
                    if (logo_band)
                        logo_tex = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                     SDL_TEXTUREACCESS_STREAMING, logo_fx.fw, band_h);
                }
                logo_w = lw;
                logo_ah = area.h;
                logo_tick = -1;
            }
            if (logo_tex) {
                if (logo_tick != L->tick) {
                    const uint32_t *px = launcher_logo_fx_render(&logo_fx, L->tick);
                    if (px) {
                        for (int y = 0; y < logo_fx.fh && band_off + y < band_h; y++)
                            memcpy(logo_band + (size_t)(band_off + y) * logo_fx.fw, px + (size_t)y * logo_fx.fw,
                                   (size_t)logo_fx.fw * sizeof(uint32_t));
                        SDL_UpdateTexture(logo_tex, NULL, logo_band, logo_fx.fw * (int)sizeof(uint32_t));
                    }
                    logo_tick = L->tick;
                }
                /* With the filters and the CRT, like the rest (report of 2026-10-22: the logo was missing them) */
                VideoRect d = { area.x + lx - logo_fx.pad, area.y + band_y0, logo_fx.fw, band_h };
                present_layer_lines(&s->present, s->renderer, logo_tex, logo_fx.fw, band_h, band_lines, &d, &s->video);
            }
            /* Two text layers: project name and window (larger font) and the help line */
            for (int p = 0; p < 2; p++) {
                int k = launcher_text_scale(area.h, p), tw = (area.w + k - 1) / k, th = (area.h + k - 1) / k;
                if (tw != txt_w[p] || th != txt_h[p]) {
                    if (txt_tex[p])
                        SDL_DestroyTexture(txt_tex[p]);
                    free(txt[p]);
                    txt[p] = malloc((size_t)tw * th * sizeof(uint32_t));
                    txt_tex[p] = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                   tw, th);
                    txt_w[p] = tw;
                    txt_h[p] = th;
                }
                if (txt[p] && txt_tex[p]) {
                    launcher_draw_text(L, txt[p], tw, th, p);
                    SDL_UpdateTexture(txt_tex[p], NULL, txt[p], tw * (int)sizeof(uint32_t));
                    VideoRect td = { area.x, area.y, tw * k, th * k };
                    present_layer(&s->present, s->renderer, txt_tex[p], tw, th, &td, &s->video);
                }
            }
            /* Options menu at the scale of the launcher's console pixels (384x216), with the filters */
            if (menu->open && menu_tex) {
                memset(menu_px, 0, sizeof(menu_px));
                /* The window stack (main one and pages to the right) stays centered, as in the game */
                menu_draw(menu, menu_px, LAUNCHER_W, LAUNCHER_H, LAUNCHER_W, menu_blink_on(SDL_GetTicks()));
                SDL_UpdateTexture(menu_tex, NULL, menu_px, LAUNCHER_W * (int)sizeof(uint32_t));
                present_layer(&s->present, s->renderer, menu_tex, LAUNCHER_W, LAUNCHER_H, &area, &s->video);
            }
        }
        for (size_t i = 0; test_dir && i < sizeof(k_shots) / sizeof(k_shots[0]); i++)
            if (L->tick == k_shots[i] && shot_tick != L->tick) {
                shot_tick = L->tick;
                char path[1024];
                snprintf(path, sizeof(path), "%s/launcher_%ld.bmp", test_dir, k_shots[i]);
                SDL_Surface *shot = SDL_RenderReadPixels(s->renderer, NULL);
                if (shot && SDL_SaveBMP(shot, path))
                    printf("Launcher: screen saved to %s.\n", path);
                SDL_DestroySurface(shot);
            }
        SDL_RenderPresent(s->renderer);
        if (!s->vsync)
            SDL_Delay(4);
    }
    if (result == 1)
        printf("Launcher: %s chosen, starting the game.\n", launcher_games[L->sel].name);
    if (capturing)
        launcher_capture_free(&cap); /* the game started before the end: recorded next time */
    launcher_sound_free(&snd);
    menu->open = 0;
    if (menu_tex)
        SDL_DestroyTexture(menu_tex);
    for (int p = 0; p < 2; p++) {
        if (txt_tex[p])
            SDL_DestroyTexture(txt_tex[p]);
        free(txt[p]);
    }
    if (logo_tex)
        SDL_DestroyTexture(logo_tex);
    free(logo_band);
    launcher_logo_fx_free(&logo_fx);
    if (img_tex)
        SDL_DestroyTexture(img_tex);
    free(img);
    launcher_free(L);
    free(L);
    return result != 0;
}

/* --launcher-wav FOLDER: writes to WAV all the Ys effects (hidden console) and the launcher music, to
 * listen to and choose */
static int launcher_wav(const char *cue, const char *dir)
{
    static LauncherCapture cap;
    uint8_t all[0x30 - 0x10];
    for (int i = 0; i < (int)sizeof(all); i++)
        all[i] = (uint8_t)(0x10 + i);
    if (launcher_capture_run(&cap, cue, all, (int)sizeof(all)) != 0)
        printf("Hidden console did not reach the game (%ld frames).\n", cap.console_frames);
    char path[1024];
    for (int i = 0; i < cap.count; i++)
        if (cap.pcm[i]) {
            snprintf(path, sizeof(path), "%s/sfx_%02X.wav", dir, cap.numbers[i]);
            if (launcher_write_wav(path, cap.pcm[i], cap.frames[i]) == 0)
                printf("Effect $%02X: %.2f s -> %s\n", cap.numbers[i], cap.frames[i] / 44100.0, path);
        }
    launcher_capture_free(&cap);
    /* And the launcher music (the simulated one), two loops */
    static LauncherSound snd;
    static int16_t pcm[44100 * 2 * 16];
    AudioVolumes full = { 100, 100, 100, 100, 100, 1 };
    launcher_sound_init(&snd, &full);
    launcher_sound_mix(&snd, pcm, 44100 * 16);
    snprintf(path, sizeof(path), "%s/music.wav", dir);
    if (launcher_write_wav(path, pcm, 44100 * 16) == 0)
        printf("Launcher music: 16 s -> %s\n", path);
    launcher_sound_free(&snd);
    return 0;
}
#endif

/* --frame-timing: how long each game frame stayed on screen, measured two ways:
 *   - in display refreshes (how many Presents since the previous frame): with V-Sync and pacing by the screen, the
 *     expected value is refreshes_per_frame; it is what the player sees (one frame more or less = a stutter);
 *   - in ms, from the Present that showed it to the next one (the Windows clock has ~1-2 ms of slack; it is the
 *     measure that counts without V-Sync/G-Sync, expected 1/60.05 s = 16.65 ms).
 * "Off the expected": refreshes different from the expected (pacing by the screen) or more than 2 ms (otherwise). */
typedef struct FrameTiming {
    int on;
    Uint64 last;
    long presents, last_present; /* Presents done; at the last new frame */
    long count, off, skipped;
    double sum;
    long hist[41];    /* by rounded ms; the last one collects 40 ms or more */
    long refreshes[6]; /* by display refreshes; the last one collects 5 or more */
} FrameTiming;

static void frame_timing_add(FrameTiming *t, Uint64 now, int expected_refreshes)
{
    if (t->last && t->skipped >= 120) { /* ignores the first 2 seconds */
        double ms = (now - t->last) / 1e6;
        int b = (int)(ms + 0.5), r = (int)(t->presents - t->last_present);
        t->hist[b > 40 ? 40 : b]++;
        t->refreshes[r > 5 ? 5 : r]++;
        t->sum += ms;
        t->count++;
        int off = expected_refreshes ? r != expected_refreshes : SDL_fabs(ms - 1000.0 / 60.05) > 2.0;
        if (off && (t->off++ < 20 || getenv("PCE_TIMING_ALL")))
            printf("  [game frame %ld] stayed %d refresh(es), %.1f ms\n", g_emu.frames_done, r, ms);
    } else if (t->last) {
        t->skipped++;
    }
    t->last = now;
    t->last_present = t->presents;
}

static void frame_timing_report(const FrameTiming *t, float hz, int expected_refreshes)
{
    if (!t->on)
        return;
    printf("Pacing on screen (%.2f Hz, expected %s): %ld frames, average %.2f ms, off the expected: %ld (%.1f%%)\n", hz,
           expected_refreshes ? "in refreshes per frame" : "16.65 ms +-2", t->count,
           t->count ? t->sum / t->count : 0.0, t->off, t->count ? 100.0 * t->off / t->count : 0.0);
    printf("  refreshes per frame:");
    for (int r = 0; r <= 5; r++)
        if (t->refreshes[r])
            printf(" %s%d: %ld", r == 5 ? ">=" : "", r, t->refreshes[r]);
    printf("\n  ms per frame:");
    for (int b = 0; b <= 40; b++)
        if (t->hist[b])
            printf(" %s%d: %ld", b == 40 ? ">=" : "", b, t->hist[b]);
    printf("\n");
    printf("Single clock (%s, %d per frame): %ld images, %ld delivered late, %ld without the frame ready, %ld jumps; "
           "estimated drawing %.2f ms (with the new frame; the others %.2f ms)\n",
           g_pace.vsync ? "V-Sync" : "no V-Sync", g_pace.per_frame, g_pace.images, g_pace.late, g_pace.holds,
           g_pace.jumps, g_pace.draw_est[1] / 1e6, g_pace.draw_est[0] / 1e6);
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    long max_frames = -1;
    int turbo = 0; /* --turbo */
    int start_motion_cpu = 0; /* --motion-cpu */
    const char *coverage_dir = NULL;
    const char *bios_path = NULL;
    const char *record_dir = NULL;
    const char *input_path = NULL;
    const char *save_path = NULL;
    static char default_save[1024];
    static uint8_t initial_bram[CD_BRAM_SIZE]; /* save the session started from (for --record) */
    int save_loaded = 0;
    int vrr = 0;
    static FrameTiming timing;
    video_defaults(&g_screen.video);
    g_screen.video.fullscreen = 0; /* development (no settings): windowed */
    int stall_ms = 0, stall_audio = 0;
    int gg_mix = 0;
    AudioVolumes vol = { 100, 100, 100, 100, 100, 1 };
    long mark_test_frame = -1; /* --mark-test: simulates F12 (mark in the recorded session) on this frame */
#ifdef PCE_APP_YS
    static Menu menu;
    long menu_test_frame = -1; /* --menu-test */
    int menu_test_step = 0;
    long menu_test_refresh = 0;
    uint8_t pad_block = 0; /* buttons that closed the menu: they do not reach the game until released */
    AudioVolumes menu_start_vol = vol; /* volumes when the menu opened (written only if they changed) */
    int skip_intro = 0, menu_start_skip = 0; /* "Skip intro" option: straight to the main menu */
    int pause_bg = 1, menu_start_pause = 1;  /* "Pause in background" option */
    int bg_paused = 0;                       /* game paused because the window lost focus */
    static InputMap menu_start_input;
    VideoSettings menu_start_video;
    int menu_pad_mode = 0; /* last device used in the menu: 1 = controller */
    input_default_keys(&input_map);
    input_default_buttons(&input_map);
    long skip_at[8];                          /* --skip-at */
    int skip_at_count = 0;
    static uint32_t menu_pixels[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    int prep_inline = 0;              /* --prep-inline: the window prepares the new frame */
    const char *prep_hashes = NULL;   /* --prep-hashes FILE */
    int menu_hidden = getenv("PCE_MENU_HIDDEN") != NULL; /* test: only the image under the menu */
    const char *config_path = NULL;
    static char app_config[1024], app_cue[1024], app_save[1024], app_record[1024];
    int launcher_on = -1; /* launcher: -1 = only in the player version */
    const char *launcher_test = NULL, *launcher_wav_dir = NULL;
#endif
    static InputScript playback;
    long frames_done = 0;
    int interpret_only = 0;
    int lockstep_on = 0, diverged = 0;
    static Coverage coverage;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--turbo") == 0)
            turbo = 1;
        else if (strcmp(argv[i], "--motion-cpu") == 0)
            start_motion_cpu = 1;
        else if (strcmp(argv[i], "--interpret") == 0)
            interpret_only = 1;
        else if (strcmp(argv[i], "--lockstep") == 0)
            lockstep_on = 1;
        else if (strcmp(argv[i], "--coverage") == 0 && i + 1 < argc)
            coverage_dir = argv[++i];
        else if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc)
            bios_path = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            record_dir = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
            input_path = argv[++i];
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc)
            save_path = argv[++i];
        else if (strcmp(argv[i], "--vrr") == 0)
            vrr = 1;
        else if (strcmp(argv[i], "--frame-timing") == 0)
            timing.on = 1;
        else if (strcmp(argv[i], "--screen-dump") == 0 && i + 3 < argc) {
            g_dump.from = strtol(argv[i + 1], NULL, 10);
            g_dump.count = strtol(argv[i + 2], NULL, 10);
            g_dump.dir = argv[i + 3];
            i += 3;
        } else if (strcmp(argv[i], "--window-size") == 0 && i + 2 < argc) {
            g_window_w = (int)strtol(argv[i + 1], NULL, 10);
            g_window_h = (int)strtol(argv[i + 2], NULL, 10);
            i += 2;
        }
        else if (strcmp(argv[i], "--gg-mix") == 0)
            gg_mix = 1;
        else if (strcmp(argv[i], "--stall-window") == 0 && i + 1 < argc)
            stall_ms = (int)strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--stall-device") == 0 && i + 1 < argc) {
            stall_ms = (int)strtol(argv[++i], NULL, 10);
            stall_audio = 1;
        } else if (strcmp(argv[i], "--audio-hiccup") == 0 && i + 1 < argc)
            g_audio_hiccup_ms = (int)strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--mark-test") == 0 && i + 1 < argc)
            mark_test_frame = strtol(argv[++i], NULL, 10);
#ifdef PCE_APP_YS
        else if (strcmp(argv[i], "--walk-rhythm") == 0 && i + 2 < argc) {
            g_prep.rhythm.from = strtol(argv[++i], NULL, 10);
            g_prep.rhythm.to = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            config_path = argv[++i];
        else if (strcmp(argv[i], "--prep-inline") == 0)
            prep_inline = 1;
        else if (strcmp(argv[i], "--prep-hashes") == 0 && i + 1 < argc)
            prep_hashes = argv[++i];
        else if (strcmp(argv[i], "--menu-test") == 0 && i + 1 < argc)
            menu_test_frame = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--intro-to-menu") == 0)
            skip_intro = 1; /* test: as with the Skip intro option on */
        else if (strcmp(argv[i], "--launcher") == 0)
            launcher_on = 1; /* development: the launcher also with the .cue on the command line */
        else if (strcmp(argv[i], "--launcher-test") == 0 && i + 1 < argc)
            launcher_test = argv[++i];
        else if (strcmp(argv[i], "--launcher-wav") == 0 && i + 1 < argc)
            launcher_wav_dir = argv[++i];
        else if (strcmp(argv[i], "--audio-wav") == 0 && i + 1 < argc)
            g_wav.path = argv[++i];
        else if (strcmp(argv[i], "--skip-at") == 0 && i + 1 < argc) {
            for (char *p = argv[++i]; *p && skip_at_count < 8; p += *p == ',') {
                char *end;
                skip_at[skip_at_count++] = strtol(p, &end, 10);
                p = end;
            }
        } else if (strcmp(argv[i], "--menu-press") == 0 && i + 1 < argc) {
            for (char *p = argv[++i]; *p && g_menu_press_count < 8; p += *p == ',') {
                char *end;
                g_menu_press[g_menu_press_count++] = strtol(p, &end, 10);
                p = end;
            }
        }
#endif
        else
            rom_path = argv[i];
    }

#ifdef PCE_APP_YS
    if (!rom_path && !bios_path) {
        /* Player version: game folder from the settings (or chosen now) */
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        if (!config_path) {
            const char *exe_dir = SDL_GetBasePath();
            snprintf(app_config, sizeof(app_config), "%sYsRecomp.ini", exe_dir ? exe_dir : "");
            config_path = app_config;
        }
        if (app_find_game(config_path, app_cue, sizeof(app_cue), app_save, sizeof(app_save)) != 0) {
            printf("No folder chosen: quitting.\n");
            SDL_Quit();
            return 0;
        }
        rom_path = app_cue;
        if (!save_path)
            save_path = app_save;
    }
    if (launcher_wav_dir)
        return rom_path ? launcher_wav(rom_path, launcher_wav_dir) : 1;
    /* Launcher: in the player version, except in tests (button script, counted frames, turbo) */
    if (launcher_test)
        launcher_on = 1;
    else if (launcher_on < 0)
        launcher_on = rom_path == app_cue && !input_path && max_frames < 0 && !turbo && menu_test_frame < 0;
#endif
    if (input_path && input_script_load(&playback, input_path) != 0) {
        printf("Could not read the script %s\n", input_path);
        return 1;
    }
    int have_rom = load_media(&g_pce, bios_path, rom_path) == 0;
    if (have_rom) {
        pce_reset(&g_pce);
#ifdef PCE_APP_YS
        g_emu.font_check = 1;
        if (config_path) {
            int record_sessions = 0;
            app_load_settings(config_path, &vol, &skip_intro, &pause_bg, &input_map, &g_screen.video, &record_sessions);
            int dir_len = 0;
            for (int i = 0; config_path[i]; i++)
                if (config_path[i] == '/' || config_path[i] == '\\')
                    dir_len = i + 1;
            if (record_sessions && !record_dir && !input_path) {
                snprintf(app_record, sizeof(app_record), "%.*ssessions", dir_len, config_path);
                record_dir = app_record;
            }
            /* Menu font (app/menu_font.h): the copy stored next to the settings, if it already exists */
            snprintf(g_emu.font_path, sizeof(g_emu.font_path), "%.*smenu_font.bin", dir_len, config_path);
            g_emu.font_check = menu_font_load(g_emu.font_path) != 0;
        }
#endif
        if (g_pce.cd && !gg_mix) {
            /* Sound with Mednafen's balance times the player's volumes */
            audio_mix_mednafen(&g_pce.mix, &vol);
            printf("Volumes: master %d%%, CD music %d%%, chip music %d%%, effects %d%%, voices %d%%.\n",
                   vol.master, vol.music, vol.chip_music, vol.effects, vol.voices);
        } else if (g_pce.cd) {
            printf("Sound mixed as in Geargrafx (--gg-mix).\n");
        }
        if (g_pce.cd) {
            /* The game's save (CD save memory): default saves/<.cue name>.brm */
            if (!save_path) {
                const char *base = rom_path;
                for (const char *c = rom_path; *c; c++)
                    if (*c == '/' || *c == '\\')
                        base = c + 1;
                const char *dot = strrchr(base, '.');
                snprintf(default_save, sizeof(default_save), "saves/%.*s.brm",
                         (int)(dot ? dot - base : (long)strlen(base)), base);
                save_path = default_save;
            }
            save_loaded = pce_load_bram(&g_pce, save_path) == 0;
            printf(save_loaded ? "Save loaded from %s.\n" : "No save in %s: new save memory.\n", save_path);
            memcpy(initial_bram, g_pce.cd->bram, sizeof(initial_bram));
        }
#ifdef PCE_RECOMP
        if (interpret_only)
            printf("Interpreter mode (--interpret).\n");
        else if (recomp_rom_crc32 && g_pce.rom_crc32 != recomp_rom_crc32) /* 0 = CD game: the BIOS is not checked */
            printf("This ROM is not the one that was recompiled: using only the interpreter.\n");
        else {
            g_pce.recomp_lookup = recomp_lookup;
            printf("Running the recompiled code.\n");
        }
#endif
        (void)interpret_only;
        if (lockstep_on) {
            if (!g_pce.recomp_lookup) {
                printf("--lockstep compares the recompiled code with the interpreter; without recompiled code, ignored.\n");
                lockstep_on = 0;
            } else if (load_media(&g_ref, bios_path, rom_path) == 0) {
                pce_reset(&g_ref);
                g_ref.mix = g_pce.mix;
                if (g_ref.cd)
                    memcpy(g_ref.cd->bram, initial_bram, sizeof(initial_bram));
                lockstep_init(&g_lockstep, &g_pce, &g_ref);
                printf("Lockstep on: the interpreter runs alongside and checks every instruction.\n");
            } else {
                lockstep_on = 0;
            }
        }
#ifdef PCE_APP_YS
        if (!lockstep_on) { /* Ys engine read by the PC (lockstep uses the same hook) and the windows */
            static YsEngine engine;
            ys_engine_init(&engine);
            g_ys_engine = &engine;
            ys_windows_init(&g_ys_windows);
            g_ys_windows_on = 1;
            ys_scroll_init(&g_ys_scroll);
            ys_screen_init(&g_ys_screen);
            ys_intro_init(&g_ys_intro);
            g_pce.on_instruction = app_instruction_hook;
        }
#endif
        if (coverage_dir) {
            if (coverage_init(&coverage) == 0) {
                g_pce.coverage = &coverage;
                printf("Recording coverage (will be saved to %s when the window closes).\n", coverage_dir);
            } else {
                printf("No memory for the coverage: going on without recording.\n");
            }
        }
    } else
        printf("ROM not found (%s): showing test pattern.\n", rom_path ? rom_path : "none");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
#ifdef PCE_APP_YS
    /* Controllers (XInput through SDL): the ones already connected arrive as SDL_EVENT_GAMEPAD_ADDED. DirectInput
     * stays on: without it, DirectInput-only controllers (e.g. 8BitDo SNES30 over Bluetooth) vanish; its scan can hold
     * the window when Windows connects a device, but the game follows the clock (emu_thread). */
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        printf("No controllers: %s\n", SDL_GetError());
#endif

    /* Sound: 16 bits, stereo, 44100 Hz, the same as the PSG produces. Without a sound device, goes on muted. */
    SDL_AudioStream *audio = NULL;
    if (!turbo && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, PSG_SAMPLE_RATE };
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) {
            queue_silence(audio, AUDIO_RESERVE_FRAMES);
            SDL_ResumeAudioStreamDevice(audio);
        }
    }
    if (!audio)
        printf("No sound: %s\n", SDL_GetError());

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    {
        int win_w, win_h;
        video_window_size(&g_screen.video, &win_w, &win_h);
        if (g_window_w > 0 && g_window_h > 0) { /* --window-size */
            win_w = g_window_w;
            win_h = g_window_h;
        }
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (hidden_window() ? SDL_WINDOW_HIDDEN : 0);
        if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, win_w, win_h, flags, &window, &renderer)) {
            fprintf(stderr, "Failed to create the window: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }
    /* Fullscreen, V-Sync and pacing (single clock, app/pacing.h): the window is the game's clock at any refresh
     * rate, with or without V-Sync (--vrr or the option, for G-Sync/FreeSync); the small difference from the
     * console's 60.05 frames/s is absorbed by the sound (rate_ppm and the fine adjustment of up to 0.5%). */
    g_screen.window = window;
    g_screen.renderer = renderer;
    g_screen.vsync_forced_off = vrr || hidden_window();
    g_screen.have_audio = audio != NULL;
    if (vrr)
        printf("--vrr mode: V-Sync off (for G-Sync/FreeSync).\n");
    screen_apply(&g_screen, 0);
    g_gpu_time = timing.on && gpu_time_init(renderer);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); /* black right away, before loading the game */
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
#ifdef PCE_APP_YS
    if (launcher_on && have_rom && g_pce.cd) {
        int dir_len = 0;
        for (int i = 0; config_path && config_path[i]; i++)
            if (config_path[i] == '/' || config_path[i] == '\\')
                dir_len = i + 1;
        char dir[1024];
        snprintf(dir, sizeof(dir), "%.*s", dir_len, config_path ? config_path : "");
        LauncherOptions lo = { &vol, &skip_intro, &pause_bg, &menu, config_path };
        int go = run_launcher(audio, &lo, rom_path, dir, launcher_test);
        if (g_pce.cd && !gg_mix)
            audio_mix_mednafen(&g_pce.mix, &vol); /* volumes changed in the launcher's options menu */
        if (!go) {
            printf("Quitting from the launcher.\n");
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        if (audio) {
            /* The game starts with the sound queue as always: only the reserve of silence */
            SDL_ClearAudioStream(audio);
            queue_silence(audio, AUDIO_RESERVE_FRAMES);
        }
    }
    if (have_rom && g_pce.cd) {
        /* Options menu sound effects (the Ys ones, recorded by the launcher next to the settings) */
        int dir_len = 0;
        for (int i = 0; config_path && config_path[i]; i++)
            if (config_path[i] == '/' || config_path[i] == '\\')
                dir_len = i + 1;
        char sfx_path[1100];
        snprintf(sfx_path, sizeof(sfx_path), "%.*slauncher_sfx.bin", dir_len, config_path ? config_path : "");
        launcher_sound_init(&g_menu_snd, &vol);
        if (launcher_sfx_load(sfx_path, &g_menu_snd) == 0)
            printf("Options menu: Ys effects read from %s.\n", sfx_path);
    }
#endif

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             VCE_MAX_WIDTH, VCE_ACTIVE_LINES);
    if (!texture) {
        fprintf(stderr, "Failed to create the texture: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
#ifdef PCE_APP_YS
    /* Smooth motion (app/motion.h): the frame on screen is the preparation's slot ds (B, the copies of the motion,
     * the bars and the crop); redraw and equip: the ones for drawing on the CPU (the preparation's are in g_prep) */
    static Redraw redraw;
    static RedrawFrame no_frame;
    no_frame.seq = -1;
    RedrawFrame *smooth_b = &no_frame;
    PrepSlot *ds = NULL;
    static CropState no_crop; /* menu opened before the 1st frame */
    long ring_images_missed = 0; /* images with a shrunk window missing what redraw_ring found */
    SDL_Texture *smooth_tex = NULL;
    motion_init(&g_prep.motion, 1);
    static Equip equip; /* equipment on Adol */
    equip_init(&equip, g_equip_table);
    equip_init(&g_prep.equip, g_equip_table);
    static double battle_x[BATTLE_STICKERS], battle_y[BATTLE_STICKERS];
    if (have_rom && redraw_init(&redraw) == 0 && redraw_init(&g_prep.redraw) == 0) {
        smooth_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       VCE_MAX_WIDTH * REDRAW_SUB, VCE_ACTIVE_LINES * REDRAW_SUB);
        if (smooth_tex)
            SDL_SetTextureScaleMode(smooth_tex, SDL_SCALEMODE_NEAREST);
    }
    /* The new motion assembled on the GPU (app/gpu_draw.h); motion_cpu (F6) or when the GPU cannot: on the
     * CPU. With the crop, over = the frame's crop with the map transparent (the moving map comes from the GPU) */
    static GpuDraw gpu;
    int gpu_ok = smooth_tex && gpu_draw_init(&gpu, renderer), motion_cpu = start_motion_cpu;
    SDL_Texture *over_tex = gpu_ok ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                       VCE_MAX_WIDTH * REDRAW_SUB, VCE_ACTIVE_LINES * REDRAW_SUB)
                                   : NULL;
    CropView over_v = { 0 };
    int over_sub = 0; /* over_tex has frame B's crop (over_sub times larger); 0 = no crop */
    long gpu_images = 0;
    Uint64 smooth_ns = 0;
    long smooth_images = 0;
    /* Border crop (app/crop.h): the cropped image (up to 4x, with smooth motion) in its own texture */
    static uint32_t crop_pixels[VCE_MAX_WIDTH * REDRAW_SUB * VCE_ACTIVE_LINES * REDRAW_SUB];
    SDL_Texture *crop_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              VCE_MAX_WIDTH * REDRAW_SUB, VCE_ACTIVE_LINES * REDRAW_SUB);
    if (crop_tex)
        SDL_SetTextureScaleMode(crop_tex, SDL_SCALEMODE_NEAREST);
    /* Menu open with the crop on: the menu in a transparent layer on top, at the cropped image's scale */
    SDL_Texture *menu_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              VCE_MAX_WIDTH, VCE_ACTIVE_LINES);
    if (menu_tex)
        SDL_SetTextureScaleMode(menu_tex, SDL_SCALEMODE_NEAREST);
    /* Full mode: the game's panel, drawn on top in the band under the map (crop_panel) */
    static uint32_t panel_pixels[CROP_PANEL_W * CROP_PANEL_H];
    SDL_Texture *panel_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                               CROP_PANEL_W, CROP_PANEL_H);
    if (panel_tex)
        SDL_SetTextureScaleMode(panel_tex, SDL_SCALEMODE_NEAREST);
    CropView crop_v = { 0 };
    int menu_h = 0;     /* height of the menu layer (crop_screen_draw) */
    int crop_ready = 0; /* crop_tex has the current frame cropped (without smooth motion), crop_ready times larger */
    long crop_images = 0, crop_menu_images = 0;
#endif

    int running = 1;
    long frame = 0;
    Uint64 next_frame = SDL_GetTicksNS();
    Uint64 start = next_frame;
    long last_seq = -1;
    Uint64 quit_ns = 0; /* when the console finished (--frames) */
    int permits = 0; /* frames released to the console (single clock) */
    int pace_log = g_emu.pace_log = getenv("PCE_PACE_LOG") != NULL;
    Uint64 last_permit = 0;
    int titled = 0;
    SDL_Thread *thread = NULL;
    if (have_rom) {
        g_emu.audio = audio;
        g_emu.tick = SDL_CreateSemaphore(0);
        g_emu.frame_ready = SDL_CreateSemaphore(0);
        g_emu.frame_lock = SDL_CreateMutex();
        g_emu.playback = input_path ? &playback : NULL;
        g_emu.record = record_dir ? &g_rec.script : NULL;
        if (record_dir)
            recording_start(record_dir, rom_path, bios_path, g_pce.cd && save_loaded ? initial_bram : NULL,
                            sizeof(initial_bram));
        g_emu.lockstep_on = lockstep_on;
        g_emu.turbo = turbo;
        g_emu.max_frames = max_frames;
        g_emu.pause_at = -1;
        g_emu.audio_log = getenv("PCE_AUDIO_LOG") != NULL;
#ifdef PCE_APP_YS
        g_emu.pause_at = menu_test_frame;
        /* Skipping the intro: only in the player version (without --bios, which first shows the System Card screen)
         * and without a button script (--input); lockstep lacks the hooks that tell the screen (app/ys_intro.h) */
        intro_init(&g_emu.intro, !bios_path && !input_path && !lockstep_on && g_pce.cd, skip_intro);
        SDL_SetAtomicInt(&g_emu.opening, !lockstep_on && g_pce.cd);
        if (g_emu.intro.to_menu)
            printf("Straight to the menu (Skip intro option): fast-forward from frame 0.\n");
        else if (g_emu.intro.enabled)
            printf("Intro: Enter skips to the next screen.\n");
        memcpy(g_emu.skip_at, skip_at, sizeof(skip_at));
        g_emu.skip_at_count = skip_at_count;
        if (smooth_tex) {
            g_pce.vdc.line_log = &g_emu.line_log; /* the VDC logs each line (does not change the console) */
            SDL_SetAtomicInt(&g_emu.smooth_on, 1);
        }
#endif
        SDL_SetAtomicInt(&g_emu.pad, keyboard_to_pad());
        /* High priority for the process, the window and the console (SDL already puts the sound at the highest): with
         * a build or the tests using all the cores, Windows delayed them and the game stuttered (--turbo, tests
         * running in parallel: normal) */
        if (!g_emu.turbo) {
            vblank_raise_priority();
            SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
        }
        /* PCE_PROFILE=FILE: sampling profiler on the window thread (tools/trace/profiler.h) */
        const char *profile_path = getenv("PCE_PROFILE");
        if (profile_path)
            profiler_start();
#ifdef PCE_APP_YS
        if (prep_start(!prep_inline, prep_hashes) != 0) {
            fprintf(stderr, "No memory for the frame preparation.\n");
            return 1;
        }
        printf("New-frame preparation: %s.\n", prep_inline ? "on the window thread (--prep-inline)"
                                                            : "on its own thread");
        SDL_SetAtomicInt(&g_emu.dialog_want, g_screen.video.instant_dialog); /* already on the 1st frame */
#endif
        g_game_t0 = SDL_GetTicksNS();
        thread = SDL_CreateThread(emu_thread, "game", NULL);
        if (!thread) {
            fprintf(stderr, "Failed to create the game thread: %s\n", SDL_GetError());
            return 1;
        }
    }
    while (running) {
        SDL_Event ev;
        int screen_changed = 0, fullscreen_toggled = 0;
#ifdef PCE_APP_YS
        int menu_result = 0, menu_opening = 0, focus = 0; /* focus: -1 = the window lost focus, 1 = regained it */
#endif
        {
            /* Windows reports connected devices here (SDL scans the controllers again): notes if it held the window */
            Uint64 pump_start = SDL_GetTicksNS();
            SDL_PumpEvents();
            Uint64 pump_ms = (SDL_GetTicksNS() - pump_start) / 1000000;
            if (pump_ms > 50) {
                char what[32];
                snprintf(what, sizeof(what), "events%u", (unsigned)pump_ms);
                audio_note(what, g_emu.frames_done);
            }
        }
        if (mark_test_frame >= 0 && last_seq >= mark_test_frame) { /* --mark-test: a real F12, through the event queue */
            SDL_Event key = { 0 };
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.scancode = SDL_SCANCODE_F12;
            key.key.down = true;
            SDL_PushEvent(&key);
            mark_test_frame = -1;
        }
#ifdef PCE_APP_YS
        if (g_menu_press_wait || (g_menu_press_next < g_menu_press_count && !menu.open && have_rom &&
                                  g_emu.frames_done >= g_menu_press[g_menu_press_next])) {
            /* --menu-press: the Esc from the previous pass went through the queue: says what it did and closes the
             * menu, if it opened */
            if (g_menu_press_wait)
                printf("--menu-press: frame %ld, program %d: menu %s\n", g_emu.frames_done, g_ys_screen.program,
                       menu.open ? "opened" : "blocked");
            if (!g_menu_press_wait || menu.open) {
                SDL_Event key = { 0 };
                key.type = SDL_EVENT_KEY_DOWN;
                key.key.scancode = SDL_SCANCODE_ESCAPE;
                key.key.down = true;
                SDL_PushEvent(&key);
            }
            if (!g_menu_press_wait)
                g_menu_press_next++;
            g_menu_press_wait = !g_menu_press_wait;
        }
#endif
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT)
                running = 0;
            else if (ev.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || ev.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED)
                screen_changed = 1; /* another monitor, or Windows changed the refresh rate: redoes the pacing */
            else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_RETURN &&
                     (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat)
                    fullscreen_toggled = 1; /* Alt+Enter: fullscreen <-> window */
            }
#ifdef PCE_APP_YS
            else if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST || ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                focus = ev.type == SDL_EVENT_WINDOW_FOCUS_LOST ? -1 : 1;
            else if (input_pads_event(&pads, &ev))
                ;
            else if (ev.type == SDL_EVENT_KEY_DOWN && have_rom) {
                int k = input_key_to_menu(&input_map, ev.key.scancode);
                menu_pad_mode = 0;
                if (menu_waiting_key(&menu) || menu_waiting_button(&menu)) {
                    /* Waiting for the new key: Esc also cancels on the controller page */
                    if (!ev.key.repeat && (menu_waiting_key(&menu) || ev.key.scancode == SDL_SCANCODE_ESCAPE))
                        menu_result |= menu_waiting_key(&menu) ? menu_capture_key(&menu, ev.key.scancode)
                                                               : menu_key(&menu, MENU_KEY_BACK);
                } else if (ev.key.scancode == SDL_SCANCODE_ESCAPE && !menu.open && !ev.key.repeat && !MENU_BLOCKED())
                    menu_opening = 1;
                else if (ev.key.scancode == SDL_SCANCODE_F6 && !menu.open && !ev.key.repeat) {
                    /* Compares on the spot (user's choice): smooth motion assembled on the GPU <-> on the
                     * CPU */
                    motion_cpu = !motion_cpu;
                    SDL_SetWindowTitle(window, motion_cpu || !gpu_ok ? WINDOW_TITLE " - smooth motion on the CPU"
                                                                     : WINDOW_TITLE " - smooth motion on the GPU");
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !menu.open && !ev.key.repeat && g_emu.record) {
                    recording_mark(last_seq >= 0 ? last_seq : g_emu.frames_done);
                } else if (menu.open && k >= 0)
                    menu_result |= menu_key(&menu, (MenuKey)k);
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && have_rom) {
                int b = ev.gbutton.button, k = input_button_to_menu(&input_map, b);
                menu_pad_mode = 1;
                if (menu_waiting_button(&menu) || menu_waiting_key(&menu)) {
                    if (menu_waiting_button(&menu) || b == SDL_GAMEPAD_BUTTON_START)
                        menu_result |= menu_waiting_button(&menu) ? menu_capture_button(&menu, b)
                                                                  : menu_key(&menu, MENU_KEY_BACK);
                } else if (b == SDL_GAMEPAD_BUTTON_START && !menu.open) {
                    if (!MENU_BLOCKED())
                        menu_opening = 1;
                }
                else if (menu.open && k >= 0)
                    menu_result |= menu_key(&menu, (MenuKey)k);
            } else if (ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && have_rom) {
                int k = input_stick_to_menu(&pads, &ev);
                if (k >= 0)
                    menu_pad_mode = 1;
                if (menu.open && !menu.waiting && k >= 0)
                    menu_result |= menu_key(&menu, (MenuKey)k);
            }
#endif
            else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
                running = 0;
        }
#ifdef PCE_APP_YS
        if (menu_test_frame >= 0 && have_rom) {
            /* --menu-test: opens on the requested frame and presses a key every 15 display refreshes */
            static const MenuKey script[] = {
                MENU_KEY_DOWN, MENU_KEY_DOWN, MENU_KEY_DOWN, MENU_KEY_OK, /* Audio */
                MENU_KEY_LEFT, MENU_KEY_LEFT, MENU_KEY_RIGHT, /* music: 90% */
                MENU_KEY_BACK, MENU_KEY_DOWN, MENU_KEY_DOWN, MENU_KEY_DOWN, /* Quit game */
                MENU_KEY_OK, MENU_KEY_DOWN, MENU_KEY_UP, MENU_KEY_OK, /* "Are you sure?" -> No */
                MENU_KEY_DOWN, MENU_KEY_OK, /* Resume */
            };
            if (menu_test_step == 0 && g_emu.frames_done >= menu_test_frame) {
                menu_opening = 1;
                menu_test_step = 1;
                printf("--menu-test: menu opened on frame %ld.\n", g_emu.frames_done);
            } else if (menu_test_step >= 1 && menu.open && ++menu_test_refresh % 15 == 0 &&
                       menu_test_step <= (int)(sizeof(script) / sizeof(script[0]))) {
                menu_result |= menu_key(&menu, script[menu_test_step++ - 1]);
                if (menu_result & MENU_CLOSED)
                    printf("--menu-test: menu closed; music %d%%.\n", menu.vol.music);
            }
        }
        menu_result |= menu_tick(&menu, SDL_GetTicks()); /* "Keep these settings?" timed out: previous mode back */
        if (menu_opening) {
            menu_open(&menu, &vol, skip_intro, pause_bg, &input_map, &g_screen.video, &g_screen.modes);
            menu.pad_mode = menu_pad_mode;
            SDL_SetAtomicInt(&g_emu.menu_music, 1); /* only the CD music goes on */
            SDL_SetAtomicInt(&g_emu.paused, 1);
        }
        if (menu.open && menu.sfx != MENU_SFX_NONE) { /* the key's sound effect, in the menu sound */
            SDL_SetAtomicInt(&g_emu.menu_sfx, menu.sfx + 1);
            menu.sfx = MENU_SFX_NONE;
        }
        if (menu_result & MENU_CHANGED_VOLUME) {
            vol = menu.vol;
            launcher_sound_volumes(&g_menu_snd, &vol); /* menu sound effects */
            if (g_pce.cd && !gg_mix) {
                SDL_LockMutex(g_emu.frame_lock);
                audio_mix_mednafen(&g_emu.mix_pending, &vol);
                g_emu.mix_dirty = 1;
                SDL_UnlockMutex(g_emu.frame_lock);
            }
        }
        menu.pad_mode = menu_pad_mode; /* menu footer: keyboard or controller help */
        if (menu_result & MENU_CHANGED_INPUT)
            input_map = menu.input; /* takes effect now (including for navigating the menu) */
        if (menu_result & MENU_CHANGED_SETTING) {
            skip_intro = menu.skip_intro; /* takes effect the next time the game starts */
            pause_bg = menu.pause_bg;
        }
        if (menu_result & (MENU_CLOSED | MENU_QUIT)) {
            if (config_path && (memcmp(&vol, &menu_start_vol, sizeof(vol)) != 0 || skip_intro != menu_start_skip ||
                                pause_bg != menu_start_pause ||
                                memcmp(&input_map, &menu_start_input, sizeof(input_map)) != 0 ||
                                memcmp(&g_screen.video, &menu_start_video, sizeof(g_screen.video)) != 0)) {
                app_save_settings(config_path, &vol, skip_intro, pause_bg, &input_map, &g_screen.video);
                printf("Options saved to %s.\n", config_path);
            }
        }
        if (menu_opening) {
            menu_start_vol = vol;
            menu_start_skip = skip_intro;
            menu_start_pause = pause_bg;
            menu_start_input = input_map;
            menu_start_video = g_screen.video;
        }
        if (menu_result & MENU_CLOSED) {
            pad_block = keyboard_to_pad();
            last_seq = -1; /* shows the image without the menu again */
            SDL_LockMutex(g_prep.lock);
            g_prep.restart = 1; /* prepared again, with the current options */
            SDL_UnlockMutex(g_prep.lock);
            SDL_SetAtomicInt(&g_emu.paused, 0);
            SDL_SetAtomicInt(&g_emu.menu_music, 0);
        }
        if (menu_result & MENU_QUIT)
            running = 0;
        /* "Pause in background": without focus, game and sound stopped until the window comes back. Not in tests
         * (--turbo, --frames, --input, --menu-test), which run without focus.
         * With the menu open, stops the CD music that keeps playing in it. */
        if (focus < 0 && pause_bg && have_rom && !bg_paused && !turbo && max_frames < 0 && !input_path &&
            menu_test_frame < 0) {
            bg_paused = 1;
            SDL_SetAtomicInt(&g_emu.paused, 1);
            if (audio)
                SDL_PauseAudioStreamDevice(audio);
        } else if (focus > 0 && bg_paused) {
            bg_paused = 0;
            if (!menu.open) {
                pad_block = keyboard_to_pad(); /* the click/key that brought the window back does not reach the game */
                SDL_SetAtomicInt(&g_emu.paused, 0);
            }
            if (audio)
                SDL_ResumeAudioStreamDevice(audio);
        }
        if (menu_result & MENU_CHANGED_VIDEO) {
            int resize = menu.video.window_scale != g_screen.video.window_scale;
            int same = video_same_display(&menu.video, &g_screen.video);
            g_screen.video = menu.video; /* takes effect now (image options: on the next drawing) */
            if (!same)
                screen_apply(&g_screen, resize);
        }
#endif
        if (fullscreen_toggled) {
            g_screen.video.fullscreen = !g_screen.video.fullscreen;
            screen_apply(&g_screen, 0);
#ifdef PCE_APP_YS
            menu.video.fullscreen = menu.pending.fullscreen = g_screen.video.fullscreen;
            pad_block = keyboard_to_pad(); /* the Enter does not reach the game (RUN) */
            if (config_path && !menu.open)
                app_save_settings(config_path, &vol, skip_intro, pause_bg, &input_map, &g_screen.video);
#endif
        } else if (screen_changed) {
            screen_query_modes(&g_screen);
            screen_update_pacing(&g_screen, 0.0);
        }
        if (stall_ms > 0 && SDL_GetTicksNS() - start > 3000000000ull) {
            /* --stall-window: imitates the window held by Windows (test of sound without dropouts) */
            printf("Window stalled for %d ms (--stall-window).\n", stall_ms);
            if (stall_audio && audio) /* --stall-device: the sound device stops too (headphones connected) */
                SDL_PauseAudioStreamDevice(audio);
            SDL_Delay((Uint32)stall_ms);
            if (stall_audio && audio)
                SDL_ResumeAudioStreamDevice(audio);
            stall_ms = 0;
        }

        int w = 256, h = 240, divider = 4, new_frame = 0;
        double pace_f = 1.0; /* fraction between A and B of the planned image (single clock) */
        if (have_rom) {
            if (SDL_GetAtomicInt(&g_emu.quit)) {
                /* The game ended (--frames) or failed. With --frames, the window (behind the console by the clock) still
                 * shows the ready frames up to the last one, as the player would see them (--screen-dump; up to 2 s) */
                Uint64 now_ns = SDL_GetTicksNS();
                if (!quit_ns)
                    quit_ns = now_ns;
                if (g_emu.max_frames < 0 || last_seq == g_emu.frame_seq || now_ns - quit_ns > 2000000000ull)
                    running = 0;
            }
#ifdef PCE_APP_YS
            {
                uint8_t keys = keyboard_to_pad();
                pad_block &= keys;
                SDL_SetAtomicInt(&g_emu.pad, keys & ~pad_block);
            }
#else
            SDL_SetAtomicInt(&g_emu.pad, keyboard_to_pad());
#endif
            int menu_is_open = 0;
#ifdef PCE_APP_YS
            menu_is_open = menu.open;
#endif
#ifdef PCE_APP_YS
            int smoothing = smooth_tex && g_screen.video.smooth_motion;
            if (running && !menu_is_open)
                g_pace_log.motion_used |= 1 << (!g_screen.video.smooth_motion ? 0 : motion_cpu || !gpu_ok ? 3 : 2);
            SDL_SetAtomicInt(&g_emu.smooth_on, smooth_tex != NULL); /* always captures: fix_scroll */
            SDL_SetAtomicInt(&g_emu.smooth_fades, g_screen.video.smooth_fades);
            SDL_SetAtomicInt(&g_emu.dialog_want, g_screen.video.instant_dialog);
            /* The new frame is prepared on the preparation thread, except with smooth motion on the CPU (the images
             * redraw with the preparation's state) and with --prep-inline; after the menu, the window prepares the
             * console frame again (the options may have changed) */
            int prep_par = !prep_inline && !(smoothing && (motion_cpu || !gpu_ok));
            SDL_SetAtomicInt(&g_prep.parallel, prep_par);
            {
                PrepSettings st = { 0 };
                st.video = g_screen.video;
                SDL_GetCurrentRenderOutputSize(g_screen.renderer, &st.ow, &st.oh);
                st.smooth_tex = smooth_tex != NULL;
                st.gpu = gpu_ok && !motion_cpu;
                st.gpu_ok = gpu.ok;
                st.crop_tex = crop_tex != NULL;
                st.panel_tex = panel_tex != NULL;
                st.over_tex = over_tex != NULL;
                SDL_LockMutex(g_prep.slot_lock);
                g_prep.set = st;
                SDL_UnlockMutex(g_prep.slot_lock);
            }
            int use_inline = !prep_par || g_prep.restart;
#define PREP_LATEST() (use_inline ? g_emu.frame_seq : prep_ready_seq())
#else
#define PREP_LATEST() (g_emu.frame_seq)
#endif
            /* Single clock (app/pacing.h): the game's position at the time this image will appear. The frame it
             * asks for, if the console has not finished it yet, is waited for until drawing time. */
            double period = 0.0;
            Uint64 vblank = screen_vblank(&period);
            screen_check_pacing(&g_screen, period);
            g_pace_plan_ns = SDL_GetTicksNS();
            pace_plan(&g_pace, g_pace_plan_ns, g_screen.vsync ? vblank : 0, last_seq);
            if (pace_need(&g_pace) > last_seq && g_pace.target > g_pace_plan_ns) { /* --frame-timing */
                Uint64 lead = (g_pace.target - g_pace_plan_ns) / 1000000;
                g_heavy_lead[lead > 15 ? 15 : lead]++;
                g_heavy_steps[g_pace.intervals > 4 ? 4 : g_pace.intervals < 0 ? 0 : g_pace.intervals]++;
            }
            g_pace_heavy = 0;
            if (g_gpu_time && !g_emu.turbo)
                gpu_time_mark(renderer, 0);
            g_pace_armed = !g_emu.turbo; /* --turbo (tests): neither the console nor the window waits */
            int live = running && !menu_is_open && !SDL_GetAtomicInt(&g_emu.paused);
            while (live && PREP_LATEST() < pace_need(&g_pace) && !SDL_GetAtomicInt(&g_emu.quit)) {
                double deadline = (double)g_pace.present_at; /* with V-Sync too (the delivery's refresh) */
                double left = deadline - g_pace.draw_ns - (double)SDL_GetTicksNS();
                if (left <= 0.0)
                    break;
                SDL_WaitSemaphoreTimeout(g_emu.frame_ready, (Sint32)(left / 1e6) + 1);
            }
            g_pace_plan_ns = SDL_GetTicksNS(); /* drawing starts here (the wait does not count) */
#ifdef PCE_APP_YS
            /* The window touches the preparation's state: when preparing itself, or with the menu (its drawings use the
             * same crop work). Before frame_lock (the lock order, Prep) */
            int prep_locked = use_inline || menu_is_open, take_k = -1, inline_k = -1;
            if (prep_locked)
                SDL_LockMutex(g_prep.lock);
            use_inline = !prep_par || g_prep.restart;
#endif
            SDL_LockMutex(g_emu.frame_lock);
            pace_f = 1.0;
            long latest = PREP_LATEST();
            long holds = g_pace.holds, jumps = g_pace.jumps, need = pace_need(&g_pace);
            long pace_b = live ? pace_sync(&g_pace, latest, last_seq, &pace_f) : last_seq;
            if (pace_log && (g_pace.holds != holds || g_pace.jumps != jumps))
                printf("[frame %ld] clock: %s (requested %ld, ready %ld)\n", g_emu.frames_done,
                       g_pace.holds != holds ? "frame not ready" : "jump", need, latest);
            w = g_emu.frame_w;
            h = g_emu.frame_h;
            divider = g_emu.frame_divider ? g_emu.frame_divider : 4;
#ifdef PCE_APP_YS
            if (ds) { /* the frame on screen */
                w = ds->w;
                h = ds->h;
                divider = ds->divider;
            }
            if (menu.open && w > 0 && h > 0) {
                /* Menu over the image (the game is paused); redrawn every time for the blinking arrow. Underneath, the
                 * frame on screen as it was shown (with the fixes, the equipment and the bars) */
                screen_info(&g_screen, menu.video_info, sizeof(menu.video_info));
                const uint32_t *under = ds ? ds->shown : g_emu.frame;
                CropState *mc = ds ? &ds->crop : &no_crop;
                SDL_Rect r = { 0, 0, w, h };
                crop_target(mc, &g_screen, divider);
                crop_ready = !menu_tex ? 0 : crop_build(under, mc, g_screen.video.crop, g_screen.video.hud_bg, w, h, crop_tex,
                                                    crop_pixels, panel_tex, panel_pixels, &crop_v,
                                                    ds ? &ds->battle : NULL, battle_x, battle_y);
                if (crop_ready) {
                    /* Crop on: the cropped image underneath and the menu in a transparent layer on top, at its
                     * scale (taller, if the image is short) */
                    menu_h = crop_v.h < MENU_LAYER_MIN_H ? MENU_LAYER_MIN_H : crop_v.h;
                    SDL_Rect mr = { 0, 0, crop_v.w, menu_h };
                    memset(menu_pixels, 0, (size_t)crop_v.w * menu_h * sizeof(uint32_t));
                    if (!menu_hidden)
                        menu_draw(&menu, menu_pixels, crop_v.w, menu_h, crop_v.w, menu_blink_on(SDL_GetTicks()));
                    SDL_UpdateTexture(menu_tex, &mr, menu_pixels, crop_v.w * (int)sizeof(uint32_t));
                } else {
                    memcpy(menu_pixels, under, (size_t)w * h * sizeof(uint32_t));
                    crop_darken(menu_pixels, w, w, h, 1, CROP_MAP_X, CROP_MAP_Y, mc->darken_q);
                    if (!menu_hidden)
                        menu_draw(&menu, menu_pixels, w, h, w, menu_blink_on(SDL_GetTicks()));
                    SDL_UpdateTexture(texture, &r, menu_pixels, w * (int)sizeof(uint32_t));
                }
                last_seq = -1;
                g_prep.restart = 1; /* with Prep.lock (menu open) */
            } else if (pace_b != last_seq && pace_b == latest && pace_b >= 0) {
                /* New frame: the slot already prepared by the preparation thread or, when preparing here, the console
                 * frame copied now (prepared just below, without frame_lock) */
                SDL_LockMutex(g_prep.slot_lock);
                if (!g_prep.restart && g_prep.ready >= 0 && g_prep.slot[g_prep.ready]->seq == pace_b)
                    take_k = g_prep.ready;
                SDL_UnlockMutex(g_prep.slot_lock);
                if (take_k < 0 && use_inline)
                    take_k = inline_k = prep_input_locked(1);
                if (take_k >= 0) {
                    last_seq = pace_b;
                    new_frame = 1;
                    g_pace_heavy = 1;
                }
            }
#else
            if (pace_b != last_seq && pace_b == g_emu.frame_seq && w > 0 && h > 0) {
                SDL_Rect r = { 0, 0, w, h };
                last_seq = g_emu.frame_seq;
                new_frame = 1;
                g_pace_heavy = 1;
                SDL_UpdateTexture(texture, &r, g_emu.frame, w * (int)sizeof(uint32_t));
            }
#endif
            /* Releases the console for the next frame, one at a time: when the window has already taken what it made
             * (or it finished a frame without an image, as at the start) */
            int started = SDL_GetAtomicInt(&g_emu.started);
            if (have_rom && permits == started && g_emu.frame_seq == last_seq && SDL_GetSemaphoreValue(g_emu.tick) == 0 &&
                (SDL_GetAtomicInt(&g_emu.runs_done) == started || g_emu.published_run == started)) {
                permits++;
                SDL_SignalSemaphore(g_emu.tick);
                last_permit = SDL_GetTicksNS();
            } else if (pace_log && live && SDL_GetTicksNS() - last_permit > 30000000ull) {
                printf("[frame %ld] clock: console without a signal for %.0f ms (signals %d, taken %d, done %d, published %d, "
                       "frame %ld, in the window %ld, tau %.2f)\n",
                       g_emu.frames_done, (SDL_GetTicksNS() - last_permit) / 1e6, permits, started,
                       SDL_GetAtomicInt(&g_emu.runs_done), g_emu.published_run, g_emu.frame_seq, last_seq, g_pace.tau);
                last_permit = SDL_GetTicksNS();
            }
            SDL_UnlockMutex(g_emu.frame_lock);
#ifdef PCE_APP_YS
            if (inline_k >= 0) { /* prepared here (--prep-inline, F6, after the menu) */
                PrepSettings st;
                SDL_LockMutex(g_prep.slot_lock);
                st = g_prep.set;
                SDL_UnlockMutex(g_prep.slot_lock);
                prep_run(inline_k, &st);
                prep_publish(inline_k);
                g_prep.restart = 0;
                g_prep.jobs[0]++;
            }
            if (prep_locked)
                SDL_UnlockMutex(g_prep.lock);
            if (take_k >= 0) {
                /* The slot goes to the screen: only the texture upload (the rest was prepared) */
                SDL_LockMutex(g_prep.slot_lock);
                g_prep.shown = take_k;
                if (g_prep.ready == take_k)
                    g_prep.ready = -1;
                SDL_UnlockMutex(g_prep.slot_lock);
                ds = g_prep.slot[take_k];
                smooth_b = &ds->frame;
                w = ds->w;
                h = ds->h;
                divider = ds->divider;
                SDL_Rect r = { 0, 0, w, h };
                SDL_UpdateTexture(texture, &r, ds->tex, w * (int)sizeof(uint32_t));
                crop_ready = ds->crop_ready;
                crop_v = ds->crop_v;
                crop_build_upload(crop_tex, ds->crop_px, &ds->crop_v, ds->crop_ready, panel_tex, ds->panel_px, ds->panel);
                over_sub = ds->over_sub;
                over_v = ds->over_v;
                if (over_sub && over_tex) {
                    SDL_Rect orr = { 0, 0, over_v.w * over_sub, over_v.h * over_sub };
                    SDL_UpdateTexture(over_tex, &orr, ds->over_px, orr.w * (int)sizeof(uint32_t));
                }
                if (ds->gpu_on)
                    gpu_draw_upload(&gpu, &ds->gpu);
                draw_cost_add(&g_draw_cost[0], SDL_GetTicksNS() - g_pace_plan_ns);
            }
#endif
#ifdef PCE_APP_YS
            if (menu_is_open || g_emu.intro.fast)
                timing.last = 0; /* game paused or fast-forwarding: does not count */
#endif
            if (SDL_GetAtomicInt(&g_emu.diverged) && !titled) {
                SDL_SetWindowTitle(window, "pce_recomp - DIVERGENCE (see the terminal)");
                titled = 1;
            }
        } else {
            draw_test_pattern((int)frame);
            SDL_Rect r = { 0, 0, 256, 240 };
            SDL_UpdateTexture(texture, &r, g_test_pixels, 256 * (int)sizeof(uint32_t));
        }

        int drawn = 0;
#ifdef PCE_APP_YS
        g_dump.menu = menu.open;
        if (have_rom && w > 0 && h > 0 && smooth_tex && g_screen.video.smooth_motion && !menu.open &&
            !g_emu.intro.fast && ds && smooth_b->seq == last_seq && smooth_b->w == w && smooth_b->h == h) {
            /* Image between A and B: at fraction f of the time it will appear (single clock) */
            double f = pace_f;
            Uint64 t0 = SDL_GetTicksNS();
            /* in the scene the windows do not shrink (crop_ring_boxes) */
            ring_images_missed += ds->crop.grp_n && !ds->crop.scene && ds->crop.active == CROP_SCREEN_GAME &&
                                  crop_small_hud(g_screen.video.crop, ds->crop.par, ds->crop.screen) &&
                                  ds->map_skip_seq != smooth_b->seq;
            int gpu_done = 0;
            CropView v;
            if (gpu_ok && !motion_cpu) {
                /* On the GPU, at the screen's scale; with the crop, the pieces of the assembled image and the
                 * frame's crop on top; the fade without the crop is left to the CPU */
                static MotionLayout lay;
                motion_layout(&ds->motion, smooth_b, f, &lay);
                battle_place(&ds->battle, smooth_b, &lay, battle_x, battle_y);
                int kx, ky;
                if (crop_tex && crop_view_state(g_screen.video.crop, &ds->crop, w, h, &v)) {
                    present_canvas_scale(g_screen.renderer, over_v.w, over_v.h, divider, &g_screen.video, &kx, &ky);
                    int ok = over_sub && gpu_draw_render(&gpu, smooth_b, &lay, kx, ky);
                    if (ok)
                        gpu_draw_battle(&gpu, &ds->battle, battle_x, battle_y, kx, ky);
                    SDL_Texture *tex = ok ? gpu_draw_crop(&gpu, &over_v, over_tex, over_sub, kx, ky) : NULL;
                    if (tex) {
                        smooth_ns += SDL_GetTicksNS() - t0;
                        crop_screen_draw(&g_screen, tex, &over_v, kx, ky, divider, panel_tex, NULL, 0);
                        crop_images++;
                        gpu_done = 1;
                    }
                } else if (!ds->crop.darken_q) {
                    present_canvas_scale(g_screen.renderer, w, h, divider, &g_screen.video, &kx, &ky);
                    if (gpu_draw_render(&gpu, smooth_b, &lay, kx, ky)) {
                        gpu_draw_battle(&gpu, &ds->battle, battle_x, battle_y, kx, ky);
                        smooth_ns += SDL_GetTicksNS() - t0;
                        present_draw_canvas(&g_screen.present, g_screen.renderer, gpu.canvas, w, h, kx, ky, divider,
                                            &g_screen.video);
                        screen_dump(g_screen.renderer);
                        screen_present(g_screen.renderer);
                        gpu_done = 1;
                    }
                }
                gpu_images += gpu_done;
            }
            /* On the CPU (F6, or a frame the GPU cannot assemble): the drawing uses the same work as the preparation
             * (redraw.c, crop.c), which waits; the Redraw is the window's, with the frame's options */
            int cpu_locked = !gpu_done;
            if (cpu_locked)
                SDL_LockMutex(g_prep.lock);
            redraw.window_front = ds->window_front;
            redraw.equip = ds->equip_on ? &equip : NULL;
            redraw.fade = 1; /* with the colors shown */
            redraw.skip = ds->map_skip_seq == smooth_b->seq ? ds->map_skip : NULL;
            const uint32_t *img = gpu_done ? NULL : motion_render(&ds->motion, &redraw, smooth_b, f);
            redraw.skip = NULL;
            redraw.fade = 0;
            if (img && ds->battle.count) { /* on the CPU image (redraw.out) */
                static MotionLayout blay;
                motion_layout(&ds->motion, smooth_b, f, &blay);
                battle_place(&ds->battle, smooth_b, &blay, battle_x, battle_y);
                battle_draw(&ds->battle, battle_x, battle_y, redraw.out, w, h, REDRAW_SUB, redraw.out_w);
            }
            if (gpu_done) {
            } else if (crop_tex && crop_view_state(g_screen.video.crop, &ds->crop, w, h, &v)) {
                v.hud_bg = g_screen.video.hud_bg;
                v.darken = ds->crop.darken_q;
                crop_compose(img, w, REDRAW_SUB, &v, crop_pixels);
                crop_darken_view(crop_pixels, v.w * REDRAW_SUB, &v, REDRAW_SUB, ds->crop.darken_q);
                upload_padded(crop_tex, crop_pixels, v.w * REDRAW_SUB, v.h * REDRAW_SUB, VCE_MAX_WIDTH * REDRAW_SUB,
                              VCE_ACTIVE_LINES * REDRAW_SUB);
                smooth_ns += SDL_GetTicksNS() - t0;
                if (cpu_locked)
                    SDL_UnlockMutex(g_prep.lock);
                crop_screen_draw(&g_screen, crop_tex, &v, REDRAW_SUB, 0, divider, panel_tex, NULL, 0);
                crop_images++;
            } else {
                SDL_Rect r = { 0, 0, redraw.out_w, redraw.out_h };
                if (ds->crop.darken_q) { /* the game's fade also on the border and the panel (app/crop.h) */
                    memcpy(crop_pixels, img, (size_t)redraw.out_w * redraw.out_h * sizeof(uint32_t));
                    crop_darken(crop_pixels, redraw.out_w, w, h, REDRAW_SUB, CROP_MAP_X, CROP_MAP_Y, ds->crop.darken_q);
                    img = crop_pixels;
                }
                SDL_UpdateTexture(smooth_tex, &r, img, redraw.out_w * (int)sizeof(uint32_t));
                smooth_ns += SDL_GetTicksNS() - t0;
                if (cpu_locked)
                    SDL_UnlockMutex(g_prep.lock);
                screen_draw(&g_screen, smooth_tex, w, h, REDRAW_SUB, divider);
            }
            smooth_images++;
            drawn = 1;
        }
#endif
        if (w > 0 && h > 0) {
#ifdef PCE_APP_YS
            if (!drawn && crop_ready && g_screen.video.crop != CROP_OFF) {
                crop_screen_draw(&g_screen, crop_tex, &crop_v, crop_ready, 0, divider, panel_tex, menu.open ? menu_tex : NULL,
                                 menu_h);
                crop_menu_images += menu.open;
                crop_images++;
                drawn = 1;
            }
#endif
            if (!drawn)
                screen_draw(&g_screen, texture, w, h, 1, divider);
            timing.presents++;
            if (timing.on && new_frame)
                frame_timing_add(&timing, SDL_GetTicksNS(), g_screen.refreshes_per_frame);
        } else {
            /* No game image yet (e.g. the "straight to the menu" fast-forward from frame 0): black screen.
             * Without any Present, Windows showed the empty window, with its frame, until the 1st frame. */
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            screen_present(renderer);
        }
        g_pace_armed = 0;

        frame++;
        if (!have_rom) {
            if (max_frames >= 0 && frame >= max_frames)
                running = 0;
            Uint64 now = SDL_GetTicksNS();
            next_frame += FRAME_NS;
            if (next_frame > now)
                SDL_DelayNS(next_frame - now);
            else if (now - next_frame > 100000000ull)
                next_frame = now;
        }
    }
    screen_dump_flush();
    if (thread) {
        SDL_SetAtomicInt(&g_emu.quit, 1);
        SDL_SignalSemaphore(g_emu.tick);
        if (getenv("PCE_PROFILE"))
            profiler_stop(getenv("PCE_PROFILE"));
        SDL_WaitThread(thread, NULL);
#ifdef PCE_APP_YS
        prep_stop(); /* uses frame_lock */
#endif
        SDL_DestroySemaphore(g_emu.tick);
        SDL_DestroySemaphore(g_emu.frame_ready);
        SDL_DestroyMutex(g_emu.frame_lock);
        frames_done = g_emu.frames_done;
#ifdef PCE_APP_YS
        printf("New-frame preparation: %ld on its own thread, %ld on the window's.\n", g_prep.jobs[1], g_prep.jobs[0]);
        if (timing.on) {
            /* [0] and [2] on the window; the others in the preparation (on its thread, or the window's with
             * --prep-inline/F6) */
            static const char *names[14] = { "new frame on the window (texture upload)", "prep: motion and GPU",
                                             "whole image", "prep: copy of the console frame",
                                             "prep: game fixes", "prep: crop, fade, texture",
                                             "prep: crop_build", "  rest", "    game windows (background)",
                                             "    crop_frame", "    redraw_ring", "    fade, rewards, copies",
                                             "    texture (copy)", "whole prep" };
            for (int i = 0; i < 14; i++)
                if (g_draw_cost[i].n)
                    printf("Drawing, %s: %ld, average %.2f ms, max %.2f ms, %ld above 3 ms\n", names[i], g_draw_cost[i].n,
                           g_draw_cost[i].sum / 1e6 / g_draw_cost[i].n, g_draw_cost[i].max / 1e6, g_draw_cost[i].over);
            printf("Drawing with the new frame, by ms:");
            for (int i = 0; i < 16; i++)
                if (g_heavy_ms[i])
                    printf(" %s%d: %ld", i == 15 ? ">=" : "", i, g_heavy_ms[i]);
            printf("\n  in the plan, ms until the refresh:");
            for (int i = 0; i < 16; i++)
                if (g_heavy_lead[i])
                    printf(" %s%d: %ld", i == 15 ? ">=" : "", i, g_heavy_lead[i]);
            printf("; refreshes since the previous one:");
            for (int i = 0; i < 5; i++)
                if (g_heavy_steps[i])
                    printf(" %s%d: %ld", i == 4 ? ">=" : "", i, g_heavy_steps[i]);
            printf("\nDelivery (SDL_RenderPresent), ms:");
            for (int i = 0; i < 16; i++)
                if (g_present_ms[i])
                    printf(" %s%d: %ld", i == 15 ? ">=" : "", i, g_present_ms[i]);
            printf("\n");
            gpu_time_report();
        }
        if (smooth_images)
            printf("Smooth motion: %ld images, %.2f ms each; %ld of %ld frames redrawn identical.\n",
                   smooth_images, smooth_ns / 1e6 / smooth_images, g_prep.redraw.exact_frames, g_prep.redraw.frames);
        if (smooth_images)
            printf("GPU: %s; %ld images assembled on it (%ld frames not assembled).\n",
                   gpu_ok ? "assembles the image" : "without its own blending (CPU)", gpu_images, gpu.unready);
        if (g_screen.video.crop != CROP_OFF)
            printf("Border crop: %ld of %ld frames with the border; %ld cropped images (%ld with the menu on "
                   "top); late scroll switch fixed in %ld.\n", g_prep.crop.framed, g_prep.crop.frames, crop_images,
                   crop_menu_images, g_prep.late_splits);
        printf("Images with a shrunk window missing its outside sprites: %ld; torn frames inside the game's "
               "windows: %ld (%ld fixed).\n", ring_images_missed, g_emu.torn_win_frames, g_prep.torn_win_fixed);
        if (g_prep.rhythm.to > 0)
            motion_rhythm_print(&g_prep.rhythm);
#endif
        diverged = SDL_GetAtomicInt(&g_emu.diverged);
    }
    frame_timing_report(&timing, g_screen.hz, g_screen.refreshes_per_frame);
    if (audio && have_rom)
        printf("Sound: the queue ran dry %d time(s) and overflowed %d time(s); the console waited for the queue %d time(s).\n",
               g_emu.audio_underruns, g_emu.audio_overflows, g_emu.audio_holds);

    printf("Window closed after %ld frames.\n", have_rom ? frames_done : frame);
    if (g_game_t0) /* without the program's startup (real-time tests) */
        printf("Game time: %.1f s (from the 1st frame to the end).\n", (SDL_GetTicksNS() - g_game_t0) / 1e9);
#ifdef PCE_APP_YS
    if (g_wav.path && g_wav.n > 0 && launcher_write_wav(g_wav.path, g_wav.pcm, (int)g_wav.n) == 0)
        printf("--audio-wav: %ld samples; menu open at samples %ld to %ld.\n", g_wav.n, g_wav.menu_from,
               g_wav.menu_to);
#endif
    if (have_rom && max_frames >= 0) {
        /* For tests: the same number of frames with the same buttons must give the same state */
        Uint32 crc = SDL_crc32(0, g_pce.ram, sizeof(g_pce.ram));
        crc = SDL_crc32(crc, g_pce.vdc.vram, sizeof(g_pce.vdc.vram));
        crc = SDL_crc32(crc, g_pce.vce.color, sizeof(g_pce.vce.color));
        if (g_pce.cd)
            crc = SDL_crc32(crc, g_pce.cd->ram, sizeof(g_pce.cd->ram));
        uint8_t regs[7] = { g_pce.cpu.a, g_pce.cpu.x, g_pce.cpu.y, g_pce.cpu.s, g_pce.cpu.p,
                            (uint8_t)g_pce.cpu.pc, (uint8_t)(g_pce.cpu.pc >> 8) };
        crc = SDL_crc32(crc, regs, sizeof(regs));
        printf("Console state: %08X\n", (unsigned)crc);
    }
    if (g_pce.recomp_lookup)
        printf("Recompiled code: %llu function entries; instructions that fell back to the interpreter: %llu.\n",
               (unsigned long long)g_pce.recomp_calls, (unsigned long long)g_pce.interpreted_steps);
    if (lockstep_on && !diverged)
        printf("Lockstep OK: %llu frames and %llu instructions identical to the interpreter.\n",
               (unsigned long long)g_lockstep.frames, (unsigned long long)g_lockstep.instructions);
    if (lockstep_on)
        pce_free(&g_ref);
    if (audio)
        SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(texture);
    present_free(&g_screen.present);
    gpu_time_quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
#ifdef PCE_APP_YS
    input_pads_close(&pads);
#endif
    SDL_Quit();
    if (have_rom && g_pce.coverage) {
        char path[1024];
        char stamp[32];
        time_t now = time(NULL);
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
        snprintf(path, sizeof(path), "%s/session_%s.txt", coverage_dir, stamp);
        SDL_CreateDirectory(coverage_dir);
        if (coverage_write(&coverage, path, g_pce.rom_crc32) == 0)
            printf("Coverage saved to %s: %zu distinct instructions, %zu distinct jumps/calls.\n",
                   path, coverage_exec_count(&coverage), coverage.edge_count);
        else
            printf("Could not write the coverage to %s\n", path);
        coverage_free(&coverage);
    }
    if (have_rom && g_pce.cd && !input_path) {
        /* Stores the game's save (the CD save memory) for next time. When replaying a session
         * (--input) it is not written, so as not to ruin the player's save. */
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", save_path);
        char *slash = strrchr(dir, '/');
        char *bslash = strrchr(dir, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
        if (slash) {
            *slash = 0;
            SDL_CreateDirectory(dir);
        }
        if (pce_save_bram(&g_pce, save_path) == 0)
            printf("Save written to %s.\n", save_path);
        else
            printf("Could not write the save to %s\n", save_path);
    }
    if (have_rom && record_dir) {
        if (recording_save(frames_done) == 0)
            printf("Session recorded to %s (%ld frames, %d button changes).\n", g_rec.path, frames_done,
                   g_rec.script.count);
        else
            printf("Could not write the session to %s\n", g_rec.path);
        input_script_free(&g_rec.script);
    }
    pce_free(&g_pce);
    return 0;
}
