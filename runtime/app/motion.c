/* New smooth motion, reading the game: see motion.h and docs/ys_notes.md ("Engine memory map"). */
#include "app/motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/crop.h"
#include "app/game_fixes.h"

/* The same as the VDC's (pce/vdc.c) */
static const int k_screen_tiles_y[8] = { 32, 32, 32, 32, 64, 64, 64, 64 };

#define RING 256 /* Ys keeps the map in a ring of 32 tile rows */

static int wrap(int d, int size)
{
    d %= size;
    if (d >= size / 2)
        d -= size;
    if (d < -size / 2)
        d += size;
    return d;
}

static int to_q(double v) { return (int)floor(v * REDRAW_SUB + 0.5); }
static int sprite_y(const uint16_t *sat, int i) { return (sat[i * 4] & 0x3FF) - 64; }
static int sprite_x(const uint16_t *sat, int i) { return (sat[i * 4 + 1] & 0x3FF) - 0x20; }
static int sprite_w(const uint16_t *e) { return (e[3] & 0x100) ? 32 : 16; }
static int sprite_h(const uint16_t *e) { return (e[3] & 0x3000) == 0 ? 16 : (e[3] & 0x3000) == 0x1000 ? 32 : 64; }

/* Line that scrolls with the camera (cx, cy): the map starts at image line CROP_MAP_Y with the camera, inside the ring
 * (lines 0-255 of the 512 map; the frame and the panel are on lines 256+) */
static int is_map_row(const VdcLineInfo *li, int r, int cx, int cy)
{
    return li->valid && (li->cr & 0x80) && (li->bgy & 511) < RING && ((li->bxr - cx) & 511) == 0 &&
           ((li->bgy - (r - CROP_MAP_Y) - cy) & (RING - 1)) == 0;
}

static int find_map_rows(const RedrawFrame *b, int cx, int cy, uint8_t *rows)
{
    int n = 0;
    for (int r = 0; r < b->h; r++) {
        rows[r] = (uint8_t)is_map_row(&b->rows[r], r, cx, cy);
        n += rows[r];
    }
    return n;
}

int motion_map_rows(const RedrawFrame *b, uint8_t *rows)
{
    const YsList *l = ys_frame_shown_list(&b->eng);
    if (!l) {
        memset(rows, 0, VCE_ACTIVE_LINES);
        return 0;
    }
    return find_map_rows(b, l->cam_x, l->cam_y, rows);
}

void motion_set_ring(Motion *m, const RedrawMask *masks, int n, long seq)
{
    m->ring_masks = masks;
    m->ring_n = n;
    m->ring_seq = seq;
}

void motion_copy(Motion *dst, const Motion *src, RedrawMask *ring_store)
{
    *dst = *src;
    if (src->sides.rows == src->map_row)
        dst->sides.rows = dst->map_row;
    if (src->sides.a_vram == src->side_vram)
        dst->sides.a_vram = dst->side_vram;
    if (src->ring_masks) {
        int n = src->ring_n < 0 ? 0 : src->ring_n > REDRAW_BEHIND ? REDRAW_BEHIND : src->ring_n;
        memcpy(ring_store, src->ring_masks, (size_t)n * sizeof(RedrawMask));
        dst->ring_masks = ring_store;
    }
}

/* Map line k lines after bgy: inside the RING-line ring, if ring */
static int ring_line(int bgy, int k, int ring)
{
    return ring ? (bgy & ~(RING - 1)) | ((bgy + k) & (RING - 1)) : bgy + k;
}

void motion_init(Motion *m, int constant_speed)
{
    memset(m, 0, sizeof(*m));
    m->constant_speed = constant_speed;
    motion_reset(m);
}

void motion_reset(Motion *m)
{
    memset(m->walk, 0, sizeof(m->walk));
    m->pose[0].valid = m->pose[1].valid = 0;
    m->lerp = 0;
    m->engine = 0;
    m->follow = -1;
}

/* ---- Late frame ---- */

int motion_fix_late(Motion *m, Redraw *s, RedrawFrame *b)
{
    static int bx[VCE_ACTIVE_LINES], by[VCE_ACTIVE_LINES], sx[YS_ACTORS], sy[YS_ACTORS];
    static uint32_t img[VCE_MAX_WIDTH * VCE_ACTIVE_LINES];
    YsFrameList *e = &b->eng;
    if (e->kind != YS_LIST_LATE)
        return 0;
    int dx = wrap(e->list.cam_x - e->shown_cam_x, 512), dy = wrap(e->list.cam_y - e->shown_cam_y, RING);
    uint8_t rows[VCE_ACTIVE_LINES];
    int ok = b->w > 0 && b->h > 0 && abs(dx) <= MOTION_JUMP && abs(dy) <= MOTION_JUMP &&
             find_map_rows(b, e->shown_cam_x, e->shown_cam_y, rows) > 0;
    if (ok) {
        /* The redraw at its own positions must be the frame */
        for (int r = 0; r < b->h; r++) {
            bx[r] = b->rows[r].bxr;
            by[r] = b->rows[r].bgy;
        }
        for (int i = 0; i < YS_ACTORS; i++) {
            sx[i] = sprite_x(b->sat, i);
            sy[i] = sprite_y(b->sat, i);
        }
        redraw_draw(s, b, 0, bx, by, sx, sy, img);
        ok = memcmp(img, b->pixels, (size_t)b->w * b->h * sizeof(uint32_t)) == 0;
    }
    if (!ok) {
        e->kind = YS_LIST_NONE;
        return 0;
    }
    for (int r = 0; r < b->h; r++)
        if (rows[r]) {
            b->rows[r].bxr = (uint16_t)((b->rows[r].bxr + dx) & 0x3FF);
            b->rows[r].bgy = (uint16_t)ring_line(b->rows[r].bgy, dy, 1);
        }
    memcpy(b->sat, e->list.sat, sizeof(e->list.sat));
    int n = 0; /* the table is the list's: without the writes to it during the frame */
    for (int k = 0; k < b->write_count; k++)
        if (b->writes[k].addr < VDC_VRAM_SIZE)
            b->writes[n++] = b->writes[k];
    b->write_count = n;
    for (int r = 0; r < b->h; r++) {
        bx[r] = b->rows[r].bxr;
        by[r] = b->rows[r].bgy;
    }
    for (int i = 0; i < YS_ACTORS; i++) {
        sx[i] = sprite_x(b->sat, i);
        sy[i] = sprite_y(b->sat, i);
    }
    redraw_draw(s, b, 0, bx, by, sx, sy, b->pixels);
    e->shown_cam_x = e->list.cam_x;
    e->shown_cam_y = e->list.cam_y;
    b->exact = -1;
    m->late_fixed++;
    return 1;
}

/* ---- Step ---- */

/* Point on the step's path with p pixels walked (past the last position, it follows the last direction) */
static void path_at(const MotionWalk *w, double p, double *x, double *y)
{
    if (w->n < 2) {
        *x = w->wx[0];
        *y = w->wy[0];
        return;
    }
    int k = 1;
    while (k < w->n - 1 && w->prog[k] < p)
        k++;
    double u = (p - w->prog[k - 1]) / (w->prog[k] - w->prog[k - 1]);
    *x = w->wx[k - 1] + (w->wx[k] - w->wx[k - 1]) * u;
    *y = w->wy[k - 1] + (w->wy[k] - w->wy[k - 1]) * u;
}

/* Outside the engine: each piece of B (consecutive lines with the same scroll) moves what its middle line moved from A */
static void pieces_step(Motion *m, const RedrawFrame *a, const RedrawFrame *b)
{
    for (int r0 = 0, r1; r0 < b->h; r0 = r1) {
        const VdcLineInfo *l0 = &b->rows[r0];
        for (r1 = r0 + 1; r1 < b->h; r1++) {
            const VdcLineInfo *l = &b->rows[r1];
            if (!l0->valid || !l->valid || l->bxr != l0->bxr || l->cr != l0->cr ||
                ((l->bgy - l0->bgy - (r1 - r0)) & (RING - 1)) != 0)
                break;
        }
        int mid = (r0 + r1) / 2, dx = 0, dy = 0;
        const VdcLineInfo *lb = &b->rows[mid];
        if (a && a->h == b->h && lb->valid && (lb->cr & 0x80)) {
            const VdcLineInfo *la = &a->rows[mid];
            dx = wrap(lb->bxr - la->bxr, 512);
            dy = wrap(lb->bgy - la->bgy, RING);
            if (!la->valid || !(la->cr & 0x80) || abs(dx) > MOTION_JUMP || abs(dy) > MOTION_JUMP)
                dx = dy = 0;
        }
        for (int r = r0; r < r1; r++) {
            m->piece_dx[r] = dx;
            m->piece_dy[r] = dy;
        }
    }
}

/* Shown position of character c at fraction f (with ideal, the constant-speed one; otherwise the game's), on B's screen:
 * the game's plus how much the image moves him away from it (where he stands relative to B's map) */
static void shown_at(const Motion *m, int c, double f, int ideal, double *x, double *y)
{
    const MotionPose *pa = &m->pose[0], *p = &m->pose[1];
    double g = m->lerp ? 1.0 - f : 0.0;
    double bx_ = ideal ? p->cam_x : p->raw_x, by_ = ideal ? p->cam_y : p->raw_y;
    double ax_ = ideal ? pa->cam_x : pa->raw_x, ay_ = ideal ? pa->cam_y : pa->raw_y;
    double dx = ideal ? p->dx[c] : p->sx[c], dy = ideal ? p->dy[c] : p->sy[c];
    double adx = ideal ? pa->dx[c] : pa->sx[c], ady = ideal ? pa->dy[c] : pa->sy[c];
    if (m->lerp && pa->present[c] && fabs(dx - adx) <= MOTION_JUMP && fabs(dy - ady) <= MOTION_JUMP) {
        dx -= (dx - adx) * g;
        dy -= (dy - ady) * g;
    }
    *x = dx + (bx_ - (bx_ - ax_) * g) - p->raw_x;
    *y = dy + (by_ - (by_ - ay_) * g) - p->raw_y;
}

/* Masks by the rule: the blocks this frame's images may use. The shown positions of each hidden walker
 * go in a straight line from f = 0 to f = 1 (with and without constant speed): on each axis, the grid points around
 * the ends cover the middle ones, and the blocks are their combinations */
static void masks_step(Motion *m, const RedrawFrame *b)
{
    for (int w = 0; w < m->ym.nwalkers; w++) {
        const YsMaskWalker *wk = &m->ym.walker[w];
        int gx[8], gy[8], nx = 0, ny = 0;
        for (int ideal = 0; ideal < 2; ideal++)
            for (int end = 0; end < 2; end++) {
                double x, y;
                int bx[4], by[4];
                shown_at(m, wk->actor, end, ideal, &x, &y);
                int n = ys_masks_step(wk, x, y, bx, by);
                for (int j = 0; j < n; j++) {
                    int kx = 0, ky = 0;
                    while (kx < nx && gx[kx] != bx[j])
                        kx++;
                    while (ky < ny && gy[ky] != by[j])
                        ky++;
                    if (kx == nx && nx < 8)
                        gx[nx++] = bx[j];
                    if (ky == ny && ny < 8)
                        gy[ny++] = by[j];
                }
            }
        for (int i = 0; i < nx; i++)
            for (int j = 0; j < ny; j++)
                ys_masks_block(&m->ym, b, w, gx[i], gy[j]);
        if (wk->has_next) /* the one for the step already decided in B (the game's points) */
            ys_masks_block(&m->ym, b, w, wk->nx, wk->ny);
    }
}

/* The masks rebuilt without this frame's shrunk windows (motion_set_ring) enter the blocks, after the
 * walkers' (ring_from); motion_layout turns them on in all images */
static void ring_step(Motion *m, const RedrawFrame *b)
{
    m->ring_from = m->ym.nblocks;
    if (m->ring_seq != b->seq || !m->ring_masks)
        return;
    for (int k = 0; k < m->ring_n && m->ym.nblocks < YS_MASK_BLOCKS; k++) {
        int j = m->ym.nblocks++;
        m->ym.block[j] = m->ring_masks[k];
        m->ym.bx[j] = m->ym.by[j] = 0;
        m->ym.bw[j] = -1;
    }
}

void motion_step(Motion *m, const RedrawFrame *a, const RedrawFrame *b)
{
    m->frames++;
    m->ym.nwalkers = m->ym.nblocks = 0;
    m->sides.rows = NULL;
    m->pose[0] = m->pose[1];
    MotionPose *pa = &m->pose[0], *p = &m->pose[1];
    const YsList *l = &b->eng.list;
    int consecutive = a && a->seq == b->seq - 1;
    m->engine = b->eng.kind != YS_LIST_NONE && find_map_rows(b, l->cam_x, l->cam_y, m->map_row) > 0;
    if (!m->engine) {
        memset(m->walk, 0, sizeof(m->walk));
        p->valid = 0;
        m->follow = -1;
        pieces_step(m, consecutive ? a : NULL, b);
        m->lerp = consecutive;
        ring_step(m, b);
        return;
    }
    m->engine_frames++;
    int have_a = consecutive && pa->valid;
    if (have_a && pa->seq == l->seq) { /* no new list (pause): everything still */
        m->lerp = 1;
        *pa = *p;
        ys_masks_frame(&m->ym, b, m->map_row, 0, 0, 1);
        m->ym.nwalkers = 0;
        ring_step(m, b);
        return;
    }
    int jump = 1, cdx = 0, cdy = 0;
    if (have_a) {
        cdx = wrap(l->cam_x - pa->cam_mx, 512);
        cdy = wrap(l->cam_y - pa->cam_my, RING);
        jump = abs(cdx) > MOTION_JUMP || abs(cdy) > MOTION_JUMP;
    }
    p->valid = 1;
    p->seq = l->seq;
    p->cam_mx = l->cam_x;
    p->cam_my = l->cam_y;
    if (jump) {
        p->raw_x = l->cam_x;
        p->raw_y = l->cam_y;
        memset(m->walk, 0, sizeof(m->walk));
    } else {
        p->raw_x = pa->raw_x + cdx;
        p->raw_y = pa->raw_y + cdy;
    }
    m->lerp = !jump;
    for (int i = 0; i < YS_ACTORS; i++) {
        p->present[i] = l->drawn[i];
        p->sx[i] = l->ax[i];
        p->sy[i] = l->ay[i];
    }
    /* Walkers: the step since the decision */
    double ix[YS_ACTORS], iy[YS_ACTORS];
    uint8_t walking[YS_ACTORS] = { 0 };
    for (int i = 0; i < YS_ACTORS; i++) {
        ix[i] = p->sx[i] + p->raw_x;
        iy[i] = p->sy[i] + p->raw_y;
    }
    for (int k = 0; k < YS_WALKERS; k++) {
        MotionWalk *w = &m->walk[k];
        int act = l->w_actor[k], vel = l->w_vel[k], left = l->w_left[k], unit = l->unit;
        if (act >= YS_ACTORS || !vel || !unit || !p->present[act] || left > MOTION_STEP || (w->on && w->actor != act) ||
            jump || !have_a || !pa->present[act]) {
            w->on = 0;
            continue;
        }
        int wx = p->sx[act] + p->raw_x, wy = p->sy[act] + p->raw_y;
        int moved = wx != pa->sx[act] + pa->raw_x || wy != pa->sy[act] + pa->raw_y;
        int prog = MOTION_STEP - left;
        if (!moved && left == MOTION_STEP) {
            /* Decision: an 8-pixel step starts; m = frames until the accumulator adds up 8 units */
            w->on = 1;
            w->actor = act;
            w->t = 0;
            w->m = (MOTION_STEP * unit - l->w_acc[k] + vel - 1) / vel;
            w->n = 1;
            w->prog[0] = 0;
            w->wx[0] = wx;
            w->wy[0] = wy;
            w->sx0 = p->sx[act];
            w->sy0 = p->sy[act];
            w->fix_x = w->fix_y = 1;
        } else if (w->on && moved && w->t < w->m && w->n < MOTION_SAMPLES && prog > w->prog[w->n - 1] &&
                   abs(wx - w->wx[w->n - 1]) + abs(wy - w->wy[w->n - 1]) <= 2 * (prog - w->prog[w->n - 1])) {
            w->fix_x &= p->sx[act] == w->sx0;
            w->fix_y &= p->sy[act] == w->sy0;
            w->t++;
            w->prog[w->n] = prog;
            w->wx[w->n] = wx;
            w->wy[w->n] = wy;
            w->n++;
        } else {
            w->on = 0;
            continue;
        }
        if (w->t == 0)
            continue;
        double x, y;
        path_at(w, (double)MOTION_STEP * w->t / (w->m + 1), &x, &y);
        if (fabs(x - wx) <= 3.0 && fabs(y - wy) <= 3.0) {
            ix[act] = x;
            iy[act] = y;
            walking[act] = 1;
        }
    }
    /* Camera: the Ys one follows Adol (the character noted when the game creates him, ys_engine.c hero_hook); the correction
     * applies on the axis on which he stayed still on screen since the step's decision (the camera moved along; at the map's edge
     * it stops and he moves on screen) */
    double ex = 0.0, ey = 0.0;
    int fx = YS_ACTORS, fy = YS_ACTORS;
    for (int k = 0; k < YS_WALKERS; k++) {
        const MotionWalk *w = &m->walk[k];
        int act = w->actor;
        if (!w->on || !walking[act] || act != l->hero)
            continue;
        if (act < fx && w->fix_x && p->sx[act] + p->raw_x != w->wx[0]) {
            fx = act;
            ex = ix[act] - (p->sx[act] + p->raw_x);
        }
        if (act < fy && w->fix_y && p->sy[act] + p->raw_y != w->wy[0]) {
            fy = act;
            ey = iy[act] - (p->sy[act] + p->raw_y);
        }
    }
    m->follow = fx < YS_ACTORS ? fx : fy < YS_ACTORS ? fy : -1;
    p->cam_x = p->raw_x + ex;
    p->cam_y = p->raw_y + ey;
    for (int i = 0; i < YS_ACTORS; i++) {
        if (walking[i]) {
            p->dx[i] = ix[i] - p->cam_x;
            p->dy[i] = iy[i] - p->cam_y;
        } else if (l->flags[i] & 0x20) { /* attached to the map: moves with the shown camera */
            p->dx[i] = p->sx[i] - ex;
            p->dy[i] = p->sy[i] - ey;
        } else {
            p->dx[i] = p->sx[i];
            p->dy[i] = p->sy[i];
        }
    }
    /* Position copied by the game from another character's (the Ys I mine light, YsList.copy_of): moves with B's map (the
     * game redraws the lit floor in the map along with it, at B's position; between A and B, the leading edge of the lit floor
     * showed outside the circle) */
    for (int i = 0; i < YS_ACTORS; i++)
        p->glued[i] = (uint8_t)(p->present[i] && pa->present[i] && l->copy_of[i] < YS_ACTORS);
    /* The masks the game shows in B are stored on every engine frame (the images of the following ones may need
     * them); the blocks, only with A */
    ys_masks_frame(&m->ym, b, m->map_row, cdx, cdy, !jump);
    if (m->lerp && a->eng.kind != YS_LIST_NONE)
        masks_step(m, b);
    else
        m->ym.nwalkers = 0;
    ring_step(m, b);
    /* Map edges: A's visible map (the same map line in both, with the same map size) */
    m->sides.rows = m->map_row;
    m->sides.l = CROP_MAP_X;
    m->sides.r = CROP_MAP_X + CROP_MAP_W;
    m->sides.a_vram = NULL;
    for (int r = 0; r < b->h && m->lerp; r++)
        if (m->map_row[r]) {
            const VdcLineInfo *ra = &a->rows[r], *rb = &b->rows[r];
            if (ra->valid && ((ra->mwr ^ rb->mwr) & 0x70) == 0) {
                memcpy(m->side_vram, a->vram, sizeof(m->side_vram));
                m->sides.a_vram = m->side_vram;
                m->sides.a_left = ra->bxr + CROP_MAP_X - ra->x0;
            }
            break;
        }
    m->lerp_frames += m->lerp;
}

/* ---- Adol's rhythm (--walk-rhythm) ---- */

void motion_rhythm_add(MotionRhythm *r, const Motion *m, long frame)
{
    if (frame < r->from || frame >= r->to)
        return;
    const MotionPose *p = &m->pose[1];
    if (!m->engine || !p->valid || !p->present[0]) {
        r->have = 0;
        return;
    }
    double x = p->cam_x + p->dx[0], y = p->cam_y + p->dy[0];
    if (r->have && m->lerp) {
        double s = hypot(x - r->wx, y - r->wy);
        if (s > 0.01) {
            int bin = (int)(s * 10.0 + 0.5);
            r->hist[bin >= MOTION_RHYTHM_BINS ? MOTION_RHYTHM_BINS - 1 : bin]++;
            r->walking++;
        }
        if (s > 0.5 && r->s1 >= 0.0 && r->s1 <= 0.01 && r->s2 > 0.5 && r->stops++ == 0)
            r->first_stop = frame - 1;
        r->s2 = r->s1;
        r->s1 = s;
    } else {
        r->s1 = r->s2 = -1.0;
    }
    r->wx = x;
    r->wy = y;
    r->have = 1;
}

void motion_rhythm_print(const MotionRhythm *r)
{
    int mode = 0;
    for (int i = 1; i < MOTION_RHYTHM_BINS; i++)
        if (r->hist[i] > r->hist[mode])
            mode = i;
    long in = 0;
    for (int i = mode > 0 ? mode - 1 : 0; i <= mode + 1 && i < MOTION_RHYTHM_BINS; i++)
        in += r->hist[i];
    printf("Adol rhythm (frames %ld-%ld): %ld walking, %ld at the rhythm of %.1f, %ld stops midway (1st: %ld)\n", r->from,
           r->to - 1, r->walking, in, mode / 10.0, r->stops, r->stops ? r->first_stop : -1L);
}

/* ---- Drawing ---- */

/* The frame's sides are in the background map itself, in the columns just outside the visible map (the game rewrites them
 * when the camera moves): on the map lines, outside the map columns the frame stays (the still border). The strip of
 * map that comes from outside B's map comes from A's map in the redraw (redraw.h, RedrawSides). */
static void keep_sides(const Motion *m, Redraw *s, const RedrawFrame *b)
{
    int ow = s->out_w, l = CROP_MAP_X << REDRAW_SUB_SHIFT, r = (CROP_MAP_X + CROP_MAP_W) << REDRAW_SUB_SHIFT;
    if (r > ow)
        return;
    for (int row = 0; row < b->h; row++) {
        if (!m->map_row[row])
            continue;
        const uint32_t *real = b->pixels + row * b->w;
        for (int j = 0; j < REDRAW_SUB; j++) {
            uint32_t *o = s->out + ((row << REDRAW_SUB_SHIFT) + j) * ow;
            for (int u = 0; u < ow; u++)
                if (u < l || u >= r)
                    o[u] = real[u >> REDRAW_SUB_SHIFT];
        }
    }
}

void motion_layout(Motion *m, const RedrawFrame *b, double f, MotionLayout *lay)
{
    f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
    lay->exact = b->exact == 1;
    lay->sides = 0;
    lay->map_ox = lay->map_oy = 0.0;
    memset(lay->behind_on, 0, sizeof(lay->behind_on));
    memset(lay->hide, 0, sizeof(lay->hide));
    if (!lay->exact)
        return;
    const MotionPose *pa = &m->pose[0], *p = &m->pose[1];
    double g = m->lerp ? 1.0 - f : 0.0; /* how much is left from A to B */
    double shift_x = 0.0, shift_y = 0.0;
    const YsList *l = &b->eng.list;
    int ideal = m->constant_speed; /* without constant speed: the game's positions */
    if (m->engine) {
        double bx_ = ideal ? p->cam_x : p->raw_x, by_ = ideal ? p->cam_y : p->raw_y;
        double ax_ = ideal ? pa->cam_x : pa->raw_x, ay_ = ideal ? pa->cam_y : pa->raw_y;
        m->shown_cam_x = bx_ - (bx_ - ax_) * g;
        m->shown_cam_y = by_ - (by_ - ay_) * g;
        shift_x = m->shown_cam_x - p->raw_x;
        shift_y = m->shown_cam_y - p->raw_y;
    }
    lay->sides = m->engine;
    lay->map_ox = shift_x;
    lay->map_oy = shift_y;
    for (int r = 0; r < b->h; r++) {
        const VdcLineInfo *li = &b->rows[r];
        double ox = 0.0, oy = 0.0;
        if (m->engine && m->map_row[r]) {
            ox = shift_x;
            oy = shift_y;
        } else if (!m->engine) {
            ox = -m->piece_dx[r] * g;
            oy = -m->piece_dy[r] * g;
        }
        lay->ox[r] = ox;
        lay->oy[r] = oy;
        lay->map_row[r] = (uint8_t)(m->engine && m->map_row[r]);
        int mask = k_screen_tiles_y[(li->mwr >> 4) & 7] * 8 - 1;
        lay->ring[r] = (uint8_t)(mask + 1 <= RING || lay->map_row[r]);
    }
    for (int i = 0; i < YS_ACTORS; i++) {
        double x = sprite_x(b->sat, i), y = sprite_y(b->sat, i);
        int a = m->engine && i < l->count ? l->owner[i] : YS_NO_ACTOR;
        lay->glued[i] = (uint8_t)(a < YS_ACTORS && p->present[a] && p->glued[a]);
        lay->sx[i] = x;
        lay->sy[i] = y;
        if (lay->glued[i]) {
            x -= (double)to_q(shift_x) / REDRAW_SUB; /* with B's map, as it was shifted */
            y -= (double)to_q(shift_y) / REDRAW_SUB;
        } else if (a < YS_ACTORS && p->present[a]) {
            double dx = ideal ? p->dx[a] : p->sx[a], dy = ideal ? p->dy[a] : p->sy[a];
            double ax_ = ideal ? pa->dx[a] : pa->sx[a], ay_ = ideal ? pa->dy[a] : pa->sy[a];
            if (m->lerp && pa->present[a] && fabs(dx - ax_) <= MOTION_JUMP && fabs(dy - ay_) <= MOTION_JUMP) {
                dx -= (dx - ax_) * g;
                dy -= (dy - ay_) * g;
            }
            x += dx - p->sx[a];
            y += dy - p->sy[a];
            lay->sx[i] = x;
            lay->sy[i] = y;
        }
        m->spr_x[i] = x;
        m->spr_y[i] = y;
    }
    /* Masks by the rule: each hidden walker away from the game's position takes those of the step it is at in this
     * image, instead of the game's (also at f = 1 with constant speed: up to 3 pixels away from the game's) */
    for (int w = 0; w < m->ym.nwalkers && !m->behind_off; w++) {
        const YsMaskWalker *wk = &m->ym.walker[w];
        double x, y;
        int bx[5], by[5], slot[5], ok = 1; /* up to 4 from the grid and the one of the decided step */
        shown_at(m, wk->actor, f, ideal, &x, &y);
        if (x == wk->px && y == wk->py)
            continue;
        int n = ys_masks_step(wk, x, y, bx, by);
        /* The mask of the step's end that the game already shows in B (the decided step) goes along in all images:
         * without it, the character's head (above the mask of the step's start) showed in the mine's darkness */
        if (wk->has_next) {
            int dup = 0;
            for (int j = 0; j < n; j++)
                dup |= bx[j] == wk->nx && by[j] == wk->ny;
            if (!dup) {
                bx[n] = wk->nx;
                by[n] = wk->ny;
                n++;
            }
        }
        for (int j = 0; j < n; j++)
            ok &= (slot[j] = ys_masks_find(&m->ym, w, bx[j], by[j])) >= 0;
        if (!ok) {
            m->mask_missing++;
            continue;
        }
        int rule = 0;
        for (int j = 0; j < n; j++) {
            lay->behind_on[slot[j]] = 1;
            rule |= m->ym.rule_made[slot[j]];
        }
        m->ym.rule_images += rule;
        if (getenv("PCE_MASK_LOG")) {
            printf("masks: frame %ld a%d at %.2f,%.2f game %d,%d f %.2f before %d:", (long)b->seq, wk->actor, x, y,
                   wk->px, wk->py, f, wk->before);
            for (int j = 0; j < n; j++) {
                int bits = 0;
                for (int v = 0; v < 32; v++)
                    for (uint32_t r = m->ym.block[slot[j]].rows[v]; r; r &= r - 1)
                        bits++;
                printf(" %d,%d(%d pixels%s)", bx[j], by[j], bits, m->ym.rule_made[slot[j]] ? " rule" : "");
            }
            printf(" hides:");
            for (int i = 0; i < 64; i++)
                if (wk->skip[i])
                    printf(" s%d", i);
            printf("\n");
        }
        if (rule && getenv("PCE_MASK_RULE_LOG")) { /* which block, and the masks the game showed for this walker */
            printf("masks: frame %ld a%d at %.2f,%.2f (game %d,%d; grid %d,%d dir %d,%d f %.2f):", (long)b->seq,
                   wk->actor, x, y, wk->px, wk->py, wk->gx, wk->gy, wk->dx, wk->dy, f);
            for (int j = 0; j < n; j++)
                printf(" %d,%d%s", bx[j], by[j], m->ym.rule_made[slot[j]] ? "*" : "");
            for (int s = 0; s < 2; s++)
                for (int j = 0; j < (s ? m->ym.nhist : m->ym.ngame); j++) {
                    const YsGameMask *gm = s ? &m->ym.hist[j] : &m->ym.game[j];
                    if (gm->actor == wk->actor)
                        printf(" %d,%d(-%d)", gm->x, gm->y, gm->age);
                }
            printf("\n");
        }
        for (int i = 0; i < YS_ACTORS; i++)
            lay->hide[i] |= wk->skip[i];
    }
    /* The ones rebuilt without the shrunk windows, in all images (the game's are removed by Redraw.skip) */
    for (int k = m->ring_from; k < m->ym.nblocks; k++)
        lay->behind_on[k] = 1;
}

const uint32_t *motion_render(Motion *m, Redraw *s, const RedrawFrame *b, double f)
{
    static int bx[VCE_ACTIVE_LINES], lines[VCE_ACTIVE_LINES * REDRAW_SUB], sx[YS_ACTORS], sy[YS_ACTORS];
    static MotionLayout lay;
    s->out_w = b->w * REDRAW_SUB;
    s->out_h = b->h * REDRAW_SUB;
    motion_layout(m, b, f, &lay);
    if (!lay.exact) {
        /* The redraw does not reproduce this frame: the real image, upscaled */
        for (int y = 0; y < s->out_h; y++)
            for (int x = 0; x < s->out_w; x++)
                s->out[y * s->out_w + x] = b->pixels[(y >> REDRAW_SUB_SHIFT) * b->w + (x >> REDRAW_SUB_SHIFT)];
        return s->out;
    }
    for (int r = 0; r < b->h; r++) {
        const VdcLineInfo *li = &b->rows[r];
        int qy = to_q(lay.oy[r]);
        bx[r] = (li->bxr << REDRAW_SUB_SHIFT) + to_q(lay.ox[r]);
        for (int j = 0; j < REDRAW_SUB; j++) {
            int k = (int)floor((double)(j + qy) / REDRAW_SUB);
            lines[(r << REDRAW_SUB_SHIFT) + j] = ring_line(li->bgy, k, lay.ring[r]);
        }
    }
    for (int i = 0; i < YS_ACTORS; i++) {
        sx[i] = to_q(lay.sx[i]) - (lay.glued[i] ? to_q(lay.map_ox) : 0);
        sy[i] = to_q(lay.sy[i]) - (lay.glued[i] ? to_q(lay.map_oy) : 0);
    }
    /* Masks by the rule: attached to B's map; the game's for those walkers are removed */
    int n = 0, hide = 0;
    for (int k = 0; k < m->ym.nblocks; k++)
        if (lay.behind_on[k]) {
            RedrawMask *mk = &m->draw_mask[n++];
            *mk = m->ym.block[k];
            mk->xq = (mk->x << REDRAW_SUB_SHIFT) - to_q(lay.map_ox);
            mk->yq = (mk->y << REDRAW_SUB_SHIFT) - to_q(lay.map_oy);
        }
    static uint8_t skip[YS_ACTORS];
    const uint8_t *ring = s->skip;
    for (int i = 0; i < YS_ACTORS; i++) {
        skip[i] = (uint8_t)((ring && ring[i]) || lay.hide[i]);
        hide |= lay.hide[i];
    }
    s->masks = m->draw_mask;
    s->nmasks = n;
    s->skip = hide ? skip : ring;
    s->sides = lay.sides && m->sides.rows && CROP_MAP_X + CROP_MAP_W <= b->w ? &m->sides : NULL;
    redraw_draw(s, b, REDRAW_SUB_SHIFT, bx, lines, sx, sy, s->out);
    s->masks = NULL;
    s->nmasks = 0;
    s->skip = ring;
    s->sides = NULL;
    if (lay.sides)
        keep_sides(m, s, b);
    return s->out;
}
