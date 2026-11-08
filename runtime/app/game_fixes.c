#include "app/game_fixes.h"

#include <stdlib.h>
#include <string.h>

/* ---- Torn frame (and inside the game's windows) ---- */

/* Last drawn line: the writes after it belong to the next frame (the game already preparing it) */
static int torn_end_row(const VdcLineInfo *rows, int h)
{
    int end_row = h - 1;
    while (end_row >= 0 && !rows[end_row].valid)
        end_row--;
    return end_row;
}

int torn_window_cells(const YsWinFrame *wf, int reg, uint8_t *cells)
{
    int tx = redraw_tiles_x(reg), ty = redraw_tiles_y(reg), n = 0;
    memset(cells, 0, (size_t)tx * ty);
    /* Frame in which the game switches layout: what it writes in the window cells is the frame stored for the next
     * scroll, not the window's content (moving it earlier put the frame in the middle of the map) */
    if (wf->layout_switch)
        return 0;
    /* The windows wrap around the game's 64x32 map (app/ys_windows.h), also with the VDC at 64x64 */
    int wx = tx < 64 ? tx : 64, wy = ty < 32 ? ty : 32;
    const YsWindow *list[3] = { wf->win, wf->prev, wf->gone };
    const int count[3] = { wf->count, wf->prev_count, wf->gone_n };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < count[k] && i < YS_WIN_MAX; i++) {
            const YsWindow *w = &list[k][i];
            uint8_t v = k == 2 || (k == 0 && w->state == YS_WIN_CLOSING) ? 2 : 1; /* closing: on top */
            for (int y = 0; y < w->h; y++)
                for (int x = 0; x < w->w; x++) {
                    uint8_t *c = &cells[((w->bat_y + y) & (wy - 1)) * tx + ((w->bat_x + x) & (wx - 1))];
                    n += !*c;
                    if (*c < v)
                        *c = v;
                }
        }
    return n;
}

/* Torn pixels outside the cells cells (of the map of size cells_reg; NULL = none); *in = the ones inside */
static int torn_rows(const uint16_t *vram0, const VdcLogWrite *writes, int write_count, const VdcLineInfo *rows, int h,
                     const uint8_t *cells, int cells_reg, int *in, int *first_row)
{
    static uint16_t vram[VDC_VRAM_SIZE], fin[VDC_VRAM_SIZE];
    static uint16_t now[VDC_MAX_WIDTH], end[VDC_MAX_WIDTH];
    int end_row = torn_end_row(rows, h);
    int last = -1, torn = 0;
    *in = 0;
    for (int i = 0; i < write_count; i++)
        if (writes[i].addr < VDC_VRAM_SIZE && writes[i].row > last && writes[i].row <= end_row)
            last = writes[i].row;
    if (first_row)
        *first_row = -1;
    if (last <= 0)
        return 0;
    memcpy(vram, vram0, sizeof(vram));
    memcpy(fin, vram0, sizeof(fin));
    for (int i = 0; i < write_count; i++)
        if (writes[i].addr < VDC_VRAM_SIZE && writes[i].row <= end_row)
            fin[writes[i].addr] = writes[i].value;
    int next_write = 0;
    for (int r = 0; r < h && r < last; r++) {
        for (; next_write < write_count && writes[next_write].row <= r; next_write++)
            if (writes[next_write].addr < VDC_VRAM_SIZE)
                vram[writes[next_write].addr] = writes[next_write].value;
        const VdcLineInfo *li = &rows[r];
        if (!li->valid || !(li->cr & 0x80))
            continue;
        int reg = (li->mwr >> 4) & 7, n = li->width > VDC_MAX_WIDTH ? VDC_MAX_WIDTH : li->width;
        const uint8_t *m = cells && reg == cells_reg ? cells : NULL; /* marked cells: 0x400 on the pixel */
        redraw_bg_row(vram, reg, li->bxr, li->bgy, n, now, m);
        redraw_bg_row(fin, reg, li->bxr, li->bgy, n, end, m);
        int diff = 0;
        for (int x = 0; x < n; x++)
            if (now[x] != end[x]) {
                diff++;
                if (now[x] & 0x400)
                    ++*in;
                else
                    torn++;
            }
        if (diff && first_row && *first_row < 0)
            *first_row = r;
    }
    return torn;
}

int fix_torn_log(const VdcLineLog *log, const YsWinFrame *wins, int *in, int *first_row)
{
    static uint8_t cells[128 * 64];
    int dummy;
    if (!in)
        in = &dummy;
    *in = 0;
    if (log->last_row < 0 || log->overflow) {
        if (first_row)
            *first_row = -1;
        return 0;
    }
    int end_row = torn_end_row(log->row, VCE_ACTIVE_LINES), reg = -1;
    if (wins && end_row >= 0) {
        reg = (log->row[end_row].mwr >> 4) & 7;
        if (!torn_window_cells(wins, reg, cells))
            reg = -1;
    }
    return torn_rows(log->vram, log->writes, log->write_count, log->row, VCE_ACTIVE_LINES, reg >= 0 ? cells : NULL,
                     reg, in, first_row);
}

int fix_torn(const RedrawFrame *b, int *in)
{
    static uint8_t cells[128 * 64];
    int end_row = torn_end_row(b->rows, b->h), reg = -1;
    if (end_row >= 0) {
        reg = (b->rows[end_row].mwr >> 4) & 7;
        if (!torn_window_cells(&b->wins, reg, cells))
            reg = -1;
    }
    return torn_rows(b->vram, b->writes, b->write_count, b->rows, b->h, reg >= 0 ? cells : NULL, reg, in, NULL);
}

int fix_torn_window(Redraw *s, RedrawFrame *b)
{
    static uint8_t cells[128 * 64], vis[128 * 64], ref_in[0x800], ref_old[0x800], ref_out[0x800];
    static uint16_t fin[VDC_VRAM_SIZE], vram[VDC_VRAM_SIZE];
    static VdcLogWrite keep[VDC_LOG_WRITES];
    static uint32_t before[VCE_MAX_WIDTH * VCE_ACTIVE_LINES], after[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    if (!b->torn_win || b->w <= 0 || b->h <= 0)
        return 0;
    int end_row = torn_end_row(b->rows, b->h);
    if (end_row < 0)
        return 0;
    int reg = (b->rows[end_row].mwr >> 4) & 7;
    if (!torn_window_cells(&b->wins, reg, cells))
        return 0;
    int bat = redraw_tiles_x(reg) * redraw_tiles_y(reg);
    /* Tiles shown inside and outside the windows, at the start and at the end of the image */
    memcpy(fin, b->vram, sizeof(fin));
    for (int i = 0; i < b->write_count; i++)
        if (b->writes[i].addr < VDC_VRAM_SIZE && b->writes[i].row <= end_row)
            fin[b->writes[i].addr] = b->writes[i].value;
    /* Cells that appear in the image (outside them, the map holds other things: the other half of the 64x64, the columns off
     * screen) */
    int tx = redraw_tiles_x(reg), ty = redraw_tiles_y(reg);
    memset(vis, 0, (size_t)bat);
    for (int r = 0; r < b->h; r++) {
        const VdcLineInfo *li = &b->rows[r];
        if (!li->valid || !(li->cr & 0x80) || ((li->mwr >> 4) & 7) != reg)
            continue;
        int cy = (li->bgy >> 3) & (ty - 1), n = li->width > VDC_MAX_WIDTH ? VDC_MAX_WIDTH : li->width;
        for (int cx = li->bxr >> 3; cx <= (li->bxr + n - 1) >> 3; cx++)
            vis[cy * tx + (cx & (tx - 1))] = 1;
    }
    memset(ref_in, 0, sizeof(ref_in));
    memset(ref_old, 0, sizeof(ref_old));
    memset(ref_out, 0, sizeof(ref_out));
    for (int i = 0; i < bat; i++) {
        if (cells[i] == 2) /* window closing: its tiles at the start of the image */
            ref_old[b->vram[i] & 0x7FF] = 1;
        else if (cells[i] || vis[i]) {
            uint8_t *ref = cells[i] ? ref_in : ref_out;
            ref[b->vram[i] & 0x7FF] = ref[fin[i] & 0x7FF] = 1;
        }
    }
    /* The image's writes to the window cells, and to the drawings of the tiles only they show, go to the start;
     * in the ones that close, they stay for the next frame (the whole window as it was: the background the game restores may
     * come without part of the scene, which it only redraws in the next frame; session 20261020_221359, frame 9471) */
    memcpy(vram, b->vram, sizeof(vram));
    int nk = 0, moved = 0;
    for (int i = 0; i < b->write_count; i++) {
        const VdcLogWrite *wr = &b->writes[i];
        int a = wr->addr, t = a >> 4, act = 0; /* 1 = to the start, 2 = out of this frame */
        if (wr->row <= end_row && a < VDC_VRAM_SIZE) {
            if (a < bat)
                act = cells[a];
            else if (!ref_out[t])
                act = ref_old[t] ? 2 : ref_in[t] ? 1 : 0;
        }
        if (act == 1)
            vram[a] = wr->value;
        if (act)
            moved++;
        else
            keep[nk++] = *wr;
    }
    if (!moved)
        return 0;
    redraw_frame(s, b, before);
    memcpy(b->vram, vram, sizeof(vram));
    memcpy(b->writes, keep, (size_t)nk * sizeof(VdcLogWrite));
    b->write_count = nk;
    redraw_frame(s, b, after);
    int changed = 0;
    for (int p = 0; p < b->w * b->h; p++)
        if (before[p] != after[p]) {
            b->pixels[p] = after[p];
            changed++;
        }
    return changed;
}

/* ---- Scroll changes by the game ---- */

/* Line r as the game wanted it (f: the frame's changes, app/ys_scroll.h); 0 if r is neither the map's nor the lower part's */
static int scroll_row(const YsScrollFrame *f, int r, VdcLineInfo *l)
{
    if (r >= YS_SCROLL_LOW_ROW) {
        l->bxr = (uint16_t)(f->low_x & 0x3FF);
        l->bgy = (uint16_t)(f->low_byr + 1 + (r - YS_SCROLL_LOW_ROW));
        l->cr = (uint8_t)(f->cr & 0x80);
        return 1;
    }
    if (r < YS_SCROLL_MAP_ROW)
        return 0;
    int y = f->map_y + (r - YS_SCROLL_MAP_ROW);
    l->bxr = (uint16_t)(f->map_x & 0x3FF);
    if (y >= REDRAW_RING && f->wrap) { /* ring wrap: the next line shows line 0 */
        y -= REDRAW_RING;
        l->bxr = (uint16_t)(f->wrap_x & 0x3FF);
    }
    l->bgy = (uint16_t)y;
    l->cr = (uint8_t)((f->cr | 0x40) & 0xC0);
    return 1;
}

static int same_scroll(const VdcLineInfo *a, const VdcLineInfo *b)
{
    int mask = redraw_tiles_y(a->mwr >> 4) * 8 - 1;
    return ((a->bgy ^ b->bgy) & mask) == 0 && ((a->bxr ^ b->bxr) & 0x3FF) == 0 && ((a->cr ^ b->cr) & 0xC0) == 0;
}

int fix_scroll(Redraw *s, RedrawFrame *b, int *map_rows, int *low_rows)
{
    static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    const YsScrollFrame *f = &b->scroll;
    *map_rows = *low_rows = 0;
    if (!f->valid || b->w <= 0 || b->h <= YS_SCROLL_LOW_ROW)
        return 0;
    if (!(f->cr & 0x80)) /* the game turned the background off: blank screen, nothing to fix */
        return -1;
    int first = -1, last = -1;
    for (int r = YS_SCROLL_MAP_ROW; r < b->h; r++) {
        VdcLineInfo *l = &b->rows[r], want = *l;
        /* a line with the background turned off by the game (it turns it back on mid-image when lighting the screen) is not the map's */
        if (!l->valid || !(l->cr & 0x80) || !scroll_row(f, r, &want) || same_scroll(&want, l))
            continue;
        *(r < YS_SCROLL_LOW_ROW ? map_rows : low_rows) += 1;
        l->bxr = want.bxr;
        l->bgy = want.bgy;
        l->cr = want.cr;
        if (first < 0)
            first = r;
        last = r;
    }
    if (first < 0)
        return 0;
    redraw_frame(s, b, img);
    memcpy(b->pixels + first * b->w, img + first * b->w, (size_t)(last + 1 - first) * b->w * sizeof(uint32_t));
    return 1;
}

int fix_window(Redraw *s, RedrawFrame *b)
{
    static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES], front[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    if (!s->window_front || b->w <= 0 || b->h <= 0)
        return 0;
    int r0 = 0;
    while (r0 < b->h && !(b->rows[r0].valid && (b->rows[r0].cr & 0x80)))
        r0++;
    if (r0 >= b->h || (!b->wins.count && !b->wins.gone_n)) /* the game's windows */
        return 0;
    /* As the console draws and with the window in front: what changes are the sprite pixels over the window */
    s->window_front = 0;
    redraw_frame(s, b, img);
    s->window_front = 1;
    redraw_frame(s, b, front);
    int changed = 0;
    for (int i = 0; i < b->w * b->h; i++)
        if (img[i] != front[i]) {
            b->pixels[i] = front[i];
            changed++;
        }
    return changed;
}

int fix_equip(Redraw *s, RedrawFrame *b)
{
    static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES], eq[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    Equip *q = s->equip;
    if (!q || b->w <= 0 || b->h <= 0 || b->equip.game == EQUIP_GAME_NONE)
        return 0;
    long shown = q->shown;
    size_t bytes = (size_t)b->w * b->h * sizeof(uint32_t);
    redraw_frame(s, b, eq);
    if (q->shown == shown) { /* no known Adol in the frame */
        /* eq is redraw_check's redraw (the same s, with the equipment): the check comes from here */
        b->exact = memcmp(eq, b->pixels, bytes) == 0;
        b->exact_known = 1;
        return 0;
    }
    s->equip = NULL;
    redraw_frame(s, b, img);
    s->equip = q;
    int changed = 0;
    for (int i = 0; i < b->w * b->h; i++)
        if (img[i] != eq[i]) {
            b->pixels[i] = eq[i];
            changed++;
        }
    b->exact = memcmp(eq, b->pixels, bytes) == 0;
    b->exact_known = 1;
    return changed;
}
