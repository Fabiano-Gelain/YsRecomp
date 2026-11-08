/* Visible equipment on Adol: see equip.h. The rules are those of tools/equip/equip_art.py, in the same order. */
#include "app/equip.h"
#include "app/equip_art.h"
#include "app/ys_fade.h"

#include <math.h>
#include <string.h>

#include "cd/cdrom.h"
#include "pce/pce.h"

/* VCE color (9 bits: green, red, blue) from r, g, b in 0 to 7 */
#define C(r, g, b) ((int16_t)(((g) << 6) | ((r) << 3) | (b)))
#define NONE_PX (-1)

enum { OUT = 14, BLADE = 11, SKIN = 10 };

/* Swords, shields and armors: hand drawings from equip_art.py, in the generated equip_art.h */

int equip_count(int game, int kind)
{
    static const int n[3][3] = { { 0, 0, 0 }, { 5, 5, 5 }, { 6, 6, 6 } };
    return game >= 0 && game <= 2 && kind >= 0 && kind <= 2 ? n[game][kind] : 0;
}

/* ---- Rules ---- */

static int index_of(const int *list, int n, int c)
{
    for (int i = 0; i < n; i++)
        if (list[i] == c)
            return i;
    return -1;
}

static int px_at(const uint8_t f[EQUIP_H][EQUIP_W], int x, int y) { return f[y][x]; }

/* Figure pixel: inside and neither transparent nor outline */
static int figure(const uint8_t f[EQUIP_H][EQUIP_W], int x, int y)
{
    return x >= 0 && x < EQUIP_W && y >= 0 && y < EQUIP_H && f[y][x] != 0 && f[y][x] != OUT;
}

static void put_outline(EquipImage out, int x, int y, const uint16_t *pal)
{
    if (x >= 0 && x < EQUIP_W && y >= 0 && y < EQUIP_H && out[y * EQUIP_W + x] == NONE_PX)
        out[y * EQUIP_W + x] = (int16_t)pal[OUT];
}

/* Game shade -> part shade (equip_art.ARMOR_TONE); -1 = neither armor nor clothing */
static int armor_tone(int c)
{
    switch (c) {
    case 1: case 9: return 0;
    case 8: case 5: case 4: return 1;
    case 7: case 6: case 2: return 2;
    default: return -1;
    }
}

/* Some neighbor is blade (equip_art.near_blade): that outline stays, so the sword doesn't merge into the armor */
static int near_blade(const uint8_t f[EQUIP_H][EQUIP_W], int x, int y)
{
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++) {
            int X = x + dx, Y = y + dy;
            if (X >= 0 && X < EQUIP_W && Y >= 0 && Y < EQUIP_H && f[Y][X] == BLADE)
                return 1;
        }
    return 0;
}

/* The pixel's body part (equip_art.armor_role): index into ArmorArt.part */
static int armor_part(int dir, int pose, int x, int y)
{
    char r = k_armor_roles[dir][pose][y - ART_ROLES_TOP][x];
    if (r == '.')
        r = y < 15 ? 'S' : y < 18 ? 'C' : y == 18 ? 'B' : y < 21 ? 'K' : 'F';
    return (int)(strchr(ART_PARTS, r) - ART_PARTS);
}

/* Shoulder-pad columns on row y: smallest and largest (returns how many) */
static int shoulder_span(const uint8_t f[EQUIP_H][EQUIP_W], int dir, int pose, int y, int *lo, int *hi)
{
    int n = 0;
    for (int x = 0; x < EQUIP_W; x++)
        if (k_armor_roles[dir][pose][y - ART_ROLES_TOP][x] == 'S' && armor_tone(f[y][x]) >= 0) {
            if (!n)
                *lo = x;
            *hi = x;
            n++;
        }
    return n;
}

static void armor(EquipImage out, const uint8_t f[EQUIP_H][EQUIP_W], int dir, int pose, const ArmorArt *sp,
                  const uint16_t *pal)
{
    for (int y = ART_ROLES_TOP; y < 24; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            int c = f[y][x], t = armor_tone(c);
            if (t < 0)
                continue;
            int part = armor_part(dir, pose, x, y);
            if (sp->y && part == ART_PART_C && c == 5)
                t = 2;
            if (((sp->mesh >> part) & 1) && t > 0)
                t = (x + y) % 2 ? 2 : 1;
            out[y * EQUIP_W + x] = sp->part[part][t];
        }
    /* gem: the 1st pixel of the chest's middle (none from behind); the one below gets the dark color, if it doesn't
     * mark the "Y" */
    if (sp->has_gem && dir != 1) {
        int done = 0;
        for (int y = 15; y < 18 && !done; y++)
            for (int x = 0; x < EQUIP_W && !done; x++)
                if (k_armor_roles[dir][pose][y - ART_ROLES_TOP][x] == 'M' && armor_tone(f[y][x]) >= 0) {
                    out[y * EQUIP_W + x] = sp->gem[1];
                    if (!sp->y && k_armor_roles[dir][pose][y + 1 - ART_ROLES_TOP][x] == 'M' && armor_tone(f[y + 1][x]) >= 0)
                        out[(y + 1) * EQUIP_W + x] = sp->gem[0];
                    done = 1;
                }
    }
    if (dir != 0 && dir != 1)
        return;
    int lo, hi;
    /* wider shoulder pad: the outermost shoulder on each side gains a pixel, and the outline goes along (never the
     * outline beside the blade) */
    if (sp->pad)
        for (int y = 13; y <= 14; y++) {
            if (!shoulder_span(f, dir, pose, y, &lo, &hi))
                continue;
            for (int side = 0; side < 2; side++) {
                int step = side ? 1 : -1, X = (side ? hi : lo) + step;
                if (X >= 0 && X < EQUIP_W && f[y][X] == OUT && !near_blade(f, X, y)) {
                    out[y * EQUIP_W + X] = sp->part[ART_PART_S][1];
                    if (X + step >= 0 && X + step < EQUIP_W && !figure(f, X + step, y))
                        out[y * EQUIP_W + X + step] = (int16_t)pal[OUT];
                }
            }
        }
    /* spikes coming out of the shoulders, upward: both or none (just one looks like a stray pixel) */
    if (sp->horn_h && shoulder_span(f, dir, pose, 13, &lo, &hi)) {
        int h = sp->horn_h, ok = lo - 1 >= 0 && hi + 1 < EQUIP_W;
        for (int j = 0; j <= h && ok; j++)
            ok = !near_blade(f, lo - 1, 13 - j) && !near_blade(f, hi + 1, 13 - j);
        for (int side = 0; side < 2 && ok; side++) {
            int x = side ? hi + 1 : lo - 1;
            for (int j = 1; j <= h; j++) {
                int Y = 13 - j;
                if (figure(f, x, Y))
                    break;
                out[Y * EQUIP_W + x] = sp->horn_col;
                put_outline(out, x - 1, Y, pal);
                put_outline(out, x + 1, Y, pal);
            }
            put_outline(out, x, 13 - h - 1, pal);
        }
    }
}

static int is_role(int c)
{
    return c == 1 || c == 8 || c == 5 || c == 7 || c == 11 || c == 13 || c == 3 || c == 12;
}

static int sign_of(int v) { return (v > 0) - (v < 0); }

/* Python's round(n * k / 10) (ties to even), in integers */
static int round_tenths(int q)
{
    int base = q / 10, r = q % 10;
    if (r > 5 || (r == 5 && (base & 1)))
        base++;
    return base;
}

/* Sword (equip_art._sword): the item's drawing along the game's blade; with no sword (sw NULL), removes the blade */
static void sword(EquipImage out, const uint8_t f[EQUIP_H][EQUIP_W], const uint16_t *mask, const SwordArt *sw,
                  const uint16_t *pal)
{
    /* blade: from the hand (lowest) to the tip (highest); on the same row, from left to right */
    int bx[EQUIP_H * EQUIP_W], by[EQUIP_H * EQUIP_W], n = 0;
    for (int y = EQUIP_H - 1; y >= 0; y--)
        for (int x = 0; x < EQUIP_W; x++)
            if (f[y][x] == BLADE && !((mask[y] >> x) & 1)) {
                bx[n] = x;
                by[n++] = y;
            }
    if (!n)
        return;
    int keep = 0;
    if (sw) {
        keep = round_tenths(n * sw->n);
        keep = keep < 2 ? 2 : keep;
        keep = keep > n ? n : keep;
    }
    uint8_t gone[EQUIP_H][EQUIP_W];
    memset(gone, 0, sizeof(gone));
    for (int i = keep; i < n; i++) {
        gone[by[i]][bx[i]] = 1;
        out[by[i] * EQUIP_W + bx[i]] = NONE_PX;
    }
    /* outline that only served the removed part goes too */
    for (int y = 0; y < 22; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            if (f[y][x] != OUT)
                continue;
            int any_gone = 0, any_fig = 0;
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    int X = x + dx, Y = y + dy;
                    if ((!dx && !dy) || X < 0 || X >= EQUIP_W || Y < 0 || Y >= EQUIP_H)
                        continue;
                    any_gone |= gone[Y][X];
                    any_fig |= figure(f, X, Y) && !gone[Y][X];
                }
            if (any_gone && !any_fig)
                out[y * EQUIP_W + x] = NONE_PX;
        }
    if (!sw)
        return;
    int K = sw->n;
    int16_t col[EQUIP_H * EQUIP_W];
    for (int i = 0; i < keep; i++) {
        int j = i == keep - 1 ? K - 1 : i * K / keep;
        col[i] = sw->blade[j];
        out[by[i] * EQUIP_W + bx[i]] = col[i];
    }
    int x0 = bx[0], y0 = by[0], tx = bx[keep - 1], ty = by[keep - 1];
    /* pixels beside the blade (in place of the outline), and the outline outside them */
    for (int s = 0; s < 2; s++) {
        const int16_t *side = s ? sw->side2 : sw->side;
        int sgn = s ? -1 : 1;
        for (int i = 0; i < keep; i++) {
            int16_t c = side[i == keep - 1 ? K - 1 : i * K / keep];
            int y = by[i];
            if (c < 0)
                continue;
            /* grows away from the body: the outline between the blade and the arm stays */
            for (int t = 0; t < 2; t++) {
                int d = t ? -sgn : sgn, X = bx[i] + d, far = bx[i] + 2 * d;
                if (X < 0 || X >= EQUIP_W || f[y][X] != OUT || ((mask[y] >> X) & 1))
                    continue;
                if (far >= 0 && far < EQUIP_W && f[y][far] != 0 && f[y][far] != OUT && f[y][far] != BLADE)
                    continue;
                out[y * EQUIP_W + X] = c;
                put_outline(out, far, y, pal);
                break;
            }
        }
    }
    /* new tip: outline around it */
    static const int tdx[5] = { 0, -1, 1, -1, 1 }, tdy[5] = { -1, 0, 0, -1, -1 };
    for (int k = 0; k < 5; k++)
        put_outline(out, tx + tdx[k], ty + tdy[k], pal);
    if (sw->curve && keep >= 3) {
        /* curved blade: the tip bends one pixel away from the body */
        int side = tx <= x0 ? -1 : 1, X = tx + side;
        if (X >= 0 && X < EQUIP_W && !figure(f, X, ty)) {
            out[ty * EQUIP_W + X] = col[keep - 1];
            out[ty * EQUIP_W + tx] = col[0];
        }
    }
    /* guard: across the base of the blade, in place of the outline (guard2: one more pixel on each side) */
    int qx = -sign_of(ty - y0), qy = sign_of(tx - x0);
    for (int sgn = 1; sgn >= -1; sgn -= 2)
        for (int k = 1; k <= (sw->guard2 ? 2 : 1); k++) {
            int X = x0 + sgn * k * qx, Y = y0 + sgn * k * qy;
            if (X < 0 || X >= EQUIP_W || Y < 0 || Y >= EQUIP_H || ((mask[Y] >> X) & 1))
                break;
            if (f[Y][X] != OUT && !(k == 2 && f[Y][X] == 0))
                break;
            out[Y * EQUIP_W + X] = sw->guard;
            put_outline(out, X + sgn * qx, Y + sgn * qy, pal);
        }
    /* hilt: the color-3 pixel stuck to the base of the blade */
    static const int hdx[6] = { 0, -1, 1, 0, -1, 1 }, hdy[6] = { 1, 1, 1, 2, 0, 0 };
    for (int k = 0; k < 6; k++) {
        int X = x0 + hdx[k], Y = y0 + hdy[k];
        if (X >= 0 && X < EQUIP_W && Y >= 0 && Y < EQUIP_H && px_at(f, X, Y) == 3) {
            out[Y * EQUIP_W + X] = sw->hilt;
            break;
        }
    }
}

static int16_t role_color(const ShieldArt *sh, int c)
{
    switch (c) {
    case 1: return sh->face[0];
    case 8: return sh->face[1];
    case 5: return sh->face[2];
    case 7: return sh->rim;
    case 11: return sh->hl;
    case 13: return sh->emblem[0];
    case 3: return sh->emblem[1];
    case 12: return sh->emblem[2];
    default: return NONE_PX;
    }
}

/* From behind and from the side (equip_art._shield_edge): the game's shield strip with the item's colors, shorter
 * for smaller shields */
static void shield_edge(EquipImage out, const uint8_t f[EQUIP_H][EQUIP_W], const uint16_t *area, const ShieldArt *sh,
                        const uint16_t *pal)
{
    int rows[EQUIP_H], nrows = 0;
    for (int y = 0; y < EQUIP_H; y++) {
        int has = 0;
        for (int x = 0; x < EQUIP_W; x++) {
            if (!((area[y] >> x) & 1))
                continue;
            int c = f[y][x];
            out[y * EQUIP_W + x] = c ? (int16_t)pal[c] : NONE_PX;
            if (is_role(c)) {
                out[y * EQUIP_W + x] = role_color(sh, c);
                has = 1;
            }
        }
        if (has)
            rows[nrows++] = y;
    }
    /* trim is for the 8-row strip; on a smaller strip, proportional */
    int top = (sh->trim[0] * nrows + 4) / 8, bottom = (sh->trim[1] * nrows + 4) / 8;
    uint8_t cut[EQUIP_H] = { 0 }, gone[EQUIP_H][EQUIP_W];
    memset(gone, 0, sizeof(gone));
    for (int i = 0; i < nrows; i++)
        if (i < top || i >= nrows - bottom)
            cut[rows[i]] = 1;
    int any = 0;
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++)
            if (((area[y] >> x) & 1) && is_role(f[y][x]) && cut[y]) {
                gone[y][x] = 1;
                out[y * EQUIP_W + x] = NONE_PX;
                any = 1;
            }
    if (!any)
        return;
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            if (!((area[y] >> x) & 1) || f[y][x] != OUT)
                continue;
            int fig = 0;
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    int X = x + dx, Y = y + dy;
                    if ((dx || dy) && figure(f, X, Y) && !gone[Y][X])
                        fig = 1;
                }
            if (!fig)
                out[y * EQUIP_W + x] = NONE_PX;
        }
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++)
            if (((area[y] >> x) & 1) && is_role(f[y][x]) && !gone[y][x])
                for (int dy = -1; dy <= 1; dy += 2) {
                    int Y = y + dy;
                    if (Y >= 0 && Y < EQUIP_H && gone[Y][x])
                        out[Y * EQUIP_W + x] = (int16_t)pal[OUT];
                }
}

static void compose_parts(const EquipFrame *fr, const uint16_t *pal, const EquipInfo *info, int pose, EquipImage out)
{
    uint8_t f[EQUIP_H][EQUIP_W];
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++)
            f[y][x] = (fr->blade7[y] >> x) & 1 ? BLADE : fr->px[y][x];
    int g = info->game;
    int ys2 = g == EQUIP_GAME_YS2;
    const ShieldArt *sh = info->shield == 0xFF ? NULL : &k_shield_art[ys2][info->shield];

    /* pixels of the game's shield (those of mask in its column; the rest of mask is the arm) */
    uint16_t area[EQUIP_H];
    int has_area = 0;
    for (int y = 0; y < EQUIP_H; y++) {
        int lo = k_shield_cols[fr->dir][0], hi = k_shield_cols[fr->dir][1];
        area[y] = sh ? (uint16_t)(fr->mask[y] & (((1u << (hi + 1)) - 1) & ~((1u << lo) - 1))) : 0;
        has_area |= area[y] != 0;
    }
    int front = fr->dir == 0 && has_area, ox = 0, oy = 0;
    if (front) {
        /* corner of the game's shield in this frame (the drawing moves along) and the body under it */
        int mx = EQUIP_W, my = EQUIP_H;
        for (int y = 0; y < EQUIP_H; y++)
            for (int x = 0; x < EQUIP_W; x++)
                if (((area[y] >> x) & 1) && is_role(f[y][x])) {
                    mx = x < mx ? x : mx;
                    my = y < my ? y : my;
                }
        ox = mx - ART_FACE_X;
        oy = my - ART_FACE_Y;
        for (int y = 0; y < EQUIP_H; y++)
            for (int x = 0; x < EQUIP_W; x++)
                if ((area[y] >> x) & 1)
                    f[y][x] = fr->under[y][x];
    }
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++)
            out[y * EQUIP_W + x] = f[y][x] ? (int16_t)pal[f[y][x]] : NONE_PX;
    armor(out, f, fr->dir, pose, info->armor == 0xFF ? &k_no_armor_art : &k_armor_art[ys2][info->armor], pal);
    sword(out, f, fr->mask, info->sword == 0xFF ? NULL : &k_sword_art[ys2][info->sword], pal);

    if (!sh) {
        /* no shield on a frame with a shield: its pixels as in the game */
        for (int y = 0; y < EQUIP_H; y++)
            for (int x = 0; x < EQUIP_W; x++)
                if ((fr->mask[y] >> x) & 1)
                    out[y * EQUIP_W + x] = f[y][x] ? (int16_t)pal[f[y][x]] : NONE_PX;
        return;
    }
    if (front) {
        for (int j = 0; j < 12; j++)
            for (int i = 0; i < 8; i++) {
                int16_t c = sh->front[j][i];
                int X = ART_FRONT_X0 + ox + i, Y = ART_FRONT_Y0 + oy + j;
                if (c != ART_KEEP && X >= 0 && X < EQUIP_W && Y >= 0 && Y < EQUIP_H)
                    out[Y * EQUIP_W + X] = c == ART_OUT ? (int16_t)pal[OUT] : c;
            }
        return;
    }
    shield_edge(out, f, area, sh, pal);
}

void equip_compose(const EquipFrame *fr, const uint16_t *pal, const EquipInfo *info, EquipImage out)
{
    compose_parts(fr, pal, info, fr->pose, out);
    /* the face as in the game (equip_art.head_guard): the head's skin and the outline around it, eyes and chin */
    const uint8_t (*base)[EQUIP_W] = fr->set == EQUIP_SET_SS ? fr->under : fr->px;
    for (int y = 5; y < 14; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            if (base[y][x] != SKIN)
                continue;
            out[y * EQUIP_W + x] = (int16_t)pal[SKIN];
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    int X = x + dx, Y = y + dy;
                    if (X >= 0 && X < EQUIP_W && base[Y][X] == OUT)
                        out[Y * EQUIP_W + X] = (int16_t)pal[OUT];
                }
        }
}

/* ---- Reflection on the ice (equip_art.ice_compose) ---- */

enum { ICE_FIRST = 24, ICE_RROWS = 7 };
static const uint8_t k_ice_rows[ICE_RROWS] = { 22, 19, 17, 14, 11, 9, 6 }; /* Adol's row for each row of the reflection */
static const uint8_t k_ice_top[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 12, 1, 14, 15 };
static const uint8_t k_ice_reflect[15] = { 0, 8, 11, 12, 2, 7, 6, 7, 5, 4, 10, 13, 6, 1, 1 };

static int cap7(int v) { return v > 7 ? 7 : v; }

/* Color of an item on Adol over the ice (equip_art.ice_top_color) */
static int16_t ice_top_color(int v)
{
    return (int16_t)(C((v >> 3) & 7, cap7(((v >> 6) & 7) + 1), v & 7));
}

/* Color of an item in the reflection (equip_art.ice_reflect_color) */
static int16_t ice_reflect_color(int v)
{
    int r = (v >> 3) & 7, g = (v >> 6) & 7, b = v & 7, top = r > g ? r : g;
    top = top > b ? top : b;
    if (b == top)
        return (int16_t)(C(cap7(r + 1), cap7(g + 3), cap7(b + 2)));
    if (r == top)
        return (int16_t)(C(cap7(r + 2), cap7(g + 1), g >= 5 ? cap7(b + 3) : b));
    return (int16_t)(C(cap7(r + 2), cap7(g + 2), cap7(b + 1)));
}

/* The pixel is still the game's: only then does its color swap apply. An item with a color equal to one in its
 * palette goes by the items' rule (the items' yellow used to turn orange on the ice) */
static int ice_own(const EquipFrame *fr, const uint16_t *pal, int x, int y, int16_t v)
{
    if (v == (int16_t)pal[OUT])
        return 1;
    int c = (fr->blade7[y] >> x) & 1 ? BLADE : fr->px[y][x];
    if (v == (int16_t)pal[c])
        return 1;
    return fr->set == EQUIP_SET_SS && v == (int16_t)pal[fr->under[y][x]];
}

void equip_ice(const EquipFrame *fr, const uint16_t *pal, const uint16_t *ice_pal, EquipImage io)
{
    /* without the shadow: its pixels only go if they are still the game's outline */
    for (int y = 0; y < ICE_FIRST; y++)
        for (int x = 0; x < EQUIP_W; x++)
            if (((fr->ice_shadow[y] >> x) & 1) && io[y * EQUIP_W + x] == (int16_t)pal[OUT])
                io[y * EQUIP_W + x] = NONE_PX;
    /* the reflection (before swapping the colors above) */
    for (int y = ICE_FIRST; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            int16_t c = NONE_PX;
            if (y < ICE_FIRST + ICE_RROWS) {
                int src = k_ice_rows[y - ICE_FIRST];
                int16_t v = io[src * EQUIP_W + x];
                if (v != NONE_PX) {
                    c = ice_reflect_color(v);
                    for (int i = 1; i <= 14 && ice_own(fr, pal, x, src, v); i++)
                        if (pal[i] == (uint16_t)v)
                            c = (int16_t)ice_pal[k_ice_reflect[i]];
                }
            }
            io[y * EQUIP_W + x] = c;
        }
    /* Adol's colors */
    for (int i = 0; i < ICE_FIRST * EQUIP_W; i++) {
        int16_t v = io[i];
        if (v == NONE_PX)
            continue;
        int16_t c = ice_top_color(v);
        for (int k = 1; k < 16 && ice_own(fr, pal, i % EQUIP_W, i / EQUIP_W, v); k++)
            if (pal[k] == (uint16_t)v)
                c = (int16_t)ice_pal[k_ice_top[k]];
        io[i] = c;
    }
}

int equip_ice_pose(int pose, const uint16_t *e)
{
    /* drawings 10-1F: direction in bits 2-3; step in bits 0-1 (standing, step 1, standing, step 2) */
    static const uint8_t k_step[4] = { 0, 1, 0, 2 };
    if (pose < 0x10 || pose > 0x1F || (e[3] & 0x0F) != 14 || (e[3] & 0x0100) || ((e[3] >> 12) & 3) != 1)
        return -1;
    return ((pose >> 2) & 3) * 3 + k_step[pose & 3];
}

static void vram_sprite(const uint16_t *vram, const uint16_t *e, uint8_t px[EQUIP_H][EQUIP_W])
{
    /* The drawing as it is in VRAM (16x32: two 16x16 pieces, one below the other) */
    uint16_t pattern = (uint16_t)((e[2] >> 1) & 0x3FD);
    for (int y = 0; y < EQUIP_H; y++) {
        uint16_t line = (uint16_t)((pattern << 6) + (y >> 4) * 128 + (y & 15));
        uint16_t p[4];
        for (int k = 0; k < 4; k++)
            p[k] = vram[(line + k * 16) & 0x7FFF];
        for (int x = 0; x < EQUIP_W; x++)
            px[y][x] = (uint8_t)(((p[0] >> (15 - x)) & 1) | (((p[1] >> (15 - x)) & 1) << 1) |
                                 (((p[2] >> (15 - x)) & 1) << 2) | (((p[3] >> (15 - x)) & 1) << 3));
    }
}

int equip_ice_diff(const EquipFrame *fr, const uint16_t *vram, const uint16_t *e)
{
    uint8_t px[EQUIP_H][EQUIP_W];
    vram_sprite(vram, e, px);
    int diff = 0;
    for (int y = 0; y < EQUIP_H; y++)
        for (int x = 0; x < EQUIP_W; x++) {
            int want = 0;
            if (y < ICE_FIRST) {
                int c = (fr->blade7[y] >> x) & 1 ? BLADE : fr->px[y][x];
                if (c && !((fr->ice_shadow[y] >> x) & 1))
                    want = k_ice_top[c];
            } else if (y < ICE_FIRST + ICE_RROWS) {
                int sy = k_ice_rows[y - ICE_FIRST], c = fr->px[sy][x];
                if (c && c < 15 && !((fr->ice_shadow[sy] >> x) & 1))
                    want = k_ice_reflect[c];
            }
            diff += want != px[y][x];
        }
    return diff;
}

/* ---- In the window ---- */

void equip_init(Equip *q, const EquipTable *table)
{
    memset(q, 0, sizeof(*q));
    q->table = table;
    for (int i = 0; i < EQUIP_CACHE; i++)
        q->cache[i].frame = -1;
}

void equip_read(const Pce *pce, EquipInfo *out)
{
    memset(out, 0, sizeof(*out));
    if (!pce->cd)
        return;
    /* $82:6321: the 1st item of each type (Ys I: 0, 5, 10, 15; Ys II: 0, 6, 12, 18), in the CD RAM (bank $82 = 2) */
    const uint8_t *t = &pce->cd->ram[2 * 0x2000 + 0x0321];
    int game = t[0] == 0 && t[1] == 5 && t[2] == 10 && t[3] == 15   ? EQUIP_GAME_YS1
               : t[0] == 0 && t[1] == 6 && t[2] == 12 && t[3] == 18 ? EQUIP_GAME_YS2
                                                                    : EQUIP_GAME_NONE;
    out->sword = pce->ram[0x2C71 - 0x2000];
    out->shield = pce->ram[0x2C72 - 0x2000];
    out->armor = pce->ram[0x2C73 - 0x2000];
    const uint8_t v[3] = { out->sword, out->shield, out->armor };
    for (int k = 0; k < 3; k++)
        if (v[k] != 0xFF && v[k] >= equip_count(game, k))
            game = EQUIP_GAME_NONE; /* before the game starts these bytes hold garbage */
    out->game = (uint8_t)game;
}

/* A palette (13 or 14) against the reference one */
static void color_set(const uint16_t *ref, const YsFadeFrame *fade, int pal, EquipColorSet *out)
{
    const uint16_t *cur = &fade->color[256 + pal * 16];
    const YsFadeColor *c = &fade->c[256 + pal * 16];
    int full = 1, flat = 1, faded = fade->table_ok;
    for (int i = 1; i < 16; i++) {
        full &= cur[i] == ref[i];
        flat &= cur[i] == cur[1];
        faded &= c[i].on && c[i].src == ref[i] && c[i].level == c[1].level && c[i].add == c[1].add;
    }
    if (full || faded) {
        out->kind = EQUIP_COLORS_MAP;
        for (int v = 0; v < 8; v++)
            out->map[v] = (uint8_t)(full ? v : ys_fade_channel(fade->table, v, c[1].level, c[1].add, 1));
    } else if (flat) {
        out->kind = EQUIP_COLORS_FLAT;
        out->flat = cur[1];
    }
}

void equip_colors(const EquipTable *table, const YsFadeFrame *fade, EquipColors *out)
{
    memset(out, 0, sizeof(*out));
    if (!table)
        return;
    EquipColorSet normal;
    memset(&normal, 0, sizeof(normal));
    color_set(table->pal, fade, 13, &normal);
    out->kind = normal.kind;
    memcpy(out->map, normal.map, sizeof(out->map));
    out->flat = normal.flat;
    color_set(table->ice_pal, fade, 14, &out->ice);
}

int equip_frame_of(const EquipTable *table, const uint16_t *vram, const uint16_t *e)
{
    uint16_t flags = e[3];
    if ((flags & 0x0100) || ((flags >> 12) & 3) != 1)
        return -1; /* not 16x32 */
    uint8_t px[EQUIP_H][EQUIP_W];
    vram_sprite(vram, e, px);
    for (int i = 0; i < EQUIP_FRAMES; i++)
        if (memcmp(px, table->frame[i].px, sizeof(px)) == 0)
            return i;
    return -1;
}

const uint16_t *equip_sprite(Equip *q, const EquipInfo *info, const EquipColors *colors, const uint16_t *vram,
                             const uint16_t *e, int pose)
{
    if (!q || !q->table || info->game == EQUIP_GAME_NONE)
        return NULL;
    /* The drawings are for palette 13, normal Adol's (the game swaps his shape for the Ys II monsters: the drawing
     * is none of the 36 and he comes out as on the console), or 14 with drawings 10-1F, his on the ice (recognized by
     * what the engine asks for, without looking at the pixels) */
    int ice = (e[3] & 0x0F) == 14;
    int kind = ice ? colors->ice.kind : colors->kind;
    if ((!ice && (e[3] & 0x0F) != 13) || kind == EQUIP_COLORS_NONE)
        return NULL;
    int fi = ice ? equip_ice_pose(pose, e) : equip_frame_of(q->table, vram, e);
    if (fi < 0)
        return NULL;
    /* The frame set of what is equipped (the game swaps the frames in VRAM a little after the RAM): same direction
     * and step; with a shield and no sword, the one with a shield (the blade goes in equip_compose) */
    int want = info->shield != 0xFF ? EQUIP_SET_SS : info->sword != 0xFF ? EQUIP_SET_SWORD : EQUIP_SET_NOSWORD;
    fi = want * (EQUIP_FRAMES / 3) + fi % (EQUIP_FRAMES / 3);
    int ci = -1;
    for (int i = 0; i < EQUIP_CACHE && ci < 0; i++)
        if (q->cache[i].frame == fi && q->cache[i].ice == ice && memcmp(&q->cache[i].info, info, sizeof(*info)) == 0)
            ci = i;
    if (ci < 0) {
        ci = q->cache_next;
        q->cache_next = (q->cache_next + 1) % EQUIP_CACHE;
        q->cache[ci].frame = fi;
        q->cache[ci].ice = ice;
        q->cache[ci].info = *info;
        equip_compose(&q->table->frame[fi], q->table->pal, info, q->cache[ci].img);
        if (ice)
            equip_ice(&q->table->frame[fi], q->table->pal, q->table->ice_pal, q->cache[ci].img);
    }
    uint16_t *out = q->out[q->out_next];
    q->out_next = (q->out_next + 1) % EQUIP_CACHE;
    const int16_t *img = q->cache[ci].img;
    const uint8_t *m = ice ? colors->ice.map : colors->map;
    uint16_t flat = ice ? colors->ice.flat : colors->flat;
    for (int i = 0; i < EQUIP_H * EQUIP_W; i++) {
        int v = img[i];
        if (v < 0) {
            out[i] = 0;
            continue;
        }
        if (kind == EQUIP_COLORS_FLAT) {
            out[i] = (uint16_t)(0x8000 | flat);
            continue;
        }
        out[i] = (uint16_t)(0x8000 | (m[(v >> 6) & 7] << 6) | (m[(v >> 3) & 7] << 3) | m[v & 7]);
    }
    q->shown++;
    return out;
}
