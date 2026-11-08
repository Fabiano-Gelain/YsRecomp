/* Smooth motion image assembled on the GPU: see gpu_draw.h. */
#include "app/gpu_draw.h"
#include "app/battle.h"

#include <math.h>
#include <string.h>

/* The same as the VDC's (pce/vdc.c) */
static const int k_screen_tiles_y[8] = { 32, 32, 32, 32, 64, 64, 64, 64 };
static const int k_sprite_width[2] = { 16, 32 };
static const int k_sprite_height[4] = { 16, 32, 64, 64 };
static const int k_sprite_mask_width[2] = { 0xFFFF, 0xFFFE };
static const int k_sprite_mask_height[4] = { 0xFFFF, 0xFFFD, 0xFFF9, 0xFFF9 };

#define RING 256 /* the Ys map ring (motion.c) */
#define M GPU_MARGIN
#define BAND_STRIDE GPU_BAND_STRIDE

static int qk(double v, int k) { return (int)floor(v * k + 0.5); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Map line k lines after bgy: inside the RING-line ring, if ring (motion.c) */
static int ring_line(int bgy, int k, int ring)
{
    return ring ? (bgy & ~(RING - 1)) | ((bgy + k) & (RING - 1)) : bgy + k;
}

static SDL_Texture *texture(SDL_Renderer *r, SDL_TextureAccess access, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, access, w, h);
    if (t)
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    return t;
}

/* w x h render target in *t (recreated if the size changed) */
static int target(SDL_Renderer *r, SDL_Texture **t, int *tw, int *th, int w, int h)
{
    if (*t && *tw == w && *th == h)
        return 1;
    if (*t)
        SDL_DestroyTexture(*t);
    *t = texture(r, SDL_TEXTUREACCESS_TARGET, w, h);
    *tw = w;
    *th = h;
    return *t != NULL;
}

int gpu_draw_init(GpuDraw *g, SDL_Renderer *r)
{
    memset(g, 0, sizeof(*g));
    g->r = r;
    g->f.seq = -1;
    /* Erase: color stays, alpha becomes 0 where the drawing is opaque */
    g->erase = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                          SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                          SDL_BLENDOPERATION_ADD);
    g->base = texture(r, SDL_TEXTUREACCESS_STREAMING, VCE_MAX_WIDTH, VCE_ACTIVE_LINES);
    g->bands = texture(r, SDL_TEXTUREACCESS_STREAMING, BAND_STRIDE, GPU_BAND_H);
    g->atlas = texture(r, SDL_TEXTUREACCESS_STREAMING, GPU_ATLAS_W, GPU_ATLAS_H);
    g->masks = texture(r, SDL_TEXTUREACCESS_STREAMING, GPU_MASK_SLOTS * 32, 64);
    g->ok = g->base && g->bands && g->atlas && g->masks && SDL_SetTextureBlendMode(g->atlas, g->erase) &&
            SDL_SetTextureBlendMode(g->masks, g->erase);
    if (!g->ok) {
        gpu_draw_free(g);
        g->r = r;
    }
    return g->ok;
}

void gpu_draw_free(GpuDraw *g)
{
    SDL_Texture *t[] = { g->base,     g->bands,     g->atlas,  g->layer[0], g->layer[1],
                         g->layer[2], g->layer[3], g->canvas, g->crop,     g->masks, g->stickers };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (t[i])
            SDL_DestroyTexture(t[i]);
    memset(g, 0, sizeof(*g));
    g->f.seq = -1;
}

/* ---- New frame ---- */

/* Line y (top to bottom on screen) of sprite e (table entry) in out[32], like the VDC's fetch_sprites: 9-bit
 * color (0x100 | palette << 4 | index), 0 = transparent */
static void sprite_row(const uint16_t *vram, const uint16_t *e, int mode1, int y, uint16_t *out)
{
    uint16_t flags = e[3];
    int cgx = (flags >> 8) & 1, cgy = (flags >> 12) & 3, height = k_sprite_height[cgy];
    memset(out, 0, 32 * sizeof(uint16_t));
    if (y < 0 || y >= height)
        return;
    int mode1_offset = mode1 ? (e[2] & 1) << 5 : 0;
    uint16_t pattern = (uint16_t)((e[2] >> 1) & 0x3FF);
    pattern &= (uint16_t)k_sprite_mask_width[cgx];
    pattern &= (uint16_t)k_sprite_mask_height[cgy];
    uint16_t address = (uint16_t)(pattern << 6);
    int x_flip = (flags & 0x0800) != 0;
    if (flags & 0x8000)
        y = height - 1 - y;
    uint16_t line_start = (uint16_t)(address + (y >> 4) * 128 + (y & 0xF) + mode1_offset);
    uint16_t palette = (uint16_t)(0x100 | (flags & 0x0F) << 4);
    int halves = k_sprite_width[cgx] == 16 ? 1 : 2;
    for (int half = 0; half < halves; half++) {
        uint16_t line = line_start, data[4];
        if (halves == 2)
            line = (uint16_t)(line_start + ((half == 0) == x_flip ? 64 : 0));
        for (int p = 0; p < 4; p++) {
            uint16_t addr = (uint16_t)(line + p * 16);
            data[p] = (mode1 && p >= 2) || addr >= VDC_VRAM_SIZE ? 0 : vram[addr];
        }
        for (int x = 0; x < 16; x++) {
            int px = x_flip ? x : 15 - x;
            int pixel = ((data[0] >> px) & 1) | (((data[1] >> px) & 1) << 1) | (((data[2] >> px) & 1) << 2) |
                        (((data[3] >> px) & 1) << 3);
            out[half * 16 + x] = pixel ? (uint16_t)(palette | pixel) : 0;
        }
    }
}

/* 16-per-line limit on VDC raster line raster (like fetch_line in smooth.c): drop[i] = 1, the whole sprite; 2, the
 * right half */
static void line_limit(const int *top, const int *hgt, const int *wide, int raster, uint8_t *drop)
{
    int count = 0, full = 0;
    for (int i = 0; i < 64; i++) {
        drop[i] = 0;
        if (!(top[i] <= raster && top[i] + hgt[i] > raster))
            continue;
        if (full || count >= 16) {
            drop[i] = 1;
            full = 1;
            continue;
        }
        if (wide[i] && ++count >= 16) {
            drop[i] = 2;
            full = 1;
            continue;
        }
        count++;
    }
}

/* Line r continues the map of line r - 1: same scroll, same offset and the next map line */
static int continues(const RedrawFrame *b, const MotionLayout *l0, const MotionLayout *l1, int r)
{
    const VdcLineInfo *a = &b->rows[r - 1], *c = &b->rows[r];
    int reg = (c->mwr >> 4) & 7, mask = k_screen_tiles_y[reg] * 8 - 1;
    return a->bxr == c->bxr && ((a->mwr >> 4) & 7) == reg && a->x0 == c->x0 && a->width == c->width &&
           l0->ox[r] == l0->ox[r - 1] && l0->oy[r] == l0->oy[r - 1] && l1->ox[r] == l1->ox[r - 1] &&
           l1->oy[r] == l1->oy[r - 1] && l0->ring[r] == l0->ring[r - 1] && l0->map_row[r] == l0->map_row[r - 1] &&
           (c->bgy & mask) == (ring_line(a->bgy, 1, l0->ring[r]) & mask);
}

/* Line k of band p (map line k lines after the piece's 1st line), with the current VRAM */
static void band_row(GpuPrep *pr, const GpuPiece *p, const RedrawFrame *b, const MotionLayout *l0, int k,
                     const uint16_t *vram, const uint8_t *win, const uint32_t *pal, const RedrawSides *sides)
{
    const GpuFrameInfo *g = &pr->f;
    uint16_t *bgl = pr->bgl;
    const VdcLineInfo *li = &b->rows[p->top];
    int n = li->width + 2 * M, reg = (li->mwr >> 4) & 7, bgy = ring_line(li->bgy, k, l0->ring[p->top]);
    redraw_bg_row(vram, reg, li->bxr - M, bgy, n, bgl, win);
    /* Engine map lines: the strip outside the map columns comes from A's map (redraw.h, RedrawSides) */
    int clamp = sides && l0->map_row[p->top], b_left = li->bxr + CROP_MAP_X - li->x0;
    uint32_t *o = pr->band + (size_t)(p->y0 + M + k) * BAND_STRIDE;
    for (int c = 0; c < n; c++) {
        int x = p->x0 + c - M;
        uint16_t v = bgl[c];
        if (clamp && (x < CROP_MAP_X || x >= CROP_MAP_X + CROP_MAP_W))
            v = redraw_side_pixel(sides, vram, reg, b_left, li->bxr - M + c, bgy, win);
        int idx = v & 0x0F;
        uint32_t col = pal[idx ? v & 0x1FF : 0];
        o[c] = idx ? col : 0;
        if (g->windows) {
            o[GPU_BAND_W + c] = (v & 0x400) ? col : 0;
            o[2 * GPU_BAND_W + c] = (v & 0x400) && idx ? col : 0;
        }
    }
}

void gpu_draw_prepare(GpuPrep *pr, int ok, Motion *m, Redraw *s, const RedrawFrame *b, int window_front)
{
    GpuFrameInfo *g = &pr->f;
    MotionLayout *l0p = &pr->l0, *l1p = &pr->l1;
    uint16_t *vram = pr->vram, *sat = pr->sat;
    int8_t *row_piece = pr->row_piece;
    uint32_t *g_base = pr->base, *g_atlas = pr->atlas;
    g->seq = b->seq;
    g->ready = 0;
    g->w = b->w;
    g->h = b->h;
    g->npieces = g->nregions = 0;
    g->windows = 0;
    g->nbehind = 0;
    pr->rows = 0;
    if (!ok || b->w <= 0 || b->h <= 0 || b->w > VCE_MAX_WIDTH || b->h > VCE_ACTIVE_LINES)
        return;
    memcpy(g_base, b->pixels, (size_t)b->w * b->h * sizeof(uint32_t));
    motion_layout(m, b, 0.0, l0p);
    motion_layout(m, b, 1.0, l1p);
    int first = -1;
    for (int r = 0; r < b->h && l0p->exact; r++) {
        const VdcLineInfo *li = &b->rows[r];
        if (!li->valid)
            continue;
        if (first < 0) {
            first = r;
            g->x0 = li->x0;
            g->raster0 = li->raster - r;
        } else if (li->x0 != g->x0 || li->raster - r != g->raster0) {
            return; /* VDC start or count differs between lines: on the CPU */
        }
        if (li->width > VCE_MAX_WIDTH)
            return;
    }
    const uint32_t *pal = b->rgb; /* the colors shown (smooth fade) */
    /* Map pieces and drawing stretches; the base gets color 0 where the VDC draws */
    int sides = l0p->sides && CROP_MAP_X + CROP_MAP_W <= b->w, prev = -1;
    for (int r = 0; r < b->h && first >= 0; r++) {
        const VdcLineInfo *li = &b->rows[r];
        int u0 = li->x0 < 0 ? -li->x0 : 0, u1 = li->width;
        if (u1 > b->w - li->x0)
            u1 = b->w - li->x0;
        int cs = li->x0 + u0, ce = li->x0 + u1;
        if (sides && l0p->map_row[r]) { /* outside the map columns the frame goes (keep_sides) */
            cs = cs > CROP_MAP_X ? cs : CROP_MAP_X;
            ce = ce < CROP_MAP_X + CROP_MAP_W ? ce : CROP_MAP_X + CROP_MAP_W;
        }
        int draw = li->valid && ce > cs, piece = -1;
        if (draw && (li->cr & 0x80)) {
            if (prev >= 0 && continues(b, l0p, l1p, r)) {
                piece = prev;
                g->piece[piece].end = r + 1;
            } else {
                if (g->npieces == GPU_PIECES)
                    return;
                piece = g->npieces++;
                GpuPiece *p = &g->piece[piece];
                p->top = r;
                p->end = r + 1;
                p->x0 = li->x0;
            }
        }
        prev = piece;
        row_piece[r] = (int8_t)piece;
        if (!draw)
            continue;
        int spr = (li->cr & 0x40) != 0;
        GpuRegion *q = g->nregions ? &g->region[g->nregions - 1] : NULL;
        if (q && q->end == r && q->x == cs && q->x1 == ce && q->piece == piece && q->sprites == spr) {
            q->end = r + 1;
        } else {
            if (g->nregions == GPU_REGIONS)
                return;
            q = &g->region[g->nregions++];
            *q = (GpuRegion){ r, r + 1, cs, ce, piece, spr };
        }
        for (int x = cs; x < ce; x++)
            g_base[r * b->w + x] = pal[0];
    }
    int rows = 0;
    for (int i = 0; i < g->npieces; i++) {
        g->piece[i].y0 = rows;
        rows += g->piece[i].end - g->piece[i].top + 2 * M;
    }
    if (rows > GPU_BAND_H)
        return;
    /* Item windows in front */
    const uint8_t *win = NULL;
    memset(g->keep, 0, sizeof(g->keep));
    int win_reg = first >= 0 ? (b->rows[first].mwr >> 4) & 7 : 0;
    if (window_front && first >= 0 && l0p->exact)
        win = redraw_windows(s, b, win_reg, g->keep);
    g->windows = win != NULL;
    /* Sprites: the size and priority at the start of the image */
    int top[64], hgt[64], wide[64];
    for (int i = 0; i < 64; i++) {
        uint16_t fl = b->sat[i * 4 + 3];
        top[i] = (b->sat[i * 4] & 0x3FF) - 64;
        g->spr_w[i] = k_sprite_width[(fl >> 8) & 1];
        g->spr_h[i] = hgt[i] = k_sprite_height[(fl >> 12) & 3];
        wide[i] = g->spr_w[i] == 32;
        g->front[i] = (fl & 0x80) != 0;
        g->used[i] = 0;
    }
    memset(g_atlas, 0, sizeof(pr->atlas));
    /* Line by line, with what the game wrote before the VDC drew it: the sprite and band lines */
    memcpy(vram, b->vram, sizeof(pr->vram));
    memcpy(sat, b->sat, sizeof(pr->sat));
    redraw_equip_begin(s, b);
    int next_write = 0;
    uint16_t px[32];
    for (int r = 0; r < b->h && first >= 0; r++) {
        for (; next_write < b->write_count && b->writes[next_write].row <= r; next_write++) {
            const VdcLogWrite *wr = &b->writes[next_write];
            if (wr->addr < VDC_VRAM_SIZE)
                vram[wr->addr] = wr->value;
            else
                sat[(wr->addr - VDC_VRAM_SIZE) & (VDC_SAT_SIZE - 1)] = wr->value;
            if (win) /* window cells the game rewrote */
                ys_windows_write(&b->wins, s->win, redraw_tiles_x(win_reg), redraw_tiles_y(win_reg), wr->addr, wr->row);
        }
        const VdcLineInfo *li = &b->rows[r];
        int raster = r + g->raster0, mode1 = ((li->mwr >> 2) & 3) == 1;
        uint8_t drop[64] = { 0 };
        if (li->valid && (li->cr & 0x40))
            line_limit(top, hgt, wide, raster, drop);
        for (int i = 0; i < 64; i++) {
            if (s->skip && s->skip[i]) /* left out around the shrunk windows (RedrawRing.skip) */
                continue;
            /* The line for this image line; on the 1st, also those above it; on the last, those below */
            int y = raster - top[i], ya = r == 0 ? 0 : y, yb = r == b->h - 1 ? hgt[i] - 1 : y;
            ya = ya < 0 ? 0 : ya;
            yb = yb >= hgt[i] ? hgt[i] - 1 : yb;
            for (int yy = ya; yy <= yb; yy++) {
                sprite_row(vram, &sat[i * 4], mode1, yy, px);
                const uint16_t *eq = g->spr_w[i] == 16 ? redraw_equip(s, vram, &sat[i * 4], i) : NULL;
                if (eq) { /* Adol with the equipment: direct color (0x8000) */
                    uint16_t fl = sat[i * 4 + 3];
                    const uint16_t *row = eq + ((fl & 0x8000) ? hgt[i] - 1 - yy : yy) * 16;
                    for (int x = 0; x < 16; x++)
                        px[x] = row[(fl & 0x0800) ? 15 - x : x];
                }
                int n = g->spr_w[i];
                if (yy == y && drop[i])
                    n = drop[i] == 1 ? 0 : 16;
                uint32_t *o = g_atlas + (size_t)((i / 8) * 64 + yy) * GPU_ATLAS_W + (i % 8) * 32;
                for (int x = 0; x < n; x++)
                    if (px[x]) {
                        o[x] = (px[x] & 0x8000) ? redraw_direct_rgb(b, px[x] & 0x1FF) : pal[px[x] & 0x1FF];
                        g->used[i] = 1;
                    }
            }
        }
        int pi = row_piece[r];
        if (pi < 0 || !li->valid)
            continue;
        const GpuPiece *p = &g->piece[pi];
        int ka = r - p->top, kb = ka;
        if (r == p->top)
            ka = -M;
        if (r == p->end - 1)
            kb = p->end - p->top + M - 1;
        for (int k = ka; k <= kb; k++)
            band_row(pr, p, b, l0p, k, vram, win, pal, sides && m->sides.rows ? &m->sides : NULL);
    }
    pr->rows = rows;
    g->nbehind = m->ym.nblocks;
    if (g->nbehind) {
        uint32_t *mpx = pr->masks;
        memset(mpx, 0, sizeof(pr->masks));
        for (int k = 0; k < g->nbehind && k < GPU_MASK_SLOTS; k++) {
            g->mask[k] = m->ym.block[k];
            for (int y = 0; y < g->mask[k].h; y++)
                for (int x = 0; x < g->mask[k].w; x++)
                    if ((g->mask[k].rows[y] >> x) & 1)
                        mpx[y * GPU_MASK_SLOTS * 32 + k * 32 + x] = 0xFFFFFFFF;
        }
    }
    g->ready = 1;
}

void gpu_draw_upload(GpuDraw *g, const GpuPrep *p)
{
    g->f = p->f;
    g->frames++;
    if (!p->f.ready)
        return;
    SDL_Rect br = { 0, 0, p->f.w, p->f.h };
    SDL_UpdateTexture(g->base, &br, p->base, p->f.w * (int)sizeof(uint32_t));
    if (p->rows > 0) {
        SDL_Rect rr = { 0, 0, p->f.windows ? BAND_STRIDE : GPU_BAND_W, p->rows };
        SDL_UpdateTexture(g->bands, &rr, p->band, BAND_STRIDE * (int)sizeof(uint32_t));
    }
    SDL_UpdateTexture(g->atlas, NULL, p->atlas, GPU_ATLAS_W * (int)sizeof(uint32_t));
    if (p->f.nbehind)
        SDL_UpdateTexture(g->masks, NULL, p->masks, GPU_MASK_SLOTS * 32 * (int)sizeof(uint32_t));
}

void gpu_draw_frame(GpuDraw *g, Motion *m, Redraw *s, const RedrawFrame *b, int window_front)
{
    static GpuPrep p;
    gpu_draw_prepare(&p, g->ok, m, s, b, window_front);
    gpu_draw_upload(g, &p);
}

/* ---- Image ---- */

/* Sprite layer L: 0 all, 1 in front of the background, 2 in front of the windows, 3 both */
static void compose_layer(GpuDraw *g, int L, const MotionLayout *lay, int kx, int ky)
{
    SDL_SetRenderTarget(g->r, g->layer[L]);
    SDL_SetRenderDrawColor(g->r, 0, 0, 0, 0);
    SDL_RenderClear(g->r);
    int mx = qk(lay->map_ox, kx), my = qk(lay->map_oy, ky);
    for (int i = 63; i >= -1; i--) {
        /* Masks by the rule before sprite "before" in the order: they erase those already drawn, in all layers */
        for (int k = 0; k < g->f.nbehind && k < GPU_MASK_SLOTS; k++) {
            if (!lay->behind_on[k])
                continue;
            const RedrawMask *mk = &g->f.mask[k];
            if (mk->before != i + 1)
                continue;
            SDL_FRect src = { (float)(k * 32), 0.0f, (float)mk->w, (float)mk->h };
            SDL_FRect dst = { (float)(g->f.x0 * kx + qk(mk->x, kx) - mx), (float)(qk(mk->y, ky) - my - g->f.raster0 * ky),
                              (float)(mk->w * kx), (float)(mk->h * ky) };
            SDL_RenderTexture(g->r, g->masks, &src, &dst);
        }
        if (i < 0 || !g->f.used[i] || lay->hide[i]) /* hide: the game's masks replaced by the rule's */
            continue;
        int show = L == 0 || (L == 1 && g->f.front[i]) || (L == 2 && g->f.keep[i]) || (L == 3 && g->f.keep[i] && g->f.front[i]);
        int x = g->f.x0 * kx + qk(lay->sx[i], kx) - (lay->glued[i] ? mx : 0);
        int y = qk(lay->sy[i], ky) - (lay->glued[i] ? my : 0) - g->f.raster0 * ky;
        SDL_FRect src = { (float)((i % 8) * 32), (float)((i / 8) * 64), (float)g->f.spr_w[i], (float)g->f.spr_h[i] };
        SDL_FRect dst = { (float)x, (float)y, (float)(g->f.spr_w[i] * kx), (float)(g->f.spr_h[i] * ky) };
        SDL_SetTextureBlendMode(g->atlas, show ? SDL_BLENDMODE_BLEND : g->erase);
        SDL_RenderTexture(g->r, g->atlas, &src, &dst);
    }
}

/* Band v (0 opaque background, 1 whole window, 2 opaque window) of stretch q's piece, shifted as the line says */
static void draw_band(GpuDraw *g, const GpuRegion *q, const MotionLayout *lay, int v, int kx, int ky,
                      const SDL_FRect *d)
{
    const GpuPiece *p = &g->f.piece[q->piece];
    int qx = clampi(qk(lay->ox[q->top], kx), -(M - 1) * kx, (M - 1) * kx);
    int qy = clampi(qk(lay->oy[q->top], ky), -(M - 1) * ky, (M - 1) * ky);
    SDL_FRect s = { (float)(v * GPU_BAND_W + M + (q->x - p->x0)) + (float)qx / (float)kx,
                    (float)(p->y0 + M + (q->top - p->top)) + (float)qy / (float)ky, (float)(q->x1 - q->x),
                    (float)(q->end - q->top) };
    SDL_SetTextureBlendMode(g->bands, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(g->r, g->bands, &s, d);
}

static void draw_layer(GpuDraw *g, int L, const SDL_FRect *d)
{
    SDL_SetTextureBlendMode(g->layer[L], SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(g->r, g->layer[L], d, d);
}

int gpu_draw_render(GpuDraw *g, const RedrawFrame *b, const MotionLayout *lay, int kx, int ky)
{
    if (!g->ok || !g->f.ready || g->f.seq != b->seq || kx < 1 || ky < 1) {
        g->unready++;
        return 0;
    }
    int cw = g->f.w * kx, ch = g->f.h * ky, nl = !g->f.nregions ? 0 : g->f.windows ? 4 : 2;
    if (!target(g->r, &g->canvas, &g->canvas_w, &g->canvas_h, cw, ch))
        return 0;
    if (g->layer_w != cw || g->layer_h != ch) {
        for (int L = 0; L < GPU_LAYERS; L++) {
            if (g->layer[L])
                SDL_DestroyTexture(g->layer[L]);
            g->layer[L] = NULL;
        }
        g->layer_w = cw;
        g->layer_h = ch;
    }
    for (int L = 0; L < nl; L++)
        if (!g->layer[L] && !(g->layer[L] = texture(g->r, SDL_TEXTUREACCESS_TARGET, cw, ch)))
            return 0;
    for (int L = 0; L < nl; L++)
        compose_layer(g, L, lay, kx, ky);
    SDL_SetRenderTarget(g->r, g->canvas);
    /* Clear before the base; with the base alone, when the console turns the image off (the title menu's loading, with no
     * lines) the last drawn frame stayed: the slots fading out frozen for ~45 frames */
    SDL_SetRenderDrawColor(g->r, 0, 0, 0, 255);
    SDL_RenderClear(g->r);
    SDL_FRect src = { 0, 0, (float)g->f.w, (float)g->f.h };
    SDL_SetTextureBlendMode(g->base, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(g->r, g->base, &src, NULL);
    for (int i = 0; i < g->f.nregions; i++) {
        const GpuRegion *q = &g->f.region[i];
        SDL_FRect d = { (float)(q->x * kx), (float)(q->top * ky), (float)((q->x1 - q->x) * kx),
                        (float)((q->end - q->top) * ky) };
        if (q->sprites)
            draw_layer(g, 0, &d);
        if (q->piece >= 0)
            draw_band(g, q, lay, 0, kx, ky, &d);
        if (q->sprites)
            draw_layer(g, 1, &d);
        if (g->f.windows && q->piece >= 0) {
            draw_band(g, q, lay, 1, kx, ky, &d);
            if (q->sprites)
                draw_layer(g, 2, &d);
            draw_band(g, q, lay, 2, kx, ky, &d);
            if (q->sprites)
                draw_layer(g, 3, &d);
        }
    }
    SDL_SetRenderTarget(g->r, NULL);
    g->renders++;
    return 1;
}

SDL_Texture *gpu_draw_crop(GpuDraw *g, const CropView *v, SDL_Texture *over, int over_sub, int kx, int ky)
{
    if (!g->canvas || !target(g->r, &g->crop, &g->crop_w, &g->crop_h, v->w * kx, v->h * ky))
        return NULL;
    SDL_SetRenderTarget(g->r, g->crop);
    SDL_SetRenderDrawColor(g->r, 0, 0, 0, 255);
    SDL_RenderClear(g->r);
    /* The image pieces, with the wide and fill slide (crop_compose) */
    int sx = v->band ? qk(v->pan_q / 4.0, kx) : 0, sy = v->band ? qk(v->pan_qy / 4.0, ky) : 0;
    SDL_SetTextureBlendMode(g->canvas, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(g->canvas, SDL_SCALEMODE_NEAREST);
    for (int i = 0; i < v->count; i++) {
        const CropPiece *p = &v->piece[i];
        SDL_FRect s = { (float)(p->sx * kx + sx), (float)(p->sy * ky + sy), (float)(p->w * kx), (float)(p->h * ky) };
        SDL_FRect d = { (float)(p->dx * kx), (float)(p->dy * ky), (float)(p->w * kx), (float)(p->h * ky) };
        SDL_RenderTexture(g->r, g->canvas, &s, &d);
    }
    SDL_FRect os = { 0, 0, (float)(v->w * over_sub), (float)(v->h * over_sub) };
    SDL_SetTextureBlendMode(over, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(over, SDL_SCALEMODE_NEAREST);
    SDL_RenderTexture(g->r, over, &os, NULL);
    SDL_SetRenderTarget(g->r, NULL);
    return g->crop;
}

void gpu_draw_battle(GpuDraw *g, const Battle *bt, const double *x, const double *y, int kx, int ky)
{
    if (!bt->count || !g->canvas)
        return;
    if (!g->stickers) {
        g->stickers = texture(g->r, SDL_TEXTUREACCESS_STREAMING, BATTLE_STICKER_W, BATTLE_STICKER_H * BATTLE_STICKERS);
        if (!g->stickers)
            return;
        SDL_SetTextureBlendMode(g->stickers, SDL_BLENDMODE_BLEND);
    }
    for (int k = 0; k < bt->count; k++) {
        const BattleSticker *s = &bt->st[k];
        SDL_Rect r = { 0, k * BATTLE_STICKER_H, s->w, s->h };
        SDL_UpdateTexture(g->stickers, &r, s->px, s->w * (int)sizeof(uint32_t));
    }
    SDL_SetRenderTarget(g->r, g->canvas);
    SDL_Rect clip = { CROP_MAP_X * kx, CROP_MAP_Y * ky, CROP_MAP_W * kx, CROP_MAP_H * ky };
    SDL_SetRenderClipRect(g->r, &clip);
    for (int k = 0; k < bt->count; k++) {
        const BattleSticker *s = &bt->st[k];
        SDL_FRect src = { 0.0f, (float)(k * BATTLE_STICKER_H), (float)s->w, (float)s->h };
        SDL_FRect dst = { (float)qk(x[k], kx), (float)qk(y[k], ky), (float)(s->w * kx), (float)(s->h * ky) };
        if (bt->small && s->kind == BATTLE_POPUP) { /* 3/4, anchored at the same top middle (battle_draw_at) */
            dst.x += dst.w / 8.0f;
            dst.w *= 0.75f;
            dst.h *= 0.75f;
        }
        SDL_SetTextureAlphaMod(g->stickers, (Uint8)(s->alpha < 0 ? 0 : s->alpha > 255 ? 255 : s->alpha));
        SDL_RenderTexture(g->r, g->stickers, &src, &dst);
    }
    SDL_SetRenderClipRect(g->r, NULL);
    SDL_SetRenderTarget(g->r, NULL);
}
