#include "app/redraw.h"
#include "app/crop.h"
#include "app/ys_masks.h"
#include "util/u8file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The same as the VDC's (pce/vdc.c) */
static const int k_screen_tiles_x[8] = { 32, 64, 128, 128, 32, 64, 128, 128 };
static const int k_screen_tiles_y[8] = { 32, 32, 32, 32, 64, 64, 64, 64 };
static const int k_sprite_width[2] = { 16, 32 };
static const int k_sprite_height[4] = { 16, 32, 64, 64 };
static const int k_sprite_mask_width[2] = { 0xFFFF, 0xFFFE };
static const int k_sprite_mask_height[4] = { 0xFFFF, 0xFFFD, 0xFFF9, 0xFFF9 };

#define LINE_MAX (VDC_MAX_WIDTH * REDRAW_SUB)

void redraw_capture(RedrawFrame *f, VdcLineLog *log, const Vce *vce, long seq)
{
    memcpy(f->color, vce->color, sizeof(f->color));
    for (int i = 0; i < 512; i++)
        f->rgb[i] = vce_rgb(f->color[i]);
    for (int c = 0; c < 3; c++)
        for (int l = 0; l < 8; l++)
            f->chan[c][l] = (uint8_t)(l * 255 / 7);
    if (log && log->last_row >= 0 && !log->overflow) {
        memcpy(f->vram, log->vram, sizeof(f->vram));
        memcpy(f->sat, log->sat, sizeof(f->sat));
        memcpy(f->rows, log->row, sizeof(f->rows));
        f->write_count = log->write_count;
        memcpy(f->writes, log->writes, (size_t)log->write_count * sizeof(VdcLogWrite));
    } else {
        memset(f->rows, 0, sizeof(f->rows)); /* nothing to redraw: the image goes as it came */
        f->write_count = 0;
    }
    if (log)
        log->last_row = -1; /* the next frame starts from scratch */
    f->w = vce->frame_width;
    f->h = vce->frame_height;
    if (f->w > VCE_MAX_WIDTH || f->h > VCE_ACTIVE_LINES)
        f->w = f->h = 0;
    memcpy(f->pixels, vce->frame, (size_t)f->w * f->h * sizeof(uint32_t));
    f->divider = vce->clock_divider;
    f->seq = seq;
    f->exact = -1;
    f->exact_known = 0;
    f->torn_win = 0;
}

int redraw_init(Redraw *s)
{
    memset(s, 0, sizeof(*s));
    s->out = malloc((size_t)VCE_MAX_WIDTH * REDRAW_SUB * VCE_ACTIVE_LINES * REDRAW_SUB * sizeof(uint32_t));
    s->pal_seq = -1;
    return s->out ? 0 : -1;
}

void redraw_free(Redraw *s)
{
    free(s->out);
    free(s->from_sprite);
    s->out = NULL;
    s->from_sprite = NULL;
}

int redraw_tiles_y(int reg) { return k_screen_tiles_y[reg & 7]; }
int redraw_tiles_x(int reg) { return k_screen_tiles_x[reg & 7]; }

/* ---- Drawing ---- */

/* n background pixels from (x, y) of the map (9-bit color: palette << 4 | index), like the VDC's
 * render_background; with mask, the pixels of the marked cells (!= 0 and without YS_CELL_FROM, ys_windows.h) get
 * 0x400 */
static void bg_row(const uint16_t *vram, int reg, int x, int y, int n, uint16_t *out, const uint8_t *mask)
{
    /* A plane byte spread over the 8 pixels (pixel p in byte p of 64 bits): the 4 planes at once */
    static uint64_t spread[256];
    if (!spread[255]) /* the last one filled */
        for (int v = 0; v < 256; v++) {
            uint64_t s = 0;
            for (int p = 0; p < 8; p++)
                s |= (uint64_t)((v >> (7 - p)) & 1) << (8 * p);
            spread[v] = s;
        }
    int tiles_x = k_screen_tiles_x[reg], mask_x = tiles_x * 8 - 1;
    y &= k_screen_tiles_y[reg] * 8 - 1;
    int tile_y = y & 7, bat_offset = (y >> 3) * tiles_x;
    x &= mask_x;
    int i = 0;
    while (i < n) {
        uint16_t bat = vram[bat_offset + (x >> 3)];
        int tile = (bat & 0x07FF) << 4;
        uint16_t a = vram[tile + tile_y], c = vram[tile + tile_y + 8];
        uint16_t palette = (uint16_t)((bat >> 12) << 4);
        if (mask && mask[bat_offset + (x >> 3)] && !(mask[bat_offset + (x >> 3)] & YS_CELL_FROM))
            palette |= 0x400;
        uint64_t pix = spread[a & 0xFF] | spread[a >> 8] << 1 | spread[c & 0xFF] << 2 | spread[c >> 8] << 3;
        for (int p = x & 7; p < 8 && i < n; p++, i++)
            out[i] = (uint16_t)(palette | (int)(pix >> (8 * p) & 0xF));
        x = (x + 8 - (x & 7)) & mask_x;
    }
}

typedef struct SprLine {
    int index, xq; /* xq: column of the 1st pixel, in 1/k */
    uint16_t flags;
    uint16_t palette;
    uint16_t data[4];
    const uint16_t *eq; /* Adol's line with the equipment (16 pixels in VRAM order), or NULL */
} SprLine;

void redraw_equip_begin(Redraw *s, const RedrawFrame *b)
{
    s->eq_frame = b;
    memset(s->eq_done, 0, sizeof(s->eq_done));
}

const uint16_t *redraw_equip(Redraw *s, const uint16_t *vram, const uint16_t *e, int i)
{
    if (!s->equip || !s->eq_frame)
        return NULL;
    if (!s->eq_done[i]) {
        s->eq_done[i] = 1;
        /* Only the sprite of Adol's character, in the list that matches the frame's sprite table */
        const YsList *l = ys_frame_shown_list(&s->eq_frame->eng);
        s->eq_img[i] = l && l->hero != YS_NO_ACTOR && l->owner[i] == l->hero
                           ? equip_sprite(s->equip, &s->eq_frame->equip, &s->eq_frame->equip_colors, vram, e,
                                          l->pose[l->hero])
                           : NULL;
    }
    return s->eq_img[i];
}

int redraw_sprite_bits(const uint16_t *vram, const uint16_t *e, int mode1, uint32_t *rows, int *h)
{
    uint16_t flags = e[3];
    int cgx = (flags >> 8) & 1, cgy = (flags >> 12) & 3, height = k_sprite_height[cgy], w = k_sprite_width[cgx];
    uint16_t pattern = (uint16_t)((e[2] >> 1) & 0x3FF);
    pattern &= (uint16_t)(k_sprite_mask_width[cgx] & k_sprite_mask_height[cgy]);
    int mode1_offset = mode1 ? (e[2] & 1) << 5 : 0, x_flip = (flags & 0x0800) != 0;
    for (int r = 0; r < height; r++) {
        int y = (flags & 0x8000) ? height - 1 - r : r;
        uint16_t line_start = (uint16_t)((pattern << 6) + (y >> 4) * 128 + (y & 0xF) + mode1_offset);
        rows[r] = 0;
        for (int half = 0; half < w / 16; half++) {
            uint16_t line = w == 32 ? (uint16_t)(line_start + ((half == 0) == x_flip ? 64 : 0)) : line_start;
            uint16_t opaque = 0;
            for (int p = 0; p < (mode1 ? 2 : 4); p++) {
                uint16_t addr = (uint16_t)(line + p * 16);
                opaque |= addr < VDC_VRAM_SIZE ? vram[addr] : 0;
            }
            for (int x = 0; x < 16; x++)
                if ((opaque >> (x_flip ? x : 15 - x)) & 1)
                    rows[r] |= 1u << (half * 16 + x);
        }
    }
    *h = height;
    return w;
}

/* Masks (masks, n) on sub-line yq with entry number >= before_min (redraw.h, RedrawMask): the sprites already
 * drawn in spl vanish at their pixels */
static void apply_masks(Redraw *s, const RedrawMask *masks, int n, int ks, int yq, int before_min, int u0, int u1,
                        uint16_t *spl, uint8_t *done)
{
    for (int k = 0; k < n; k++) {
        const RedrawMask *mk = &masks[k];
        if (done[k] || mk->before < before_min)
            continue;
        done[k] = 1;
        if (!(mk->yq <= yq && mk->yq + (mk->h << ks) > yq))
            continue;
        uint32_t bits = mk->rows[(yq - mk->yq) >> ks];
        int from = mk->xq > u0 ? mk->xq : u0, to = mk->xq + (mk->w << ks);
        to = to > u1 ? u1 : to;
        for (int u = from; u < to; u++) {
            if (!((bits >> ((u - mk->xq) >> ks)) & 1))
                continue;
            if (spl[u]) {
                spl[u] = 0;
                s->mask_hidden++;
            }
        }
    }
}

/* Sprites of sub-line yq (VDC line in 1/k), like the VDC's fetch_sprites (16-per-line limit) */
static int fetch_line(Redraw *s, int ks, int yq, int mode1, const int *sx_q, const int *sy_q, SprLine *out)
{
    int count = 0;
    for (int i = 0; i < 64; i++) {
        const uint16_t *e = &s->sat[i * 4];
        uint16_t flags = e[3];
        int cgy = (flags >> 12) & 3, height = k_sprite_height[cgy];
        if (!(sy_q[i] <= yq && sy_q[i] + (height << ks) > yq))
            continue;
        int y = (yq - sy_q[i]) >> ks;
        if (count >= 16)
            break;
        int mode1_offset = mode1 ? (e[2] & 1) << 5 : 0;
        int cgx = (flags >> 8) & 1;
        uint16_t pattern = (uint16_t)((e[2] >> 1) & 0x3FF);
        pattern &= (uint16_t)k_sprite_mask_width[cgx];
        pattern &= (uint16_t)k_sprite_mask_height[cgy];
        uint16_t address = (uint16_t)(pattern << 6);
        int x_flip = (flags & 0x0800) != 0;
        if (flags & 0x8000)
            y = height - 1 - y;
        uint16_t line_start = (uint16_t)(address + (y >> 4) * 128 + (y & 0xF) + mode1_offset);
        int halves = k_sprite_width[cgx] == 16 ? 1 : 2;
        for (int half = 0; half < halves; half++) {
            if (half == 1 && ++count >= 16)
                return count;
            uint16_t line = line_start;
            if (halves == 2)
                line = (uint16_t)(line_start + ((half == 0) == x_flip ? 64 : 0));
            SprLine *sp = &out[count];
            sp->index = i;
            sp->xq = sx_q[i] + ((half * 16) << ks);
            sp->flags = flags;
            sp->palette = (uint16_t)((flags & 0x0F) << 4);
            const uint16_t *eq = halves == 1 ? redraw_equip(s, s->vram, e, i) : NULL;
            sp->eq = eq ? eq + y * 16 : NULL;
            for (int p = 0; p < 4; p++) {
                uint16_t addr = (uint16_t)(line + p * 16);
                sp->data[p] = (mode1 && p >= 2) || addr >= VDC_VRAM_SIZE ? 0 : s->vram[addr];
            }
        }
        count++;
    }
    return count;
}

/* Draws b at 1/k pixel (k = 1 << ks): horizontal scroll of each line bx_q (in 1/k), map line
 * of each sub-line bg_line[(r << ks) + j] (in pixels) and position of each sprite sx_q/sy_q (VDC
 * coordinates: x from the 1st visible pixel, y in VDC lines, in 1/k). */
static void draw(Redraw *s, const RedrawFrame *b, int ks, const int *bx_q, const int *bg_line, const int *sx_q,
                 const int *sy_q, uint32_t *out)
{
    int k = 1 << ks, w = b->w, ow = w << ks;
    if (s->pal_seq != b->seq || s->pal_fade != s->fade) {
        s->pal_seq = b->seq;
        s->pal_fade = s->fade;
        for (int i = 0; i < 512; i++)
            s->pal[i] = s->fade ? b->rgb[i] : vce_rgb(b->color[i]);
    }
    static uint16_t bgl[LINE_MAX], spl[LINE_MAX], src[VDC_MAX_WIDTH + 2];
    SprLine spr[16];
    memcpy(s->vram, b->vram, sizeof(s->vram));
    memcpy(s->sat, b->sat, sizeof(s->sat));
    redraw_equip_begin(s, b);
    const RedrawMask *masks = s->masks;
    int nmasks = masks ? s->nmasks : 0;
    int next_write = 0, win_reg = -1;
    const uint8_t *win = NULL;
    uint8_t keep[64] = { 0 }; /* sprites in front of the windows (redraw_windows) */
    int spr_only = s->layer == REDRAW_LAYER_SPRITES; /* the sprites without the background (Redraw.layer) */
    uint16_t bg_hides = spr_only ? 0 : 0x0F;
    if (s->window_front) /* game windows at the start of the image, at its map size */
        for (int r = 0; r < b->h && win_reg < 0; r++)
            if (b->rows[r].valid) {
                win_reg = (b->rows[r].mwr >> 4) & 7;
                win = redraw_windows(s, b, win_reg, keep);
            }
    for (int r = 0; r < b->h; r++) {
        /* What the game wrote before the VDC drew this line */
        for (; next_write < b->write_count && b->writes[next_write].row <= r; next_write++) {
            const VdcLogWrite *wr = &b->writes[next_write];
            if (wr->addr < VDC_VRAM_SIZE)
                s->vram[wr->addr] = wr->value;
            else
                s->sat[(wr->addr - VDC_VRAM_SIZE) & (VDC_SAT_SIZE - 1)] = wr->value;
            if (win) /* window cells the game rewrote */
                ys_windows_write(&b->wins, s->win, k_screen_tiles_x[win_reg], k_screen_tiles_y[win_reg], wr->addr, wr->row);
        }
        const uint32_t *real = b->pixels + r * w;
        const VdcLineInfo *li = &b->rows[r];
        int reg = (li->mwr >> 4) & 7, mode1 = ((li->mwr >> 2) & 3) == 1;
        if (s->window_front && li->valid && reg != win_reg) { /* map size change mid-image */
            win_reg = reg;
            win = redraw_windows(s, b, reg, keep);
        }
        int base = li->x0 << ks;
        int u0 = base < 0 ? -base : 0, u1 = li->width << ks;
        if (u1 > ow - base)
            u1 = ow - base;
        int prev_bg_y = -1, prev_n = -1;
        SprLine prev_spr[16];
        for (int j = 0; j < k; j++) {
            uint32_t *o = out + ((r << ks) + j) * ow;
            /* Pixels that came from a sprite (only when drawing into s->out, if requested) */
            uint8_t *m = out == s->out && s->from_sprite ? s->from_sprite + ((r << ks) + j) * ow : NULL;
            if (m)
                memset(m, 0, (size_t)ow);
            if (!li->valid || u1 <= u0) {
                for (int x = 0; x < w; x++)
                    for (int t = 0; t < k; t++)
                        o[(x << ks) + t] = spr_only ? 0 : real[x];
                continue;
            }
            int bg_y = (li->cr & 0x80) ? bg_line[(r << ks) + j] & 0xFFFF : -2;
            memset(spr, 0, sizeof(spr));
            int n = (li->cr & 0x40) && s->layer != REDRAW_LAYER_BG ? fetch_line(s, ks, (li->raster << ks) + j, mode1, sx_q, sy_q, spr) : 0;
            if (j > 0 && !nmasks && bg_y == prev_bg_y && n == prev_n && memcmp(spr, prev_spr, sizeof(spr)) == 0) {
                memcpy(o, o - ow, (size_t)ow * sizeof(uint32_t)); /* same map and sprite line */
                if (m)
                    memcpy(m, m - ow, (size_t)ow);
                continue;
            }
            prev_bg_y = bg_y;
            prev_n = n;
            memcpy(prev_spr, spr, sizeof(spr));
            for (int x = 0; x < w; x++)
                for (int t = 0; t < k; t++)
                    o[(x << ks) + t] = spr_only ? 0 : real[x];
            if (li->cr & 0x80) {
                int xq = bx_q[r], first = (xq + u0) >> ks;
                bg_row(s->vram, reg, first, bg_line[(r << ks) + j], ((xq + u1 - 1) >> ks) - first + 1, src, win);
                for (int u = u0; u < u1; u++)
                    bgl[u] = src[((xq + u) >> ks) - first];
                const RedrawSides *sd = s->sides;
                if (sd && sd->rows[r]) { /* the strip outside B's map (the rest outside keeps the frame: keep_sides) */
                    int b_left = li->bxr + sd->l - li->x0, wmask = k_screen_tiles_x[reg] * 8 - 1, last = -1;
                    uint16_t v = 0;
                    for (int u = u0; u < u1; u++) {
                        int col = (base + u) >> ks, bgx = (xq + u) >> ks;
                        if (col < sd->l || col >= sd->r || ((bgx - b_left) & wmask) < sd->r - sd->l)
                            continue;
                        if (bgx != last)
                            v = redraw_side_pixel(sd, s->vram, reg, b_left, bgx, bg_line[(r << ks) + j], win);
                        last = bgx;
                        bgl[u] = v;
                    }
                }
            } else {
                for (int u = u0; u < u1; u++)
                    bgl[u] = 0x100;
            }
            if (n > 0)
                memset(spl + u0, 0, (size_t)(u1 - u0) * sizeof(uint16_t));
            /* From last to first: the lower-numbered sprite stays on top */
            uint8_t mask_done[REDRAW_DRAW_MASKS] = { 0 };
            int yq = (li->raster << ks) + j;
            for (int i = n - 1; i >= 0; i--) {
                const SprLine *sp = &spr[i];
                if (nmasks)
                    apply_masks(s, masks, nmasks, ks, yq, sp->index + 1, u0, u1, spl, mask_done);
                if (s->skip && s->skip[sp->index])
                    continue;
                uint16_t opaque = sp->data[0] | sp->data[1] | sp->data[2] | sp->data[3];
                if (!opaque && !sp->eq)
                    continue;
                int priority = (sp->flags & 0x0080) != 0, x_flip = (sp->flags & 0x0800) != 0;
                int from = sp->xq > u0 ? sp->xq : u0, to = sp->xq + (16 << ks);
                if (to > u1)
                    to = u1;
                for (int u = from; u < to; u++) {
                    int x = (u - sp->xq) >> ks, px = x_flip ? x : 15 - x;
                    uint16_t keep_bit = (uint16_t)((keep[sp->index] ? 0x800 : 0) | (spr_only && !priority ? 0x2000 : 0));
                    if (sp->eq) { /* Adol with the equipment: direct color (0x1000) */
                        uint16_t v = sp->eq[15 - px];
                        if (!v)
                            continue;
                        spl[u] = !priority && (bgl[u] & bg_hides) ? 0x101 : (uint16_t)((v & 0x1FF) | 0x1000 | 0x200 | keep_bit);
                        continue;
                    }
                    if (!((opaque >> px) & 1))
                        continue;
                    uint16_t pixel = (uint16_t)(((sp->data[0] >> px) & 1) | (((sp->data[1] >> px) & 1) << 1) |
                                                (((sp->data[2] >> px) & 1) << 2) | (((sp->data[3] >> px) & 1) << 3));
                    if (!priority && (bgl[u] & bg_hides))
                        spl[u] = (uint16_t)(pixel | 0x100); /* behind the background */
                    else
                        spl[u] = (uint16_t)(pixel | sp->palette | 0x100 | 0x200 | keep_bit);
                }
            }
            if (nmasks && n > 0)
                apply_masks(s, masks, nmasks, ks, yq, 0, u0, u1, spl, mask_done);
            uint32_t *dst = o + base;
            for (int u = u0; u < u1; u++) {
                /* the item window (0x400) stays in front of the sprites, except those inside it (0x800) */
                int sprite = n > 0 && (spl[u] & 0x200) && (!(bgl[u] & 0x400) || (spl[u] & 0x800));
                uint16_t p = sprite ? (uint16_t)(spl[u] & 0x1FF) : bgl[u];
                dst[u] = sprite && (spl[u] & 0x1000) ? (s->fade ? redraw_direct_rgb(b, p) : vce_rgb(p)) : s->pal[(p & 0x0F) ? (p & 0x1FF) : 0];
                if (m)
                    m[base + u] = (uint8_t)sprite;
                if (s->layer == REDRAW_LAYER_BG) /* alpha 0x80 where the background is transparent */
                    dst[u] = (p & 0x0F) ? dst[u] : (dst[u] & 0xFFFFFFu) | 0x80000000u;
                else if (spr_only) /* alpha 0 without a sprite; 0x80 the sprite that stays behind the opaque background */
                    dst[u] = !sprite ? 0 : (spl[u] & 0x2000) ? (dst[u] & 0xFFFFFFu) | 0x80000000u : dst[u] | 0xFF000000u;
            }
        }
    }
}

void redraw_draw(Redraw *s, const RedrawFrame *b, int ks, const int *bx_q, const int *bg_line, const int *sx_q,
                 const int *sy_q, uint32_t *out)
{
    draw(s, b, ks, bx_q, bg_line, sx_q, sy_q, out);
}

static int sprite_y(const RedrawFrame *b, int i) { return (b->sat[i * 4] & 0x3FF) - 64; }
static int sprite_x(const RedrawFrame *b, int i) { return (b->sat[i * 4 + 1] & 0x3FF) - 0x20; }

void redraw_frame(Redraw *s, const RedrawFrame *b, uint32_t *out)
{
    static int bx[VCE_ACTIVE_LINES], by[VCE_ACTIVE_LINES], sx[64], sy[64];
    for (int r = 0; r < b->h; r++) {
        bx[r] = b->rows[r].bxr;
        by[r] = b->rows[r].bgy;
    }
    for (int i = 0; i < 64; i++) {
        sx[i] = sprite_x(b, i);
        sy[i] = sprite_y(b, i);
    }
    draw(s, b, 0, bx, by, sx, sy, out);
}

void redraw_bg_row(const uint16_t *vram, int reg, int x, int y, int n, uint16_t *out, const uint8_t *mask)
{
    bg_row(vram, reg, x, y, n, out, mask);
}

uint16_t redraw_side_pixel(const RedrawSides *sd, const uint16_t *vram, int reg, int b_left, int bgx, int bgy,
                           const uint8_t *win)
{
    int wmask = k_screen_tiles_x[reg] * 8 - 1, w = sd->r - sd->l;
    uint16_t v;
    bgx &= wmask;
    if (sd->a_vram && ((bgx - sd->a_left) & wmask) < w) {
        bg_row(sd->a_vram, reg, bgx, bgy, 1, &v, NULL);
    } else {
        int d = (bgx - b_left) & wmask, left = wmask + 1 - d < d - (w - 1); /* closer to the 1st map column */
        bg_row(vram, reg, (left ? b_left : b_left + w - 1) & wmask, bgy, 1, &v, win);
    }
    return v;
}

/* own[i] = 1 on b's sprites that belong to the game windows (cursor, the equipment's pink frames, inventory
 * icons: engine characters without bit 5 inside a window, RedrawFrame.wins.own), found in the shown table
 * by the identical entry (the engine list may not be the table's) */
static void wins_own(const RedrawFrame *b, uint8_t *own)
{
    for (int i = 0; i < 64; i++) {
        own[i] = 0;
        for (int j = 0; b->wins.sprites_ok && j < YS_ACTORS && !own[i]; j++)
            own[i] = b->wins.own[j] && memcmp(&b->wins.sat[j * 4], &b->sat[i * 4], 4 * sizeof(uint16_t)) == 0;
    }
}

/* The sprites in front of the windows: the windows' in the frame's list or the previous one's (the table shown at the
 * start of the image may be the latter's) */
static void wins_front(const RedrawFrame *b, uint8_t *keep)
{
    wins_own(b, keep);
    for (int i = 0; i < 64; i++)
        for (int j = 0; b->wins.prev_sprites_ok && j < YS_ACTORS && !keep[i]; j++)
            keep[i] = b->wins.prev_own[j] && memcmp(&b->wins.prev_sat[j * 4], &b->sat[i * 4], 4 * sizeof(uint16_t)) == 0;
}

const uint8_t *redraw_windows(Redraw *s, const RedrawFrame *b, int reg, uint8_t *keep)
{
    memset(keep, 0, 64);
    if (!ys_windows_cells(&b->wins, k_screen_tiles_x[reg], k_screen_tiles_y[reg], s->win))
        return NULL;
    wins_front(b, keep);
    return s->win;
}

void redraw_check(Redraw *s, RedrawFrame *b)
{
    static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    if (b->w <= 0 || b->h <= 0) {
        b->exact = 0;
        return;
    }
    if (!b->exact_known) {
        redraw_frame(s, b, img);
        b->exact = memcmp(img, b->pixels, (size_t)b->w * b->h * sizeof(uint32_t)) == 0;
    }
    b->exact_known = 0;
    s->frames++;
    s->exact_frames += b->exact;
}

static FILE *ring_log_file(void)
{
    static FILE *lf;
    static int tried;
    if (!tried) {
        tried = 1;
        const char *p = getenv("PCE_RING_LOG");
        lf = p ? u8_fopen(p, "w") : NULL;
    }
    return lf;
}

static uint32_t ring_log_sum(const void *data, size_t bytes)
{
    const uint8_t *d = data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < bytes; i++)
        h = (h ^ d[i]) * 16777619u;
    return h;
}

/* own = 1 on b's sprites that belong to the game windows (wins_own). skip = those and the masks that touch one of the
 * windows (x0, y0, x1, y1 in b's pixels): with it open, Ys rebuilds the masks ($80:4F78) and each character
 * under it gets in front a sprite behind the background, which erases it; around the shrunk window he must show */
static void ring_skip(RedrawRing *rr, const RedrawFrame *b, const RedrawFrame *rule_frame, int n, const int (*boxes)[4],
                      uint8_t *skip, uint8_t *own, uint8_t *hole, const uint8_t *map_row)
{
    /* The characters that only exist because of the windows (in the mine, the one that punches the light mask in the
     * window's cells): around it, with the map from before it, the mask stays whole */
    memset(hole, 0, 64);
    int nh = ys_masks_window_actors(b, hole);
    rr->hole_sprites += nh;
    rr->hole_frames += nh > 0;
    /* The game's masks that touch a window, rebuilt by the rule with the map from before it */
    uint8_t remade[64] = { 0 };
    int nm = map_row ? ys_masks_unwindowed(rule_frame, rr->keep_ok ? rr->keep : NULL, map_row, boxes, n, remade, rr->masks,
                                          REDRAW_BEHIND)
                    : -1;
    rr->nmasks = nm > 0 ? nm : 0;
    rr->no_list += nm < 0;
    rr->remade += rr->nmasks;
    rr->remade_frames += rr->nmasks > 0;
    wins_own(b, own);
    /* Which sprite is a mask comes only from the engine list. With no list at all (never with a window in the recorded sessions), the
     * game's masks stay: before, all sprites behind the background that touched a window were removed, with nothing in their place */
    for (int i = 0; i < 64; i++)
        skip[i] = (uint8_t)(own[i] || hole[i] || remade[i]);
    { /* PCE_RING_LOG=FILE: per frame with a window, the boxes, the list used (lista=0 outside the engine, 1 the frame's, 2 the one
       * the console showed), the rebuilt masks (m), the sprite table checksum (sat) and the sprites left out
       * (position, attribute, pattern, from the window); ring_log_layer appends the sprite layer checksum */
        FILE *lf = ring_log_file();
        if (lf) {
            fprintf(lf, "F %d", (int)b->seq);
            for (int k = 0; k < n; k++)
                fprintf(lf, " B%d,%d,%d,%d", boxes[k][0], boxes[k][1], boxes[k][2], boxes[k][3]);
            fprintf(lf, " lista=%d m=%d sat=%08X\n",
                    b->eng.kind != YS_LIST_NONE ? 1 : ys_frame_shown_list(&b->eng) ? 2 : 0, rr->nmasks,
                    ring_log_sum(b->sat, sizeof(b->sat)));
            for (int i = 0; i < 64; i++)
                if (skip[i])
                    fprintf(lf, " %d %d,%d a%04X p%03X o%d\n", i, sprite_x(b, i), sprite_y(b, i), b->sat[i * 4 + 3],
                            b->sat[i * 4 + 2], own[i]);
        }
    }
}

/* skip = 1 on b's sprites identical to one that was entirely inside a window up to RING_GHOST_FRAMES frames before
 * (RedrawRing.ghost); then stores b's with own = 1 (NULL: none). Returns how many it marked */
static int ring_ghosts(RedrawRing *r, const RedrawFrame *b, const uint8_t *own, uint8_t *skip)
{
    int n = 0, any = 0;
    for (int k = 0; k < r->ghost_n; k++) /* old ones are dropped */
        if (b->seq >= r->ghost_seq[k] && b->seq - r->ghost_seq[k] <= RING_GHOST_FRAMES) {
            memcpy(r->ghost[n], r->ghost[k], sizeof(r->ghost[0]));
            r->ghost_seq[n++] = r->ghost_seq[k];
        }
    r->ghost_n = n;
    for (int i = 0; i < 64; i++)
        for (int k = 0; k < r->ghost_n; k++)
            if (memcmp(&b->sat[i * 4], r->ghost[k], sizeof(r->ghost[k])) == 0) {
                any += !skip[i];
                skip[i] = 1;
                break;
            }
    for (int i = 0; own && i < 64; i++) {
        if (!own[i])
            continue;
        int k = 0;
        while (k < r->ghost_n && memcmp(&b->sat[i * 4], r->ghost[k], sizeof(r->ghost[k])) != 0)
            k++;
        if (k == r->ghost_n) {
            if (r->ghost_n == 64)
                continue;
            memcpy(r->ghost[r->ghost_n++], &b->sat[i * 4], sizeof(r->ghost[0]));
        }
        r->ghost_seq[k] = b->seq;
    }
    return any;
}

/* The whole image in a single scroll (docs/ys_notes.md, "Large menu screens"): the frame above the map and the middle
 * of the map follow the same scroll; in normal play the frame comes from lines 256+ of the background map and the map from the lower ones */
static int ring_one_scroll(const RedrawFrame *b)
{
    int r0 = CROP_MAP_Y - 2, r1 = CROP_MAP_Y + CROP_MAP_H / 2;
    const VdcLineInfo *top = &b->rows[r0], *mid = &b->rows[r1];
    return top->valid && mid->valid && top->bxr == mid->bxr && ((mid->bgy - top->bgy - (r1 - r0)) & 511) == 0;
}

void redraw_bg_only(const RedrawFrame *b, int end, uint32_t *out)
{
    static Redraw s;      /* drawing only */
    static RedrawFrame t; /* b with the VRAM at the end of the frame */
    const RedrawFrame *f = b;
    if (end) {
        memcpy(&t, b, sizeof(t));
        for (int i = 0; i < b->write_count; i++)
            if (b->writes[i].addr < VDC_VRAM_SIZE)
                t.vram[b->writes[i].addr] = b->writes[i].value;
        t.write_count = 0;
        f = &t;
    }
    s.layer = REDRAW_LAYER_BG;
    redraw_frame(&s, f, out);
    for (int i = 0; i < b->w * b->h; i++)
        out[i] |= 0xFF000000u;
}

int redraw_ring(RedrawRing *r, const RedrawFrame *b, int windows, const int (*boxes)[4], Equip *equip, uint32_t *bg,
                uint32_t *spr, const uint32_t *bg_shown, uint32_t *fix, const uint8_t *map_row)
{
    static Redraw s;      /* drawing only: without s->out, masks and edges */
    static RedrawFrame t; /* b with the VRAM from before the windows */
    static uint8_t skip[64], own[64], hole[64], gone[64], mine_skip[64];
    static uint32_t other[VCE_MAX_WIDTH * VCE_ACTIVE_LINES], mine[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    if (!windows) {
        r->skip_seq = -1;
        r->nmasks = 0;
        memset(r->skip, 0, sizeof(r->skip));
        memset(r->own, 0, sizeof(r->own));
        if (ring_ghosts(r, b, NULL, r->skip))
            r->skip_seq = b->seq;
        if (r->next_ok) {
            memcpy(r->keep, r->next, sizeof(r->keep));
            r->keep_ok = 1;
        }
        memcpy(r->next, b->vram, sizeof(r->next));
        memcpy(r->keep_rows, r->next_rows, sizeof(r->keep_rows));
        memcpy(r->keep_map, r->next_map, sizeof(r->keep_map));
        memcpy(r->next_rows, b->rows, sizeof(r->next_rows));
        if (map_row)
            memcpy(r->next_map, map_row, sizeof(r->next_map));
        else
            memset(r->next_map, 0, sizeof(r->next_map));
        r->next_ok = 1;
        return 0;
    }
    r->next_ok = 0;
    s.equip = equip;
    s.layer = REDRAW_LAYER_SPRITES;
    /* the map from before the windows with its own scroll, if the current one is different (ring_one_scroll) */
    int one = r->keep_ok && ring_one_scroll(b);
    if (one) {
        memcpy(&t, b, sizeof(t));
        memcpy(t.rows, r->keep_rows, sizeof(t.rows));
        r->one_scroll_frames++;
    }
    ring_skip(r, b, one ? &t : b, windows, boxes, skip, own, hole, one ? r->keep_map : map_row);
    /* the list that still comes out after the window closes brings both back */
    for (int i = 0; i < 64; i++)
        gone[i] = (uint8_t)(own[i] | hole[i]);
    ring_ghosts(r, b, gone, skip);
    memcpy(r->skip, skip, sizeof(r->skip));
    memcpy(r->own, own, sizeof(r->own));
    r->skip_seq = b->seq;
    s.skip = skip;
    s.masks = r->nmasks ? r->masks : NULL; /* the ones rebuilt without the window (ys_masks_unwindowed: xq/yq = x/y) */
    s.nmasks = r->nmasks;
    long hid0 = s.mask_hidden;
    redraw_frame(&s, b, spr);
    if (ring_log_file()) {
        fprintf(ring_log_file(), " S %08X\n", ring_log_sum(spr, (size_t)b->w * b->h * sizeof(uint32_t)));
        fflush(ring_log_file());
    }
    r->hidden_px += s.mask_hidden - hid0;
    s.masks = NULL;
    s.nmasks = 0;
    if (fix && bg_shown) {
        /* inside the window: where there is a sprite not from it (Adol over the border on the 1st frame, the dark mask
         * the game puts on him), the background shown without it; where there is also a sprite from it, the frame stays (the pink frames in
         * front of Adol hidden under the equipment) */
        s.skip = own;
        redraw_frame(&s, b, other);
        int owned = 0;
        for (int i = 0; i < 64; i++) {
            mine_skip[i] = (uint8_t)!own[i];
            owned |= own[i];
        }
        if (owned) {
            s.skip = mine_skip;
            redraw_frame(&s, b, mine);
        }
        for (int i = 0; i < b->w * b->h; i++)
            fix[i] = other[i] >> 24 && !(owned && mine[i] >> 24) ? bg_shown[i] | 0xFF000000u : 0;
    }
    s.skip = NULL;
    if (!r->keep_ok)
        return 1;
    memcpy(&t, b, sizeof(t));
    memcpy(t.vram, r->keep, sizeof(t.vram));
    if (one)
        memcpy(t.rows, r->keep_rows, sizeof(t.rows));
    t.write_count = 0; /* the frame's writes (the windows) are left out */
    s.layer = REDRAW_LAYER_BG;
    redraw_frame(&s, &t, bg);
    return 2;
}
