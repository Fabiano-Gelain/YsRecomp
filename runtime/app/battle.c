/* Kill rewards and enemy health bars on the map: see battle.h */
#include "app/battle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/crop.h"
#include "app/menu_font.h"
#include "app/motion.h"
#include "app/redraw.h"

#define BAR_W 20
#define BAR_H 2
#define RING_X 512 /* camera X on the map ring (as in motion.c) */
#define RING_Y 256
#define POPUP_RISE 12
#define POPUP_RISE_FRAMES 30
#define POPUP_FADE 20

static const uint32_t k_yellow = 0xFFFFB600u, k_red = 0xFFB62400u, k_white = 0xFFFFFFFFu, k_black = 0xFF000000u;
static const int k_sprite_h[4] = { 16, 32, 64, 64 };
static const uint16_t k_mask_w[2] = { 0x3FF, 0x3FE }, k_mask_h[4] = { 0x3FF, 0x3FD, 0x3FB, 0x3F9 };

static int wrap(int d, int size)
{
    d %= size;
    if (d > size / 2)
        d -= size;
    if (d < -size / 2)
        d += size;
    return d;
}

void battle_init(Battle *bt)
{
    int bars = bt->bars, popups = bt->popups, small = bt->small;
    memset(bt, 0, sizeof(*bt));
    bt->bars = bars;
    bt->popups = popups;
    bt->small = small;
}

/* Image column and row of the 1st column/row of the VDC area (sprite at x = 32, y = 64): 0 = no VDC line */
static int frame_origin(const RedrawFrame *b, int *x0, int *raster0, int *reg)
{
    for (int r = 0; r < b->h; r++)
        if (b->rows[r].valid) {
            *x0 = b->rows[r].x0;
            *raster0 = b->rows[r].raster - r;
            *reg = (b->rows[r].mwr >> 4) & 7;
            return 1;
        }
    return 0;
}

/* Lit pixels of sprite e (4 table words): rows [*top, *bot) and columns [*left, *right) from the corner;
 * 0 = none */
static int sprite_lit(const uint16_t *vram, const uint16_t *e, int *top, int *bot, int *left, int *right)
{
    uint16_t flags = e[3];
    int cgx = (flags >> 8) & 1, cgy = (flags >> 12) & 3, h = k_sprite_h[cgy], halves = cgx ? 2 : 1;
    uint16_t pattern = (uint16_t)(((e[2] >> 1) & 0x3FF) & k_mask_w[cgx] & k_mask_h[cgy]);
    int x_flip = (flags & 0x0800) != 0, y_flip = (flags & 0x8000) != 0, any = 0;
    for (int y = 0; y < h; y++) {
        int sy = y_flip ? h - 1 - y : y;
        for (int half = 0; half < halves; half++) {
            int addr = (pattern << 6) + (sy >> 4) * 128 + (sy & 15) + (half ? 64 : 0);
            uint16_t row = 0;
            for (int p = 0; p < 4; p++)
                row |= vram[(addr + p * 16) & (VDC_VRAM_SIZE - 1)];
            if (!row)
                continue;
            /* bit 15 = leftmost pixel (unflipped); half 1 is on the right unflipped, on the left when flipped */
            int hx = (halves == 2 && (half == 1) != x_flip) ? 16 : 0;
            int lo = 16, hi = -1;
            for (int x = 0; x < 16; x++)
                if (row & (0x8000 >> x)) {
                    int px = x_flip ? 15 - x : x;
                    lo = px < lo ? px : lo;
                    hi = px > hi ? px : hi;
                }
            if (!any || y < *top)
                *top = y;
            if (!any || y + 1 > *bot)
                *bot = y + 1;
            if (!any || hx + lo < *left)
                *left = hx + lo;
            if (!any || hx + hi + 1 > *right)
                *right = hx + hi + 1;
            any = 1;
        }
    }
    return any;
}

/* Lit pixels of actor a in the frame (in the image): top, left, right */
static int actor_box(const RedrawFrame *b, int a, int x0, int raster0, int *top, int *left, int *right)
{
    const YsList *l = &b->eng.list;
    int any = 0;
    for (int i = 0; i < l->count && i < YS_ACTORS; i++) {
        if (l->owner[i] != a)
            continue;
        const uint16_t *e = &b->sat[i * 4];
        int t, bo, le, ri;
        if (!sprite_lit(b->vram, e, &t, &bo, &le, &ri))
            continue;
        int x = x0 + (e[1] & 0x3FF) - 32, y = (e[0] & 0x3FF) - 64 - raster0;
        if (!any || y + t < *top)
            *top = y + t;
        if (!any || x + le < *left)
            *left = x + le;
        if (!any || x + ri > *right)
            *right = x + ri;
        any = 1;
    }
    return any;
}

static void put(BattleSticker *s, int x, int y, uint32_t c)
{
    if (x >= 0 && x < s->w && y >= 0 && y < s->h)
        s->px[y * s->w + x] = c;
}

/* Text line in the game's font with a black outline, starting at (x, y) */
static void text_line(BattleSticker *s, const char *t, int x, int y, uint32_t color)
{
    for (int i = 0; t[i]; i++) {
        uint8_t rows[8];
        if (!menu_font_glyph(t[i], rows))
            continue;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (rows[r] & (0x80 >> c))
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int px = x + i * 8 + c + dx, py = y + r + dy;
                            if (px >= 0 && px < s->w && py >= 0 && py < s->h && s->px[py * s->w + px] != color)
                                s->px[py * s->w + px] = k_black;
                        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (rows[r] & (0x80 >> c))
                    put(s, x + i * 8 + c, y + r, color);
    }
}

/* Line width without the empty column to the right of the last letter */
static int text_w(const char *t)
{
    int n = (int)strlen(t);
    return n ? n * 8 - 1 : 0;
}

/* Small font (3x5; the "Small" style of the rewards): bit 2 = leftmost pixel */
static const struct { char ch; uint8_t rows[5]; } k_small[] = {
    { '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } }, { '2', { 7, 1, 7, 4, 7 } }, { '3', { 7, 1, 3, 1, 7 } },
    { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } }, { '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 2, 2, 2 } },
    { '8', { 7, 5, 7, 5, 7 } }, { '9', { 7, 5, 7, 1, 7 } }, { '+', { 0, 2, 7, 2, 0 } }, { 'E', { 7, 4, 6, 4, 7 } },
    { 'X', { 5, 5, 2, 5, 5 } }, { 'P', { 6, 5, 6, 4, 4 } }, { 'G', { 7, 4, 5, 5, 7 } }, { 'O', { 7, 5, 5, 5, 7 } },
    { 'L', { 4, 4, 4, 4, 7 } }, { 'D', { 6, 5, 5, 5, 6 } },
};

/* One line in the small font, with a black outline, at (x, y); returns the width (without the trailing empty column) */
static int small_line(BattleSticker *s, const char *t, int x, int y, uint32_t color, int draw)
{
    int cx = 0;
    for (int i = 0; t[i]; i++) {
        const uint8_t *rows = NULL;
        for (size_t k = 0; k < sizeof(k_small) / sizeof(k_small[0]); k++)
            if (k_small[k].ch == t[i])
                rows = k_small[k].rows;
        if (rows && draw) {
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (rows[r] & (4 >> c))
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++) {
                                int px = x + cx + c + dx, py = y + r + dy;
                                if (px >= 0 && px < s->w && py >= 0 && py < s->h && s->px[py * s->w + px] != color)
                                    s->px[py * s->w + px] = k_black;
                            }
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (rows[r] & (4 >> c))
                        put(s, x + cx + c, y + r, color);
        }
        cx += 4;
    }
    return cx ? cx - 1 : 0;
}

static void make_bar(BattleSticker *s, int hp, int max)
{
    s->kind = BATTLE_BAR;
    s->w = BAR_W;
    s->h = BAR_H;
    s->alpha = 255;
    int lit = (BAR_W * hp + max / 2) / max;
    lit = lit < 1 ? 1 : lit;
    for (int y = 0; y < BAR_H; y++)
        for (int x = 0; x < BAR_W; x++)
            s->px[y * BAR_W + x] = x < lit ? k_yellow : k_red;
}

static void make_popup(BattleSticker *s, const BattlePopup *p, int style)
{
    char line[2][24];
    uint32_t color[2];
    int n = 0;
    if (p->exp > 0) {
        snprintf(line[n], sizeof(line[n]), "+%d EXP", p->exp);
        color[n++] = k_white;
    }
    if (p->gold > 0) {
        snprintf(line[n], sizeof(line[n]), "+%d GOLD", p->gold);
        color[n++] = k_yellow;
    }
    int w = 0;
    s->kind = BATTLE_POPUP;
    s->actor = -1;
    if (style == 2) { /* Small: one line, "+N EXP +N GOLD", yellow */
        char one[48];
        snprintf(one, sizeof(one), "%s%s%s", n > 0 ? line[0] : "", n > 1 ? " " : "", n > 1 ? line[1] : "");
        w = small_line(s, one, 0, 0, 0, 0);
        s->w = w + 2 > BATTLE_STICKER_W ? BATTLE_STICKER_W : w + 2;
        s->h = 7;
        memset(s->px, 0, sizeof(uint32_t) * (size_t)s->w * s->h);
        small_line(s, one, 1, 1, k_yellow, 1);
    } else {
        for (int i = 0; i < n; i++)
            w = text_w(line[i]) > w ? text_w(line[i]) : w;
        s->w = w + 2 > BATTLE_STICKER_W ? BATTLE_STICKER_W : w + 2;
        s->h = n * 10;
        memset(s->px, 0, sizeof(uint32_t) * (size_t)s->w * s->h);
        for (int i = 0; i < n; i++)
            text_line(s, line[i], 1 + (w - text_w(line[i])) / 2, 1 + i * 10, color[i]);
    }
    int rise = (p->age < POPUP_RISE_FRAMES ? p->age : POPUP_RISE_FRAMES) * POPUP_RISE / POPUP_RISE_FRAMES;
    s->alpha = p->age < BATTLE_POPUP_FRAMES - POPUP_FADE ? 255 : (BATTLE_POPUP_FRAMES - p->age) * 255 / POPUP_FADE;
    s->x = p->wx - s->w / 2;
    s->y = p->wy - s->h - 1 - rise;
}

/* A reward slot: a free one, otherwise the oldest */
static BattlePopup *new_popup(Battle *bt)
{
    int k = 0;
    for (int i = 0; i < BATTLE_POPUPS; i++) {
        if (!bt->pop[i].on) {
            k = i;
            break;
        }
        if (bt->pop[i].age > bt->pop[k].age)
            k = i;
    }
    BattlePopup *p = &bt->pop[k];
    memset(p, 0, sizeof(*p));
    p->on = 1;
    return p;
}

/* The rewards the game paid (app/ys_reward.h), pinned to the ground where the enemy was last seen */
static void take_rewards(Battle *bt, const RedrawFrame *b)
{
    for (int i = 0; i < b->rewards.n; i++) {
        const YsRewardEvent *e = &b->rewards.ev[i];
        int w = e->walker;
        if (!e->exp && !e->gold)
            continue;
        if (w >= YS_WALKERS || !bt->box_w[w] || bt->actor[w] != e->actor) {
            bt->rewards_unplaced++;
            continue;
        }
        BattlePopup *p = new_popup(bt);
        p->exp = (int)e->exp;
        p->gold = (int)e->gold;
        p->wx = bt->box_x[w] + bt->box_w[w] / 2 + (int)bt->cam_x;
        p->wy = bt->box_y[w] + (int)bt->cam_y;
        bt->popups_made++;
    }
}

void battle_frame(Battle *bt, const RedrawFrame *b)
{
    bt->count = 0;
    const YsList *l = &b->eng.list;
    int x0, raster0, reg;
    if (b->eng.kind == YS_LIST_NONE || !frame_origin(b, &x0, &raster0, &reg)) {
        bt->valid = 0;
        memset(bt->pop, 0, sizeof(bt->pop));
        bt->rewards_unplaced += b->rewards.n;
        return;
    }
    if (bt->valid && l->seq == bt->seq) { /* no new list (pause): the same drawings */
        take_rewards(bt, b);
    } else {
        /* Unwrapped camera; jump (room change, save): start over */
        int jump = !bt->valid;
        if (bt->valid) {
            int dx = wrap(l->cam_x - bt->cam_mx, RING_X), dy = wrap(l->cam_y - bt->cam_my, RING_Y);
            jump = abs(dx) > MOTION_JUMP || abs(dy) > MOTION_JUMP;
            bt->cam_x += dx;
            bt->cam_y += dy;
        }
        bt->cam_mx = l->cam_x;
        bt->cam_my = l->cam_y;
        if (jump) {
            memset(bt->pop, 0, sizeof(bt->pop));
            bt->cam_x = l->cam_x;
            bt->cam_y = l->cam_y;
            for (int w = 0; w < YS_WALKERS; w++)
                bt->hit_age[w] = -1;
        }
        for (int i = 0; i < BATTLE_POPUPS; i++) {
            BattlePopup *p = &bt->pop[i];
            if (p->on && ++p->age >= BATTLE_POPUP_FRAMES)
                p->on = 0;
        }
        for (int w = 0; w < YS_WALKERS; w++) {
            int a = l->w_actor[w], hp = l->w_hp[w], max = l->w_hpmax[w];
            int same = bt->valid && !jump && a == bt->actor[w] && max == bt->max[w];
            if (!same || max == 0 || a >= YS_ACTORS || a == l->hero) { /* Adol (marked by the game) has no bar */
                bt->hit_age[w] = -1;
            } else if (hp < bt->hp[w]) {
                bt->hit_age[w] = hp > 0 ? 0 : -1;
            } else if (hp > bt->hp[w]) {
                bt->hit_age[w] = -1; /* another enemy in the slot */
            } else if (bt->hit_age[w] >= 0 && ++bt->hit_age[w] >= BATTLE_BAR_FRAMES) {
                bt->hit_age[w] = -1;
            }
            int top, left, right;
            if (max && a < YS_ACTORS && l->drawn[a] && actor_box(b, a, x0, raster0, &top, &left, &right)) {
                bt->box_x[w] = left; /* in the image */
                bt->box_y[w] = top;
                bt->box_w[w] = right - left;
            } else if (!same) {
                bt->box_w[w] = 0;
            }
            bt->hp[w] = (uint8_t)hp;
            bt->max[w] = (uint8_t)max;
            bt->actor[w] = (uint8_t)a;
        }
        /* The reward the game paid, with the enemy's box in this list (built before the blow: still alive)
         * or in the last one where it was drawn */
        take_rewards(bt, b);
        bt->seq = l->seq;
        bt->valid = 1;
        /* Some game window in the frame (opening, open, closing or closed in it) */
        bt->window = b->wins.count > 0 || b->wins.gone_n > 0;
    }
    if (bt->window)
        return;
    /* The frame's drawings: the bars and the rewards (positions in the image, the frame's camera) */
    for (int w = 0; w < YS_WALKERS && bt->bars; w++) {
        int a = bt->actor[w];
        if (bt->hit_age[w] < 0 || !bt->box_w[w] || !bt->hp[w] || bt->hp[w] >= bt->max[w] || a >= YS_ACTORS ||
            !l->drawn[a] || (l->flags[a] & 0x08) || bt->count >= BATTLE_STICKERS)
            continue;
        BattleSticker *s = &bt->st[bt->count++];
        make_bar(s, bt->hp[w], bt->max[w]);
        s->actor = a;
        s->x = bt->box_x[w] + bt->box_w[w] / 2 - BAR_W / 2;
        s->y = bt->box_y[w] - BAR_H - 2;
        bt->bars_shown++;
    }
    for (int i = 0; i < BATTLE_POPUPS && bt->popups; i++) {
        BattlePopup *p = &bt->pop[i];
        if (!p->on || bt->count >= BATTLE_STICKERS)
            continue;
        BattleSticker *s = &bt->st[bt->count++];
        BattlePopup q = *p;
        q.wx -= (int)bt->cam_x;
        q.wy -= (int)bt->cam_y;
        make_popup(s, &q, bt->popups);
    }
}

void battle_place(const Battle *bt, const RedrawFrame *b, const MotionLayout *lay, double *out_x, double *out_y)
{
    const YsList *l = &b->eng.list;
    for (int k = 0; k < bt->count; k++) {
        const BattleSticker *s = &bt->st[k];
        double dx = 0.0, dy = 0.0;
        if (lay && lay->exact) {
            if (s->kind == BATTLE_POPUP) {
                dx = -lay->map_ox;
                dy = -lay->map_oy;
            } else {
                /* as the enemy's 1st sprite moved in this image */
                for (int i = 0; i < l->count && i < YS_ACTORS; i++)
                    if (l->owner[i] == s->actor) {
                        dx = lay->sx[i] - ((b->sat[i * 4 + 1] & 0x3FF) - 0x20) - (lay->glued[i] ? lay->map_ox : 0.0);
                        dy = lay->sy[i] - ((b->sat[i * 4] & 0x3FF) - 64) - (lay->glued[i] ? lay->map_oy : 0.0);
                        break;
                    }
            }
        }
        out_x[k] = s->x + dx;
        out_y[k] = s->y + dy;
    }
}

void battle_draw(const Battle *bt, const double *x, const double *y, uint32_t *dst, int w, int h, int sub, int stride)
{
    int clip[4] = { 0, 0, w * sub, h * sub };
    battle_draw_at(bt, x, y, dst, stride, sub, 0, 0, clip, 1 << BATTLE_BAR | 1 << BATTLE_POPUP);
}

void battle_draw_at(const Battle *bt, const double *x, const double *y, uint32_t *dst, int stride, int sub, int ox,
                    int oy, const int clip[4], int kinds)
{
    int cx0 = ox + CROP_MAP_X * sub, cy0 = oy + CROP_MAP_Y * sub;
    int cx1 = ox + (CROP_MAP_X + CROP_MAP_W) * sub, cy1 = oy + (CROP_MAP_Y + CROP_MAP_H) * sub;
    cx0 = cx0 < clip[0] ? clip[0] : cx0;
    cy0 = cy0 < clip[1] ? clip[1] : cy0;
    cx1 = cx1 > clip[2] ? clip[2] : cx1;
    cy1 = cy1 > clip[3] ? clip[3] : cy1;
    for (int k = 0; k < bt->count; k++) {
        const BattleSticker *s = &bt->st[k];
        if (!(kinds & 1 << s->kind))
            continue;
        int qx = ox + (int)floor(x[k] * sub + 0.5), qy = oy + (int)floor(y[k] * sub + 0.5);
        /* Rewards at 3/4 (Fill with the HUD at 3/4), anchored to the same top center */
        int q = bt->small && s->kind == BATTLE_POPUP && sub % 4 == 0 ? 3 : 4;
        int dw = s->w * sub * q / 4, dh = s->h * sub * q / 4, ps = sub * q / 4;
        qx += (s->w * sub - dw) / 2;
        for (int v = 0; v < dh; v++) {
            int ty = qy + v;
            if (ty < cy0 || ty >= cy1)
                continue;
            for (int u = 0; u < dw; u++) {
                int tx = qx + u;
                if (tx < cx0 || tx >= cx1)
                    continue;
                uint32_t c = s->px[(v / ps) * s->w + u / ps];
                if (!(c >> 24))
                    continue;
                uint32_t *d = &dst[ty * stride + tx];
                if (s->alpha >= 255) {
                    *d = c | 0xFF000000u;
                    continue;
                }
                int al = s->alpha;
                uint32_t o = *d, r = 0xFF000000u;
                for (int sh = 0; sh <= 16; sh += 8) {
                    int ch = ((int)(c >> sh & 0xFF) * al + (int)(o >> sh & 0xFF) * (255 - al)) / 255;
                    r |= (uint32_t)ch << sh;
                }
                *d = r;
            }
        }
    }
}
