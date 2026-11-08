/*
 * Ys extension for PCERecomp's pce_trace (PCERecomp/tools/trace/pce_trace.h): the checks and research of the
 * PC version (smooth motion, crop, windows, fades, equipment, dialog...). Built twice by
 * CMake: pce_trace (interpreter) and ys_trace (with the game's generated C, PCE_RECOMP). Options: YS_HELP below
 * (pce_trace --help shows the generic ones and the Ys ones).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "input_script.h"
#include "pce_trace.h"
#include "util/u8file.h"
#include "cd/cdrom.h"
#include "pce/mix.h"
#include "pce/pce.h"
#include "app/crop.h"
#include "app/menu_font.h"
#include "app/menu_chip.h"
#include "app/video.h"
#include "app/gpu_draw.h"
#include "app/motion.h"
#include "app/game_fixes.h"
#include "app/redraw.h"
#include "app/battle.h"
#include "ys_actors.h"
#include "hud_old.h"
#include "app/ys_windows.h"
#include "app/ys_fade.h"
#include "app/ys_reward.h"
#include "app/ys_scroll.h"
#include "app/ys_music.h"
#include "app/ys_dialog.h"
#include "app/ys_screen.h"

static const char YS_HELP[] =
    "  (all with --frame)\n"
    "  --smooth-check         smooth motion (app/motion.h, reading the Ys engine) in every frame: the redraw\n"
    "                         (app/redraw.h) equal to the real image, 2 images per frame (120 Hz screen), the\n"
    "                         late frames swapped, the border/panel and the light out of place\n"
    "  --smooth-frames FROM COUNT FOLDER  same, with 4 images per frame (240 Hz), and saves those of frames\n"
    "                         FROM..FROM+COUNT-1 (4x larger) in FOLDER/qNNNNNN_F.ppm (F = 0..3 quarters; FOLDER \"-\" =\n"
    "                         do not save); counts the odd points (PCE_ODD_LOG=1) and the camera and hero jerks\n"
    "                         (PCE_JERK_LOG=1; PCE_MOTION_LOG=1: the camera of each image). Without --smooth-check,\n"
    "                         smooth motion is only computed from 30 frames before FROM\n"
    "  --gpu                  (with --smooth-*; hidden window) also builds each image on the GPU\n"
    "                         (app/gpu_draw.h) and checks it against the CPU one; measures the time at 2560x1440\n"
    "                         (PCE_GPU_LOG=1 lists the different ones)\n"
    "  --equip                (ys_trace only) the equipment on Adol (app/equip.h) as in the window with\n"
    "                         visual_equipment=1 (PCE_EQUIP_LOG=1 lists the frames with the console's Adol)\n"
    "  --equip-force E,S,A    (with --equip) draws this sword, shield and armor (255 = none) instead of the RAM ones\n"
    "  --battle, --battle-small  enemy life bars and rewards (app/battle.h, app/ys_reward.h)\n"
    "  --game-fixes           the fixes the window always makes: item window in front of the face, frames\n"
    "                         torn inside the windows and, with --volumes, the --audio without the old CD\n"
    "                         track; lists and counts each one (PCE_TORN_LOG=1, PCE_TORN_SHOT=FOLDER)\n"
    "  --walk-rhythm FROM TO  (with --smooth-*) Adol's rhythm on screen in these frames (MotionRhythm)\n"
    "  --windows <file|->     game windows (app/ys_windows.h) per frame and their sprites; \"-\" = summary\n"
    "  --scroll-log <file|->  mid-image scroll changes read from the game (app/ys_scroll.h) and the fix of\n"
    "                         the late ones (fix_scroll); at the end, the summary\n"
    "  --actors <file|->      Ys characters (ys_actors.h, docs/ys_notes.md): camera, position, drawing, bits and\n"
    "                         sprites; at the end, the summary; \"-\" = summary only\n"
    "  --fades                fades read from the game (app/ys_fade.h): level and brightening of each step, colors and\n"
    "                         calls (PCE_FADES_DETAIL=1: the levels of each palette)\n"
    "  --crop-log             when the border crop (app/crop.h) turns on and off, windows, scenes, HUD from the game\n"
    "                         (PCE_CROP_DIFF=1)\n"
    "  --crop-shot MODE BACKGROUND FILE  the last frame as the window would show it with the crop (MODE: frame,\n"
    "                         compact, wide, fill or full; BACKGROUND: one of the HUD backgrounds, e.g. black)\n"
    "  --panel-log            the HUD panel read from the game (app/ys_panel.h), when it changes\n"
    "  --equip-log            Adol and the equipment colors from the game against the old way (implies\n"
    "                         --game-fixes; PCE_EQUIP_SAT_LOG=1)\n"
    "  --instant-dialog       instant dialog from reset (app/ys_dialog.h; a session turns it on with \"# dialog\")\n"
    "  --dialog-log           the text stretches the game writes slowly\n"
    "  Sessions with \"# menu\" repeat the chip music of the options menu (app/menu_chip.h).\n";

static TraceRun *g_tr; /* the run (ys_setup) */
static int g_actors_on;        /* --actors: Ys characters (ys_actors.h) */
static YsWindows g_windows;     /* --windows: game windows (app/ys_windows.h) */
static YsWindows *g_windows_on;
static int g_windows_quiet;     /* turned on only by --crop-log: no summary at the end */
static YsScroll g_scroll;        /* game scroll changes (app/ys_scroll.h): with --frame */
static YsScrollFrame g_scroll_frame; /* those of the frame that ended */
static YsScreen g_screen = { -1, 0 }; /* the game program running (app/ys_screen.h) */
/* --crop-log: the game screen against the border shape in the image (crop_detect, the old way) */
static long g_scr_frames[3], g_scr_shape, g_scr_black, g_scr_noshape, g_scr_noshape_first = -1, g_scr_extra,
    g_scr_extra_first = -1, g_scr_white, g_scr_black_lit, g_scr_black_lit_first = -1;
/* --crop-log: the HUD from the panel cells (app/ys_panel.h) against the old way (from the colors, hud_old.c) */
static long g_hud_frames, g_hud_same, g_hud_invalid, g_hud_items, g_hud_bars, g_hud_save, g_hud_first = -1,
    g_hud_no_old, g_hud_empty, g_hud_dark, g_hud_late;

static void hud_compare(const CropState *c, const Pce *pce, long frame)
{
    CropHudLayout a, b;
    g_hud_frames++;
    if (!c->panel.valid) {
        g_hud_invalid++;
        return;
    }
    crop_hud_layout(&c->panel, &a);
    hud_old_layout(crop_unwhiten_panel(pce->vce.frame, pce->vce.frame_width, c->whiten), pce->vce.frame_width, &b);
    int items = a.n != b.n, bars = a.bars != b.bars, save = a.save != b.save;
    for (int k = 0; k < a.n && k < b.n; k++)
        items |= a.l0[k] != b.l0[k] || a.l1[k] != b.l1[k] || a.v0[k] != b.v0[k] || a.v1[k] != b.v1[k];
    for (int k = 0; k < a.bars && k < b.bars; k++)
        bars |= a.bar_y[k] != b.bar_y[k] || a.bar_x0[k] != b.bar_x0[k] || a.bar_x1[k] != b.bar_x1[k] ||
                a.bar_len[k] != b.bar_len[k];
    if (!items && !bars && !save) {
        g_hud_same++;
        g_hud_empty += a.n == 0;
        return;
    }
    if (b.n == 0 && b.bars == 0) /* the old one found nothing (screen blanked, flash): the new one finds it by the cells */
        g_hud_no_old++;
    else if (c->darken > 0) /* darkened by the game fade: white stops being white (the old one misses the bars) */
        g_hud_dark++;
    else if (g_scroll_frame.valid && g_scroll_frame.row_low != YS_SCROLL_LOW_ROW - 1)
        g_hud_late++; /* late scroll change: the panel moves down in the console image (the window fixes it, fix_scroll) */
    else {
        g_hud_items += items;
        g_hud_bars += bars;
        g_hud_save += save;
        if (g_hud_first < 0)
            g_hud_first = frame;
    }
    if (getenv("PCE_HUD_DIFF")) {
        printf("panel: frame %ld darkened %d new", frame, c->darken);
        printf(" %d labels, %d bars, save %d; old %d, %d, %d;", a.n, a.bars, a.save,
               b.n, b.bars, b.save);
        for (int k = 0; k < a.n; k++)
            printf(" n%d:%d-%d/%d-%d", k, a.l0[k], a.l1[k], a.v0[k], a.v1[k]);
        for (int k = 0; k < b.n; k++)
            printf(" a%d:%d-%d/%d-%d", k, b.l0[k], b.l1[k], b.v0[k], b.v1[k]);
        for (int k = 0; k < a.bars; k++)
            printf(" nb%d:%d,%d-%d,%d", k, a.bar_y[k], a.bar_x0[k], a.bar_x1[k], a.bar_len[k]);
        for (int k = 0; k < b.bars; k++)
            printf(" ab%d:%d,%d-%d,%d", k, b.bar_y[k], b.bar_x0[k], b.bar_x1[k], b.bar_len[k]);
        printf("\n");
    }
}

static YsFade g_fade;               /* game fades (app/ys_fade.h): with --frame */
static YsFadeFrame g_fade_frame;    /* those of the frame that ended */
static int g_fades_log;             /* --fades */
static FILE *g_scroll_out;       /* --scroll-log */
static int g_scroll_out_on;
static FILE *g_windows_out;
static YsWinFrame g_win_snap;   /* --windows: the windows of the last frame (ys_windows_snapshot) */
static long g_win_frames, g_win_spr, g_win_in, g_win_out, g_win_out_frames, g_win_out_first = -1, g_win_alone,
    g_win_alone_frames, g_win_alone_first = -1, g_win_states[4], g_win_scene,
    g_st50_frames, g_st50_alone, g_st50_alone_first = -1;
static long g_layout_count, g_layout_list[256]; /* frames where the game switched layout (YsWinFrame.layout_switch) */

/* --windows: the windows and their sprites at the end of frame `frame` (engine list eng) */
static void windows_frame(Pce *pce, long frame, const YsFrameList *eng)
{
    YsWindows *w = &g_windows;
    ys_windows_frame(w, pce, frame);
    ys_windows_snapshot(w, eng, &g_win_snap);
    if (g_win_snap.layout_switch && g_layout_count++ < 256)
        g_layout_list[g_layout_count - 1] = frame;
    uint8_t mine[YS_ACTORS] = { 0 };
    int ns = eng->kind != YS_LIST_NONE ? ys_window_sprites(&eng->list, mine) : 0;
    /* the shown list was built in the previous frame: its windows count too */
    static int prev_rects[YS_WIN_MAX][4], prev_nr, prev_scene;
    if (!w->count && !ns && w->last_ok) {
        prev_nr = prev_scene = 0;
        return;
    }
    int rects[2 * YS_WIN_MAX][4], nr = 0, out = 0, alone = 0, scene = 0;
    memcpy(rects, prev_rects, sizeof(prev_rects));
    nr = prev_nr;
    if (w->count)
        g_win_frames++;
    if (g_windows_out)
        fprintf(g_windows_out, "q%ld 3E=%d%s", frame, pce->ram[0x3E], w->last_ok ? "" : " DIFFERENT");
    for (int k = 0; k < w->count; k++) {
        const YsWindow *win = &w->win[k];
        int r[4], vis = eng->kind != YS_LIST_NONE && ys_window_rect(win, eng->list.cam_x, eng->list.cam_y, r);
        g_win_states[win->state]++;
        scene |= !win->page;
        if (vis)
            memcpy(rects[nr++], r, sizeof(r));
        if (g_windows_out) {
            fprintf(g_windows_out, " | J%d %d,%d %dx%d p%02X %s l%d-%d", k, win->col, win->row, win->w, win->h,
                    win->page, win->state == YS_WIN_OPEN ? "open" : win->state == YS_WIN_OPENING ? "opening" : "closing",
                    win->vis0, win->vis1);
            if (vis)
                fprintf(g_windows_out, " %d,%d-%d,%d", r[0], r[1], r[2], r[3]);
        }
    }
    for (int i = 0; i < YS_ACTORS; i++) {
        if (!mine[i])
            continue;
        int b[4], in = 0;
        ys_sprite_box(&eng->list, i, b);
        for (int k = 0; k < nr && !in; k++)
            in = b[0] >= rects[k][0] && b[1] >= rects[k][1] && b[2] <= rects[k][2] && b[3] <= rects[k][3];
        g_win_spr++;
        const char *what = "inside";
        if (in)
            g_win_in++;
        else if (scene || prev_scene) {
            g_win_scene++;
            what = "outside (scene)";
        } else if (!w->count && !prev_nr) {
            alone++;
            g_win_alone++;
            what = "no window";
        } else {
            out++;
            g_win_out++;
            what = "outside";
        }
        if (g_windows_out)
            fprintf(g_windows_out, " | s%d a%d %d,%d-%d,%d %s", i, eng->list.owner[i], b[0], b[1], b[2], b[3], what);
    }
    /* characters of state $50 (the area program rebuilds them when opening/closing windows: ys_list_window_actor) */
    if (eng->kind != YS_LIST_NONE) {
        int st50 = 0;
        for (int a = 0; a < YS_ACTORS; a++) {
            if (!ys_list_window_actor(&eng->list, a))
                continue;
            st50++;
            if (g_windows_out)
                fprintf(g_windows_out, " | e50 a%d f%02X p%02X %d,%d", a, eng->list.flags[a], eng->list.pose[a],
                        eng->list.ax[a], eng->list.ay[a]);
        }
        if (st50) {
            g_st50_frames++;
            /* the shown list may be the previous frame's (like the window sprites, above) */
            if (!w->count && !prev_nr && g_st50_alone++ == 0)
                g_st50_alone_first = frame;
        }
    }
    prev_nr = 0;
    for (int k = 0; k < w->count; k++)
        if (eng->kind != YS_LIST_NONE && ys_window_rect(&w->win[k], eng->list.cam_x, eng->list.cam_y, prev_rects[prev_nr]))
            prev_nr++;
    prev_scene = scene;
    if (out && g_win_out_frames++ == 0)
        g_win_out_first = frame;
    if (alone && g_win_alone_frames++ == 0)
        g_win_alone_first = frame;
    if (g_windows_out)
        fprintf(g_windows_out, "\n");
}
static YsActors g_actors;

/* --smooth-check: checks the smooth motion redraw (app/redraw.h) in every frame; --smooth-frames FROM
 * COUNT FOLDER: also saves the in-between images (f = 0, 0.25, 0.5, 0.75) of those frames */
static int g_smooth_on, g_smooth_all; /* g_smooth_all: --smooth-check (otherwise, only near --smooth-frames) */
static long g_smooth_from, g_smooth_count;
static const char *g_smooth_dir;
static Redraw g_smooth;
static int g_fixes_on;         /* --game-fixes */
static MotionRhythm g_rhythm;  /* --walk-rhythm */
/* --equip */
static int g_equip_on, g_equip_force[3] = { -1, -1, -1 };
static Equip g_equip;
static long g_equip_frames, g_equip_hero, g_equip_fixed, g_equip_unknown, g_equip_unknown_first = -1;
static int g_battle_on;        /* --battle: enemy rewards and life bars (app/battle.h) */
static Battle g_battle;
static long g_battle_window, g_battle_bar_frames;
static YsReward g_reward;          /* rewards on kills, from the hooks (app/ys_reward.h) */
static YsRewardFrame g_reward_frame; /* those of the frame that ended */
static long g_cd_pop_count, g_cd_pop_list[64];
static int g_cd_pop_prev;
static VdcLineLog *g_line_log; /* TraceRun.line_log (ys_setup) */
static RedrawFrame g_smooth_frames[2];
static long g_smooth_bad_list[64];
static int g_smooth_bad_count;
static long g_smooth_big_ok, g_smooth_big_checked;
static clock_t g_smooth_clock; /* time of the 4x drawings */
static long g_smooth_late, g_smooth_late_first = -1; /* frames with the late scroll change fixed */
static long g_smooth_late_end, g_smooth_late_end_first = -1; /* same, at the bottom of the map */
/* --smooth-frames: images with color points that neither A nor B has nearby, with more than SMOOTH_ODD_BIG
 * such subpoints (something moving fast gives few; a misplaced piece, many) */
#define SMOOTH_ODD_BIG 1000
/* Light out of place: more than SMOOTH_LIGHT_MAX lit points where A and B are dark, in one image */
#define SMOOTH_LIGHT_MAX 8
/* --game-fixes: item window in front of the face (fix_window) */
static long g_window_frames, g_window_first = -1;
static RedrawFrame g_window_frame; /* without smooth motion */
static const RedrawFrame *g_window_last; /* the last frame, with the window in front (--crop-shot) */

static void window_fix(RedrawFrame *b, long frame)
{
    if (fix_window(&g_smooth, b)) {
        g_window_frames++;
        if (g_window_first < 0)
            g_window_first = frame;
    }
    g_window_last = b;
}

/* Frame torn inside the game windows (fix_torn_window, as in the window): counts, checks that no tear is left inside
 * them and, with PCE_TORN_SHOT=FOLDER, saves the fixed image (t<frame>.ppm) */
static long g_torn_win_count, g_torn_win_fixed, g_torn_win_left, g_torn_win_list[4096];
static void torn_window(Pce *pce, long frame)
{
    static RedrawFrame tw;
    VdcLineLog *log = pce->vdc.line_log;
    int last_row = log->last_row; /* redraw_capture restarts the log: the frame is still captured afterwards */
    redraw_capture(&tw, log, &pce->vce, frame);
    log->last_row = last_row;
    tw.wins = g_win_snap;
    tw.torn_win = 1;
    if (g_torn_win_count++ < 4096)
        g_torn_win_list[g_torn_win_count - 1] = frame;
    int changed = fix_torn_window(&g_smooth, &tw), in = 0;
    g_torn_win_fixed += changed > 0;
    fix_torn(&tw, &in);
    if (in > 0) {
        g_torn_win_left++;
        if (getenv("PCE_TORN_LOG"))
            printf("torn %ld: %d points inside the windows after the fix\n", frame, in);
    }
    const char *dir = getenv("PCE_TORN_SHOT");
    if (dir && changed > 0) {
        fix_window(&g_smooth, &tw); /* as in the window: the item window in front of the sprites comes afterwards */
        char p[1024];
        snprintf(p, sizeof(p), "%s/t%06ld.ppm", dir, frame);
        trace_save_ppm_argb(tw.pixels, tw.w, tw.h, p);
    }
}

/* Smooth motion (app/motion.h) in --smooth-check/--smooth-frames */
static Motion g_motion;
static YsEngine g_engine;
static YsFrameList g_eng_frame; /* engine list of the last frame */
static long g_mo_frame_bad, g_mo_frame_bad_first = -1, g_mo_odd, g_mo_odd_big, g_mo_odd_big_first = -1;
static long g_mo_jerks, g_mo_jerk_first = -1, g_mo_hero_jerks, g_mo_hero_first = -1, g_mo_light, g_mo_light_first = -1;
static long g_mo_edge_checked, g_mo_edge_bad, g_mo_edge_first = -1; /* map edges at f = 0 */
/* Masks by the game's rule (app/ys_masks.h) in the images between A and B */
static long g_rule_frames, g_rule_blocks, g_rule_images, g_rule_pixels, g_rule_checked, g_rule_bad, g_rule_bad_first = -1;
static long g_ring_map[3], g_ring_map_first = -1; /* the map around the windows: matches, does not match, none */
static long g_ring_remade, g_ring_remade_frames, g_ring_no_list, g_ring_hidden; /* redraw_ring, masks remade without the window */
static long g_ring_hole_sprites, g_ring_hole_frames; /* redraw_ring, sprites of the window characters outside */
static double g_rule_out = -1e9;   /* how far a hidden character went out of the rule masks' area */
static long g_rule_out_frame = -1;
static double g_game_out[YS_ACTORS]; /* in B, the same with the game masks (1e9 = the game hides nothing) */

/* How far the sprites of character c (at positions sx, sy) go out of the box (x0, y0)-(x1, y1); -1e9 without sprites */
static double box_out(const RedrawFrame *b, int c, const double *sx, const double *sy, double x0, double y0, double x1,
                      double y1)
{
    double out = -1e9;
    for (int i = 0; i < b->eng.list.count && i < YS_ACTORS; i++) {
        if (b->eng.list.owner[i] != c)
            continue;
        const uint16_t *e = &b->sat[i * 4];
        double w = (e[3] & 0x100) ? 32 : 16, h = (e[3] & 0x3000) == 0 ? 16 : (e[3] & 0x3000) == 0x1000 ? 32 : 64;
        out = fmax(out, fmax(fmax(x0 - sx[i], sx[i] + w - x1), fmax(y0 - sy[i], sy[i] + h - y1)));
    }
    return out;
}

/* Jerk: walking without stopping (10 frames or more without 2 in a row standing still), the step shown from one image
 * to the next (|x| + |y|: turning at the same speed is not a jerk) changes by more than 0.05 point */
typedef struct JerkMeter {
    double last_x, last_y, step;
    long last_n, last_frame;
    int raw_x, raw_y, still, calm;
} JerkMeter;

static int jerk_add(JerkMeter *j, long frame, int q, long n, int raw_x, int raw_y, int max_d, double x, double y)
{
    int jerk = 0;
    if (q == 0) {
        int dx = raw_x - j->raw_x, dy = raw_y - j->raw_y;
        j->still = dx || dy ? 0 : j->still + 1;
        j->calm = frame == j->last_frame + 1 && j->still < 2 && abs(dx) <= max_d && abs(dy) <= max_d ? j->calm + 1 : 0;
        j->raw_x = raw_x;
        j->raw_y = raw_y;
        j->last_frame = frame;
    }
    double sx = x - j->last_x, sy = y - j->last_y;
    if (n == j->last_n + 1 && fabs(sx) <= 4 && fabs(sy) <= 4) {
        double step = fabs(sx) + fabs(sy);
        jerk = j->calm >= 10 && fabs(step - j->step) > 0.05;
        if (jerk && getenv("PCE_JERK_LOG"))
            printf("jerk at frame %ld (image %d): step %.2f -> %.2f (%.2f %.2f)\n", frame, q, j->step, step, sx, sy);
        j->step = step;
    } else {
        j->step = 0;
    }
    j->last_x = x;
    j->last_y = y;
    j->last_n = n;
    return jerk;
}

/* --gpu: the image built on the GPU */
static int g_gpu_on;
static GpuDraw g_gpu, g_gpu_t; /* g_gpu_t: only to measure the time, always at the same scale */
static SDL_Texture *g_gpu_screen; /* 2560x1440, to measure the time */
static long g_gpu_frames, g_gpu_frames_ok, g_gpu_images, g_gpu_images_ok, g_gpu_pixels_bad, g_gpu_unready;
static long g_gpu_timed;
static Uint64 g_gpu_ns;

/* Points (without alpha) of g_gpu.canvas different from img (w x h) */
static long gpu_compare(const uint32_t *img, int w, int h)
{
    SDL_SetRenderTarget(g_gpu.r, g_gpu.canvas);
    SDL_Surface *s = SDL_RenderReadPixels(g_gpu.r, NULL);
    SDL_SetRenderTarget(g_gpu.r, NULL);
    SDL_Surface *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (s)
        SDL_DestroySurface(s);
    if (!c || c->w != w || c->h != h) {
        if (c)
            SDL_DestroySurface(c);
        return (long)w * h;
    }
    long bad = 0;
    int x0 = w, x1 = -1, y0 = h, y1 = -1;
    for (int y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)c->pixels + (size_t)y * c->pitch);
        for (int x = 0; x < w; x++)
            if ((row[x] ^ img[y * w + x]) & 0xFFFFFF) {
                bad++;
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
    }
    if (bad && getenv("PCE_GPU_LOG"))
        printf("gpu: different points at x %d-%d, y %d-%d (of %d x %d)\n", x0, x1, y0, y1, w, h);
    SDL_DestroySurface(c);
    return bad;
}

/* On the GPU, at fraction f, with k points per point; compares with img (NULL: only builds) */
static long gpu_image(const RedrawFrame *b, double f, int k, const uint32_t *img)
{
    static MotionLayout lay;
    motion_layout(&g_motion, b, f, &lay);
    if (!gpu_draw_render(&g_gpu, b, &lay, k, k)) {
        g_gpu_unready++;
        if (getenv("PCE_GPU_LOG")) {
            int x0 = -9999, ra = -9999, diff = 0;
            for (int r = 0; r < b->h; r++)
                if (b->rows[r].valid) {
                    if (x0 == -9999) {
                        x0 = b->rows[r].x0;
                        ra = b->rows[r].raster - r;
                    } else if (b->rows[r].x0 != x0 || b->rows[r].raster - r != ra) {
                        printf("gpu: not built at frame %ld: line %d with x0 %d, count %d (before %d, %d)\n",
                               b->seq, r, b->rows[r].x0, b->rows[r].raster - r, x0, ra);
                        diff = 1;
                        break;
                    }
                }
            if (!diff)
                printf("gpu: not built at frame %ld (%d x %d, %d pieces, %d stretches)\n", b->seq, b->w, b->h,
                       g_gpu.f.npieces, g_gpu.f.nregions);
        }
        return -1;
    }
    return img ? gpu_compare(img, b->w * k, b->h * k) : 0;
}

/* Time of one image as in the window at 2560x1440: built with 6 points per point and scaled down to the screen, until
 * the GPU finishes (reads 1 point) */
static void gpu_time(const RedrawFrame *b, double f)
{
    if (!g_gpu_screen) {
        g_gpu_screen = SDL_CreateTexture(g_gpu.r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 2560, 1440);
        if (!g_gpu_screen)
            return;
    }
    static MotionLayout lay;
    motion_layout(&g_motion, b, f, &lay);
    Uint64 t0 = SDL_GetTicksNS();
    if (!gpu_draw_render(&g_gpu_t, b, &lay, 6, 6))
        return;
    SDL_SetRenderTarget(g_gpu.r, g_gpu_screen);
    SDL_SetTextureScaleMode(g_gpu_t.canvas, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(g_gpu_t.canvas, SDL_BLENDMODE_NONE);
    SDL_FRect d = { 240, 0, 2080, 1440 };
    SDL_RenderTexture(g_gpu.r, g_gpu_t.canvas, NULL, &d);
    SDL_Rect one = { 1000, 700, 1, 1 };
    SDL_Surface *s = SDL_RenderReadPixels(g_gpu.r, &one);
    SDL_SetRenderTarget(g_gpu.r, NULL);
    if (s)
        SDL_DestroySurface(s);
    Uint64 dt = SDL_GetTicksNS() - t0;
    if (getenv("PCE_GPU_TIME_LOG") && dt > 3000000)
        printf("gpu: image of frame %ld took %.2f ms\n", b->seq, dt / 1e6);
    g_gpu_ns += dt;
    g_gpu_timed++;
}

static void gpu_log(const char *what, long frame, int q, long bad)
{
    static int shown;
    if (bad && (getenv("PCE_GPU_LOG") || shown < 3)) {
        printf("gpu: %s different at frame %ld (image %d): %ld points\n", what, frame, q, bad);
        shown++;
    }
}

static void motion_frame_done(Pce *pce, long frame)
{
    RedrawFrame *b = &g_smooth_frames[frame & 1], *a = &g_smooth_frames[(frame + 1) & 1];
    redraw_capture(b, g_line_log, &pce->vce, frame);
    b->eng = g_eng_frame;
    b->wins = g_win_snap; /* the game windows (window in front, bars) */
    b->scroll = g_scroll_frame;
    equip_read(pce, &b->equip);
    equip_colors(g_equip_table, &g_fade_frame, &b->equip_colors);
    if (g_equip_force[0] >= 0 && b->equip.game != EQUIP_GAME_NONE) {
        b->equip.sword = (uint8_t)g_equip_force[0];
        b->equip.shield = (uint8_t)g_equip_force[1];
        b->equip.armor = (uint8_t)g_equip_force[2];
    }
    b->rewards = g_reward_frame;
    if (g_battle_on) {
        long made = g_battle.popups_made;
        battle_frame(&g_battle, b);
        g_battle_window += g_battle.window;
        int bars = 0;
        for (int k = 0; k < g_battle.count; k++)
            bars += g_battle.st[k].kind == BATTLE_BAR;
        g_battle_bar_frames += bars > 0;
        for (int k = 0; k < BATTLE_POPUPS && made != g_battle.popups_made; k++) {
            const BattlePopup *p = &g_battle.pop[k];
            if (p->on && p->age == 0)
                printf("reward at frame %ld: +%d EXP +%d GOLD at %ld,%ld\n", frame, p->exp, p->gold,
                       p->wx - g_battle.cam_x, p->wy - g_battle.cam_y);
        }
    }
    g_smooth.equip = NULL; /* as in the window: the late one and the scrolls without the equipment */
    static CropState crop; /* as the window (main.c) */
    int late_map, late_low; /* late scroll changes, from the game */
    if (fix_scroll(&g_smooth, b, &late_map, &late_low) > 0) {
        if (late_map && g_smooth_late++ == 0)
            g_smooth_late_first = frame;
        if (late_low && g_smooth_late_end++ == 0)
            g_smooth_late_end_first = frame;
    }
    motion_fix_late(&g_motion, &g_smooth, b); /* after the scroll changes (as the window, main.c) */
    if (g_fixes_on)
        window_fix(b, frame);
    /* Adol's sprite is the one of the character the game created as the hero, in the list that matches the frame's table */
    int adol_spr = -1;
    const YsList *hero_list = ys_frame_shown_list(&b->eng);
    for (int i = 0; hero_list && hero_list->hero != YS_NO_ACTOR && i < 64 && adol_spr < 0; i++)
        if (hero_list->owner[i] == hero_list->hero && (b->sat[i * 4] & 0x3FF))
            adol_spr = i;
    if (g_equip_on && crop.active != CROP_SCREEN_GAME && adol_spr >= 0 && getenv("PCE_EQUIP_LOG"))
        printf("equipment: Adol recognized outside the map at frame %ld (crop %d)\n", frame, crop.active);
    if (g_equip_on && crop.active == CROP_SCREEN_GAME) {
        g_smooth.equip = &g_equip;
        long shown = g_equip.shown;
        g_equip_frames++;
        int equip_changed = fix_equip(&g_smooth, b);
        g_equip_fixed += equip_changed > 0;
        if (adol_spr >= 0 && g_equip.shown != shown && !equip_changed && getenv("PCE_EQUIP_LOG"))
            printf("equipment: Adol swapped without changing any point at frame %ld (drawing %03X, y %d x %d)\n", frame,
                   (b->sat[adol_spr * 4 + 2] >> 1) & 0x3FF, b->sat[adol_spr * 4], b->sat[adol_spr * 4 + 1]);
        if (adol_spr >= 0 && b->equip.game != EQUIP_GAME_NONE) {
            g_equip_hero++;
            if (g_equip.shown == shown && g_equip_unknown++ == 0)
                g_equip_unknown_first = frame;
        }
        if (adol_spr >= 0 && g_equip.shown == shown && getenv("PCE_EQUIP_LOG")) /* Adol as the console draws him */
            printf("equipment: Adol not swapped at frame %ld (game %d, pieces %d,%d,%d, drawing %03X)\n", frame,
                   b->equip.game, b->equip.sword, b->equip.shield, b->equip.armor, (b->sat[adol_spr * 4 + 2] >> 1) & 0x3FF);
    }
    crop_set_fade(&crop, g_fade_frame.map_level, g_fade_frame.map_add, g_fade_frame.table_ok ? g_fade_frame.table : NULL);
    crop_set_fade_stray(&crop, g_fade_frame.map_stray);
    crop_set_screen(&crop, ys_screen_layout(g_screen.program));
    crop_frame(&crop, b->pixels, b->w, b->h);
    redraw_check(&g_smooth, b);
    if (b->exact != 1 && b->h > 0 && g_smooth_bad_count < 64)
        g_smooth_bad_list[g_smooth_bad_count++] = frame;
    int prev_ok = a->seq == frame - 1 && a->w == b->w && a->h == b->h;
    motion_step(&g_motion, prev_ok ? a : NULL, b);
    motion_rhythm_add(&g_rhythm, &g_motion, frame);
    g_rule_frames += g_motion.ym.nwalkers > 0;
    g_rule_blocks += g_motion.ym.nblocks;
    for (int w = 0; w < g_motion.ym.nwalkers; w++) { /* in B, how far the character goes out of the game masks */
        const YsMaskWalker *wk = &g_motion.ym.walker[w];
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, sx[YS_ACTORS], sy[YS_ACTORS];
        for (int i = 0; i < YS_ACTORS; i++) {
            sx[i] = (b->sat[i * 4 + 1] & 0x3FF) - 0x20;
            sy[i] = (b->sat[i * 4] & 0x3FF) - 64;
            if (wk->skip[i]) {
                x0 = fmin(x0, sx[i]);
                y0 = fmin(y0, sy[i]);
                x1 = fmax(x1, sx[i] + 16);
                y1 = fmax(y1, sy[i] + 32);
            }
        }
        g_game_out[wk->actor] = x0 > x1 ? 1e9 : box_out(b, wk->actor, sx, sy, x0, y0, x1, y1);
    }
    if (g_gpu_on) {
        gpu_draw_frame(&g_gpu, &g_motion, &g_smooth, b, g_smooth.window_front);
        gpu_draw_frame(&g_gpu_t, &g_motion, &g_smooth, b, g_smooth.window_front);
    }
    /* Without constant speed, at f = 1 the 4x image is the real one enlarged */
    if (b->exact == 1 && (frame % 10 == 0 || getenv("PCE_F1_ALL"))) { /* PCE_F1_ALL: in every frame */
        g_motion.constant_speed = 0;
        clock_t t0 = clock();
        const uint32_t *img = motion_render(&g_motion, &g_smooth, b, 1.0);
        g_smooth_clock += clock() - t0;
        if (g_gpu_on) { /* on the GPU, 1 point per point: the frame */
            long bad = gpu_image(b, 1.0, 1, b->pixels);
            g_gpu_frames++;
            g_gpu_frames_ok += bad == 0;
            gpu_log("image at f = 1", frame, 0, bad);
            /* PCE_GPU_K=K: the frame also with K points per point, compared with the enlarged frame */
            int gk = getenv("PCE_GPU_K") ? atoi(getenv("PCE_GPU_K")) : 0;
            if (gk > 1) {
                static uint32_t big[VCE_MAX_WIDTH * VCE_ACTIVE_LINES * 64];
                if ((size_t)b->w * b->h * gk * gk <= sizeof(big) / sizeof(big[0])) {
                    for (int y = 0; y < b->h * gk; y++)
                        for (int x = 0; x < b->w * gk; x++)
                            big[(size_t)y * b->w * gk + x] = b->pixels[(y / gk) * b->w + x / gk];
                    long bk = gpu_image(b, 1.0, gk, big);
                    if (bk)
                        printf("gpu: frame %ld with %d points per point different from the frame in %ld points\n", frame, gk,
                               bk);
                }
            }
        }
        g_motion.constant_speed = 1;
        int ok = 1;
        for (int y = 0; y < g_smooth.out_h && ok; y++)
            for (int x = 0; x < g_smooth.out_w; x++)
                if (img[y * g_smooth.out_w + x] != b->pixels[(y >> REDRAW_SUB_SHIFT) * b->w + (x >> REDRAW_SUB_SHIFT)]) {
                    ok = 0;
                    break;
                }
        g_smooth_big_checked++;
        g_smooth_big_ok += ok;
        if (!ok && g_smooth_big_ok + 3 > g_smooth_big_checked)
            printf("smooth motion: 4x image different at frame %ld\n", frame);
    }
    /* Map edges: without constant speed, at f = 0 the map is at A's camera; in the first and last 6
     * columns of the map, the points that are not from a sprite must be A's (the strip that B already swapped for the
     * border comes from A's map) */
    if (b->exact == 1 && prev_ok && g_motion.engine && g_motion.lerp && crop.active == CROP_SCREEN_GAME &&
        g_motion.pose[0].raw_x != g_motion.pose[1].raw_x) {
        g_motion.constant_speed = 0;
        const uint32_t *img = motion_render(&g_motion, &g_smooth, b, 0.0);
        g_motion.constant_speed = 1;
        long bad = 0;
        for (int y = 0; y < g_smooth.out_h; y++) {
            int r = y >> REDRAW_SUB_SHIFT;
            if (!g_motion.map_row[r])
                continue;
            for (int x = CROP_MAP_X * REDRAW_SUB; x < (CROP_MAP_X + CROP_MAP_W) * REDRAW_SUB; x++) {
                int c = x >> REDRAW_SUB_SHIFT;
                if (c == CROP_MAP_X + 6)
                    x = (CROP_MAP_X + CROP_MAP_W - 6) * REDRAW_SUB;
                c = x >> REDRAW_SUB_SHIFT;
                bad += !g_smooth.from_sprite[y * g_smooth.out_w + x] && img[y * g_smooth.out_w + x] != a->pixels[r * a->w + c];
            }
        }
        g_mo_edge_checked++;
        if (bad > 16 * REDRAW_SUB * REDRAW_SUB) {
            if (g_mo_edge_bad++ == 0)
                g_mo_edge_first = frame;
            if (getenv("PCE_EDGE_LOG"))
                printf("smooth motion: map edge different from A at frame %ld (%ld 4x points)\n", frame, bad);
        }
    }
    int steps = g_smooth_dir ? 4 : 2; /* as on a 120 Hz screen, or 240 Hz with --smooth-frames */
    if (getenv("PCE_SMOOTH_STEPS")) /* more images per frame (e.g. the fractions the window shows) */
        steps = atoi(getenv("PCE_SMOOTH_STEPS"));
    static uint8_t dark[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    int dark_ok = b->exact == 1 && prev_ok;
    /* dark = A and B black in the 5x5 square around (in two passes, row and column, same result) */
    static uint8_t dark_row[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    for (int y = 0; dark_ok && y < b->h; y++) {
        int run = 0; /* points black in A and B in a row up to x */
        for (int x = 0; x < b->w; x++) {
            int i = y * b->w + x;
            run = !(a->pixels[i] & 0xFFFFFF) && !(b->pixels[i] & 0xFFFFFF) ? run + 1 : 0;
            dark_row[i] = 0;
            if (x >= 4)
                dark_row[i - 2] = run >= 5; /* x - 4 .. x black: center at x - 2 */
        }
    }
    for (int y = 0; dark_ok && y < b->h; y++)
        for (int x = 0; x < b->w; x++) {
            int d = x >= CROP_MAP_X && x < CROP_MAP_X + CROP_MAP_W && y >= CROP_MAP_Y && y < CROP_MAP_Y + CROP_MAP_H &&
                    x >= 2 && x < b->w - 2 && y >= 2 && y < b->h - 2;
            for (int dy = -2; dy <= 2 && d; dy++)
                d = dark_row[(y + dy) * b->w + x];
            dark[y * b->w + x] = (uint8_t)d;
        }
    /* the GPU time before the images: it builds f = 0.5 and would change the shown camera the calculations below read */
    if (g_gpu_on)
        gpu_time(b, 0.5);
    for (int q = 0; q < steps; q++) {
        /* Masks by the rule: how many points the image changes with them (against the game ones only) */
        static uint32_t *no_rule;
        size_t img_size = (size_t)VCE_MAX_WIDTH * VCE_ACTIVE_LINES * REDRAW_SUB * REDRAW_SUB * sizeof(uint32_t);
        int no_behind = getenv("PCE_NO_BEHIND") != NULL; /* the before: only the game masks */
        if (q > 0 && b->exact == 1 && g_motion.ym.nblocks > 0 && !no_behind) {
            if (!no_rule)
                no_rule = malloc(img_size);
            g_motion.behind_off = 1;
            memcpy(no_rule, motion_render(&g_motion, &g_smooth, b, (double)q / steps),
                   (size_t)g_smooth.out_w * g_smooth.out_h * sizeof(uint32_t));
            g_motion.behind_off = 0;
            const uint32_t *o = motion_render(&g_motion, &g_smooth, b, (double)q / steps);
            long changed = 0;
            for (int i = 0; i < g_smooth.out_w * g_smooth.out_h; i++)
                changed += o[i] != no_rule[i];
            g_rule_images += changed > 0;
            g_rule_pixels += changed;
            if (changed && getenv("PCE_BEHIND_LOG"))
                printf("smooth motion: rule masks changed %ld 4x points at frame %ld (image %d)\n", changed,
                       frame, q);
        }
        g_motion.behind_off = no_behind;
        const uint32_t *img = motion_render(&g_motion, &g_smooth, b, (double)q / steps);
        /* the camera of this image (the mask calculation below also builds f = 1 and would change it; they gave false
         * jerks 0.8 -> 0 in the last image of each frame near houses) */
        double shown_x = g_motion.shown_cam_x, shown_y = g_motion.shown_cam_y;
        double spr_x[YS_ACTORS], spr_y[YS_ACTORS];
        memcpy(spr_x, g_motion.spr_x, sizeof(spr_x));
        memcpy(spr_y, g_motion.spr_y, sizeof(spr_y));
        /* Masks by the rule: does the hidden character go out of their area (tied to the map) more than out of the game
         * ones in B? (PCE_BEHIND_LOG lists). At the fractions q/steps and, at the last one, also at f = 1 (with constant
         * speed the character is not at the game position even there) */
        for (int qq = q; qq <= (q == steps - 1 ? steps : q) && g_motion.ym.nwalkers > 0 && b->exact == 1 && !no_behind;
             qq++) {
            static MotionLayout lay;
            if (qq == 0)
                continue;
            motion_layout(&g_motion, b, (double)qq / steps, &lay);
            for (int w = 0; w < g_motion.ym.nwalkers; w++) {
                const YsMaskWalker *wk = &g_motion.ym.walker[w];
                double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                for (int k = 0; k < g_motion.ym.nblocks; k++) {
                    const RedrawMask *mk = &g_motion.ym.block[k];
                    if (g_motion.ym.bw[k] != w || !lay.behind_on[k])
                        continue;
                    x0 = fmin(x0, mk->x - lay.map_ox);
                    y0 = fmin(y0, mk->y - lay.map_oy);
                    x1 = fmax(x1, mk->x + mk->w - lay.map_ox);
                    y1 = fmax(y1, mk->y + mk->h - lay.map_oy);
                }
                if (x0 > x1)
                    continue; /* at the game position: the game masks */
                double out = box_out(b, wk->actor, g_motion.spr_x, g_motion.spr_y, x0, y0, x1, y1);
                double game = g_game_out[wk->actor];
                g_rule_checked++;
                if (out > g_rule_out) {
                    g_rule_out = out;
                    g_rule_out_frame = frame;
                }
                if (out > fmax(game, 0.0) + 1e-6) {
                    if (g_rule_bad++ == 0)
                        g_rule_bad_first = frame;
                    if (getenv("PCE_BEHIND_LOG"))
                        printf("rule masks: a%d goes %.2f points out of the area at frame %ld (image %d; in the game %.2f): "
                               "area %.2f,%.2f-%.2f,%.2f\n", wk->actor, out, frame, qq, game, x0, y0, x1, y1);
                }
            }
        }
        if (g_gpu_on) { /* on the GPU, 4 points per point: the CPU one */
            long bad = gpu_image(b, (double)q / steps, REDRAW_SUB, img);
            g_gpu_images++;
            g_gpu_images_ok += bad == 0;
            g_gpu_pixels_bad += bad > 0 ? bad : 0;
            gpu_log("4x image", frame, q, bad);
            /* PCE_GPU_K=K: also saves the GPU image with K points per point (the window scale) in the frames of
             * --smooth-frames, in FOLDER/gNNNNNN_F.ppm */
            int gk = getenv("PCE_GPU_K") ? atoi(getenv("PCE_GPU_K")) : 0;
            if (gk > 0 && g_smooth_dir && strcmp(g_smooth_dir, "-") != 0 && frame >= g_smooth_from &&
                frame < g_smooth_from + g_smooth_count && gpu_image(b, (double)q / steps, gk, NULL) == 0) {
                SDL_SetRenderTarget(g_gpu.r, g_gpu.canvas);
                SDL_Surface *s = SDL_RenderReadPixels(g_gpu.r, NULL);
                SDL_SetRenderTarget(g_gpu.r, NULL);
                SDL_Surface *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888) : NULL;
                if (c) {
                    char p[1024];
                    snprintf(p, sizeof(p), "%s/g%06ld_%d.ppm", g_smooth_dir, frame, q);
                    static uint32_t gimg[VCE_MAX_WIDTH * VCE_ACTIVE_LINES * 64];
                    for (int y = 0; y < c->h && (size_t)(y + 1) * c->w <= sizeof(gimg) / sizeof(gimg[0]); y++)
                        memcpy(gimg + (size_t)y * c->w, (const uint8_t *)c->pixels + (size_t)y * c->pitch,
                               (size_t)c->w * sizeof(uint32_t));
                    trace_save_ppm_argb(gimg, c->w, c->h, p);
                    SDL_DestroySurface(c);
                }
                if (s)
                    SDL_DestroySurface(s);
            }
        }
        if (b->exact != 1)
            continue;
        /* The map comes out shifted from B by the shown camera (up to one step + the constant speed lag, ~4
         * points): what belongs to B's map is also looked for at that place */
        int mx = 0, my = 0;
        if (g_motion.engine) {
            mx = (int)lround(g_motion.shown_cam_x - g_motion.pose[1].raw_x);
            my = (int)lround(g_motion.shown_cam_y - g_motion.pose[1].raw_y);
        }
        /* Light out of place: map lit where A and B are dark in a 5x5 square (the mine circle), and B not
         * lit at the place of the shown map */
        if (dark_ok) {
            /* per dark cell (1x) and then its 4x points: same result, without scanning the whole 4x image */
            long lit = 0;
            int ch = (g_smooth.out_h + REDRAW_SUB - 1) >> REDRAW_SUB_SHIFT;
            int cw = (g_smooth.out_w + REDRAW_SUB - 1) >> REDRAW_SUB_SHIFT;
            for (int cy = 0; cy < ch; cy++)
                for (int cx = 0; cx < cw; cx++) {
                    if (!dark[cy * b->w + cx])
                        continue;
                    long n = 0; /* lit 4x points of the cell that are not from a sprite (a lit sprite in the dark is its own) */
                    for (int y = cy << REDRAW_SUB_SHIFT; y < ((cy + 1) << REDRAW_SUB_SHIFT) && y < g_smooth.out_h; y++)
                        for (int x = cx << REDRAW_SUB_SHIFT; x < ((cx + 1) << REDRAW_SUB_SHIFT) && x < g_smooth.out_w; x++)
                            n += (img[y * g_smooth.out_w + x] & 0xFFFFFF) && !g_smooth.from_sprite[y * g_smooth.out_w + x];
                    if (!n)
                        continue;
                    int lit_b = 0;
                    for (int dy = -2; dy <= 2 && !lit_b; dy++)
                        for (int dx = -2; dx <= 2 && !lit_b; dx++) {
                            int px = cx + mx + dx, py = cy + my + dy;
                            lit_b = px >= 0 && py >= 0 && px < b->w && py < b->h && (b->pixels[py * b->w + px] & 0xFFFFFF);
                        }
                    if (!lit_b)
                        lit += n;
                }
            if (lit > SMOOTH_LIGHT_MAX * REDRAW_SUB * REDRAW_SUB) {
                g_mo_light++;
                if (g_mo_light_first < 0)
                    g_mo_light_first = frame;
            }
        }
        /* On the game screen, the border and the panel (outside the map lines) are the frame's at any fraction (except
         * sprite points: a sprite walking near the map edge passes over it, as on the console) */
        if (crop.active == CROP_SCREEN_GAME) {
            int bad_row = -1;
            for (int y = 0; y < g_smooth.out_h && bad_row < 0; y++) {
                int r = y >> REDRAW_SUB_SHIFT;
                if ((r >= CROP_MAP_Y && r < CROP_MAP_Y + CROP_MAP_H) || r < CROP_MAP_Y - 16 ||
                    r >= CROP_MAP_Y - 16 + CROP_FRAME_VIEW_H)
                    continue;
                for (int x = 0; x < g_smooth.out_w; x++)
                    if (img[y * g_smooth.out_w + x] != b->pixels[r * b->w + (x >> REDRAW_SUB_SHIFT)] &&
                        !g_smooth.from_sprite[y * g_smooth.out_w + x]) {
                        bad_row = y;
                        break;
                    }
            }
            if (bad_row >= 0 && ++g_mo_frame_bad <= 12)
                printf("smooth motion: border/panel different at frame %ld (image %d, 4x line %d)\n", frame, q,
                       bad_row);
            if (bad_row >= 0 && g_mo_frame_bad_first < 0)
                g_mo_frame_bad_first = frame;
        }
        if (!g_smooth_dir || frame < g_smooth_from || frame >= g_smooth_from + g_smooth_count)
            continue;
        /* Odd points: of a color that neither A nor B has in a 5x5 square around */
        if (q > 0 && prev_ok) {
            long odd = 0;
            int ox0 = 1 << 30, oy0 = 1 << 30, ox1 = -1, oy1 = -1; /* box of the odd points (PCE_ODD_LOG) */
            for (int y = 0; y < g_smooth.out_h; y++)
                for (int x = 0; x < g_smooth.out_w; x++) {
                    uint32_t c = img[y * g_smooth.out_w + x];
                    int cx = x >> REDRAW_SUB_SHIFT, cy = y >> REDRAW_SUB_SHIFT, found = 0;
                    for (int dy = -2; dy <= 2 && !found; dy++)
                        for (int dx = -2; dx <= 2 && !found; dx++) {
                            int px = cx + dx, py = cy + dy;
                            if (px < 0 || py < 0 || px >= b->w || py >= b->h)
                                continue;
                            found = a->pixels[py * b->w + px] == c || b->pixels[py * b->w + px] == c;
                            px += mx;
                            py += my;
                            found |= px >= 0 && py >= 0 && px < b->w && py < b->h && b->pixels[py * b->w + px] == c;
                        }
                    odd += !found;
                    if (!found) {
                        ox0 = x < ox0 ? x : ox0;
                        ox1 = x > ox1 ? x : ox1;
                        oy0 = y < oy0 ? y : oy0;
                        oy1 = y > oy1 ? y : oy1;
                    }
                }
            if (odd > SMOOTH_ODD_BIG && g_mo_odd_big++ == 0)
                g_mo_odd_big_first = frame;
            if (odd && (++g_mo_odd <= 12 || getenv("PCE_ODD_LOG")))
                printf("smooth motion: %ld odd points at frame %ld (image %d; 4x at x %d-%d, y %d-%d)\n", odd,
                       frame, q, ox0, ox1, oy0, oy1);
        }
        if (g_battle_on && g_battle.count) { /* the bars and the rewards, as in the window */
            static uint32_t bimg[VCE_MAX_WIDTH * REDRAW_SUB * VCE_ACTIVE_LINES * REDRAW_SUB];
            static MotionLayout blay;
            double bx[BATTLE_STICKERS], by[BATTLE_STICKERS];
            memcpy(bimg, img, (size_t)g_smooth.out_w * g_smooth.out_h * sizeof(uint32_t));
            motion_layout(&g_motion, b, (double)q / steps, &blay);
            battle_place(&g_battle, b, &blay, bx, by);
            battle_draw(&g_battle, bx, by, bimg, b->w, b->h, REDRAW_SUB, g_smooth.out_w);
            img = bimg;
        }
        char p[1024];
        snprintf(p, sizeof(p), "%s/q%06ld_%d.ppm", g_smooth_dir, frame, q);
        if (strcmp(g_smooth_dir, "-") != 0)
            trace_save_ppm_argb(img, g_smooth.out_w, g_smooth.out_h, p);
        long n = frame * steps + q;
        /* Shown camera (in the engine) */
        static JerkMeter cam = { .last_n = -2, .last_frame = -2 }, hero = { .last_n = -2, .last_frame = -2 };
        if (g_motion.engine) {
            const MotionPose *pb = &g_motion.pose[1];
            if (getenv("PCE_MOTION_LOG"))
                printf("new q%ld_%d: camera %.2f %.2f (game %d %d) follows %d%s\n", frame, q, shown_x, shown_y, pb->raw_x,
                       pb->raw_y, g_motion.follow, b->eng.kind == YS_LIST_LATE ? " late" : "");
            if (jerk_add(&cam, frame, q, n, pb->raw_x, pb->raw_y, MOTION_JUMP, shown_x, shown_y) &&
                g_mo_jerks++ == 0)
                g_mo_jerk_first = frame;
        } else {
            cam.last_n = -2;
        }
        /* Hero walking on screen: the 1st sprite of palette 13 in front of the background (Adol) */
        int h = -1;
        for (int i = 0; i < 64 && h < 0; i++)
            if ((b->sat[i * 4 + 3] & 0x8F) == 0x8D)
                h = i;
        if (h < 0) {
            hero.last_n = -2;
            continue;
        }
        if (jerk_add(&hero, frame, q, n, b->sat[h * 4 + 1] & 0x3FF, b->sat[h * 4] & 0x3FF, 4, spr_x[h], spr_y[h]) &&
            g_mo_hero_jerks++ == 0)
            g_mo_hero_first = frame;
    }
}

static void motion_summary(void)
{
    printf("smooth motion: %ld of %ld frames redrawn equal; %ld with the engine list (%ld interpolated); "
           "%ld late frames swapped for the list the console did not show; 4x image equal in %ld of %ld",
           g_smooth.exact_frames, g_smooth.frames, g_motion.engine_frames, g_motion.lerp_frames, g_motion.late_fixed,
           g_smooth_big_ok, g_smooth_big_checked);
    if (g_smooth_bad_count) {
        printf("; different:");
        for (int k = 0; k < g_smooth_bad_count; k++)
            printf(" %ld", g_smooth_bad_list[k]);
    }
    printf("\n");
    printf("smooth motion: late scroll change fixed in %ld frames (1st: %ld), at the bottom of the map in %ld (1st: "
           "%ld)\n", g_smooth_late, g_smooth_late_first, g_smooth_late_end, g_smooth_late_end_first);
    printf("smooth motion: %ld images with the border/panel different from the frame (1st at frame %ld)\n", g_mo_frame_bad,
           g_mo_frame_bad_first);
    printf("smooth motion: %ld images with light out of place (1st at frame %ld)\n", g_mo_light, g_mo_light_first);
    printf("smooth motion: rule masks (characters behind walls) in %ld frames (%ld blocks: %ld with the game's points "
           "in B, %ld in A, %ld by the rule, used in %ld images; %ld hidden walkers, %ld blocks without a slot, %ld images without the "
           "block); changed %ld 4x points in %ld images; the character stays %.2f points from their edge (frame %ld); %ld "
           "of %ld images going out more than the game's (1st at frame %ld)\n", g_rule_frames, g_rule_blocks,
           g_motion.ym.from_b, g_motion.ym.from_a, g_motion.ym.by_rule, g_motion.ym.rule_images, g_motion.ym.walkers, g_motion.ym.no_slot,
           g_motion.mask_missing, g_rule_pixels, g_rule_images, -g_rule_out, g_rule_out_frame, g_rule_bad,
           g_rule_checked, g_rule_bad_first);
    if (g_rule_bad || g_motion.mask_missing)
        printf("rule masks: FAILED\n");
    printf("smooth motion: mine light (position copied by the game) in %ld copies\n", g_engine.copies);
    printf("smooth motion: map edge at f = 0 different from A in %ld of %ld frames with the camera moving sideways "
           "(1st at frame %ld)\n", g_mo_edge_bad, g_mo_edge_checked, g_mo_edge_first);
    if (g_smooth_dir) {
        printf("smooth motion: %ld images with odd points in frames %ld-%ld; %ld with more than %d (1st at frame "
               "%ld)\n", g_mo_odd, g_smooth_from, g_smooth_from + g_smooth_count - 1, g_mo_odd_big, SMOOTH_ODD_BIG,
               g_mo_odd_big_first);
        printf("smooth motion: %ld camera jerks while moving (1st at frame %ld); %ld jerks of the hero walking on screen "
               "(1st at frame %ld)\n", g_mo_jerks, g_mo_jerk_first, g_mo_hero_jerks, g_mo_hero_first);
    }
    if (g_gpu_on) {
        printf("GPU (%s): image at f = 1 equal to the frame in %ld of %ld; 4x image equal to the CPU one "
               "in %ld of %ld (%ld different points); %ld not built; time per image at 2560x1440: %.2f ms\n",
               SDL_GetRendererName(g_gpu.r), g_gpu_frames_ok, g_gpu_frames, g_gpu_images_ok, g_gpu_images,
               g_gpu_pixels_bad, g_gpu_unready, g_gpu_timed ? (double)g_gpu_ns / g_gpu_timed / 1e6 : 0.0);
        /* Not built (mode change mid-image) goes to the CPU; off the whole positions remain the
         * differences that app/gpu_draw.h lists */
        if (g_gpu_frames_ok != g_gpu_frames || g_gpu_images_ok * 100 < g_gpu_images * 97)
            printf("GPU: FAILED\n");
    }
    if (g_smooth.exact_frames < g_smooth.frames * 99 / 100 || g_smooth_big_ok != g_smooth_big_checked || g_mo_frame_bad)
        printf("smooth motion: FAILED\n");
    if (g_battle_on)
        printf("battle: %ld rewards; %ld frames with a bar; %ld frames with a window (nothing drawn)\n",
               g_battle.popups_made, g_battle_bar_frames, g_battle_window);
    if (g_battle_on) /* what the hooks saw (app/ys_reward.h) */
        printf("game rewards: %ld kills, %ld paid, %ld event sums, %ld lost; %ld without the enemy on "
               "screen%s\n", g_reward.kills, g_reward.paid, g_reward.events, g_reward.lost, g_battle.rewards_unplaced,
               g_reward.kills != g_reward.paid || g_reward.lost ? " FAILED" : "");
    if (g_equip_on)
        printf("equipment: %ld map frames; %ld with Adol in the game, %ld swapped; Adol not recognized in %ld "
               "(1st at frame %ld)\n", g_equip_frames, g_equip_hero, g_equip_fixed, g_equip_unknown,
               g_equip_unknown_first);
}

/* --scroll-log: the game's scroll changes and the fix of the late ones (fix_scroll); per frame, only the
 * late ones (change after the usual line), fixed or not matching the game */
static long g_scroll_frames, g_scroll_late, g_scroll_fixed, g_scroll_fixed_first = -1, g_scroll_fix_map,
    g_scroll_fix_low, g_scroll_off, g_scroll_bad, g_scroll_bad_first = -1;
static Redraw g_scroll_rd;
static void scroll_log(Pce *pce, VdcLineLog *log, long frame)
{
    const YsScrollFrame *f = &g_scroll_frame;
    if (!f->valid)
        return;
    g_scroll_frames++;
    static RedrawFrame a;
    int keep = log->last_row; /* redraw_capture restarts the log: smooth motion will still read it */
    redraw_capture(&a, log, &pce->vce, frame);
    log->last_row = keep;
    a.scroll = *f;
    int mr, lr, res = fix_scroll(&g_scroll_rd, &a, &mr, &lr);
    int late = f->row_map > YS_SCROLL_MAP_ROW - 1 || f->row_low > YS_SCROLL_LOW_ROW - 1 ||
               (f->wrap && f->row_wrap > YS_SCROLL_MAP_ROW - 1 + REDRAW_RING - f->map_y);
    g_scroll_late += late;
    if (res > 0) {
        if (g_scroll_fixed++ == 0)
            g_scroll_fixed_first = frame;
        g_scroll_fix_map += mr > 0;
        g_scroll_fix_low += lr > 0;
    } else if (res < 0) {
        if (!(f->cr & 0x80)) /* background turned off by the game: screen blanked */
            g_scroll_off++;
        else if (g_scroll_bad++ == 0)
            g_scroll_bad_first = frame;
    }
    if (g_scroll_out && (late || res))
        fprintf(g_scroll_out, "%ld: changes at lines %d (map), %d (wrap), %d (bottom); y %d%s%s; %s (%d+%d lines)\n",
                frame, f->row_map, f->row_wrap, f->row_low, f->map_y, late ? ", late" : "",
                (f->cr & 0x80) ? "" : ", background off",
                res > 0 ? "fixed" : res < 0 ? "DOES NOT MATCH the game" : "on time", mr, lr);
}

/* Instruction hooks of the Ys engine: --actors and smooth motion together */
static void engine_hooks(Pce *pce, int after)
{
    if (g_ys_actors)
        ys_actors_hook(pce, after);
    if (g_ys_engine)
        ys_engine_hook(pce, after);
    if (g_windows_on && !after)
        ys_windows_hook(g_windows_on, pce);
    if (!after) {
        ys_scroll_hook(&g_scroll, pce);
        ys_screen_hook(&g_screen, pce);
        ys_fade_hook(&g_fade, pce);
        ys_reward_hook(&g_reward, pce);
        ys_music_hook(pce);
    }
}

/* --fades: the steps of the map fade and how many VCE colors are in the fade */
static void fades_log(long frame)
{
    static int last_level = -1, last_add = -1, last_n = -1;
    const YsFadeFrame *f = &g_fade_frame;
    int steps = 0, on = 0;
    for (int i = 0; i < 512; i++) {
        steps += f->step[i];
        on += f->c[i].on;
    }
    int n = f->map_colors > 0;
    if (f->map_level != last_level || f->map_add != last_add || n != last_n || steps) {
        printf("fades: frame %ld level %d brightening %d (background colors %d, in the fade %d, steps %d)", frame,
               f->map_level, f->map_add, f->map_colors, on, steps);
        if (getenv("PCE_FADES_DETAIL")) { /* levels of the colors in the fade: background / sprites */
            int h[2][8] = { { 0 } }, off[2] = { 0 };
            for (int i = 0; i < 512; i++) {
                if (f->c[i].on)
                    h[i >> 8][f->c[i].level]++;
                else
                    off[i >> 8]++;
            }
            for (int k = 0; k < 2; k++) {
                printf(k ? " | spr" : " | bg");
                for (int l = 0; l < 8; l++)
                    if (h[k][l])
                        printf(" %d:%d", l, h[k][l]);
                printf(" off:%d", off[k]);
            }
            printf(" | palettes");
            for (int p = 0; p < 32; p++) { /* palette level (that of the 1st color in the fade; - = none) */
                int l = -1;
                for (int k = 1; k < 16 && l < 0; k++)
                    if (f->c[p * 16 + k].on && f->c[p * 16 + k].src)
                        l = f->c[p * 16 + k].level;
                printf(l < 0 ? " -" : " %d", l);
            }
        }
        printf("\n");
    }
    last_level = f->map_level;
    last_add = f->map_add;
    last_n = n;
}

static int g_crop_log, g_crop_last_d;
static int g_crop_shot_mode, g_crop_shot_bg; /* --crop-shot */
static const char *g_crop_shot_path;
static CropState g_crop;

/* --equip-log: Adol and the equipment darkening from the game (sprite owner in the engine list; level and
 * brightening of the fade routine) against the old way (palette 13, size and colors compared with the reference ones) */
static int g_equip_log;
static long g_eql_frames, g_eql_old, g_eql_new, g_eql_old_only, g_eql_new_only, g_eql_nolist, g_eql_multi;
static long g_eql_col_map, g_eql_col_flat, g_eql_col_none, g_eql_col_lost, g_eql_col_gained, g_eql_map_diff;
static long g_eql_map_diff_known, g_eql_oldmap_no, g_eql_sat_diff, g_eql_unknown;
static long g_eql_actor[66], g_eql_screen[4];

static int equip_old_map(const uint16_t *ref, const uint16_t *cur, uint8_t map[3][8], uint8_t known_out[3][8])
{
    for (int ch = 0; ch < 3; ch++) {
        int known[8], val[8];
        for (int v = 0; v < 8; v++)
            known[v] = 0;
        for (int c = 1; c < 16; c++) {
            int a = (ref[c] >> (ch * 3)) & 7, b = (cur[c] >> (ch * 3)) & 7;
            if (known[a] && val[a] != b)
                return 0;
            known[a] = 1;
            val[a] = b;
        }
        for (int v = 0; v < 8; v++)
            known_out[ch][v] = (uint8_t)known[v];
        if (!known[0]) {
            known[0] = 1;
            val[0] = 0;
        }
        for (int v = 0; v < 8; v++) {
            if (known[v]) {
                map[ch][v] = (uint8_t)val[v];
                continue;
            }
            int lo = v - 1, hi = v + 1;
            while (!known[lo])
                lo--;
            while (hi < 8 && !known[hi])
                hi++;
            int m = hi < 8 ? val[lo] + (val[hi] - val[lo]) * (v - lo) / (hi - lo)
                           : (lo ? (val[lo] * v + lo / 2) / lo : v);
            map[ch][v] = (uint8_t)(m > 7 ? 7 : m);
        }
    }
    return 1;
}

static long g_eql_ice, g_eql_ice_bad; /* frames of Adol on the ice; with the drawing outside the rule */
static int g_eql_ice_diff_max;

static void equip_log(const Pce *pce, int frame)
{
    static long printed;
    EquipInfo info;
    equip_read(pce, &info);
    if (!g_equip_table || info.game == EQUIP_GAME_NONE)
        return;
    int screen = ys_screen_layout(g_screen.program);
    g_eql_screen[screen & 3]++;
    g_eql_frames++;
    const YsFrameList *ef = &g_eng_frame;
    const YsList *l = ef->shown_ok ? &ef->shown : NULL;
    int adol = l ? l->hero : -1;
    g_eql_actor[adol < 0 ? 65 : adol < 64 ? adol : 64]++;
    /* The table and the VRAM the frame was drawn with (what the window uses; the VDC may already be on the next copy) */
    const uint16_t *frame_sat = g_window_frame.sat, *frame_vram = g_window_frame.vram;
    if (l && screen == CROP_SCREEN_GAME && memcmp(l->sat, frame_sat, sizeof(l->sat)) != 0) {
        g_eql_sat_diff++;
        if (getenv("PCE_EQUIP_SAT_LOG")) {
            int slots = 0, first = -1;
            for (int i = 0; i < 64; i++)
                if (memcmp(&l->sat[i * 4], &frame_sat[i * 4], 8) != 0) {
                    slots++;
                    first = first < 0 ? i : first;
                }
            printf("equip: frame %d table: list %ld base %04X R13 %04X, %d different slots (1st: %d), built %ld;", frame,
                   l->seq, l->base, pce->vdc.reg[0x13], slots, first, g_engine.done_count);
            for (int k = 0; k < 4; k++) {
                const YsList *d = &g_engine.done[k];
                int ds = 0;
                for (int i = 0; i < 64; i++)
                    ds += memcmp(&d->sat[i * 4], &frame_sat[i * 4], 8) != 0;
                printf(" [%ld %04X: %d]", d->seq, d->base, ds);
            }
            printf("\n");
        }
    }
    int n_old = 0, n_new = 0, hero = -1, hero_unknown = -1;
    for (int i = 0; i < 64; i++) {
        const uint16_t *e = &frame_sat[i * 4];
        int fi = equip_frame_of(g_equip_table, frame_vram, e);
        int old = fi >= 0 && (e[3] & 0x0F) == 13;
        int now = fi >= 0 && l && adol < YS_ACTORS && l->owner[i] == adol;
        n_old += old;
        n_new += now;
        if (old || now)
            hero = i;
        int ice = l && adol < YS_ACTORS && l->owner[i] == adol ? equip_ice_pose(l->pose[adol], e) : -1;
        if (ice >= 0) {
            /* the drawing the engine asks for against the VRAM points, in the two sets of frames there are on the ice */
            int want = info.shield != 0xFF ? EQUIP_SET_SS : EQUIP_SET_SWORD;
            int d = equip_ice_diff(&g_equip_table->frame[want * (EQUIP_FRAMES / 3) + ice], frame_vram, e);
            g_eql_ice++;
            g_eql_ice_diff_max = d > g_eql_ice_diff_max ? d : g_eql_ice_diff_max;
            if (d > 3 && g_eql_ice_bad++ < 12)
                printf("equip: frame %d ice: drawing %02X, %d points different from the rule\n", frame, l->pose[adol], d);
        }
        if (fi < 0 && ice < 0 && l && adol < YS_ACTORS && l->owner[i] == adol && (e[0] & 0x3FF) && hero_unknown < 0)
            hero_unknown = i;
        if (old && !now && g_eql_old_only++ < 12 && printed++ < 60)
            printf("equip: frame %d old only: sprite %d owner %d adol %d list %d screen %d attr %04X\n", frame, i,
                   l ? l->owner[i] : -1, adol, l != NULL, screen, e[3]);
        if (now && !old && g_eql_new_only++ < 12 && printed++ < 60)
            printf("equip: frame %d new only: sprite %d owner %d attr %04X screen %d\n", frame, i, l->owner[i], e[3], screen);
    }
    g_eql_old += n_old > 0;
    g_eql_new += n_new > 0;
    g_eql_multi += n_old > 1 || n_new > 1;
    g_eql_nolist += !l && n_old > 0;
    /* Adol's character with a drawing outside the 36 (the Ys II monsters): the start and end of each stretch */
    static int in_unknown;
    int unknown_now = hero_unknown >= 0 && n_new == 0 && screen == CROP_SCREEN_GAME;
    g_eql_unknown += unknown_now;
    if (unknown_now != in_unknown) {
        if (printed++ < 60 && unknown_now) {
            const uint16_t *e = &frame_sat[hero_unknown * 4];
            printf("equip: frame %d unknown shape: drawing %03X attr %04X\n", frame, (e[2] >> 1) & 0x3FF, e[3]);
        } else if (printed < 60) {
            printf("equip: frame %d normal shape again\n", frame);
        }
        in_unknown = unknown_now;
    }
    if (hero < 0)
        return;
    /* The colors: from the game x from the comparison */
    const uint16_t *ref = g_equip_table->pal;
    const YsFadeFrame *f = &g_fade_frame;
    int base = 256 + 13 * 16;
    EquipColors col;
    equip_colors(g_equip_table, f, &col);
    (col.kind == EQUIP_COLORS_MAP ? &g_eql_col_map : col.kind == EQUIP_COLORS_FLAT ? &g_eql_col_flat : &g_eql_col_none)[0]++;
    uint8_t oldm[3][8], known[3][8];
    int old_ok = equip_old_map(ref, &f->color[base], oldm, known);
    g_eql_oldmap_no += !old_ok;
    int lost = old_ok && col.kind == EQUIP_COLORS_NONE, gained = !old_ok && col.kind != EQUIP_COLORS_NONE;
    g_eql_col_lost += lost;
    g_eql_col_gained += gained;
    if ((lost || gained || col.kind == EQUIP_COLORS_NONE) && printed++ < 60) {
        printf("equip: frame %d colors %s:", frame, lost ? "lost" : gained ? "gained" : "of an effect");
        for (int c = 1; c < 16; c++)
            printf(" %03X/%03X:%d+%d%s", f->color[base + c], f->c[base + c].src, f->c[base + c].level, f->c[base + c].add,
                   f->c[base + c].on ? "" : "x");
        printf("\n");
    }
    if (old_ok) {
        int diff = 0, diff_known = 0;
        for (int ch = 0; ch < 3; ch++)
            for (int v = 0; v < 8; v++) {
                if (col.kind == EQUIP_COLORS_NONE)
                    continue;
                int m = col.kind == EQUIP_COLORS_MAP ? col.map[v] : (col.flat >> (ch * 3)) & 7;
                if (m != oldm[ch][v]) {
                    diff = 1;
                    diff_known |= known[ch][v];
                }
            }
        g_eql_map_diff += diff;
        g_eql_map_diff_known += diff_known;
        if (diff_known && printed++ < 60)
            printf("equip: frame %d map differs in a known value\n", frame);
    }
}

static void equip_log_end(void)
{
    printf("equip: %ld frames (screens %ld %ld %ld %ld); old Adol %ld, new %ld; old only %ld, new only %ld, no list %ld, 2+ sprites %ld\n",
           g_eql_frames, g_eql_screen[0], g_eql_screen[1], g_eql_screen[2], g_eql_screen[3], g_eql_old, g_eql_new,
           g_eql_old_only, g_eql_new_only, g_eql_nolist, g_eql_multi);
    printf("equip: colors: by the table %ld, single color %ld, effect %ld; old refused %ld; lost %ld, gained %ld; map differs %ld (in a known value %ld)\n",
           g_eql_col_map, g_eql_col_flat, g_eql_col_none, g_eql_oldmap_no, g_eql_col_lost, g_eql_col_gained, g_eql_map_diff,
           g_eql_map_diff_known);
    printf("equip: list different from the table %ld; unknown shape %ld\n", g_eql_sat_diff, g_eql_unknown);
    printf("equip: ice %ld frames, with more than 3 points outside the rule %ld (at most %d points)\n", g_eql_ice, g_eql_ice_bad,
           g_eql_ice_diff_max);
    printf("equip: Adol's character:");
    for (int i = 0; i < 66; i++)
        if (g_eql_actor[i])
            printf(" %d=%ld", i == 65 ? -1 : i, g_eql_actor[i]);
    printf("\n");
}

/* --panel-log: each change of what the game writes on the panel, with what is in RAM: the text (pieces: letters and
 * digits), whether there is a digit in a palette other than the label's, what the HUD concludes (crop_hud_layout), the
 * length of the bars, H.P, maximum, EXP, next level and GOLD from RAM and the HUD's NEXT (from the panel) against the one computed from RAM. */
static int g_panel_log;
static void panel_log(const Pce *pce, int frame)
{
    static YsPanel p;
    static char last[320];
    ys_panel_read(pce, &p);
    static const char k_label[] = "H.PEXGOLDM"; /* pieces 257-266 */
    static const char k_digit[] = "0123456789:/"; /* pieces 449-460 */
    char txt[YS_PANEL_COLS + 1];
    int label_pal = -1, odd = 0;
    for (int c = 0; c < YS_PANEL_COLS; c++) {
        int t = YS_TILE(p.cell[0][c]);
        char ch = ' ';
        if (t >= YS_TILE_LABEL_FIRST && t <= YS_TILE_LABEL_LAST) {
            ch = k_label[t - YS_TILE_LABEL_FIRST];
            label_pal = YS_PAL(p.cell[0][c]);
        } else if (t >= YS_TILE_DIGIT_FIRST && t <= YS_TILE_DIGIT_LAST) {
            ch = k_digit[t - YS_TILE_DIGIT_FIRST];
            odd |= t != YS_TILE_SLASH && YS_PAL(p.cell[0][c]) != label_pal;
        } else if (t != YS_TILE_BLANK)
            ch = '?';
        txt[c] = ch;
    }
    txt[YS_PANEL_COLS] = 0;
    CropHudLayout hl;
    crop_hud_layout(&p, &hl);
    const uint8_t *r = pce->ram;
    int exp = r[0xABB] | r[0xABC] << 8 | r[0xABD] << 16, next = r[0xC4A] | r[0xC4B] << 8 | r[0xC4C] << 16;
    /* the bar color read from the palette (piece 506) against the image one: the 1st point inside the bar */
    uint32_t bar_img = pce->vce.frame_width > YS_PANEL_X + 8 * YS_BAR_FIRST_COL
                           ? pce->vce.frame[(YS_PANEL_Y + 16 + 4) * pce->vce.frame_width + YS_PANEL_X + 8 * YS_BAR_FIRST_COL] & 0xFFFFFF
                           : 0;
    char line[320];
    snprintf(line, sizeof(line),
             "valid %d [%s] other-palette %d hud-save %d bars %d/%d ram H.P %u/%u EXP %u/%u GOLD %u NEXT %d (from RAM %d) color %06X image %06X",
             p.valid, txt, odd, hl.save, hl.bars > 0 ? hl.bar_len[0] : -1, hl.bars > 1 ? hl.bar_len[1] : -1,
             r[0xC43] | r[0xC44] << 8, r[0xC48] | r[0xC49] << 8, exp, next, r[0xC41] | r[0xC42] << 8, crop_next_exp(&p),
             next > exp ? next - exp : 0, p.bar_yellow & 0xFFFFFF, bar_img);
    if (strcmp(line, last) != 0) {
        printf("panel %d: %s\n", frame, line);
        memcpy(last, line, sizeof(last));
    }
}

/* Instant dialog (app/ys_dialog.h). --instant-dialog turns it on from reset; the session turns it on and off
 * ("# dialog"); --dialog-log lists the text stretches the game writes slowly (speed other than 0). */
static int g_dialog_forced, g_dialog_on;
static int g_dialog_log;
static struct {
    long first, last, hits, slow_frames;
    int x, pc, smin, smax, limit, open;
    long total_frames, count;
    long waits, zeroed; /* per-letter waits (slow text); speeds zeroed by the option */
} g_dlg;

static void dialog_log_close(void)
{
    if (!g_dlg.open)
        return;
    printf("dialog: frames %ld-%ld task %02X program %04X speed %02X-%02X limit $92=%02X (%ld tests)\n",
           g_dlg.first, g_dlg.last, g_dlg.x, g_dlg.pc, g_dlg.smin, g_dlg.smax, g_dlg.limit, g_dlg.hits);
    g_dlg.total_frames += g_dlg.last - g_dlg.first + 1;
    g_dlg.count++;
    g_dlg.open = 0;
}

static void dialog_log_patch(Pce *pce)
{
    if (pce != g_tr->pce) { /* the lockstep reference: only the option */
        if (g_dialog_on)
            ys_dialog_patch(pce);
        return;
    }
    if (ys_dialog_wait_at(pce))
        g_dlg.waits++;
    if (!ys_dialog_at(pce))
        return;
    int speed = pce->ram[(0x27F6 + pce->cpu.x) & 0x1FFF];
    long frame = (long)pce->vce.frame_count;
    if (speed) {
        if (g_dlg.open && (g_dlg.x != pce->cpu.x || frame > g_dlg.last + 120))
            dialog_log_close();
        if (!g_dlg.open) {
            g_dlg.open = 1;
            g_dlg.first = frame;
            g_dlg.x = pce->cpu.x;
            g_dlg.pc = pce->cpu.pc;
            g_dlg.smin = g_dlg.smax = speed;
            g_dlg.hits = 0;
        }
        g_dlg.last = frame;
        g_dlg.hits++;
        g_dlg.limit = pce->ram[0x92];
        if (speed < g_dlg.smin)
            g_dlg.smin = speed;
        if (speed > g_dlg.smax)
            g_dlg.smax = speed;
    }
    if (g_dialog_on) {
        if (speed)
            g_dlg.zeroed++;
        ys_dialog_patch(pce);
    }
}

static void dialog_apply(Pce *pce, long frames_done)
{
    InputEvent e;
    while (input_script_event(g_tr->input, "dialog", frames_done, &e))
        if (e.n >= 1)
            g_dialog_on = (int)e.v[0];
    if (g_dialog_forced)
        g_dialog_on = 1;
    if (g_dialog_log)
        pce->game_patch = dialog_log_patch;
    else
        ys_dialog_set(pce, g_dialog_on);
}


/* Ys options (TraceExt.arg). */
static int ys_arg(int argc, char **argv, int *pi)
{
    int i = *pi;
    if (strcmp(argv[i], "--fades") == 0)
        g_fades_log = 1;
    else if (strcmp(argv[i], "--crop-log") == 0)
        g_crop_log = 1;
    else if (strcmp(argv[i], "--panel-log") == 0)
        g_panel_log = 1;
    else if (strcmp(argv[i], "--instant-dialog") == 0)
        g_dialog_forced = 1;
    else if (strcmp(argv[i], "--dialog-log") == 0)
        g_dialog_log = 1;
    else if (strcmp(argv[i], "--equip-log") == 0) { /* like --game-fixes: the frame as the window draws it */
        g_equip_log = 1;
        g_fixes_on = 1;
    }
    else if (strcmp(argv[i], "--crop-shot") == 0 && i + 3 < argc) {
        g_crop_shot_mode = strcmp(argv[i + 1], "frame") == 0     ? CROP_FRAME
                           : strcmp(argv[i + 1], "compact") == 0 ? CROP_COMPACT
                           : strcmp(argv[i + 1], "wide") == 0    ? CROP_WIDE
                           : strcmp(argv[i + 1], "fill") == 0    ? CROP_FILL
                                                                 : CROP_FULL;
        g_crop_shot_bg = CROP_HUD_BLACK;
        for (int k = 0; k < CROP_HUD_COUNT; k++)
            if (strcmp(argv[i + 2], crop_hud_names[k]) == 0)
                g_crop_shot_bg = k;
        g_crop_shot_path = argv[i + 3];
        i += 3;
    }
    else if (strcmp(argv[i], "--game-fixes") == 0)
        g_fixes_on = 1;
    else if (strcmp(argv[i], "--walk-rhythm") == 0 && i + 2 < argc) {
        g_rhythm.from = strtol(argv[++i], NULL, 10);
        g_rhythm.to = strtol(argv[++i], NULL, 10);
    }
    else if (strcmp(argv[i], "--gpu") == 0)
        g_gpu_on = 1;
    else if (strcmp(argv[i], "--equip") == 0) {
        if (!g_equip_table) {
            fprintf(stderr, "--equip: ys_trace only (Adol's frames come from the disc at build time)\n");
            return -1;
        }
        g_equip_on = 1;
        equip_init(&g_equip, g_equip_table);
    }
    else if (strcmp(argv[i], "--equip-force") == 0 && i + 1 < argc &&
             sscanf(argv[i + 1], "%d,%d,%d", &g_equip_force[0], &g_equip_force[1], &g_equip_force[2]) == 3)
        i++;
    else if (strcmp(argv[i], "--battle") == 0 || strcmp(argv[i], "--battle-small") == 0) {
        g_battle_on = g_smooth_on = 1;
        g_battle.bars = 1;
        g_battle.popups = strcmp(argv[i], "--battle-small") == 0 ? 2 : 1;
    } else if (strcmp(argv[i], "--smooth-check") == 0) {
        g_smooth_on = 1;
        g_smooth_all = 1;
    } else if (strcmp(argv[i], "--smooth-frames") == 0 && i + 3 < argc) {
        g_smooth_on = 1;
        g_smooth_from = strtol(argv[++i], NULL, 10);
        g_smooth_count = strtol(argv[++i], NULL, 10);
        g_smooth_dir = argv[++i];
    } else if (strcmp(argv[i], "--scroll-log") == 0 && i + 1 < argc) {
        if (strcmp(argv[++i], "-") == 0)
            g_scroll_out = stdout;
        else if (!(g_scroll_out = u8_fopen(argv[i], "w"))) {
            printf("could not create %s\n", argv[i]);
            return -1;
        }
        g_scroll_out_on = 1;
    } else if (strcmp(argv[i], "--windows") == 0 && i + 1 < argc) {
        if (strcmp(argv[++i], "-") != 0 && !(g_windows_out = u8_fopen(argv[i], "w"))) {
            printf("could not create %s\n", argv[i]);
            return -1;
        }
        ys_windows_init(&g_windows);
        g_windows_on = &g_windows;
    } else if (strcmp(argv[i], "--actors") == 0 && i + 1 < argc) {
        FILE *out = NULL;
        if (strcmp(argv[++i], "-") != 0 && !(out = u8_fopen(argv[i], "w"))) {
            printf("could not create %s\n", argv[i]);
            return -1;
        }
        ys_actors_init(&g_actors, out);
        g_actors_on = 1;
    } else
        return 0;
    *pi = i;
    return 1;
}

/* After the reset and the generic options (TraceExt.setup). */
static int ys_setup(TraceRun *r)
{
    Pce *pce = r->pce;
    g_tr = r;
    g_line_log = r->line_log;
    if (g_smooth_on) {
        if (redraw_init(&g_smooth) != 0 ||
            !(g_smooth.from_sprite = malloc((size_t)VCE_MAX_WIDTH * REDRAW_SUB * VCE_ACTIVE_LINES * REDRAW_SUB)))
            return 1;
        pce->vdc.line_log = g_line_log;
    }
    ys_scroll_init(&g_scroll);
    ys_fade_init(&g_fade);
    if (g_scroll_out_on && redraw_init(&g_scroll_rd) != 0)
        return 1;
    if (g_crop_log || g_scroll_out_on) /* --crop-log: the map around the shrunken windows (redraw_ring) */
        pce->vdc.line_log = g_line_log;
    if (g_actors_on) {
        if (r->lockstep) {
            printf("--actors does not work together with --lockstep\n");
            return 1;
        }
        pce->vdc.line_log = g_line_log;
        g_ys_actors = &g_actors;
        pce->on_instruction = engine_hooks;
    }
    if (g_smooth_on) {
        if (r->lockstep) {
            printf("--smooth-check and --smooth-frames do not work together with --lockstep\n");
            return 1;
        }
        motion_init(&g_motion, 1);
    }
    if (g_gpu_on) {
        SDL_Window *win = NULL;
        SDL_Renderer *ren = NULL;
        if (!g_smooth_on || !SDL_Init(SDL_INIT_VIDEO) ||
            !SDL_CreateWindowAndRenderer("pce_trace --gpu", 64, 64, SDL_WINDOW_HIDDEN, &win, &ren)) {
            printf("--gpu goes with --smooth-check or --smooth-frames and needs a window (%s)\n", SDL_GetError());
            return 1;
        }
        if (!gpu_draw_init(&g_gpu, ren) || !gpu_draw_init(&g_gpu_t, ren)) {
            printf("--gpu: the renderer %s does not do its own blending\n", SDL_GetRendererName(ren));
            return 1;
        }
    }
    if (g_windows_on && r->lockstep) {
        printf("--windows does not work together with --lockstep\n");
        return 1;
    }
    /* the crop uses the game windows; so does the torn frame */
    if ((g_crop_log || g_fixes_on || g_battle_on) && !g_windows_on && !r->lockstep) { /* bars too */
        ys_windows_init(&g_windows);
        g_windows_on = &g_windows;
        g_windows_quiet = 1; /* without the windows summary at the end (only with --windows) */
    }
    if ((g_smooth_on || g_fixes_on || g_windows_on || g_scroll_out_on || g_fades_log || g_crop_shot_path || g_equip_log) &&
        !r->lockstep) {
        /* Ys engine: the new motion */
        ys_engine_init(&g_engine);
        g_ys_engine = &g_engine;
        pce->on_instruction = engine_hooks;
    }
    if (g_fixes_on) {
        pce->vdc.line_log = g_line_log;
        if (!g_smooth_on && redraw_init(&g_smooth) != 0)
            return 1;
        g_smooth.window_front = 1;
    }
    if (r->volumes)
        pce->mix.hide_cd_pop = g_fixes_on;
    return 0;
}

/* Session events (TraceExt.input): chip music of the options menu and instant dialog. */
static void ys_input(TraceRun *r, long frame)
{
    InputEvent e;
    while (frame > 0 && input_script_event(r->input, "menu", frame, &e)) /* menu opened in the session */
        if (e.n >= 2) {
            menu_chip_replay(r->pce, e.v[0], (int)e.v[1]);
            if (r->ref)
                menu_chip_replay(r->ref, e.v[0], (int)e.v[1]);
        }
    dialog_apply(r->pce, frame);
}

/* End of each frame (TraceExt.frame). */
static void ys_frame(TraceRun *r, long frame)
{
    Pce *pce = r->pce;
    long i = frame - 1;
        if (g_ys_engine)
            ys_engine_frame(&g_engine, &g_eng_frame);
        if (g_windows_on)
            windows_frame(pce, i + 1, &g_eng_frame);
        ys_scroll_frame(&g_scroll, &g_scroll_frame);
        g_reward_frame.n = 0; /* this frame's rewards */
        ys_reward_take(&g_reward, &g_reward_frame);
        static long events_seen;
        if (g_reward.events != events_seen && g_battle_on)
            printf("event reward at frame %ld (%ld sums)\n", i + 1, g_reward.events - events_seen);
        events_seen = g_reward.events;
        if (g_scroll_out_on)
            scroll_log(pce, pce->vdc.line_log, i + 1);
        ys_fade_frame(&g_fade, pce, &g_fade_frame);
        if (g_fades_log)
            fades_log(i + 1);
        if (g_fixes_on) {
            int torn_row = -1, torn_in = 0;
            int torn_n = fix_torn_log(pce->vdc.line_log, &g_win_snap, &torn_in, &torn_row);
            if ((torn_n > 0 || torn_in) && getenv("PCE_TORN_LOG")) /* torn points and the 1st line, per frame */
                printf("torn %ld: %d points outside the windows and %d inside, from line %d\n", i + 1, torn_n, torn_in,
                       torn_row);
            if (torn_in > 0) /* inside the game windows, the window as at the end of the image */
                torn_window(pce, i + 1);
            /* frames where the silence of the old track started (cdaudio.h: pc_mute, pc_mutes) */
            int pop = pce->cd ? pce->cd->audio.pc_mutes : 0;
            if (pop > g_cd_pop_prev && g_cd_pop_count++ < 64)
                g_cd_pop_list[g_cd_pop_count - 1] = i + 1;
            g_cd_pop_prev = pop;
            if (!g_smooth_on) {
                redraw_capture(&g_window_frame, pce->vdc.line_log, &pce->vce, i + 1);
                g_window_frame.wins = g_win_snap;
                g_window_frame.eng = g_eng_frame;
                window_fix(&g_window_frame, i + 1);
            }
        }
        if (g_smooth_on && (g_smooth_all || i + 1 >= g_smooth_from - 30)) {
            motion_frame_done(pce, i + 1);
        }
        if (g_panel_log)
            panel_log(pce, i + 1);
        if (g_equip_log)
            equip_log(pce, i + 1);
        if (g_crop_log) {
            /* --crop-log: when the border crop (app/crop.h) turns on and off */
            int was = g_crop.active, was_dark = g_crop.darken, was_win = g_crop.window, was_scene = g_crop.scene;
            int was_white = g_crop.whiten;
            int was_ext = g_crop.win_x0 * 1000000 + g_crop.win_x1 * 1000 + g_crop.win_y1;
            int was_pan = g_crop.pan_q * 1000 + g_crop.pan_qy, was_hold = g_crop.scene_hold;
            int d = crop_detect(pce->vce.frame, pce->vce.frame_width, pce->vce.frame_height);
            /* the game windows, as the window does (main.c) */
            static uint32_t win_bg[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
            if (!g_smooth_on && !g_fixes_on)
                redraw_capture(&g_window_frame, pce->vdc.line_log, &pce->vce, i + 1);
            int cur = !g_smooth_on && g_window_frame.seq == i + 1 && g_window_frame.w == pce->vce.frame_width &&
                      g_window_frame.h == pce->vce.frame_height;
            if (cur) {
                g_window_frame.wins = g_win_snap;
                g_window_frame.eng = g_eng_frame; /* the masks around the windows (redraw_ring) */
                int yb[CROP_WINDOWS_MAX][4], ysc, yn = ys_windows_boxes(&g_win_snap, g_window_frame.rows,
                                                                         g_window_frame.h, yb, CROP_WINDOWS_MAX, &ysc);
                crop_set_windows(&g_crop, (const int (*)[4])yb, yn, ysc);
                if (yn)
                    redraw_bg_only(&g_window_frame, 0, win_bg);
            }
            crop_set_fade(&g_crop, g_fade_frame.map_level, g_fade_frame.map_add, g_fade_frame.table_ok ? g_fade_frame.table : NULL);
            crop_set_fade_stray(&g_crop, g_fade_frame.map_stray);
            crop_set_screen(&g_crop, ys_screen_layout(g_screen.program));
            crop_frame(&g_crop, pce->vce.frame, pce->vce.frame_width, pce->vce.frame_height);
            {
                /* the game screen (from the program) against the border shape in the image (the old way) */
                int a = g_crop.active;
                g_scr_frames[a]++;
                int diff = 0;
                if (a && d == a)
                    g_scr_shape++;
                else if (a && d == -1) {
                    /* the HUD band lit on a black screen (the load screen has no band) */
                    CropView bv;
                    if (a == CROP_SCREEN_GAME && g_crop.darken < 7 &&
                        crop_view(CROP_FILL, a, pce->vce.frame_width, pce->vce.frame_height, &bv) && g_scr_black_lit++ == 0)
                        g_scr_black_lit_first = i + 1;
                    g_scr_black++;
                }
                else if (a) {
                    diff = 1;
                    if (g_scr_noshape++ == 0)
                        g_scr_noshape_first = i + 1;
                } else if (d > 0 && g_crop.white_out) {
                    g_scr_white++;
                } else if (d > 0) {
                    diff = 1;
                    if (g_scr_extra++ == 0)
                        g_scr_extra_first = i + 1;
                }
                if (diff && getenv("PCE_CROP_DIFF"))
                    printf("crop: frame %ld game screen %d (program %02X), shape in the image %d\n", i + 1, a,
                           g_screen.program & 0xFF, d);
            }
            if (g_crop.scene_hold != was_hold)
                printf("crop: frame %ld scene %s (darkened %d, %d frames after the window)\n", i + 1,
                       g_crop.scene_hold ? "starts" : "ends", g_crop.darken, g_crop.scene_held);
            /* around the shrunken windows, as the window does (main.c), without smooth motion */
            if (!g_smooth_on && g_crop.active == CROP_SCREEN_GAME) {
                static RedrawRing ring;
                static uint32_t ring_bg[VCE_MAX_WIDTH * VCE_ACTIVE_LINES], ring_spr[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
                if (g_window_frame.seq == i + 1) {
                    int boxes[CROP_WINDOWS_MAX][4], nb = crop_ring_boxes(&g_crop, boxes);
                    static uint32_t ring_fix[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
                    static uint8_t ring_rows[VCE_ACTIVE_LINES];
                    motion_map_rows(&g_window_frame, ring_rows);
                    int got = redraw_ring(&ring, &g_window_frame, nb, boxes, NULL, ring_bg, ring_spr, win_bg, ring_fix,
                                          ring_rows);
                    g_ring_remade = ring.remade;
                    g_ring_remade_frames = ring.remade_frames;
                    g_ring_no_list = ring.no_list;
                    g_ring_hidden = ring.hidden_px;
                    g_ring_hole_sprites = ring.hole_sprites;
                    g_ring_hole_frames = ring.hole_frames;
                    crop_ring(&g_crop, got == 2 ? ring_bg : NULL, got ? ring_spr : NULL);
                    g_crop.fix_px = got ? ring_fix : NULL;
                    if (nb) { /* the map around the windows: whether it matches the current one outside them (otherwise, the game changed the map) */
                        long out = 0, diff = 0;
                        for (int y = CROP_MAP_Y; got == 2 && y < CROP_MAP_Y + CROP_MAP_H; y++)
                            for (int x = CROP_MAP_X; x < CROP_MAP_X + CROP_MAP_W; x++) {
                                int in = 0;
                                for (int k = 0; k < nb; k++)
                                    in |= x >= boxes[k][0] && x <= boxes[k][2] && y >= boxes[k][1] && y <= boxes[k][3];
                                if (!in) {
                                    size_t px = (size_t)y * pce->vce.frame_width + x;
                                    out++;
                                    diff += ((ring_bg[px] ^ win_bg[px]) & 0xFFFFFF) != 0;
                                }
                            }
                        g_ring_map[got != 2 ? 2 : diff * 10 > out ? 1 : 0]++;
                        if (got != 2 && g_ring_map_first < 0)
                            g_ring_map_first = i + 1;
                    }
                }
            }
            g_crop.next_on = 1;
            ys_panel_read(pce, &g_crop.panel);
            g_crop.next = crop_next_exp(&g_crop.panel);
            if (g_crop.active == CROP_SCREEN_GAME && pce->vce.frame_width > 327 && pce->vce.frame_height > 220)
                hud_compare(&g_crop, pce, i + 1);
            crop_pan_step(&g_crop, CROP_FILL);
            if (g_crop.pan_q * 1000 + g_crop.pan_qy != was_pan)
                printf("crop: frame %ld pan %d,%d (1/4 point)\n", i + 1, g_crop.pan_q, g_crop.pan_qy);
            if (g_crop.window != was_win || g_crop.scene != was_scene ||
                (g_crop.window && g_crop.win_x0 * 1000000 + g_crop.win_x1 * 1000 + g_crop.win_y1 != was_ext))
                printf("crop: frame %ld game window %s (columns %d-%d, rows %d-%d of the map)%s, darkened %d of 7\n",
                       i + 1, g_crop.window ? "opens" : "closes", g_crop.win_x0, g_crop.win_x1, g_crop.win_y0,
                       g_crop.win_y1, g_crop.scene ? ", scene in fill" : "", g_crop.darken);
            if (g_crop.active != was || (g_crop.active && d == -1 && g_crop_last_d != -1))
                printf("crop: frame %ld %s%s (program %02X)\n", i + 1,
                       g_crop.active != was ? (g_crop.active ? "on" : "off") : "black (kept)",
                       g_crop.active == CROP_SCREEN_LOAD ? ", load screen" : "", g_screen.program & 0xFF);
            g_crop_last_d = d;
            if (g_crop.darken != was_dark)
                printf("crop: frame %ld darkens %d step(s) outside the map\n", i + 1, g_crop.darken);
            if (g_crop.whiten != was_white)
                printf("crop: frame %ld flash %d of 7\n", i + 1, g_crop.whiten);
        }
        if (g_actors_on)
            ys_actors_frame(&g_actors, g_line_log->sat);
}

/* After the last frame (TraceExt.done): the summaries, the smooth motion check and --crop-shot. */
static int ys_done(TraceRun *r, long frames, const char *path)
{
    Pce *pce = r->pce;
    (void)path;
    if (g_dialog_log) {
        dialog_log_close();
        printf("dialog: %ld slow stretches, %ld frames; per-letter waits: %ld; zeroed speeds: %ld (option %s)\n",
               g_dlg.count, g_dlg.total_frames, g_dlg.waits, g_dlg.zeroed, g_dialog_on ? "on" : "off");
    }
    if (g_scroll_out_on)
        printf("scroll: %ld frames with the game's changes, %ld with a late change; %ld fixed (1st: %ld; %ld on the "
               "map, %ld at the bottom); not matching the game: %ld with the screen blanked, %ld with it lit (1st: %ld)\n",
               g_scroll_frames, g_scroll_late, g_scroll_fixed, g_scroll_fixed_first, g_scroll_fix_map, g_scroll_fix_low,
               g_scroll_off, g_scroll_bad, g_scroll_bad_first);
    if (g_crop_log) {
        printf("map around the shrunken windows: redrawn in %ld frames, %ld not matching the current one, %ld without a "
               "map (1st: %ld)\n", g_ring_map[0] + g_ring_map[1], g_ring_map[1], g_ring_map[2], g_ring_map_first);
        printf("around the shrunken windows: %ld game masks remade without the window in %ld frames (hiding %ld character points); %ld frames "
               "with a window without the engine list\n", g_ring_remade, g_ring_remade_frames, g_ring_hidden, g_ring_no_list);
        printf("window characters outside the ring: %ld sprites in %ld frames\n", g_ring_hole_sprites,
               g_ring_hole_frames);
        printf("screen from the game: %ld program loads; %ld frames in the game, %ld on the load screen, %ld without border; "
               "with the border: %ld with the shape in the image, %ld black (%ld not darkened, 1st: %ld), %ld without the shape (1st: %ld); without border: %ld with the "
               "shape after the flash, %ld with the shape (1st: %ld)\n", g_screen.loads, g_scr_frames[CROP_SCREEN_GAME],
               g_scr_frames[CROP_SCREEN_LOAD], g_scr_frames[CROP_SCREEN_NONE], g_scr_shape, g_scr_black, g_scr_black_lit,
               g_scr_black_lit_first, g_scr_noshape,
               g_scr_noshape_first, g_scr_white, g_scr_extra, g_scr_extra_first);
        printf("panel from the game: %ld frames in the game, %ld outside the cells; equal to the old one %ld (%ld without labels); the "
               "old one with nothing %ld; different darkened %ld, with the late change %ld; lit and different: labels %ld, bars %ld, "
               "save %ld (1st: %ld)\n",
               g_hud_frames, g_hud_invalid, g_hud_same, g_hud_empty, g_hud_no_old, g_hud_dark, g_hud_late, g_hud_items, g_hud_bars,
               g_hud_save, g_hud_first);
    }
    if (g_equip_log)
        equip_log_end();
    if (g_fades_log)
        printf("fades: %ld calls of the fade routine (%ld with unknown destination), %ld of the flash, %ld copies to the VCE\n",
               g_fade.transforms, g_fade.unknown_dest, g_fade.flashes, g_fade.uploads);
    if (g_windows_on && !g_windows_quiet) {
        if (g_windows_out)
            fclose(g_windows_out);
        printf("game windows: %ld frames with a window (adding up the windows: opening %ld, open %ld, closing %ld), %ld openings, %ld "
               "closings; different from $3E and the game stack in %ld of %ld frames (1st: %ld)\n",
               g_win_frames, g_win_states[YS_WIN_OPENING], g_win_states[YS_WIN_OPEN], g_win_states[YS_WIN_CLOSING],
               g_windows.opens, g_windows.closes, g_windows.mismatch, g_windows.frames, g_windows.first_mismatch);
        printf("window sprites: %ld, inside a window %ld, outside in a scene (window without saved background) %ld, "
               "outside %ld in %ld frames (1st: %ld), without a window %ld in %ld frames (1st: %ld)\n", g_win_spr, g_win_in,
               g_win_scene, g_win_out, g_win_out_frames, g_win_out_first, g_win_alone, g_win_alone_frames,
               g_win_alone_first);
        printf("state $50 characters: in %ld frames, without a window (not even in the previous frame) in %ld (1st: %ld)\n",
               g_st50_frames, g_st50_alone, g_st50_alone_first);
    }
    if (g_actors_on) {
        ys_actors_summary(&g_actors);
        if (g_actors.out)
            fclose(g_actors.out);
    }
    if (g_rhythm.to > 0)
        motion_rhythm_print(&g_rhythm);
    if (g_fixes_on) {
        printf("item window in front: %ld frames (1st: %ld)\n", g_window_frames, g_window_first);
        fflush(stdout);
        printf("torn inside the windows: %ld, fixed %ld, with a tear inside afterwards: %ld:",
               g_torn_win_count, g_torn_win_fixed, g_torn_win_left);
        for (long k = 0; k < g_torn_win_count && k < 4096; k++)
            printf(" %ld", g_torn_win_list[k]);
        printf("\nlayout switches: %ld:", g_layout_count);
        for (long k = 0; k < g_layout_count && k < 256; k++)
            printf(" %ld", g_layout_list[k]);
        printf("\n");
        fflush(stdout); /* the whole lists before other output (stderr) */
        if (pce->cd) {
            printf("old CD track silenced: %ld:", g_cd_pop_count);
            for (long k = 0; k < g_cd_pop_count && k < 64; k++)
                printf(" %ld", g_cd_pop_list[k]);
            printf("\n");
        }
    }
    if (g_smooth_on) {
        motion_summary();
        if (g_smooth.exact_frames < g_smooth.frames * 99 / 100 || g_smooth_big_ok != g_smooth_big_checked || g_mo_frame_bad) {
            return 1;
        }
    }
    if (g_crop_shot_path) {
        /* --crop-shot: the last frame as the window would show it with the crop (app/crop.h) */
        static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES * 16];
        const Vce *v = &pce->vce;
        /* with --game-fixes, the image with the item window in front */
        const uint32_t *frame = g_window_last && g_window_last->seq == frames && g_window_last->w == v->frame_width &&
                                        g_window_last->h == v->frame_height
                                    ? g_window_last->pixels
                                    : v->frame;
        /* with --crop-log, the state of the whole session (flash); otherwise, only this frame */
        static CropState st;
        if (!menu_font_ready()) /* the game font, for the HUD's NEXT */
            menu_font_capture(pce->vdc.vram);
        if (g_crop_log) {
            st = g_crop;
        } else {
            crop_set_fade(&st, g_fade_frame.map_level, g_fade_frame.map_add, g_fade_frame.table_ok ? g_fade_frame.table : NULL);
            crop_set_fade_stray(&st, g_fade_frame.map_stray);
            crop_set_screen(&st, ys_screen_layout(g_screen.program));
            crop_frame(&st, frame, v->frame_width, v->frame_height);
            st.next_on = 1;
            ys_panel_read(pce, &st.panel);
            st.next = crop_next_exp(&st.panel);
            for (int k = 0; k < 64; k++) /* wide, fill: the pan to the end (in a single image) */
                crop_pan_step(&st, g_crop_shot_mode);
        }
        CropView cv;
        if (!crop_view_state(g_crop_shot_mode, &st, v->frame_width, v->frame_height, &cv)) {
            printf("--crop-shot: no border in this frame\n");
            return 1;
        }
        cv.hud_bg = g_crop_shot_bg;
        cv.darken = st.darken_q;
        int os = crop_out_sub(&cv); /* 4 with the HUD at 3/4 (fill with the TV points) */
        crop_compose_sub(frame, v->frame_width, 1, &cv, img, os);
        crop_darken_view(img, cv.w * os, &cv, os, st.darken_q);
        if (cv.band) {
            CropHudLayout hl; /* the enemy: its bar on the panel (the 2nd) has life */
            crop_hud_layout(cv.panel, &hl);
            printf("--crop-shot: HUD%s%s, pan %d, flash %d\n", hl.bars >= 2 && hl.bar_len[1] > 0 ? " with the enemy" : "",
                   st.scene ? " (scene: whole map)" : "", st.pan_q / 4, st.whiten);
        }
        trace_save_ppm_argb(img, cv.w * os, cv.h * os, g_crop_shot_path);
        printf("--crop-shot: %dx%d in %s\n", cv.w, cv.h, g_crop_shot_path);
    }
    return 0;
}

static const TraceExt ys_trace_ext = { "Ys", YS_HELP, ys_arg, ys_setup, ys_input, ys_frame, ys_done };

int main(int argc, char **argv)
{
    return pce_trace_main(argc, argv, &ys_trace_ext);
}
