/* The walkers' masks by the game's rule: see ys_masks.h and docs/ys_notes.md. */
#include "app/ys_masks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_W 16 /* type 1 block: 2 cells x 4 rows */
#define BLOCK_H 32
#define BLOCK_ROWS 4

static int sat_x(const uint16_t *sat, int i) { return (sat[i * 4 + 1] & 0x3FF) - 0x20; }
static int sat_y(const uint16_t *sat, int i) { return (sat[i * 4] & 0x3FF) - 64; }

/* Map lines of f (map_row: 1 per image line): for each VDC line (& 1023), the image line on the map lines and, on the
 * others, that of the nearest map line (the map continues above and below the shown part: the game also reads the
 * cells off screen); all -1 without map lines */
static void bg_rows(const RedrawFrame *f, const uint8_t *map_row, int *row_at)
{
    static int dist[1024];
    int any = 0;
    for (int y = 0; y < 1024; y++) {
        row_at[y] = -1;
        dist[y] = 1 << 20;
    }
    for (int r = 0; r < f->h; r++)
        if (map_row[r] && f->rows[r].valid && (f->rows[r].cr & 0x80)) {
            row_at[f->rows[r].raster & 1023] = r;
            dist[f->rows[r].raster & 1023] = 0;
            any = 1;
        }
    for (int pass = 0; pass < 4 && any; pass++) /* down and up, two rounds (the index wraps around) */
        for (int k = 0; k < 1024; k++) {
            int y = pass & 1 ? 1023 - k : k, p = (y + (pass & 1 ? 1 : 1023)) & 1023;
            if (dist[p] + 1 < dist[y]) {
                dist[y] = dist[p] + 1;
                row_at[y] = row_at[p];
            }
        }
}

/* Map line (in the 256-line ring) of VDC line y of f, and *li the image line that gives the scroll; -1 if none */
static int map_line(const RedrawFrame *f, const int *row_at, int y, const VdcLineInfo **li)
{
    int r = row_at[y & 1023];
    if (r < 0)
        return -1;
    *li = &f->rows[r];
    return ((*li)->bgy & ~(REDRAW_RING - 1)) | (((*li)->bgy + y - (*li)->raster) & (REDRAW_RING - 1));
}

/* Cell row of VDC line y of f; -1 if none */
static int cell_row(const RedrawFrame *f, const int *row_at, int y)
{
    const VdcLineInfo *li;
    int my = map_line(f, row_at, y, &li);
    return my < 0 ? -1 : my >> 3;
}

/* The point (bgx, bgy) of the vram map (size reg) in table t: -1 = tile outside the table or of palette 14/15 (the
 * game puts the full mask, $81:7E18); 0 = in the table with no drawing (empty, $81:7E10); 1 = table cell (*attr, *bit =
 * the pixel in the front drawing) */
static int front_point(const YsFront *t, const uint16_t *vram, int reg, int bgx, int bgy, int *attr, int *bit)
{
    int tx = redraw_tiles_x(reg), ty = redraw_tiles_y(reg);
    uint16_t bat = vram[((bgy >> 3) & (ty - 1)) * tx + ((bgx >> 3) & (tx - 1))];
    uint16_t rel = (uint16_t)((bat & 0x0FFF) - t->base);
    if ((bat >> 8) >= 0xE0 || rel >= t->count)
        return -1;
    if (rel >= 256)
        return 0;
    *attr = t->attr[rel];
    *bit = (t->bits[rel * 8 + (bgy & 7)] >> (7 - (bgx & 7))) & 1;
    return 1;
}

/* Cell rows of the cell on line bgy above the feet row feet (0 = the feet's; negative = below), in the ring */
static int rows_above(int feet, int bgy)
{
    int n = REDRAW_RING / 8, d = (feet - (bgy >> 3)) & (n - 1);
    return d >= n / 2 ? d - n : d;
}

/* Cell of point (x, y) (VDC line) of f, with the VRAM map vram, for whoever has their feet on cell row feet: 0 =
 * no map or outside the block; otherwise 2 + front_point (*attr, *bit) and *d = rows above the feet */
static int cell_at(const RedrawFrame *f, const YsFront *front, const uint16_t *vram, const int *row_at, int x, int y,
                   int feet, int *attr, int *bit, int *d)
{
    const VdcLineInfo *li;
    int my = map_line(f, row_at, y, &li);
    if (my < 0)
        return 0;
    *d = rows_above(feet, my);
    if (*d < 0 || *d >= BLOCK_ROWS)
        return 0;
    return 2 + front_point(front, vram, (li->mwr >> 4) & 7, li->bxr + x, my, attr, bit);
}

/* Block of the checked position (x, y) with the VRAM map vram (the scroll and the tables of f): the mask pixels in
 * rows (BLOCK_H rows, bit u = column u) and whether the game hides it (bit 4 rule) */
static void rule_block(const RedrawFrame *f, const YsFront *front, const uint16_t *vram, const int *row_at, int x, int y,
                       uint32_t *rows, int *hidden)
{
    int feet = cell_row(f, row_at, y + 8 + 24), bits70 = 0xFF;
    memset(rows, 0, BLOCK_H * sizeof(uint32_t));
    if (feet < 0) {
        *hidden = 1;
        return;
    }
    for (int v = 0; v < BLOCK_H; v++)
        for (int u = 0; u < BLOCK_W; u++) {
            int attr = 0, bit = 0, d = 0, k = cell_at(f, front, vram, row_at, x + u, y + 8 + v, feet, &attr, &bit, &d);
            if (!k)
                continue;
            if (k == 1 || (k == 3 && bit && (attr & 7) >= d))
                rows[v] |= 1u << u;
            /* bit 4 rule, once per cell (at its 1st pixel) */
            if ((u & 7) == 0 && (v & 7) == 0) {
                if (k == 1)
                    bits70 &= ~0x10;
                else if (k == 3 && (attr & 7) >= d)
                    bits70 &= attr;
            }
        }
    *hidden = (bits70 & 0x10) != 0;
}

/* Is m a mask of actor c in list l (shown or hidden by the bit 4 rule)? */
static int is_mask(const YsList *l, int m, int c)
{
    return m < YS_ACTORS && m != c && l->drawn[m] && (l->flags[m] & 0x27) == 0x22 && l->pose[m] >= 0x40 &&
           l->pose[m] < 0x60 && abs(l->ax[m] - l->ax[c]) <= 16 && abs(l->ay[m] - 8 - l->ay[c]) <= 16;
}

/* m is a type 1 mask (the walkers' one, drawing $40-$5F) in list l */
static int is_type1(const YsList *l, int m)
{
    return m < YS_ACTORS && l->drawn[m] && (l->flags[m] & 0x27) == 0x22 && l->pose[m] >= 0x40 && l->pose[m] < 0x60;
}

static int frame_mode1(const RedrawFrame *f)
{
    for (int r = 0; r < f->h; r++)
        if (f->rows[r].valid)
            return ((f->rows[r].mwr >> 2) & 3) == 1;
    return 0;
}

/* Mask m as the game shows it in f: the pixels of its sprites (engine list), pinned to the checked position
 * (x, y) = (P.x, P.y - 8); none if the game hides it (bit 3) or put no sprites (off screen) */
static void game_rows(const RedrawFrame *f, int m, uint32_t *rows)
{
    const YsList *l = &f->eng.list;
    int x = l->ax[m], y = l->ay[m] - 8, mode1 = frame_mode1(f);
    memset(rows, 0, YS_MASK_H * sizeof(uint32_t));
    for (int i = 0; i < l->count && i < YS_ACTORS; i++) {
        if (l->owner[i] != m)
            continue;
        uint32_t bits[64];
        int h, ox = sat_x(f->sat, i) - x, oy = sat_y(f->sat, i) - (y + 8);
        redraw_sprite_bits(f->vram, &f->sat[i * 4], mode1, bits, &h);
        for (int v = 0; v < h; v++)
            if (oy + v >= 0 && oy + v < YS_MASK_H && ox > -32 && ox < 32)
                rows[oy + v] |= ox >= 0 ? bits[v] << ox : bits[v] >> -ox;
    }
}

static int sign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/* Step direction of masks cur and nxt of l (both shown or hidden): 1 if it is an 8-pixel step */
static int step_dir(const YsList *l, int cur, int nxt, int *dx, int *dy)
{
    int ddx = l->ax[nxt] - l->ax[cur], ddy = l->ay[nxt] - l->ay[cur];
    if ((ddx && abs(ddx) != 8) || (ddy && abs(ddy) != 8) || (!ddx && !ddy))
        return 0;
    *dx = sign(ddx);
    *dy = sign(ddy);
    return 1;
}

void ys_masks_frame(YsMasks *ms, const RedrawFrame *b, const uint8_t *map_row, int cdx, int cdy, int keep)
{
    ms->nwalkers = 0;
    ms->nblocks = 0;
    /* The previous frame's ones join the earlier ones, in B's screen (pinned to the map: the camera moved cdx, cdy);
     * those too old and the repeated ones (same actor and position: the newest stays) go */
    if (!keep || ms->game_seq != b->seq - 1)
        ms->nhist = ms->ngame = 0;
    static YsGameMask old[YS_MASK_HIST];
    int nold = 0;
    for (int s = 0; s < 2; s++) {
        const YsGameMask *src = s ? ms->hist : ms->game;
        int n = s ? ms->nhist : ms->ngame;
        for (int k = 0; k < n && nold < YS_MASK_HIST; k++) {
            YsGameMask g = src[k];
            g.x -= cdx;
            g.y -= cdy;
            g.age++;
            int dup = g.age > YS_MASK_AGE;
            for (int j = 0; j < nold && !dup; j++)
                dup = old[j].actor == g.actor && old[j].x == g.x && old[j].y == g.y;
            if (!dup)
                old[nold++] = g;
        }
    }
    memcpy(ms->hist, old, (size_t)nold * sizeof(old[0]));
    ms->nhist = nold;
    ms->ngame = 0;
    ms->game_seq = b->seq;
    const YsList *l = &b->eng.list;
    if (b->eng.kind == YS_LIST_NONE || !l->front.count)
        return;
    bg_rows(b, map_row, ms->row_at);
    uint8_t done[YS_ACTORS] = { 0 };
    for (int k = 0; k < YS_WALKERS && ms->nwalkers < YS_MASK_WALKERS; k++) {
        int c = l->w_actor[k];
        if (c >= YS_ACTORS || done[c] || !l->drawn[c] || (l->flags[c] & 0x08))
            continue;
        done[c] = 1;
        int cur = l->w_mask[k], nxt = l->w_next[k];
        if (!is_mask(l, cur, c))
            continue; /* the game doesn't hide this actor */
        int shown = 0;
        for (int i = 0; i < l->count && i < YS_ACTORS && !shown; i++)
            shown = l->owner[i] == c;
        if (!shown)
            continue; /* off screen: the game put neither its sprites nor the masks' */
        ms->walkers++;
        nxt = is_mask(l, nxt, c) ? nxt : -1;
        YsMaskWalker *w = &ms->walker[ms->nwalkers];
        w->dx = w->dy = 0;
        /* The masks the game shows of this walker in B (both go to the next frame's images as A's) */
        int ms_[2] = { cur, nxt };
        for (int j = 0; j < 2; j++)
            if (ms_[j] >= 0 && ms->ngame < YS_MASK_WALKERS * 2) {
                YsGameMask *g = &ms->game[ms->ngame++];
                g->actor = c;
                g->age = 0;
                g->x = l->ax[ms_[j]];
                g->y = l->ay[ms_[j]] - 8;
                game_rows(b, ms_[j], g->rows);
            }
        /* Without the step-end mask in B (the step ended, the next decision is pending): the direction of the last step
         * the game showed of this walker (the image may still be reaching its end) */
        if (nxt >= 0 && step_dir(l, cur, nxt, &w->dx, &w->dy)) {
            ms->last_actor[k] = (uint8_t)c;
            ms->last_dx[k] = (int8_t)w->dx;
            ms->last_dy[k] = (int8_t)w->dy;
        } else if (ms->last_actor[k] == c) {
            w->dx = ms->last_dx[k];
            w->dy = ms->last_dy[k];
        }
        w->actor = c;
        w->px = l->ax[c];
        w->py = l->ay[c];
        w->gx = l->ax[cur];
        w->gy = l->ay[cur] - 8;
        w->has_next = nxt >= 0;
        w->nx = nxt >= 0 ? l->ax[nxt] : 0;
        w->ny = nxt >= 0 ? l->ay[nxt] - 8 : 0;
        w->before = YS_ACTORS;
        for (int i = 0; i < l->count && i < YS_ACTORS; i++) {
            int o = l->owner[i];
            w->skip[i] = (uint8_t)(o == cur || (nxt >= 0 && o == nxt));
            if ((w->skip[i] || o == c) && i < w->before)
                w->before = i;
        }
        for (int i = l->count; i < YS_ACTORS; i++)
            w->skip[i] = 0;
        if (w->before < YS_ACTORS)
            ms->nwalkers++;
    }
}

int ys_masks_find(const YsMasks *ms, int w, int x, int y)
{
    for (int k = 0; k < ms->nblocks; k++)
        if (ms->bw[k] == w && ms->bx[k] == x && ms->by[k] == y)
            return k;
    return -1;
}

int ys_masks_block(YsMasks *ms, const RedrawFrame *b, int w, int x, int y)
{
    int k = ys_masks_find(ms, w, x, y);
    if (k >= 0)
        return k;
    if (ms->nblocks >= YS_MASK_BLOCKS) {
        ms->no_slot++;
        return -1;
    }
    k = ms->nblocks++;
    RedrawMask *mk = &ms->block[k];
    memset(mk, 0, sizeof(*mk));
    /* The pixels the game showed at this checked position (in B, else in A); only with none, the rule with B's map */
    const YsGameMask *g = NULL;
    for (int s = 0; s < 2 && !g; s++)
        for (int j = 0; j < (s ? ms->nhist : ms->ngame) && !g; j++) {
            const YsGameMask *c = s ? &ms->hist[j] : &ms->game[j];
            if (c->actor == ms->walker[w].actor && c->x == x && c->y == y) {
                g = c;
                if (s)
                    ms->from_a++;
                else
                    ms->from_b++;
            }
        }
    ms->rule_made[k] = g == NULL;
    if (g)
        memcpy(mk->rows, g->rows, sizeof(g->rows));
    else {
        int hidden;
        rule_block(b, &b->eng.list.front, b->vram, ms->row_at, x, y, mk->rows, &hidden);
        if (hidden)
            memset(mk->rows, 0, sizeof(mk->rows));
        ms->by_rule++;
    }
    mk->x = x;
    mk->y = y + 8;
    mk->w = BLOCK_W;
    mk->h = BLOCK_H;
    mk->before = ms->walker[w].before;
    ms->bx[k] = x;
    ms->by[k] = y;
    ms->bw[k] = w;
    return k;
}

/* The points of grid g + 8n around v: the one below and the one above; just one if v is on the grid */
static int around(double v, int g, int *out)
{
    double n = (v - g) / 8.0, k = floor(n + 1e-6);
    out[0] = g + 8 * (int)k;
    if (fabs(n - k) < 1e-6)
        return 1;
    out[1] = out[0] + 8;
    return 2;
}

int ys_masks_step(const YsMaskWalker *w, double x, double y, int *bx, int *by)
{
    int xs[2], ys[2], nx = around(x, w->gx, xs), ny = around(y, w->gy, ys), n = 0;
    if (nx == 2 && ny == 2 && w->dx && w->dy) {
        /* diagonal step: of the 4 corners, its start and end */
        int same = w->dx * w->dy > 0;
        bx[0] = xs[0];
        by[0] = same ? ys[0] : ys[1];
        bx[1] = xs[1];
        by[1] = same ? ys[1] : ys[0];
        return 2;
    }
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < ny; j++) {
            bx[n] = xs[i];
            by[n] = ys[j];
            n++;
        }
    return n;
}

int ys_masks_window_actors(const RedrawFrame *b, uint8_t *mark)
{
    const YsList *l = ys_frame_shown_list(&b->eng);
    if (!l)
        return 0;
    int marked = 0;
    for (int j = 0; j < l->count && j < YS_ACTORS; j++) {
        if (!ys_list_window_actor(l, l->owner[j]))
            continue;
        /* by content, as in ys_masks_unwindowed: the list may be behind */
        for (int i = 0; i < 64; i++)
            if (!mark[i] && memcmp(&b->sat[i * 4], &l->sat[j * 4], 4 * sizeof(uint16_t)) == 0) {
                mark[i] = 1;
                marked++;
                break;
            }
    }
    return marked;
}

int ys_masks_unwindowed(const RedrawFrame *b, const uint16_t *vram, const uint8_t *map_row, const int (*boxes)[4],
                        int n, uint8_t *skip, RedrawMask *out, int max)
{
    /* in the late frame that couldn't switch lists, the one the console showed (the one in b's table) */
    const YsList *l = ys_frame_shown_list(&b->eng);
    if (!l || !l->front.count)
        return -1;
    static int row_at[1024], line_at[1024];
    bg_rows(b, map_row, row_at);
    for (int y = 0; y < 1024; y++)
        line_at[y] = -1;
    for (int r = 0; r < b->h; r++)
        if (b->rows[r].valid && (b->rows[r].cr & 0x40))
            line_at[b->rows[r].raster & 1023] = r;
    int count = 0;
    for (int m = 0; m < YS_ACTORS; m++) {
        if (!is_type1(l, m))
            continue;
        /* its sprites in b's table (by content: the list may be behind) and whether one touches a window */
        uint8_t mine[64] = { 0 };
        int touch = 0, before = 64;
        for (int j = 0; j < l->count && j < YS_ACTORS; j++) {
            if (l->owner[j] != m)
                continue;
            for (int i = 0; i < 64; i++) {
                if (mine[i] || memcmp(&b->sat[i * 4], &l->sat[j * 4], 4 * sizeof(uint16_t)) != 0)
                    continue;
                mine[i] = 1;
                before = i < before ? i : before;
                uint16_t fl = b->sat[i * 4 + 3];
                int x0 = sat_x(b->sat, i), y0 = sat_y(b->sat, i);
                int x1 = x0 + ((fl & 0x100) ? 32 : 16), y1 = y0 + ((fl & 0x3000) == 0 ? 16 : (fl & 0x3000) == 0x1000 ? 32 : 64);
                for (int y = y0; y < y1 && !touch; y++) {
                    int r = y >= 0 ? line_at[y & 1023] : -1;
                    if (r < 0)
                        continue;
                    int fx0 = b->rows[r].x0 + x0, fx1 = b->rows[r].x0 + x1 - 1;
                    for (int k = 0; k < n; k++)
                        touch |= r >= boxes[k][1] && r <= boxes[k][3] && fx1 >= boxes[k][0] && fx0 <= boxes[k][2];
                }
                break;
            }
        }
        if (!touch)
            continue;
        for (int i = 0; i < 64; i++)
            skip[i] |= mine[i];
        if (count >= max)
            continue;
        RedrawMask *mk = &out[count++];
        memset(mk, 0, sizeof(*mk));
        int hidden;
        rule_block(b, &l->front, vram ? vram : b->vram, row_at, l->ax[m], l->ay[m] - 8, mk->rows, &hidden);
        if (hidden)
            memset(mk->rows, 0, sizeof(mk->rows));
        mk->x = mk->xq = l->ax[m];
        mk->y = mk->yq = l->ay[m];
        mk->w = BLOCK_W;
        mk->h = BLOCK_H;
        mk->before = before;
    }
    return count;
}
