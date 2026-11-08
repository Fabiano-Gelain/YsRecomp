#include "app/crop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/menu_font.h" /* the game's 8x8 font (NEXT and Ys II's max H.P) */
#include "app/video.h" /* VideoRect */
#include "app/ys_fade.h" /* ys_fade_channel */
#include "pce/vce.h"   /* VCE_MAX_WIDTH, VCE_ACTIVE_LINES */

/* Measurements of the game image (app/crop.h) */
#define FRAME_LEFT 8      /* border outline columns */
#define FRAME_RIGHT 327
#define FRAME_TOP 11      /* outline lines */
#define FRAME_MAP_TOP 25
#define FRAME_MAP_BOTTOM 156
#define FRAME_BOTTOM 170
#define MAP_X 24          /* map: 288 x 128 */
#define MAP_Y 27
#define MAP_W 288
#define MAP_H 128
#define PANEL_TOP 171     /* 171-178 black; panel 179-219 */
#define PANEL_Y 179
#define PANEL_H 41
#define PANEL_W CROP_PANEL_W /* columns 8-327, as wide as the border */
#define LOAD_INNER_BOTTOM 218 /* load screen: inner area down to 218, outlines on lines 220 and 234 */
#define LOAD_BOTTOM 234
#define LOAD_MID_Y 37         /* Frame: the middle (181 lines from here) between the two edges */
#define FRAME_BELOW 4         /* Frame: black breathing-room lines below the panel */
#define FRAME_VIEW_H CROP_FRAME_VIEW_H
#define MIN_W 336
#define MIN_H 220

static int is_black(uint32_t p) { return (p & 0xFFFFFF) == 0; }

static int row_is(const uint32_t *px, int w, int y, int x0, int x1, uint32_t c)
{
    for (int x = x0; x <= x1; x++)
        if ((px[y * w + x] & 0xFFFFFF) != c)
            return 0;
    return 1;
}

static int col_is(const uint32_t *px, int w, int x, int y0, int y1, uint32_t c)
{
    for (int y = y0; y <= y1; y++)
        if ((px[y * w + x] & 0xFFFFFF) != c)
            return 0;
    return 1;
}

/* The border with the inner area (from line MAP_Y, columns MAP_X..MAP_X+MAP_W-1) down to line
 * inner_bottom, bottom outline of the area at inner_bottom + 1 and outer outline at bottom: single-color
 * outlines, black line around the inner area and black around the border */
static int frame_shape(const uint32_t *px, int w, int h, int inner_bottom, int bottom)
{
    uint32_t c = px[FRAME_TOP * w + FRAME_LEFT + 1] & 0xFFFFFF;
    if (c == 0 || bottom >= h)
        return 0;
    if (!row_is(px, w, FRAME_TOP, FRAME_LEFT + 1, FRAME_RIGHT - 1, c) ||
        !row_is(px, w, FRAME_MAP_TOP, MAP_X - 1, MAP_X + MAP_W, c) ||
        !row_is(px, w, inner_bottom + 2, MAP_X - 1, MAP_X + MAP_W, c) ||
        !row_is(px, w, bottom, FRAME_LEFT + 1, FRAME_RIGHT - 1, c) ||
        !col_is(px, w, FRAME_LEFT, FRAME_TOP + 1, bottom - 1, c) ||
        !col_is(px, w, FRAME_RIGHT, FRAME_TOP + 1, bottom - 1, c))
        return 0;
    for (int y = 0; y < FRAME_TOP; y++)
        if (!row_is(px, w, y, 0, w - 1, 0))
            return 0;
    for (int x = 0; x < FRAME_LEFT; x++)
        if (!col_is(px, w, x, 0, h - 1, 0))
            return 0;
    for (int x = FRAME_RIGHT + 1; x < w; x++)
        if (!col_is(px, w, x, 0, h - 1, 0))
            return 0;
    return row_is(px, w, MAP_Y - 1, MAP_X - 1, MAP_X + MAP_W, 0) &&
           row_is(px, w, inner_bottom + 1, MAP_X - 1, MAP_X + MAP_W, 0) &&
           col_is(px, w, MAP_X - 1, MAP_Y - 1, inner_bottom + 1, 0) &&
           col_is(px, w, MAP_X + MAP_W, MAP_Y - 1, inner_bottom + 1, 0);
}

int crop_detect(const uint32_t *px, int w, int h)
{
    int any = 0;
    for (int i = 0; i < w * h && !any; i++)
        any = !is_black(px[i]);
    if (!any)
        return -1;
    if (w < MIN_W || h < MIN_H)
        return CROP_SCREEN_NONE;
    /* Game: the map, the border down to line 170 and the panel below (lines 171-178 black) */
    if (frame_shape(px, w, h, MAP_Y + MAP_H - 1, FRAME_BOTTOM)) {
        for (int y = PANEL_TOP; y < PANEL_Y; y++)
            if (!row_is(px, w, y, 0, w - 1, 0))
                return CROP_SCREEN_NONE;
        return CROP_SCREEN_GAME;
    }
    /* Game loading: the border down to line 234, no panel, and black below it */
    if (frame_shape(px, w, h, LOAD_INNER_BOTTOM, LOAD_BOTTOM)) {
        for (int y = LOAD_BOTTOM + 1; y < h; y++)
            if (!row_is(px, w, y, 0, w - 1, 0))
                return CROP_SCREEN_NONE;
        return CROP_SCREEN_LOAD;
    }
    return CROP_SCREEN_NONE;
}


void crop_band_size(int mode, double par, double screen, int *w, int *map_h, int *band_h)
{
    /* full: the whole map and the band as tall as the game's panel, with the two-line HUD in its middle */
    int fw = MAP_W, fm = MAP_H, bh = mode == CROP_FULL ? CROP_HUD_H : CROP_BAND_H;
    par = par > 0.0 ? par : 6.0 / 7.0;
    screen = screen == 0.0 ? 16.0 / 9.0 : screen;
    if (screen > 0.0 && (mode == CROP_WIDE || mode == CROP_FILL)) {
        /* fill: the screen's aspect ratio; wider than it, columns go; taller, map lines */
        if (MAP_W * par / CROP_BAND_VIEW_H > screen)
            fw = (int)(screen * CROP_BAND_VIEW_H / par) / 2 * 2;
        else
            fm = ((int)(MAP_W * par / screen) - CROP_BAND_H) / 2 * 2;
        fw = fw < CROP_BAND_MIN_W ? CROP_BAND_MIN_W : fw;
        fm = fm < CROP_BAND_MIN_MAP_H ? CROP_BAND_MIN_MAP_H : fm;
        if (mode == CROP_WIDE) { /* half the crop */
            fw = (MAP_W + fw) / 4 * 2;
            fm = (MAP_H + fm) / 4 * 2;
        } else if (fm < MAP_H) {
            /* fill cropping lines: the whole map (as far as possible) and the lower band, with the half-size HUD */
            int total = (int)(MAP_W * par / screen) / 2 * 2;
            fm = total - CROP_BAND_SMALL_H;
            fm = fm > MAP_H ? MAP_H : fm < CROP_BAND_MIN_MAP_H ? CROP_BAND_MIN_MAP_H : fm;
            bh = total - fm < CROP_BAND_SMALL_H ? CROP_BAND_SMALL_H : total - fm;
        }
    }
    *w = fw;
    *map_h = fm;
    if (band_h)
        *band_h = bh;
}

int crop_small_hud(int mode, double par, double screen)
{
    int bw, bm, bh;
    if (mode != CROP_FILL)
        return 0;
    crop_band_size(mode, par, screen, &bw, &bm, &bh);
    return bh < CROP_BAND_H;
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* The middle of a0-a1 (a whole pixel: the edges of the windows at 3/4 fall on whole quarter pixels) and the
 * lines or columns a0-a1 after shrinking to 3/4 around point c */
static int shrink_mid(int a0, int a1)
{
    return (a0 + a1 + 1) / 2;
}

static void shrink_span(int c, int *a0, int *a1)
{
    *a0 = c + floor_div((*a0 - c) * 3, 4);
    *a1 = c - 1 - floor_div(-(*a1 + 1 - c) * 3, 4);
}

/* The box the windows r take together */
static void rects_box(const CropRect *r, int n, CropRect *box)
{
    *box = (CropRect){ MAP_W, -1, MAP_H, -1 };
    for (int i = 0; i < n; i++) {
        box->x0 = r[i].x0 < box->x0 ? r[i].x0 : box->x0;
        box->x1 = r[i].x1 > box->x1 ? r[i].x1 : box->x1;
        box->y0 = r[i].y0 < box->y0 ? r[i].y0 : box->y0;
        box->y1 = r[i].y1 > box->y1 ? r[i].y1 : box->y1;
    }
}

/* Slide (in 1/4 pixel) that makes the window a0-a1 fit in the middle size pixels of full. A window that already fits
 * does not slide (the SELECT menu, from line 9, fits in fill with TV pixels, which shows from line 8: sliding 1 pixel
 * for slack and back when opening the inventory made the menu shake); a cropped one slides until it has up to 2 pixels
 * of slack (the inventory, lines 9-118, fits tightly in the 112) */
static int pan_target(int a0, int a1, int size, int full)
{
    int c0 = (full - size) / 2, m = (size - (a1 - a0 + 1)) / 2;
    if (a0 >= c0 && a1 <= c0 + size - 1)
        return 0;
    m = m > 2 ? 2 : m;
    a0 += 2 - m; /* below, the slack becomes m */
    a1 -= 2 - m;
    if (a0 - 2 < c0)
        return (a0 - 2 - c0) * 4;
    if (a1 + 2 > c0 + size - 1)
        return (a1 + 2 - (c0 + size - 1)) * 4;
    return 0;
}

static int pan_move(int q, int target)
{
    if (q < target)
        return q + CROP_PAN_STEP_Q < target ? q + CROP_PAN_STEP_Q : target;
    return q - CROP_PAN_STEP_Q > target ? q - CROP_PAN_STEP_Q : target;
}

void crop_pan_step(CropState *s, int mode)
{
    int bw, bm, tx = 0, ty = 0;
    crop_band_size(mode, s->par, s->screen, &bw, &bm, NULL);
    s->scene = s->scene_hold && (mode == CROP_WIDE || mode == CROP_FILL);
    if (s->window && !s->scene && (mode == CROP_WIDE || mode == CROP_FILL)) {
        /* Scene (game window with no saved background, until the darkening: scene_hold), window touching the map
         * edge (GAME OVER, wide lists) or too large: whole map and the HUD. Otherwise, slides the minimum for it to
         * fit (in fill with the HUD at 3/4, the window already shrunk) */
        int x0 = s->win_x0, x1 = s->win_x1, y0 = s->win_y0, y1 = s->win_y1;
        int small = crop_small_hud(mode, s->par, s->screen);
        if (s->grp_n && small) {
            CropRect b;
            rects_box(s->grp_r, s->grp_n, &b);
            x0 = b.x0 - 1; /* with the black outline (crop_view_state) */
            x1 = b.x1 + 1;
            y0 = b.y0 - 1;
            y1 = b.y1 + 1;
            shrink_span(s->grp_cx, &x0, &x1);
            shrink_span(s->grp_cy, &y0, &y1);
        }
        if (s->win_x0 <= 1 || s->win_x1 >= MAP_W - 2 || x1 - x0 + 1 > bw || y1 - y0 + 1 > bm) {
            s->scene = 1;
        } else if (small && !s->grp_n) {
            /* HUD at 3/4: no open window to shrink (the game's closed and the box is only kept until the next one,
             * CROP_WINDOW_OFF_FRAMES): the view stays where it is. It used to follow the box at full size, which the
             * shrunk window never had: in the lines at the end of Ys II (windows at the top of the map, one per line)
             * the whole screen went down and up at each line */
            tx = s->pan_q;
            ty = s->pan_qy;
        } else {
            tx = pan_target(x0, x1, bw, MAP_W);
            ty = pan_target(y0, y1, bm, MAP_H);
        }
    }
    if (s->scene || !(mode == CROP_WIDE || mode == CROP_FILL)) {
        s->pan_q = s->pan_qy = 0;
        return;
    }
    s->pan_q = pan_move(s->pan_q, tx);
    s->pan_qy = pan_move(s->pan_qy, ty);
}

/* ---- Flash: at the end of Ys I the whole screen (map, border, panel and the black around) goes to white, each
 * channel gaining one level (of 7) every ~6 frames, up to 7 ---- */

/* Flash: each channel gains whiten levels (up to 7) */
static void white_table(uint8_t lut[256], int whiten)
{
    for (int v = 0; v < 256; v++) {
        int l = (v * 7 + 127) / 255 + whiten;
        lut[v] = (uint8_t)(l >= 7 ? 255 : (l * 255 + 3) / 7);
    }
}

/* The flash undone: a channel at 7 stays (it was 7, like the white of the text and the bars; what went past 7 does
 * not come back, but brightened again it comes out the same), the others lose whiten levels */
static void unwhite_table(uint8_t lut[256], int whiten)
{
    for (int v = 0; v < 256; v++) {
        int l = (v * 7 + 127) / 255;
        l = l >= 7 ? 7 : l > whiten ? l - whiten : 0;
        lut[v] = (uint8_t)((l * 255 + 3) / 7);
    }
}

static uint32_t lut_pixel(uint32_t p, const uint8_t lut[256])
{
    return (p & 0xFF000000u) | (uint32_t)lut[p >> 16 & 0xFF] << 16 | (uint32_t)lut[p >> 8 & 0xFF] << 8 | lut[p & 0xFF];
}

void crop_whiten(uint32_t *px, int stride, int w, int h, int whiten)
{
    if (whiten <= 0)
        return;
    uint8_t lut[256];
    white_table(lut, whiten);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[(size_t)y * stride + x] = lut_pixel(px[(size_t)y * stride + x], lut);
}

static uint32_t at(const uint32_t *src, int src_w, int sub, int x, int y);

/* The panel lines of src (sub times larger) with the flash undone, in an image of 1 pixel per pixel (width
 * src_w; only the panel lines are valid) */
static const uint32_t *unwhite_panel(const uint32_t *src, int src_w, int sub, int whiten)
{
    static uint32_t buf[VCE_MAX_WIDTH * (PANEL_Y + PANEL_H)];
    uint8_t lut[256];
    unwhite_table(lut, whiten);
    for (int y = PANEL_Y; y < PANEL_Y + PANEL_H; y++)
        for (int x = 0; x < src_w && x < VCE_MAX_WIDTH; x++)
            buf[y * src_w + x] = lut_pixel(0xFF000000u | at(src, src_w, sub, x, y), lut);
    return buf;
}

const uint32_t *crop_unwhiten_panel(const uint32_t *src, int src_w, int whiten)
{
    return whiten > 0 && src_w <= VCE_MAX_WIDTH ? unwhite_panel(src, src_w, 1, whiten) : src;
}

/* The game's fade table (crop_set_fade; the same in all the game's programs) */
static uint8_t g_fade_table[64];
static int g_fade_table_ok;

void crop_set_fade(CropState *s, int level, int add, const uint8_t *table)
{
    s->ys_dark = level < 0 ? 7 : level > 7 ? 0 : 7 - level;
    s->ys_add = add < 0 ? 0 : add > 7 ? 7 : add;
    if (table) {
        memcpy(g_fade_table, table, sizeof(g_fade_table));
        g_fade_table_ok = 1;
    }
}

void crop_set_fade_stray(CropState *s, int stray)
{
    s->ys_stray = stray;
}

void crop_set_screen(CropState *s, int screen)
{
    s->ys_screen = screen == CROP_SCREEN_GAME || screen == CROP_SCREEN_LOAD ? screen : CROP_SCREEN_NONE;
}

static int in_rects(const CropRect *r, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        if (x >= r[i].x0 && x <= r[i].x1 && y >= r[i].y0 && y <= r[i].y1)
            return 1;
    return 0;
}

void crop_set_windows(CropState *s, const int (*boxes)[4], int n, int scene)
{
    /* the window's box goes to the last pixel of its cells; the drawn border sits 1 pixel inward (the outer pixel
     * of the border pieces is transparent): the crop uses the drawn one, with the black outline outside */
    s->ys_scene = scene;
    s->ys_n = 0;
    for (int k = 0; k < n && s->ys_n < CROP_WINDOWS_MAX; k++) {
        CropRect r = { boxes[k][0] - MAP_X + 1, boxes[k][2] - MAP_X - 1, boxes[k][1] - MAP_Y + 1, boxes[k][3] - MAP_Y - 1 };
        r.x0 = r.x0 < 0 ? 0 : r.x0;
        r.y0 = r.y0 < 0 ? 0 : r.y0;
        r.x1 = r.x1 > MAP_W - 1 ? MAP_W - 1 : r.x1;
        r.y1 = r.y1 > MAP_H - 1 ? MAP_H - 1 : r.y1;
        if (r.x0 <= r.x1 && r.y0 <= r.y1)
            s->ys_r[s->ys_n++] = r;
    }
}

/* The open windows (CropState.grp_r) = the game's; the middle stays that of the first ones (the top window shrinks
 * around it, and the lower ones do not jump when it opens) */
static void win_group(CropState *s, int n)
{
    int was = s->grp_n;
    memcpy(s->grp_r, s->ys_r, (size_t)n * sizeof(s->grp_r[0]));
    s->grp_n = n;
    if (n && !was) {
        CropRect b;
        rects_box(s->grp_r, n, &b);
        s->grp_cx = shrink_mid(b.x0, b.x1);
        s->grp_cy = shrink_mid(b.y0, b.y1);
    }
}

void crop_ring(CropState *s, const uint32_t *bg, const uint32_t *spr)
{
    s->bg_px = s->grp_n ? bg : NULL;
    s->spr_px = s->grp_n ? spr : NULL;
}

int crop_ring_boxes(const CropState *s, int boxes[CROP_WINDOWS_MAX][4])
{
    /* in a scene (shop, portrait) the windows do not shrink: no redraw_ring (it removed the masks touching a window,
     * and in the shop they are what makes the choice window cover the shopkeeper's drawing) */
    if (s->scene)
        return 0;
    for (int i = 0; i < s->grp_n; i++) {
        boxes[i][0] = MAP_X + s->grp_r[i].x0;
        boxes[i][1] = MAP_Y + s->grp_r[i].y0;
        boxes[i][2] = MAP_X + s->grp_r[i].x1;
        boxes[i][3] = MAP_Y + s->grp_r[i].y1;
    }
    return s->grp_n;
}

static void frame_levels(CropState *s, const uint32_t *px, int w, int h)
{
    (void)px;
    s->bg_px = s->spr_px = s->fix_px = NULL;
    s->frames++;
    /* the screen the game program shows (crop_set_screen); the black screen, the border being drawn and the delayed
     * screen split are in the same program (the game's fade, below, leaves the rest dark with the map) */
    if (s->white_out && s->ys_screen != s->white_screen)
        s->white_out = 0;
    s->active = s->white_out ? CROP_SCREEN_NONE : s->ys_screen;
    /* Flash (the levels the game added, crop_set_fade): the crop goes on (without it the border vanishes at the
     * first step), the HUD comes from the panel with the flash undone and brightens along (crop_compose); with the
     * image all white, it turns off at once and until the game switches programs (the next scene, without the border,
     * starts from white still in the game program) */
    int white = s->active == CROP_SCREEN_GAME ? s->ys_add : 0;
    if (white == 7 && s->whiten) {
        s->active = CROP_SCREEN_NONE;
        s->white_out = 1;
        s->white_screen = s->ys_screen;
        s->window = 0;
    }
    s->whiten = white > 0 && white < 7 ? white : 0;
    s->framed += s->active == CROP_SCREEN_GAME;
    if (s->whiten) {
        s->darken = 0;
        return;
    }
    /* the game's windows (crop_set_windows); their union stays up to CROP_WINDOW_OFF_FRAMES frames with none */
    int wn = s->active == CROP_SCREEN_GAME ? s->ys_n : 0;
    if (s->active == CROP_SCREEN_GAME && w >= MIN_W && h >= MIN_H) {
        win_group(s, wn);
    } else {
        s->grp_n = 0;
    }
    if (wn) {
        CropRect b;
        rects_box(s->ys_r, wn, &b);
        s->window = 1;
        s->window_misses = 0;
        s->win_x0 = b.x0;
        s->win_x1 = b.x1;
        s->win_y0 = b.y0;
        s->win_y1 = b.y1;
    } else if (s->window && (s->active != CROP_SCREEN_GAME || ++s->window_misses >= CROP_WINDOW_OFF_FRAMES)) {
        s->window = 0;
    }
    /* Fade (read from the game, crop_set_fade; it used to be guessed from the map pixels): the game takes the map
     * colors from one level of 7 to the next, one step every 4 frames, and leaves the border and the panel lit; outside
     * the map, the window darkens by the same level */
    s->darken = s->active == CROP_SCREEN_GAME && w >= MIN_W && h >= MIN_H ? s->ys_dark : 0;
}

void crop_frame(CropState *s, const uint32_t *px, int w, int h)
{
    frame_levels(s, px, w, h);
    /* Smooth fade: the steps pass continuously, like the map colors (fade.h: the same pacing) */
    s->darken_q = fade_track(&s->darken_t, s->darken, s->smooth_fades ? 1 : 0);
    /* scene = game window with no saved background (portrait, fullscreen); after it closes, the portrait stays
     * drawn until the game darkens the screen (user's request: the scene ends at the darkening), with the image already
     * all black (the smooth fade's too): at the start of the fade the switch to the band looked like a zoom; with no
     * colors lit outside the fade (the GAME OVER text stays lit with the map level at black). The scroll screen only
     * opens as a window with no background while the smooth fade is still ending: the scene waits for the image to
     * turn black (the whole map showed the border outline darkening) */
    int fading_out = s->darken >= 7 && s->darken_q < 7 * FADE_Q && !s->ys_stray;
    if (s->ys_scene && s->active == CROP_SCREEN_GAME) {
        if (s->scene_hold || !fading_out) {
            s->scene_hold = 1;
            s->scene_held = 0;
        }
    } else if (s->scene_hold && (s->active != CROP_SCREEN_GAME ||
                                 (s->darken >= 7 && s->darken_q >= 7 * FADE_Q && !s->ys_stray) ||
                                 s->whiten || ++s->scene_held > CROP_SCENE_HOLD_MAX)) {
        s->scene_hold = 0;
    }
}

static void dark_table(uint8_t lut[256], int darken);

void crop_darken(uint32_t *px, int stride, int w, int h, int sub, int map_x, int map_y, int darken)
{
    if (darken <= 0)
        return;
    if (darken > 7 * FADE_Q)
        darken = 7 * FADE_Q;
    uint8_t lut[256];
    dark_table(lut, darken);
    int mx0 = map_x * sub, mx1 = (map_x + MAP_W) * sub, my0 = map_y * sub, my1 = (map_y + MAP_H) * sub;
    for (int y = 0; y < h * sub; y++) {
        uint32_t *row = px + (size_t)y * stride;
        int in_map_rows = map_y != CROP_NO_MAP && y >= my0 && y < my1;
        for (int x = 0; x < w * sub; x++) {
            if (in_map_rows && x >= mx0 && x < mx1) {
                x = mx1 - 1;
                continue;
            }
            uint32_t p = row[x];
            row[x] = (p & 0xFF000000u) | (uint32_t)lut[p >> 16 & 0xFF] << 16 | (uint32_t)lut[p >> 8 & 0xFF] << 8 |
                     lut[p & 0xFF];
        }
    }
}

/* Floor of q/4 (slide in 1/4 pixel -> whole pixels) */
static int quarter_floor(int q)
{
    return q >= 0 ? q / 4 : -((-q + 3) / 4);
}

/* Compact, wide, fill: the middle columns and lines of the map (crop_band_size), slid by pan_q/pan_qy (1/4
 * pixel: the whole part goes into the crop; the rest, in crop_compose, with smooth motion), and the HUD band
 * below */
static void band_view(int mode, double par, double screen, int pan_q, int pan_qy, CropView *v)
{
    int bw, bm, bh;
    crop_band_size(mode, par, screen, &bw, &bm, &bh);
    int c0 = (MAP_W - bw) / 2, r0 = (MAP_H - bm) / 2;
    int px = quarter_floor(pan_q), py = quarter_floor(pan_qy);
    int qx = pan_q - px * 4, qy = pan_qy - py * 4; /* 0-3 */
    if (px < -c0 || px >= c0) { /* at the limit, without the 1/4 pixel */
        px = px < -c0 ? -c0 : c0;
        qx = 0;
    }
    if (py < -r0 || py >= r0) {
        py = py < -r0 ? -r0 : r0;
        qy = 0;
    }
    v->w = bw;
    v->h = bm + bh;
    v->hud_q = bh < CROP_BAND_H ? CROP_HUD_SMALL_Q : 0;
    v->count = 1;
    v->piece[0] = (CropPiece){ MAP_X + c0 + px, MAP_Y + r0 + py, bw, bm, 0, 0 };
    v->map_x = -(c0 + px);
    v->map_y = -(r0 + py);
    v->band_y = bm;
    v->band = 1;
    v->pan_q = qx;
    v->pan_qy = qy;
}

void crop_darken_view(uint32_t *px, int stride, const CropView *v, int sub, int darken)
{
    if (v->band) /* the shown map is only what is above the band (with TV pixels, wide and fill crop lines) */
        crop_darken(px + (size_t)v->band_y * sub * stride, stride, v->w, v->h - v->band_y, sub, CROP_NO_MAP,
                    CROP_NO_MAP, darken);
    else
        crop_darken(px, stride, v->w, v->h, sub, v->map_x, v->map_y, darken);
}

int crop_view(int mode, int active, int w, int h, CropView *v)
{
    memset(v, 0, sizeof(*v));
    if (!active || w < MIN_W || h < MIN_H)
        return 0;
    int full_w = FRAME_RIGHT - FRAME_LEFT + 1;
    if (active == CROP_SCREEN_LOAD && mode != CROP_OFF) {
        /* In every mode (user's request: also in those without the border; the zoom changes in the fade when entering
         * the game), the game's size in frame with the whole border: top edge (lines 11-26), the middle with all the
         * text (37-217; only black area goes) and bottom edge (219-234) */
        int edge = MAP_Y - FRAME_TOP, mid = FRAME_VIEW_H - 2 * edge;
        v->w = full_w;
        v->h = FRAME_VIEW_H;
        v->count = 3;
        v->piece[0] = (CropPiece){ FRAME_LEFT, FRAME_TOP, full_w, edge, 0, 0 };
        v->piece[1] = (CropPiece){ FRAME_LEFT, LOAD_MID_Y, full_w, mid, 0, edge };
        v->piece[2] = (CropPiece){ FRAME_LEFT, LOAD_BOTTOM + 1 - edge, full_w, edge, 0, edge + mid };
        return 1;
    }
    if (mode == CROP_FRAME) {
        /* The border and the panel, with FRAME_BELOW black breathing-room lines below it */
        v->w = full_w;
        v->h = FRAME_VIEW_H;
        v->count = 1;
        v->piece[0] = (CropPiece){ FRAME_LEFT, FRAME_TOP, v->w, v->h, 0, 0 };
        v->fix_bars = 1;
        v->map_x = MAP_X - FRAME_LEFT;
        v->map_y = MAP_Y - FRAME_TOP;
        return 1;
    }
    if (CROP_IS_BAND(mode)) {
        band_view(mode, 0.0, 0.0, 0, 0, v);
        return 1;
    }
    return 0;
}

int crop_view_state(int mode, const CropState *s, int w, int h, CropView *v)
{
    if (!crop_view(mode, s->active, w, h, v))
        return 0;
    v->whiten = s->whiten;
    v->next_on = s->next_on;
    v->next = s->next;
    v->panel = s->panel.valid ? &s->panel : NULL;
    if (v->band) {
        band_view(mode, s->par, s->screen, s->pan_q, s->pan_qy, v);
        if (s->scene) {
            /* scene: the whole map and, below, the mode's HUD band (the image gets a bit smaller) */
            int bh = v->h - v->band_y;
            v->w = MAP_W;
            v->h = MAP_H + bh;
            v->piece[0] = (CropPiece){ MAP_X, MAP_Y, MAP_W, MAP_H, 0, 0 };
            v->map_x = v->map_y = 0;
            v->band_y = MAP_H;
            v->pan_q = v->pan_qy = 0;
        } else if (s->grp_n && s->active == CROP_SCREEN_GAME && crop_small_hud(mode, s->par, s->screen)) {
            /* the open windows at 3/4, around the middle of the 1st, with the 1-pixel black outline outside the
             * white edge */
            for (int i = 0; i < s->grp_n; i++)
                v->shrink[i] = (CropRect){ s->grp_r[i].x0 > 0 ? s->grp_r[i].x0 - 1 : 0,
                                           s->grp_r[i].x1 < MAP_W - 1 ? s->grp_r[i].x1 + 1 : MAP_W - 1,
                                           s->grp_r[i].y0 > 0 ? s->grp_r[i].y0 - 1 : 0,
                                           s->grp_r[i].y1 < MAP_H - 1 ? s->grp_r[i].y1 + 1 : MAP_H - 1 };
            v->shrink_n = s->grp_n;
            v->shrink_cx = s->grp_cx;
            v->shrink_cy = s->grp_cy;
            /* around: the background without the windows (crop_ring: VRAM from before them, redraw_ring); black only
             * without it */
            v->shrink_map = s->bg_px ? s->bg_px + (size_t)MAP_Y * w + MAP_X : NULL;
            v->shrink_map_w = w;
            v->shrink_spr = s->spr_px;
            v->shrink_spr_w = w;
            v->shrink_fix = s->fix_px;
        }
    }
    return 1;
}

void crop_map_out(const CropView *v, int out_sub, int *ox, int *oy, int clip[4])
{
    const CropPiece *p = &v->piece[0];
    *ox = (p->dx - p->sx) * out_sub;
    *oy = (p->dy - p->sy) * out_sub;
    clip[0] = p->dx * out_sub;
    clip[1] = p->dy * out_sub;
    clip[2] = (p->dx + p->w) * out_sub;
    clip[3] = (p->dy + p->h) * out_sub;
}

/* The windows of v at 3/4 (crop_compose_sub, after the pieces): each pixel of a window becomes 3/4 of a pixel
 * (3 output pixels with out_sub 4) around (shrink_cx, shrink_cy); what the windows covered and was left out
 * shows the map from before them. Opaque (in the GPU crop, the src map is transparent) */
static void shrink_windows(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst, int out_sub)
{
    enum { MAX_OUT = MAP_W * 8 };
    static int col_s[MAX_OUT], col_p[MAX_OUT], col_d[MAX_OUT], row_s[MAX_OUT], row_p[MAX_OUT], row_d[MAX_OUT];
    const CropPiece *p = &v->piece[0];
    int ow = v->w * out_sub, k = out_sub / sub;
    int x0 = MAP_W, x1 = -1, y0 = MAP_H, y1 = -1;
    for (int i = 0; i < v->shrink_n; i++) {
        const CropRect *r = &v->shrink[i];
        x0 = r->x0 < x0 ? r->x0 : x0;
        x1 = r->x1 > x1 ? r->x1 : x1;
        y0 = r->y0 < y0 ? r->y0 : y0;
        y1 = r->y1 > y1 ? r->y1 : y1;
    }
    /* the map point at the start of the piece, in output pixels (with the slide, like crop_compose_sub) */
    int ox = (p->sx - MAP_X) * out_sub + v->pan_q * sub / 4 * k, oy = (p->sy - MAP_Y) * out_sub + v->pan_qy * sub / 4 * k;
    int cx = v->shrink_cx * out_sub, cy = v->shrink_cy * out_sub;
    int xa = x0 * out_sub - ox, xb = (x1 + 1) * out_sub - ox, ya = y0 * out_sub - oy, yb = (y1 + 1) * out_sub - oy;
    xa = xa < 0 ? 0 : xa;
    ya = ya < 0 ? 0 : ya;
    xb = xb > p->w * out_sub ? p->w * out_sub : xb;
    yb = yb > p->h * out_sub ? p->h * out_sub : yb;
    if (xb - xa > MAX_OUT)
        xb = xa + MAX_OUT;
    if (yb - ya > MAX_OUT)
        yb = ya + MAX_OUT;
    if (xa >= xb || ya >= yb)
        return;
    /* where each column and line comes from: the window pixel (col_p), the src pixel (col_s, in 1/sub) and the map
     * pixel shown there (col_d) */
    for (int x = xa; x < xb; x++) {
        int m = ox + x, so = cx + floor_div((m - cx) * 4, 3);
        col_p[x - xa] = floor_div(so, out_sub);
        col_s[x - xa] = floor_div(so, k);
        col_d[x - xa] = floor_div(m, out_sub);
    }
    for (int y = ya; y < yb; y++) {
        int m = oy + y, so = cy + floor_div((m - cy) * 4, 3);
        row_p[y - ya] = floor_div(so, out_sub);
        row_s[y - ya] = floor_div(so, k);
        row_d[y - ya] = floor_div(m, out_sub);
    }
    const uint32_t *map = v->shrink_map, *spr = v->shrink_spr;
    int sw = src_w * sub;
    for (int y = ya; y < yb; y++) {
        int sp = row_p[y - ya], dp = row_d[y - ya];
        const uint32_t *srow = src + (size_t)(MAP_Y * sub + row_s[y - ya]) * sw + MAP_X * sub;
        uint32_t *d = dst + (size_t)(p->dy * out_sub + y) * ow + p->dx * out_sub;
        for (int x = xa; x < xb; x++) {
            int cp = col_p[x - xa], cd = col_d[x - xa];
            if (in_rects(v->shrink, v->shrink_n, cp, sp)) {
                uint32_t f = v->shrink_fix ? v->shrink_fix[(size_t)(MAP_Y + sp) * v->shrink_spr_w + MAP_X + cp] : 0;
                d[x] = (f >> 24 ? f : srow[col_s[x - xa]]) | 0xFF000000u;
            }
            else if (in_rects(v->shrink, v->shrink_n, cd, dp)) {
                uint32_t c = map ? map[(size_t)dp * v->shrink_map_w + cd] : 0xFF000000u;
                if (spr) { /* the current sprites: in front of the background, or behind it where it is transparent */
                    uint32_t q = spr[(size_t)(MAP_Y + dp) * v->shrink_spr_w + MAP_X + cd];
                    if (q >> 24 == 0xFF || (q >> 24 == 0x80 && c >> 24 == 0x80))
                        c = q;
                }
                d[x] = c | 0xFF000000u;
            }
        }
    }
}

/* ---- Panel ---- */

#define WHITE 0xFFFFFFu
/* Backgrounds: two colors from the console palette (3 bits per channel) in a gradient from top (a) to bottom (b). Chosen
 * by the user (2026-10-25, in the preview with the real HUD) to match the gold of the labels (H.P, M.P, EXP, GOLD):
 * flat black and dark gradients, almost all ending in black; the lighter and more saturated ones were dropped */
static const struct {
    uint32_t a, b;
} k_hud_bg[CROP_HUD_COUNT] = {
    { 0x000000u, 0x000000u }, { 0x484848u, 0x000000u }, { 0x242448u, 0x000000u }, /* black, charcoal, slate */
    { 0x000048u, 0x000000u }, { 0x00006Du, 0x000024u }, { 0x24006Du, 0x000024u }, /* midnight, navy, indigo */
    { 0x480048u, 0x000000u }, { 0x480024u, 0x240000u }, { 0x480000u, 0x000000u }, /* plum, wine, oxblood */
    { 0x482400u, 0x000000u }, { 0x484824u, 0x000000u }, { 0x004848u, 0x000000u }, /* leather, walnut, petrol */
    { 0x002448u, 0x000000u }, { 0x002424u, 0x000000u }, { 0x242424u, 0x000000u }, /* deepsea, pine, shadow */
};

const char *const crop_hud_names[CROP_HUD_COUNT] = {
    "black",   "charcoal", "slate",  "midnight", "navy",    "indigo", "plum",   "wine",
    "oxblood", "leather",  "walnut", "petrol",   "deepsea", "pine",   "shadow",
};

static int bg_index(int bg)
{
    return bg >= 0 && bg < CROP_HUD_COUNT ? bg : 0;
}

/* How far along the way from a (top) to b (bottom) at line y of h, in 1/12 (0 = a, 12 = b; 4 shades every
 * 4/12) */
static double hud_pos(int y, int h)
{
    return 12 * (h > 1 ? (double)y / (h - 1) : 1);
}

static uint32_t hud_mix(int bg, double t)
{
    uint32_t a = k_hud_bg[bg].a, b = k_hud_bg[bg].b, out = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8)
        out |= (uint32_t)((a >> sh & 0xFF) * (1 - t) + (b >> sh & 0xFF) * t + 0.5) << sh;
    return out;
}

uint32_t crop_hud_bottom(int bg, int x, int w)
{
    (void)x;
    (void)w;
    return hud_mix(bg_index(bg), 1.0);
}

uint32_t crop_hud_pixel(int bg, int x, int y, int w, int h)
{
    bg = bg_index(bg);
    if (k_hud_bg[bg].a == k_hud_bg[bg].b)
        return 0xFF000000u | k_hud_bg[bg].a;
    /* 4 shades between the two colors; between two shades, 2x2 dithering (like in the games of the time) */
    static const int bayer[2][2] = { { 0, 2 }, { 3, 1 } };
    (void)w;
    int p = (int)(hud_pos(y, h) + 1e-9); /* position in quarter shades, 0-12 */
    int k = p / 4 < 2 ? p / 4 : 2;
    if (p - k * 4 > bayer[y & 1][x & 1])
        k++;
    return hud_mix(bg, k / 3.0);
}

static uint32_t at(const uint32_t *src, int src_w, int sub, int x, int y)
{
    return src[(y * sub) * src_w * sub + x * sub] & 0xFFFFFF;
}

/* Panel bars (through the cells the game writes, app/ys_panel.h; formerly, through the white outline in the
 * image): in a row of cells, the two side pieces (YS_TILE_BAR_SIDE: their lit column is the side outline);
 * the top outline is the last line of the cell above and the bottom one, 9 lines below (both checked lit). Calls
 * fn for each one, with the top line and the columns, in image coordinates */
typedef void (*BarFn)(void *ctx, int y, int x0, int x1);

/* The lit column (the last one) of the piece in panel cell (r, c), on its 1st line, in the image; -1 = none */
static int side_col(const YsPanel *p, int r, int c)
{
    for (int i = 7; i >= 0; i--)
        if (p->pix[r * 8][c * 8 + i])
            return YS_PANEL_X + c * 8 + i;
    return -1;
}

static void each_bar(const YsPanel *p, BarFn fn, void *ctx)
{
    if (!p || !p->valid)
        return;
    for (int r = 1; r + 1 < YS_PANEL_ROWS; r++) {
        /* the left side: the 1st side piece; the right: the last non-empty cell (the side piece or, with the bar full
         * to the end, max life 255, the end piece with the side included: 511) */
        int c0 = -1, c1 = -1;
        for (int c = 0; c < YS_PANEL_COLS; c++) {
            if (c0 < 0 && YS_TILE(p->cell[r][c]) == YS_TILE_BAR_SIDE)
                c0 = c;
            if (c0 >= 0 && YS_TILE(p->cell[r][c]) != YS_TILE_BLANK)
                c1 = c;
        }
        if (c0 < 0 || c1 == c0)
            continue;
        int x0 = side_col(p, r, c0), x1 = side_col(p, r, c1), y = YS_PANEL_Y + r * 8 - 1;
        if (x0 >= 0 && x1 > x0 + 1 && ys_panel_lit(p, x0 + 1, y) && ys_panel_lit(p, x0 + 1, y + 9))
            fn(ctx, y, x0, x1);
    }
}

typedef struct PanelCtx {
    uint32_t *out;
    int out_w, sub, x0, y0; /* position of the panel in the output, in image pixels */
    uint8_t keep[PANEL_H][PANEL_W]; /* inside the bars: the black stays */
} PanelCtx;

static void bar_corner(void *ctx, int y, int x0, int x1)
{
    /* The bottom outline the same as the top one (the ENEMY bar has a missing corner in the game) */
    PanelCtx *c = ctx;
    for (int x = x0; x <= x1; x++)
        for (int j = 0; j < c->sub; j++)
            for (int i = 0; i < c->sub; i++)
                c->out[((c->y0 + y + 9 - PANEL_Y) * c->sub + j) * c->out_w + (c->x0 + x - FRAME_LEFT) * c->sub + i] =
                    0xFF000000u | WHITE;
    for (int yy = y + 1; yy < y + 9; yy++)
        for (int x = x0 + 1; x < x1; x++)
            c->keep[yy - PANEL_Y][x - FRAME_LEFT] = 1;
}

void crop_panel(const uint32_t *src, int src_w, int sub, const YsPanel *panel, uint32_t *out, int out_w)
{
    static PanelCtx c;
    memset(&c, 0, sizeof(c));
    c.out = out;
    c.out_w = out_w;
    c.sub = 1;
    for (int y = 0; y < PANEL_H; y++)
        for (int x = 0; x < PANEL_W; x++)
            out[y * out_w + x] = 0xFF000000u | at(src, src_w, sub, FRAME_LEFT + x, PANEL_Y + y);
    each_bar(panel, bar_corner, &c);
    for (int y = 0; y < PANEL_H; y++)
        for (int x = 0; x < PANEL_W; x++)
            if (!c.keep[y][x] && (out[y * out_w + x] & 0xFFFFFF) == 0)
                out[y * out_w + x] = 0; /* transparent: the band (in the background color) shows */
}

/* ---- HUD in two lines (compact, wide, fill) ---- */

#define HUD_TEXT_Y PANEL_Y /* panel text (8 lines): colored labels, white numbers */
#define HUD_TEXT_H 8
#define HUD_BAR_H 8
#define HUD_MARGIN 8    /* pixels between the HUD and the sides of the band (formerly 2) */

typedef struct HudBars {
    int count;
    int y[2], x0[2], x1[2]; /* outline of each bar: PLAYER and ENEMY */
} HudBars;

static void hud_bar(void *ctx, int y, int x0, int x1)
{
    HudBars *b = ctx;
    if (b->count < 2) {
        b->y[b->count] = y;
        b->x0[b->count] = x0;
        b->x1[b->count] = x1;
        b->count++;
    }
}

/* Lit pixels of the panel bar that starts at line y (outline from x0 to x1): in the game, 1 pixel per life
 * point; the lit part is the max life, yellow = what remains */
static int hud_bar_len(const YsPanel *p, int y, int x0, int x1)
{
    int len = 0;
    while (x0 + 1 + len < x1 && ys_panel_lit(p, x0 + 1 + len, y + 4))
        len++;
    return len;
}

/* A label of the panel text and its number (through the cells the game writes, app/ys_panel.h; formerly,
 * through the image colors) */
typedef struct HudItem {
    int l0, l1, v0, v1; /* lit columns of the label and of the number (v0 < 0 = no number), in the image */
    int kind;           /* the label's 1st piece: YS_TILE_H (H.P), _M (M.P), _E (EXP), _G (GOLD) */
    int v_end;          /* the column after the number's last cell */
    int slash;          /* the number has "/": the panel already shows the max (Ys I) */
    int save;           /* the number in a palette other than the label's: the chosen save's (save screen, palette 5) */
    int value;          /* the number written in the cells, up to the "/" */
} HudItem;

/* 0 = empty cell, 1 = label letter, 2 = digit or "/" (cell c of the text line) */
static int text_cell(const YsPanel *p, int c)
{
    int t = YS_TILE(p->cell[0][c]);
    return t >= YS_TILE_LABEL_FIRST && t <= YS_TILE_LABEL_LAST ? 1 : t >= YS_TILE_DIGIT_FIRST && t <= YS_TILE_DIGIT_LAST ? 2 : 0;
}

/* The 1st and last lit columns of text cells c0..c1, in the image (with none lit: those of the cells) */
static void lit_span(const YsPanel *p, int c0, int c1, int *lo, int *hi)
{
    *lo = *hi = -1;
    for (int x = c0 * 8; x < (c1 + 1) * 8; x++)
        for (int y = 0; y < 8; y++)
            if (p->pix[y][x]) {
                *lo = *lo < 0 ? x : *lo;
                *hi = x;
                break;
            }
    if (*lo < 0) {
        *lo = c0 * 8;
        *hi = c1 * 8 + 7;
    }
    *lo += YS_PANEL_X;
    *hi += YS_PANEL_X;
}

/* The labels of the panel text, left to right (Ys I: H.P, EXP, GOLD; Ys II: H.P, M.P, EXP, GOLD): each one
 * is the consecutive letters and its number, the digit cells up to the next one */
static int hud_items(const YsPanel *p, HudItem *it)
{
    int n = 0;
    if (!p || !p->valid)
        return 0;
    for (int c = 0; c < YS_PANEL_COLS && n < CROP_HUD_ITEMS;) {
        if (text_cell(p, c) != 1) {
            c++;
            continue;
        }
        int e = c;
        while (e + 1 < YS_PANEL_COLS && text_cell(p, e + 1) == 1)
            e++;
        HudItem *h = &it[n++];
        *h = (HudItem){ .kind = YS_TILE(p->cell[0][c]), .v0 = -1, .v1 = -1 };
        lit_span(p, c, e, &h->l0, &h->l1);
        int d0 = -1, d1 = -1, k = e + 1;
        for (; k < YS_PANEL_COLS && text_cell(p, k) != 1; k++)
            if (text_cell(p, k) == 2) {
                d0 = d0 < 0 ? k : d0;
                d1 = k;
                if (!h->slash && YS_TILE(p->cell[0][k]) < YS_TILE_DIGIT_FIRST + 10)
                    h->value = h->value * 10 + YS_TILE(p->cell[0][k]) - YS_TILE_DIGIT_FIRST;
                h->slash |= YS_TILE(p->cell[0][k]) == YS_TILE_SLASH;
                h->save |= YS_TILE(p->cell[0][k]) != YS_TILE_SLASH && YS_PAL(p->cell[0][k]) != YS_PAL(p->cell[0][c]);
            }
        if (d0 >= 0) {
            lit_span(p, d0, d1, &h->v0, &h->v1);
            h->v_end = YS_PANEL_X + (d1 + 1) * 8;
        }
        c = k;
    }
    return n;
}

void crop_hud_layout(const YsPanel *panel, CropHudLayout *o)
{
    *o = (CropHudLayout){ 0 };
    HudItem it[CROP_HUD_ITEMS];
    o->n = hud_items(panel, it);
    for (int i = 0; i < o->n; i++) {
        o->l0[i] = it[i].l0;
        o->l1[i] = it[i].l1;
        o->v0[i] = it[i].v0;
        o->v1[i] = it[i].v1;
        o->save |= it[i].save;
    }
    HudBars bars = { 0 };
    each_bar(panel, hud_bar, &bars);
    o->bars = bars.count;
    for (int i = 0; i < bars.count; i++) {
        o->bar_y[i] = bars.y[i];
        o->bar_x0[i] = bars.x0[i];
        o->bar_x1[i] = bars.x1[i];
        o->bar_len[i] = hud_bar_len(panel, bars.y[i], bars.x0[i], bars.x1[i]);
    }
}

/* The cropped image (w x h pixels, sub times larger) */
typedef struct Out {
    uint32_t *px;
    int w, h, sub; /* sub: output pixels per pixel (w x h) */
    int src_sub;   /* src pixels per console pixel */
    const uint8_t *dark; /* not NULL: each color darkened by this table (game fade) */
    int stride, oy;      /* smaller HUD in the band (hud_q): output pixels per line and 1st output line (0 = w * sub, 0) */
    const YsPanel *panel; /* the letter pixels are the lit ones in the panel pieces (not NULL: out_piece) */
} Out;

/* Game fade table (user's request: the HUD, the border and the panel darken like the map): each channel through
 * the game's table at level 7 - darken/FADE_Q (between two levels, the linear transition) */
static void dark_table(uint8_t lut[256], int darken)
{
    darken = darken > 7 * FADE_Q ? 7 * FADE_Q : darken;
    for (int v = 0; v < 256; v++) {
        int c = (v * 7 + 127) / 255;
        int l = g_fade_table_ok ? ys_fade_channel(g_fade_table, c, 7 * FADE_Q - darken, 0, FADE_Q) : c * FADE_Q;
        lut[v] = (uint8_t)(l > 0 ? (l * 255 + 3 * FADE_Q) / (7 * FADE_Q) : 0);
    }
}

static void out_put(const Out *o, int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= o->w || y >= o->h)
        return;
    if (o->dark)
        c = (c & 0xFF000000u) | (uint32_t)o->dark[c >> 16 & 0xFF] << 16 | (uint32_t)o->dark[c >> 8 & 0xFF] << 8 |
            o->dark[c & 0xFF];
    int stride = o->stride ? o->stride : o->w * o->sub;
    for (int j = 0; j < o->sub; j++)
        for (int i = 0; i < o->sub; i++)
            o->px[(o->oy + y * o->sub + j) * stride + x * o->sub + i] = c;
}

/* The lit pixels of columns x0..x1 (h lines from y) of the panel at (dx, dy), in the image color; returns the
 * width. Lit: those of the pieces the game wrote (in a hit flash the panel background turns white and only the
 * letters come out); without the cells, those that are not black in the image */
static int out_piece(const Out *o, const uint32_t *src, int src_w, int x0, int x1, int y, int h, int dx, int dy)
{
    for (int j = 0; j < h; j++)
        for (int x = x0; x <= x1; x++) {
            uint32_t p = at(src, src_w, o->src_sub, x, y + j);
            if (o->panel ? ys_panel_lit(o->panel, x, y + j) : p != 0)
                out_put(o, dx + x - x0, dy + j, 0xFF000000u | p);
        }
    return x1 - x0 + 1;
}

/* The panel bar (line y, outline x0-x1) compressed to CROP_HUD_BAR_W pixels: full = the max life.
 * On the save screen (save_hp >= 0), the panel bar is the player's: this one comes out with the chosen save's life
 * (save_hp of pn->hp_max), in the game bar's colors; with no max (empty save), unlit */
static void out_bar(const Out *o, const uint32_t *src, int src_w, const YsPanel *pn, int y, int x0, int x1, int dx,
                    int dy, int bw, int save_hp)
{
    int len = hud_bar_len(pn, y, x0, x1);
    uint32_t border = 0xFF000000u | at(src, src_w, o->src_sub, x0, y);
    for (int j = 0; j < HUD_BAR_H; j++)
        for (int i = 0; i < bw; i++) {
            uint32_t p = 0xFF000000u;
            if (j == 0 || j == HUD_BAR_H - 1 || i == 0 || i == bw - 1)
                p = border;
            else if (save_hp >= 0) {
                if (pn->hp_max > 0)
                    p |= (i - 1) * pn->hp_max < save_hp * (bw - 2) ? pn->bar_yellow : pn->bar_red;
            } else if (len > 0)
                p |= at(src, src_w, o->src_sub, x0 + 1 + (i - 1) * len / (bw - 2), y + 4);
            out_put(o, dx + i, dy + j, p);
        }
}

/* The 1st lit line of column x in the panel text (the color of the numbers) */
static int first_lit(const YsPanel *p, int x)
{
    for (int y = HUD_TEXT_Y; y < HUD_TEXT_Y + HUD_TEXT_H; y++)
        if (ys_panel_lit(p, x, y))
            return y;
    return HUD_TEXT_Y;
}

static int item_w(const HudItem *it)
{
    return it->l1 - it->l0 + 1 + (it->v0 >= 0 ? 4 + it->v1 - it->v0 + 1 : 0);
}

static void out_item(const Out *o, const uint32_t *src, int src_w, const HudItem *it, int dx, int dy)
{
    dx += out_piece(o, src, src_w, it->l0, it->l1, HUD_TEXT_Y, HUD_TEXT_H, dx, dy) + 4;
    if (it->v0 >= 0)
        out_piece(o, src, src_w, it->v0, it->v1, HUD_TEXT_Y, HUD_TEXT_H, dx, dy);
}

/* Text in the game's 8x8 font (menu_font), one piece every 8 pixels, from (dx, dy): in color c, or, with lab
 * (8x8 colors of a panel label), in the label pixel's color or, if it is unlit, in that of the nearest lit one on the
 * same line (the labels have a top-to-bottom gradient). Returns the width (without the last empty column). */
/* "NEXT" with the panel letters (the menu font was shorter and drawn differently from H.P, M.P and GOLD): the E and
 * the X of the EXP label (columns l0.., line HUD_TEXT_Y), the N and the T drawn in the same style, in its two colors
 * (a = dark outline, b = the light one). Returns the width */
static int out_next(const Out *o, const uint32_t *src, int src_w, int l0, int dx, int dy)
{
    static const char *const k_n[8] = { "abb..bbb", ".abb..b.", ".abab.b.", ".ab.b.b.",
                                        ".ab..bb.", ".ab..bb.", ".ab...b.", "abbb..b." };
    static const char *const k_t[8] = { "abbbbbbb", "b..ab..b", "...ab...", "...ab...",
                                        "...ab...", "...ab...", "...ab...", "..abbb.." };
    uint32_t ca = 0, cb = 0;
    for (int j = 0; j < 8; j++) /* the two colors of the E: the 1st lit column is the dark one */
        for (int i = 0; i < 8; i++) {
            uint32_t p = at(src, src_w, o->src_sub, l0 + i, HUD_TEXT_Y + j);
            if (p && !ca && i == 0)
                ca = p;
            else if (p && !cb && i > 0 && p != ca)
                cb = p;
        }
    ca = ca ? ca : cb;
    cb = cb ? cb : ca;
    const char *const *hand[2] = { k_n, k_t };
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++) {
                uint32_t p = 0;
                if (k == 1 || k == 2) /* E and X of EXP */
                    p = at(src, src_w, o->src_sub, l0 + (k - 1) * 8 + i, HUD_TEXT_Y + j);
                else {
                    char ch = hand[k / 3][j][i];
                    p = ch == 'a' ? ca : ch == 'b' ? cb : 0;
                }
                if (p)
                    out_put(o, dx + 8 * k + i, dy + j, 0xFF000000u | p);
            }
    return 8 * 4 - 1;
}

static int out_text(const Out *o, const char *text, int dx, int dy, uint32_t c, const uint32_t lab[8][8])
{
    int n = 0;
    for (; text[n]; n++) {
        uint8_t rows[8];
        menu_font_glyph(text[n], rows);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++) {
                if (!(rows[j] & (0x80 >> i)))
                    continue;
                uint32_t p = c;
                for (int d = 0; lab && d < 8; d++) {
                    if (i - d >= 0 && lab[j][i - d]) {
                        p = lab[j][i - d];
                        break;
                    }
                    if (i + d < 8 && lab[j][i + d]) {
                        p = lab[j][i + d];
                        break;
                    }
                }
                out_put(o, dx + 8 * n + i, dy + j, 0xFF000000u | p);
            }
    }
    return n ? 8 * n - 1 : 0;
}

int crop_next_exp(const YsPanel *panel)
{
    HudItem it[CROP_HUD_ITEMS];
    int n = hud_items(panel, it);
    for (int i = 0; i < n; i++)
        if (it[i].kind == YS_TILE_E)
            return (int)panel->next_level > it[i].value ? (int)panel->next_level - it[i].value : 0;
    return 0;
}

/* The HUD in two lines in the band below the map and the ENEMY floating in the top right corner (app/crop.h) */
static void hud_draw(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst, int out_sub)
{
    /* the game's flash: everything the HUD draws brightens along */
    uint8_t white[256];
    const uint8_t *wl = NULL;
    if (v->whiten > 0) {
        white_table(white, v->whiten);
        wl = white;
    }
    Out o = { dst, v->w, v->h, out_sub, sub, wl };
    /* The two lines: in map pixels, or smaller (hud_q quarter pixels: the band seen in smaller pixels) */
    /* The two lines in the band (text of 8, 3 of slack; 2 in the low band), with 2/3 of the leftover on top: centered,
     * the top one was stuck to the map (user's request) */
    Out t = o;
    int r1 = v->band_y + (v->h - v->band_y - (HUD_TEXT_H * 2 + 3)) * 2 / 3, r2 = r1 + HUD_TEXT_H + 3;
    int small = v->hud_q > 0 && v->hud_q < 4 && out_sub * v->hud_q % 4 == 0;
    if (small) {
        int k = out_sub * v->hud_q / 4;
        t = (Out){ dst, v->w * out_sub / k, (v->h - v->band_y) * out_sub / k, k, sub, wl, v->w * out_sub,
                   v->band_y * out_sub };
        r1 = (t.h - (HUD_TEXT_H * 2 + 2)) * 2 / 3;
        r2 = r1 + HUD_TEXT_H + 2;
    }
    t.panel = v->panel;
    const YsPanel *pn = v->panel;
    HudItem it[CROP_HUD_ITEMS];
    int n = hud_items(pn, it), save = 0;
    HudBars bars = { 0 };
    each_bar(pn, hud_bar, &bars);
    /* each label by the 1st letter the game wrote (formerly, by the order in the panel) */
    const HudItem *hp = NULL, *exp = NULL, *gold = NULL;
    for (int i = 0; i < n; i++) {
        hp = it[i].kind == YS_TILE_H && !hp ? &it[i] : hp;
        exp = it[i].kind == YS_TILE_E && !exp ? &it[i] : exp;
        gold = it[i].kind == YS_TILE_G && !gold ? &it[i] : gold;
        save |= it[i].save; /* on the save screen, the numbers are the chosen save's (the bar and the max too) */
    }
    /* Simulation approved by the user, the same in Ys I and Ys II: top left, H.P, the bar and the number
     * with the max; below it the other labels (Ys II: M.P); on the right, NEXT (the EXP left to the next
     * level) above GOLD, right-aligned. Margin of HUD_MARGIN on the sides; the bar shortens if the top
     * line would touch NEXT. */
    char hp_max[8] = "", next[8] = "";
    int has_next = exp && v->next_on;
    /* the Ys II panel shows the H.P without the max (no "/"): the max is the game's ($2C48, 8 bits in Ys II; on the
     * save screen, with the cursor on a save, the game leaves that save's max there) */
    if (hp && hp->v0 >= 0 && !hp->slash && bars.count > 0)
        snprintf(hp_max, sizeof(hp_max), "/%03d", pn->hp_max);
    if (has_next)
        snprintf(next, sizeof(next), "%05d", v->next > 99999 ? 99999 : v->next);
    /* the H.P number: the panel pieces and, in Ys II, the max in the next piece */
    int hp_tail = hp && hp->v0 >= 0 ? hp->v_end - hp->v0 : 0;
    int hp_w = hp && hp->v0 >= 0 ? (hp_max[0] ? hp_tail + 8 * 4 - 1 : hp->v1 - hp->v0 + 1) : 0;
    int next_w = has_next ? 8 * 4 - 1 + 4 + 8 * 5 - 1 : exp ? item_w(exp) : 0;
    int hx = HUD_MARGIN, bar_w = CROP_HUD_BAR_W;
    if (hp) {
        int left = HUD_MARGIN + hp->l1 - hp->l0 + 1 + 4 + 4 + hp_w;
        int room = t.w - HUD_MARGIN - next_w - 8 - left;
        bar_w = room < bar_w ? (room < 40 ? 40 : room) : bar_w;
        hx += out_piece(&t, src, src_w, hp->l0, hp->l1, HUD_TEXT_Y, HUD_TEXT_H, hx, r1) + 4;
        if (bars.count > 0)
            out_bar(&t, src, src_w, pn, bars.y[0], bars.x0[0], bars.x1[0], hx, r1, bar_w, save ? hp->value : -1);
        hx += bar_w + 4;
        if (hp->v0 >= 0)
            out_piece(&t, src, src_w, hp->v0, hp->v1, HUD_TEXT_Y, HUD_TEXT_H, hx, r1);
        if (hp_max[0])
            out_text(&t, hp_max, hx + hp_tail, r1, 0xFF000000u | at(src, src_w, sub, hp->v0, first_lit(pn, hp->v0)),
                     NULL);
    }
    hx = HUD_MARGIN;
    for (int i = 0; i < n; i++)
        if (&it[i] != hp && &it[i] != exp && &it[i] != gold) {
            out_item(&t, src, src_w, &it[i], hx, r2);
            hx += item_w(&it[i]) + 10;
        }
    if (has_next) { /* NEXT with the letters of the panel's EXP label (out_next); the numbers in the panel's color */
        uint32_t digit = WHITE;
        if (gold && gold->v0 >= 0)
            digit = at(src, src_w, sub, gold->v0, first_lit(pn, gold->v0));
        int x = t.w - HUD_MARGIN - next_w;
        x += out_next(&t, src, src_w, exp->l0, x, r1) + 4;
        out_text(&t, next, x, r1, digit, NULL);
    } else if (exp)
        out_item(&t, src, src_w, exp, t.w - HUD_MARGIN - item_w(exp), r1);
    if (gold)
        out_item(&t, src, src_w, gold, t.w - HUD_MARGIN - item_w(gold), r2);
    /* The ENEMY no longer floats on the map (the life bar sits above the enemy, app/battle.h) */
}

static void frame_bars(void *ctx, int y, int x0, int x1)
{
    bar_corner(ctx, y, x0, x1);
}

int crop_out_sub(const CropView *v)
{
    return v->band && v->hud_q == 2 ? 2 : v->band && v->hud_q == 3 ? 4 : 1;
}

void crop_compose(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst)
{
    crop_compose_sub(src, src_w, sub, v, dst, sub);
}

void crop_compose_sub(const uint32_t *src, int src_w, int sub, const CropView *v, uint32_t *dst, int out_sub)
{
    int ow = v->w * out_sub, k = out_sub / sub;
    for (int i = 0; i < ow * v->h * out_sub; i++)
        dst[i] = 0xFF000000u;
    /* slide in 1/4 pixel (only with smooth motion) */
    int shift = v->band ? v->pan_q * sub / 4 : 0, shift_y = v->band ? v->pan_qy * sub / 4 : 0;
    for (int i = 0; i < v->count; i++) {
        const CropPiece *p = &v->piece[i];
        for (int y = 0; y < p->h * out_sub; y++) {
            const uint32_t *s = src + (p->sy * sub + y / k + shift_y) * src_w * sub + p->sx * sub + shift;
            uint32_t *d = dst + (p->dy * out_sub + y) * ow + p->dx * out_sub;
            if (k == 1)
                memcpy(d, s, (size_t)p->w * sub * sizeof(uint32_t));
            else if (y % k) /* the same source line as the one above (it took ~1.4 ms per frame with the HUD at 4x) */
                memcpy(d, d - ow, (size_t)p->w * out_sub * sizeof(uint32_t));
            else
                for (int x = 0, n = p->w * sub; x < n; x++)
                    for (int j = 0; j < k; j++)
                        *d++ = s[x];
        }
    }
    if (v->shrink_n && v->band && v->count == 1)
        shrink_windows(src, src_w, sub, v, dst, out_sub);
    /* In the flash, the band and the HUD brighten along with the image; the HUD comes from the panel, flash undone */
    uint8_t white[256];
    if (v->whiten > 0)
        white_table(white, v->whiten);
    if (v->hud || v->band) { /* the band below the map, in the chosen background (the panel or the HUD go on top) */
        Out o = { dst, v->w, v->h, out_sub, sub, v->whiten > 0 ? white : NULL };
        for (int y = v->band_y; y < v->h; y++)
            for (int x = 0; x < v->w; x++)
                out_put(&o, x, y, crop_hud_pixel(v->hud_bg, x, y - v->band_y, v->w, v->h - v->band_y));
    }
    if (v->band && v->whiten > 0 && src_w <= VCE_MAX_WIDTH)
        hud_draw(unwhite_panel(src, src_w, sub, v->whiten), src_w, 1, v, dst, out_sub);
    else if (v->band)
        hud_draw(src, src_w, sub, v, dst, out_sub);
    if (v->fix_bars) {
        static PanelCtx c;
        memset(&c, 0, sizeof(c));
        c.out = dst;
        c.out_w = ow;
        c.sub = out_sub;
        c.x0 = 0;
        c.y0 = PANEL_Y - FRAME_TOP;
        each_bar(v->panel, frame_bars, &c);
    }
}
