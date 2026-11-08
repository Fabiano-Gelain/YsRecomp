#include "app/video.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void video_defaults(VideoSettings *v)
{
    memset(v, 0, sizeof(*v));
    v->fullscreen = 1;
    v->vsync = 1;
    v->window_scale = 3;
    v->smooth_motion = 1;
    v->smooth_fades = 1;
    v->equipment = 1;
    v->enemy_bars = 1;
    v->reward_popups = 1;
    v->instant_dialog = 1;
    /* distribution default (user request, 2026-11-05): Fill with the HUD on the Shadow background, no CRT */
    v->crop = CROP_FILL;
    v->hud_bg = CROP_HUD_SHADOW;
    v->crt = VIDEO_CRT_OFF;
}

int video_same_display(const VideoSettings *a, const VideoSettings *b)
{
    return a->fullscreen == b->fullscreen && a->res_w == b->res_w && a->res_h == b->res_h &&
           a->refresh == b->refresh && a->vsync == b->vsync && a->window_scale == b->window_scale;
}

static const char *const k_crt_names[VIDEO_CRT_COUNT] = { "off",    "scanlines", "mask",     "slot",
                                                          "shadow", "glow",      "glow_mask", "glow_slot" };
static const char *const k_crop_names[CROP_COUNT] = { "off", "frame", "full", "compact", "wide", "fill" };
static const char *const k_aspect_names[VIDEO_ASPECT_COUNT] = { "tv", "square", "stretch" };

/* Index of the text in the list (0 = the first, also for unknown values) */
static int pick_name(const char *text, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
        if (strcmp(text, names[i]) == 0)
            return i;
    return 0;
}

int video_load(Config *cfg, VideoSettings *v)
{
    video_defaults(v);
    v->fullscreen = config_get_int(cfg, "fullscreen", 1, 0, 1);
    v->vsync = config_get_int(cfg, "vsync", 1, 0, 1);
    v->window_scale = config_get_int(cfg, "window_scale", 3, VIDEO_SCALE_MIN, VIDEO_SCALE_MAX);
    v->integer_scale = config_get_int(cfg, "integer_scale", 0, 0, 1);
    v->smooth = strcmp(config_get(cfg, "filter", "sharp"), "smooth") == 0;
    v->crt = pick_name(config_get(cfg, "crt", "off"), k_crt_names, VIDEO_CRT_COUNT);
    v->crt_bright = strcmp(config_get(cfg, "crt_brightness", "normal"), "bright") == 0;
    v->aspect = pick_name(config_get(cfg, "aspect", "tv"), k_aspect_names, VIDEO_ASPECT_COUNT);
    const char *crop = config_get(cfg, "crop", "fill");
    v->crop = strcmp(crop, "hud") == 0 ? CROP_COMPACT : pick_name(crop, k_crop_names, CROP_COUNT); /* hud: the old name */
    if (v->crop == CROP_OFF)
        v->crop = CROP_FRAME; /* no-frame is no longer offered */
    /* the removed backgrounds (plain ones on 2026-10-21; light, metallic, center light and left-to-right ones on
     * 2026-10-25) map to the closest one in today's list */
    static const char *const old_bg[][2] = {
        { "teal", "petrol" },     { "parchment", "leather" }, { "graphite", "charcoal" }, { "blue", "navy" },
        { "purple", "indigo" },   { "forest", "pine" },       { "sea", "petrol" },        { "stone", "slate" },
        { "bronze", "leather" },  { "dusk", "plum" },         { "ember", "oxblood" },     { "steel", "charcoal" },
        { "minea", "pine" },      { "zepik", "walnut" },      { "noltia", "deepsea" },    { "ramia", "plum" },
        { "darm", "slate" },      { "solomon", "leather" },   { "rastine", "leather" },   { "toal", "petrol" },
        { "lava", "oxblood" },    { "shrine", "charcoal" },   { "moondoria", "midnight" }, { "kefin", "indigo" },
        { "barbado", "deepsea" }, { "esteria", "pine" },      { "lance", "plum" },
    };
    const char *bg = config_get(cfg, "hud_background", "shadow");
    for (int i = 0; i < (int)(sizeof(old_bg) / sizeof(old_bg[0])); i++)
        if (strcmp(bg, old_bg[i][0]) == 0)
            bg = old_bg[i][1];
    v->hud_bg = pick_name(bg, crop_hud_names, CROP_HUD_COUNT);
    v->smooth_motion = config_get_int(cfg, "smooth_motion", 1, 0, 2) != 0; /* 2: the former "new" mode */
    v->smooth_fades = config_get_int(cfg, "smooth_fades", 1, 0, 1);
    v->equipment = config_get_int(cfg, "visual_equipment", 1, 0, 1);
    v->enemy_bars = config_get_int(cfg, "enemy_hp_bars", 1, 0, 1);
    v->reward_popups = config_get_int(cfg, "reward_popups", 1, 0, 2);
    v->instant_dialog = config_get_int(cfg, "instant_dialog", 1, 0, 1);
    int w, h;
    char tail;
    if (sscanf(config_get(cfg, "resolution", ""), "%dx%d%c", &w, &h, &tail) == 2 && w >= 320 && h >= 240 &&
        w <= 16384 && h <= 16384) {
        v->res_w = w;
        v->res_h = h;
    }
    if (strcmp(config_get(cfg, "refresh_rate", "auto"), "auto") != 0)
        v->refresh = config_get_int(cfg, "refresh_rate", 0, 24, 1000);
    static const char *const keys[] = { "fullscreen", "resolution",    "refresh_rate", "vsync", "window_scale",
                                        "integer_scale", "filter", "crt", "crt_brightness", "aspect", "crop", "hud_background",
                                        "smooth_motion", "smooth_fades", "visual_equipment", "enemy_hp_bars",
                                        "reward_popups" };
    int added = 0;
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
        added |= config_get(cfg, keys[i], NULL) == NULL;
    if (added)
        video_store(cfg, v);
    return added;
}

void video_store(Config *cfg, const VideoSettings *v)
{
    char text[32];
    config_set_int(cfg, "fullscreen", v->fullscreen);
    if (v->res_w)
        snprintf(text, sizeof(text), "%dx%d", v->res_w, v->res_h);
    else
        snprintf(text, sizeof(text), "desktop");
    config_set(cfg, "resolution", text);
    if (v->refresh)
        config_set_int(cfg, "refresh_rate", v->refresh);
    else
        config_set(cfg, "refresh_rate", "auto");
    config_set_int(cfg, "vsync", v->vsync);
    config_set_int(cfg, "window_scale", v->window_scale);
    config_set_int(cfg, "integer_scale", v->integer_scale);
    config_set(cfg, "filter", v->smooth ? "smooth" : "sharp");
    config_set(cfg, "crt", k_crt_names[v->crt >= 0 && v->crt < VIDEO_CRT_COUNT ? v->crt : 0]);
    config_set(cfg, "crt_brightness", v->crt_bright ? "bright" : "normal");
    config_set(cfg, "aspect", k_aspect_names[v->aspect >= 0 && v->aspect < VIDEO_ASPECT_COUNT ? v->aspect : 0]);
    config_set(cfg, "crop", k_crop_names[v->crop >= 0 && v->crop < CROP_COUNT ? v->crop : 0]);
    config_set(cfg, "hud_background", crop_hud_names[v->hud_bg >= 0 && v->hud_bg < CROP_HUD_COUNT ? v->hud_bg : 0]);
    config_set_int(cfg, "smooth_motion", v->smooth_motion);
    config_set_int(cfg, "smooth_fades", v->smooth_fades);
    config_set_int(cfg, "visual_equipment", v->equipment);
    config_set_int(cfg, "enemy_hp_bars", v->enemy_bars);
    config_set_int(cfg, "reward_popups", v->reward_popups);
    config_set_int(cfg, "instant_dialog", v->instant_dialog);
}

int video_refreshes_per_frame(float hz)
{
    int n = (int)(hz / 60.05f + 0.5f);
    return n >= 1 && n <= 4 && fabsf(hz - n * 60.05f) < 0.015f * hz ? n : 0;
}

static int same_size(const VideoMode *a, int w, int h)
{
    return a->w == w && a->h == h;
}

VideoMode video_fullscreen_mode(const VideoModes *modes, const VideoSettings *v, int *exclusive)
{
    const VideoMode *desk = &modes->desktop;
    int w = desk->w, h = desk->h;
    if (v->res_w) {
        for (int i = 0; i < modes->count; i++)
            if (same_size(&modes->list[i], v->res_w, v->res_h)) {
                w = v->res_w;
                h = v->res_h;
                break;
            }
    }
    int at_desktop = w == desk->w && h == desk->h;
    const VideoMode *pick = NULL;
    if (v->refresh) {
        for (int i = 0; i < modes->count && !pick; i++)
            if (same_size(&modes->list[i], w, h) && (int)lroundf(modes->list[i].hz) == v->refresh)
                pick = &modes->list[i];
        if (!pick && at_desktop && (int)lroundf(desk->hz) == v->refresh)
            pick = desk;
    }
    /* auto: the Windows one, without changing the monitor mode (the single clock, app/pacing.h, is smooth at any
     * rate); at another resolution, the refresh rate closest to the Windows one */
    if (!pick && at_desktop)
        pick = desk;
    for (int i = 0; i < modes->count && !pick; i++)
        if (same_size(&modes->list[i], w, h))
            for (int j = i; j < modes->count; j++) {
                const VideoMode *m = &modes->list[j];
                if (same_size(m, w, h) && (!pick || fabsf(m->hz - desk->hz) < fabsf(pick->hz - desk->hz)))
                    pick = m;
            }
    if (!pick)
        pick = desk;
    *exclusive = !(same_size(pick, desk->w, desk->h) && lroundf(pick->hz) == lroundf(desk->hz));
    return *pick;
}

int video_resolutions(const VideoModes *modes, VideoMode *out, int max)
{
    int n = 0;
    for (int i = 0; i < modes->count; i++) {
        const VideoMode *m = &modes->list[i];
        int k = 0;
        while (k < n && !same_size(&out[k], m->w, m->h))
            k++;
        if (k < n || n >= max)
            continue;
        /* from largest to smallest: by width, then by height */
        for (k = n; k > 0 && (out[k - 1].w < m->w || (out[k - 1].w == m->w && out[k - 1].h < m->h)); k--)
            out[k] = out[k - 1];
        out[k] = *m;
        n++;
    }
    return n;
}

int video_rates(const VideoModes *modes, int w, int h, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < modes->count; i++) {
        if (!same_size(&modes->list[i], w, h))
            continue;
        int hz = (int)lroundf(modes->list[i].hz), k = 0;
        while (k < n && out[k] != hz)
            k++;
        if (k < n || n >= max)
            continue;
        for (k = n; k > 0 && out[k - 1] < hz; k--)
            out[k] = out[k - 1];
        out[k] = hz;
        n++;
    }
    return n;
}

void video_layout(int out_w, int out_h, int src_w, int src_h, int divider, const VideoSettings *v, VideoRect *dst,
                  int *kx, int *ky)
{
    double par = v && v->aspect == VIDEO_ASPECT_SQUARE ? 1.0 : divider / 3.5; /* width/height of a pixel */
    if (v && v->aspect == VIDEO_ASPECT_STRETCH && src_w > 0 && src_h > 0) {
        /* Stretched: the whole output (with integer scale, the largest integer in each direction) */
        *kx = v->integer_scale && out_w >= src_w ? out_w / src_w : (out_w + src_w - 1) / src_w;
        *ky = v->integer_scale && out_h >= src_h ? out_h / src_h : (out_h + src_h - 1) / src_h;
        *kx = *kx < 1 ? 1 : *kx;
        *ky = *ky < 1 ? 1 : *ky;
        dst->w = v->integer_scale && out_w >= src_w ? *kx * src_w : out_w;
        dst->h = v->integer_scale && out_h >= src_h ? *ky * src_h : out_h;
        dst->x = (out_w - dst->w) / 2;
        dst->y = (out_h - dst->h) / 2;
        return;
    }
    if (v && v->integer_scale && src_w > 0 && src_h > 0) {
        for (int n = out_h / src_h; n >= 1; n--) {
            int m = (int)lround(n * par);
            if (m < 1)
                m = 1;
            if (m * src_w <= out_w) {
                *kx = m;
                *ky = n;
                dst->w = m * src_w;
                dst->h = n * src_h;
                dst->x = (out_w - dst->w) / 2;
                dst->y = (out_h - dst->h) / 2;
                return;
            }
        }
    }
    double aspect = src_w * par / src_h; /* width/height of the image */
    int w = out_w, h = (int)lround(out_w / aspect);
    if (h > out_h) {
        h = out_h;
        w = (int)lround(out_h * aspect);
    }
    dst->w = w;
    dst->h = h;
    dst->x = (out_w - w) / 2;
    dst->y = (out_h - h) / 2;
    *kx = (w + src_w - 1) / src_w;
    *ky = (h + src_h - 1) / src_h;
    if (*kx < 1)
        *kx = 1;
    if (*ky < 1)
        *ky = 1;
}

void video_window_size(const VideoSettings *v, int *w, int *h)
{
    *w = v->aspect == VIDEO_ASPECT_SQUARE ? v->window_scale * 256 : (int)lround(v->window_scale * 256 * 4 / 3.5);
    *h = v->window_scale * 242;
}

/* Full crop (app/crop.h): the panel at the frame mode size, centered in the strip below the map */
void crop_hud_place(int ow, int oh, int divider, const VideoSettings *v, const VideoRect *full, VideoRect *panel)
{
    /* At the frame mode size: what the panel would measure on screen with the frame crop */
    VideoRect fr;
    int kx, ky;
    video_layout(ow, oh, CROP_PANEL_W, CROP_FRAME_VIEW_H, divider, v, &fr, &kx, &ky);
    int pw = fr.w, ph = fr.h * CROP_PANEL_H / CROP_FRAME_VIEW_H;
    int strip_y = full->y + full->h * CROP_MAP_H / (CROP_MAP_H + CROP_HUD_H), strip_h = full->y + full->h - strip_y;
    if (pw > full->w || ph > strip_h) { /* does not fit (narrow screen): shrinks to fit */
        double k = (double)full->w / pw < (double)strip_h / ph ? (double)full->w / pw : (double)strip_h / ph;
        pw = (int)(pw * k);
        ph = (int)(ph * k);
    }
    panel->w = pw;
    panel->h = ph;
    panel->x = full->x + (full->w - pw) / 2;
    panel->y = strip_y + (strip_h - ph) / 2;
}
