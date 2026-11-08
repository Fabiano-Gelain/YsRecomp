/* The HUD read from the image colors, the old way (crop.c: each_bar, label_colors, text_col, hud_items), only to check
 * the new one (from the panel cells, app/ys_panel.h) in every session: pce_trace --crop-log, "panel from the game". */
#include "hud_old.h"

#define FRAME_LEFT 8
#define FRAME_RIGHT 327
#define PANEL_Y 179
#define PANEL_H 41
#define PANEL_W 320
#define WHITE 0xFFFFFFu
#define BAR_MIN 64
#define HUD_TEXT_Y PANEL_Y
#define HUD_TEXT_H 8
#define HUD_LABEL_GAP 7
#define HUD_LABEL_COLORS 8

static uint32_t at(const uint32_t *src, int src_w, int x, int y)
{
    return src[y * src_w + x] & 0xFFFFFF;
}

static void bars(const uint32_t *src, int src_w, CropHudLayout *o)
{
    for (int y = PANEL_Y; y + 9 < PANEL_Y + PANEL_H; y++)
        for (int x = FRAME_LEFT; x <= FRAME_RIGHT;) {
            if (at(src, src_w, x, y) != WHITE) {
                x++;
                continue;
            }
            int e = x;
            while (e + 1 <= FRAME_RIGHT && at(src, src_w, e + 1, y) == WHITE)
                e++;
            if (e - x + 1 >= BAR_MIN && at(src, src_w, x + 1, y + 9) == WHITE &&
                (y == PANEL_Y || at(src, src_w, x + 1, y - 1) != WHITE) && o->bars < 2) {
                int len = 0;
                while (x + 1 + len < e && at(src, src_w, x + 1 + len, y + 4))
                    len++;
                o->bar_y[o->bars] = y;
                o->bar_x0[o->bars] = x;
                o->bar_x1[o->bars] = e;
                o->bar_len[o->bars] = len;
                o->bars++;
            }
            x = e + 1;
        }
}

static int text_col(const uint32_t *src, int src_w, int x, const uint32_t *lc, int nlc)
{
    int lit = 0;
    for (int y = HUD_TEXT_Y; y < HUD_TEXT_Y + HUD_TEXT_H; y++) {
        uint32_t p = at(src, src_w, x, y);
        if (!p)
            continue;
        int k = 0;
        while (k < nlc && lc[k] != p)
            k++;
        if (k == nlc)
            return 2;
        lit = 1;
    }
    return lit;
}

static int first_lit(const uint32_t *src, int src_w, int x)
{
    for (int y = HUD_TEXT_Y; y < HUD_TEXT_Y + HUD_TEXT_H; y++)
        if (at(src, src_w, x, y))
            return y;
    return HUD_TEXT_Y;
}

void hud_old_layout(const uint32_t *src, int src_w, CropHudLayout *o)
{
    *o = (CropHudLayout){ 0 };
    uint32_t lc[HUD_LABEL_COLORS];
    int nlc = 0, cls[PANEL_W], n = 0;
    for (int y = HUD_TEXT_Y; y < HUD_TEXT_Y + HUD_TEXT_H; y++)
        for (int x = FRAME_LEFT; x < FRAME_LEFT + 8; x++) {
            uint32_t p = at(src, src_w, x, y);
            int k = 0;
            while (k < nlc && lc[k] != p)
                k++;
            if (p && k == nlc && nlc < HUD_LABEL_COLORS)
                lc[nlc++] = p;
        }
    for (int x = 0; x < PANEL_W; x++)
        cls[x] = text_col(src, src_w, FRAME_LEFT + x, lc, nlc);
    for (int x = 0; x < PANEL_W && n < CROP_HUD_ITEMS;) {
        if (cls[x] != 1) {
            x++;
            continue;
        }
        int e = x;
        for (int k = x + 1; k < PANEL_W && k <= e + HUD_LABEL_GAP + 1 && cls[k] != 2; k++)
            if (cls[k] == 1)
                e = k;
        o->l0[n] = FRAME_LEFT + x;
        o->l1[n] = FRAME_LEFT + e;
        o->v0[n] = o->v1[n] = -1;
        n++;
        x = e + 1;
        while (x < PANEL_W && cls[x] != 1)
            x++;
    }
    for (int i = 0; i < n; i++) {
        int end = i + 1 < n ? o->l0[i + 1] - 1 : FRAME_RIGHT;
        for (int x = o->l1[i] + 1; x <= end; x++)
            if (cls[x - FRAME_LEFT]) {
                if (o->v0[i] < 0)
                    o->v0[i] = x;
                o->v1[i] = x;
            }
    }
    o->n = n;
    o->save = n >= 2 && o->v0[n - 1] >= 0 && at(src, src_w, o->v0[n - 1], first_lit(src, src_w, o->v0[n - 1])) != WHITE;
    bars(src, src_w, o);
}
