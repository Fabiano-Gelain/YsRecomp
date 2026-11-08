#include "app/launcher.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app/menu_font.h"

/* Verified dates (Hudson/NEC): Ys Book I & II 1989-12-21 in Japan, 1990 in the US; Ys III 1991-03-22 in
 * Japan, 1991 in the US; Ys IV 1993-12-22, Japan only */
const LauncherGame launcher_games[LAUNCHER_GAMES] = {
    { "Ys Book I & II", "JP 1989  US 1990", 1 },
    { "Ys III: Wanderers from Ys", "JP 1991  US 1991", 0 },
    { "Ys IV: The Dawn of Ys", "JP 1993", 0 },
};

static const char *const k_msg[3] = { NULL, "Now loading... Adol's journey begins.",
                                      "This adventure has not been released yet." };

/* Positions (console pixels), the mock-up's */
#define COVER_GAP 20
#define COVER_X0 ((LAUNCHER_W - (LAUNCHER_COVER_W * 3 + COVER_GAP * 2)) / 2)
#define COVER_Y 97 /* covers 10% smaller (request of 2026-10-22), with the center in the same place (was 93) */
#define WIN_X 24
#define WIN_Y 174
#define WIN_W (LAUNCHER_W - 48)
#define WIN_H 30
#define START_DELAY 20 /* ticks between the loading message finishing and the game starting */

static const uint8_t k_levels[8] = { 0x00, 0x24, 0x48, 0x6D, 0x91, 0xB6, 0xDA, 0xFF };
static const int k_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

#define RGB(c) (0xFF000000u | (uint32_t)(c))

/* One channel to the console palette, with 4x4 dithering */
static uint32_t quant(float v, int x, int y)
{
    float f = v / 255.0f * 7.0f + ((float)k_bayer[y & 3][x & 3] / 16.0f - 0.5f) * 0.9f;
    int i = (int)(f + (f >= 0 ? 0.5f : -0.5f));
    return k_levels[i < 0 ? 0 : i > 7 ? 7 : i];
}

static uint32_t g_seed;
static float rnd(void)
{
    g_seed = (uint32_t)(((uint64_t)g_seed * 1103515245u + 12345u) % 2147483648u);
    return (float)g_seed / 2147483648.0f;
}

static void fill(uint32_t *px, int pw, int ph, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > pw ? pw : x + w, y1 = y + h > ph ? ph : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            px[yy * pw + xx] = c;
}

/* Rectangle in console pixels on the 768x432 image */
static void fill2(uint32_t *out, int x, int y, int w, int h, uint32_t c)
{
    fill(out, LAUNCHER_IMG_W, LAUNCHER_IMG_H, x * 2, y * 2, w * 2, h * 2, c);
}

static void ridge(uint32_t *bg, float base, float amp, uint32_t c, int step)
{
    float h = base;
    for (int x = 0; x < LAUNCHER_W; x += step) {
        h += (rnd() - 0.5f) * amp;
        h = h < base - 22 ? base - 22 : h > base + 8 ? base + 8 : h;
        fill(bg, LAUNCHER_W, LAUNCHER_H, x, (int)h, step, LAUNCHER_H - (int)h, c);
    }
}

void launcher_init(Launcher *L)
{
    memset(L, 0, sizeof(*L));
    g_seed = 11;
    uint32_t *bg = L->bg;
    /* Sky in dithered bands */
    static const uint32_t bands[6] = { 0x000024, 0x000048, 0x00006D, 0x24246D, 0x48246D, 0x6D2448 };
    float bh = 150.0f / 5.0f;
    for (int y = 0; y < LAUNCHER_H; y++)
        for (int x = 0; x < LAUNCHER_W; x++) {
            float f = (float)y / bh;
            if (f > 5 - 0.001f)
                f = 5 - 0.001f;
            int k = (int)f;
            float t = f - (float)k;
            bg[y * LAUNCHER_W + x] = RGB(bands[k + (t * 16 > (float)(k_bayer[y & 3][x & 3] + 2) && t > 0.5f)]);
        }
    /* Moon */
    const int mx = 338, my = 30, mr = 11;
    for (int y = -mr; y <= mr; y++)
        for (int x = -mr; x <= mr; x++)
            if (x * x + y * y <= mr * mr)
                bg[(my + y) * LAUNCHER_W + mx + x] =
                    RGB((x + 4) * (x + 4) + (y + 4) * (y + 4) > 120 ? 0xDADA91 : 0xFFFFDA);
    static const int craters[4][4] = { { 3, 2, 3, 2 }, { -5, 4, 2, 2 }, { -1, -6, 2, 1 }, { 5, -3, 1, 2 } };
    for (int i = 0; i < 4; i++)
        fill(bg, LAUNCHER_W, LAUNCHER_H, mx + craters[i][0], my + craters[i][1], craters[i][2], craters[i][3],
             RGB(0xB6B66D));
    /* Distant mountains, Darm Tower on the horizon to the left, near mountains */
    ridge(bg, 150, 9, RGB(0x242448), 2);
    const int tx = 30, top = 92;
    fill(bg, LAUNCHER_W, LAUNCHER_H, tx, top, 16, LAUNCHER_H - top, RGB(0x000024));
    fill(bg, LAUNCHER_W, LAUNCHER_H, tx - 2, top + 6, 20, 3, RGB(0x000024));
    for (int i = 0; i < 5; i++)
        fill(bg, LAUNCHER_W, LAUNCHER_H, tx - 2 + i * 4, top - 3, 2, 3, RGB(0x000024));
    for (int i = 0; i < 6; i++)
        fill(bg, LAUNCHER_W, LAUNCHER_H, tx + 4 + (i % 2) * 7, top + 16 + i * 14, 2, 3, RGB(0xFFDA48));
    ridge(bg, 172, 7, RGB(0x000024), 3);
    /* Stars only in the sky, outside the moon (on the 768x432 image) */
    for (int i = 0; i < LAUNCHER_STARS; i++) {
        int x = (int)(rnd() * LAUNCHER_IMG_W), y = (int)(rnd() * 250);
        int p = (int)(rnd() * 8);
        float c = rnd();
        if ((x - 676) * (x - 676) + (y - 60) * (y - 60) <= 30 * 30)
            continue;
        L->stars[L->star_count].x = (int16_t)x;
        L->stars[L->star_count].y = (int16_t)y;
        L->stars[L->star_count].phase = (uint8_t)p;
        L->stars[L->star_count].kind = c;
        L->star_count++;
    }
}

void launcher_free(Launcher *L)
{
    free(L->logo);
    L->logo = NULL;
}

int launcher_crop_box(const uint32_t *argb, int w, int h, int *x0, int *y0, int *cw, int *ch)
{
    int *rows = calloc((size_t)h, sizeof(int)), *cols = calloc((size_t)w, sizeof(int));
    if (!rows || !cols) {
        free(rows);
        free(cols);
        return -1;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = argb[(size_t)y * w + x];
            int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (m > 48) {
                rows[y]++;
                cols[x]++;
            }
        }
    /* A row/column counts when it has more than a few lit pixels (JPG noise in the black does not count) */
    int min_r = w / 256 > 1 ? w / 256 : 1, min_c = h / 256 > 1 ? h / 256 : 1;
    int top = 0, bottom = h - 1, left = 0, right = w - 1;
    while (top < h && rows[top] <= min_r)
        top++;
    while (bottom > top && rows[bottom] <= min_r)
        bottom--;
    while (left < w && cols[left] <= min_c)
        left++;
    while (right > left && cols[right] <= min_c)
        right--;
    free(rows);
    free(cols);
    if (top >= h || left >= w)
        return -1;
    *x0 = left;
    *y0 = top;
    *cw = right - left + 1;
    *ch = bottom - top + 1;
    return 0;
}

void launcher_resample(const uint32_t *argb, int w, int sx, int sy, int sw, int sh, uint32_t *out, int dw, int dh)
{
    for (int dy = 0; dy < dh; dy++) {
        int y0 = sy + (int)((int64_t)dy * sh / dh), y1 = sy + (int)((int64_t)(dy + 1) * sh / dh);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int dx = 0; dx < dw; dx++) {
            int x0 = sx + (int)((int64_t)dx * sw / dw), x1 = sx + (int)((int64_t)(dx + 1) * sw / dw);
            if (x1 <= x0)
                x1 = x0 + 1;
            uint64_t r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    uint32_t c = argb[(size_t)y * w + x];
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            out[dy * dw + dx] = 0xFF000000u | (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
        }
    }
}

void launcher_pixelize(uint32_t *px, int w, int h, int gray)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            float r = (float)((c >> 16) & 0xFF), g = (float)((c >> 8) & 0xFF), b = (float)(c & 0xFF);
            if (gray)
                r = g = b = (r * 0.3f + g * 0.59f + b * 0.11f) * 0.6f;
            px[y * w + x] = 0xFF000000u | quant(r, x, y) << 16 | quant(g, x, y) << 8 | quant(b, x, y);
        }
}

int launcher_set_image(Launcher *L, int which, const uint32_t *argb, int w, int h)
{
    int x0, y0, cw, ch;
    if (which < 0 || which > LAUNCHER_GAMES || launcher_crop_box(argb, w, h, &x0, &y0, &cw, &ch) != 0)
        return -1;
    if (which == 0) {
        /* Logo: without the bottom text line (the top 80% of the outline); the project name comes as text */
        ch = ch * 4 / 5;
        uint32_t *logo = malloc((size_t)cw * ch * sizeof(uint32_t));
        if (!logo)
            return -1;
        for (int y = 0; y < ch; y++)
            memcpy(logo + (size_t)y * cw, argb + (size_t)(y0 + y) * w + x0, (size_t)cw * sizeof(uint32_t));
        free(L->logo);
        L->logo = logo;
        L->logo_w = cw;
        L->logo_h = ch;
        return 0;
    }
    int i = which - 1, dw = LAUNCHER_COVER_W * 2, dh = LAUNCHER_COVER_H * 2;
    launcher_resample(argb, w, x0, y0, cw, ch, L->cover[i], dw, dh);
    memcpy(L->gray[i], L->cover[i], sizeof(L->gray[i]));
    launcher_pixelize(L->cover[i], dw, dh, 0);
    launcher_pixelize(L->gray[i], dw, dh, 1);
    L->have_cover[i] = 1;
    return 0;
}

uint32_t *launcher_logo(const Launcher *L, int w, int *h)
{
    if (!L->logo || w <= 0)
        return NULL;
    int lh = (int)((int64_t)w * L->logo_h / L->logo_w);
    if (lh <= 0)
        return NULL;
    uint32_t *out = malloc((size_t)w * lh * sizeof(uint32_t));
    if (!out)
        return NULL;
    launcher_resample(L->logo, L->logo_w, 0, 0, L->logo_w, L->logo_h, out, w, lh);
    /* Golden (2026-10-22 mock-up): the light/dark of the drawing becomes a gold gradient (light on
     * top, a shine band in the middle, bronze at the bottom), with more contrast; the JPG's black background vanishes */
    static const struct {
        float t, c[3];
    } stops[5] = { { 0.0f, { 255, 250, 214 } },
                   { 0.3f, { 255, 214, 92 } },
                   { 0.5f, { 255, 246, 196 } },
                   { 0.62f, { 236, 164, 30 } },
                   { 1.0f, { 120, 62, 8 } } };
    for (int y = 0; y < lh; y++) {
        float t = lh > 1 ? (float)y / (float)(lh - 1) : 0.0f, base[3];
        int k = 0;
        while (k < 3 && t > stops[k + 1].t)
            k++;
        float f = (t - stops[k].t) / (stops[k + 1].t - stops[k].t);
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        for (int c = 0; c < 3; c++)
            base[c] = stops[k].c[c] + (stops[k + 1].c[c] - stops[k].c[c]) * f;
        for (int x = 0; x < w; x++) {
            uint32_t p = out[y * w + x];
            int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
            float lum = ((float)r * 0.3f + (float)g * 0.59f + (float)b * 0.11f) / 255.0f;
            /* x 0.85: a bit darker than in the mock-up (request of 2026-10-22) */
            float s = 0.35f + powf(lum, 0.8f) * 1.1f;
            s = (s > 1.25f ? 1.25f : s) * 0.85f;
            uint32_t rgb = 0;
            for (int c = 0; c < 3; c++) {
                float v = base[c] * s;
                rgb = rgb << 8 | (uint32_t)(v > 255.0f ? 255.0f : v);
            }
            int a = (m - 28) * 4;
            a = a < 0 ? 0 : a > 255 ? 255 : a;
            out[y * w + x] = a ? (uint32_t)a << 24 | rgb : 0;
        }
    }
    *h = lh;
    return out;
}

/* Maximum of each pixel over a square of radius r (separable) */
static void dilate(uint8_t *a, uint8_t *tmp, int w, int h, int r)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t m = 0;
            for (int d = -r; d <= r; d++)
                if (x + d >= 0 && x + d < w && a[y * w + x + d] > m)
                    m = a[y * w + x + d];
            tmp[y * w + x] = m;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t m = 0;
            for (int d = -r; d <= r; d++)
                if (y + d >= 0 && y + d < h && tmp[(y + d) * w + x] > m)
                    m = tmp[(y + d) * w + x];
            a[y * w + x] = m;
        }
}

/* Average over a segment of 2r+1 pixels, along the row and then the column (3 times: almost a Gaussian) */
static void blur(uint8_t *a, uint8_t *tmp, int w, int h, int r)
{
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            int sum = 0;
            for (int x = -r; x <= r; x++)
                sum += x >= 0 && x < w ? a[y * w + x] : 0;
            for (int x = 0; x < w; x++) {
                tmp[y * w + x] = (uint8_t)(sum / (2 * r + 1));
                sum += (x + r + 1 < w ? a[y * w + x + r + 1] : 0) - (x - r >= 0 ? a[y * w + x - r] : 0);
            }
        }
        for (int x = 0; x < w; x++) {
            int sum = 0;
            for (int y = -r; y <= r; y++)
                sum += y >= 0 && y < h ? tmp[y * w + x] : 0;
            for (int y = 0; y < h; y++) {
                a[y * w + x] = (uint8_t)(sum / (2 * r + 1));
                sum += (y + r + 1 < h ? tmp[(y + r + 1) * w + x] : 0) - (y - r >= 0 ? tmp[(y - r) * w + x] : 0);
            }
        }
    }
}

void launcher_logo_fx_free(LauncherLogoFx *fx)
{
    free(fx->gold);
    free(fx->outline);
    free(fx->lip);
    free(fx->glow);
    free(fx->out);
    memset(fx, 0, sizeof(*fx));
}

int launcher_logo_fx_build(LauncherLogoFx *fx, const Launcher *L, int w, int area_h)
{
    launcher_logo_fx_free(fx);
    int h = 0;
    uint32_t *logo = launcher_logo(L, w, &h);
    if (!logo)
        return -1;
    /* Mock-up sizes in % of the area height: outline 0.25, relief 0.5, glow 1.9 (blur radius;
     * deviation of half of it) */
    float u = (float)area_h / 100.0f;
    int ro = (int)(0.25f * u + 0.5f), lip = (int)(0.5f * u + 0.5f), rg = (int)(1.9f * u + 0.5f);
    ro = ro < 1 ? 1 : ro;
    lip = lip < 1 ? 1 : lip;
    rg = rg < 1 ? 1 : rg;
    /* deviation rg/2 with 3 averages of 2b+1 pixels: 3((2b+1)^2 - 1)/12 = (rg/2)^2 */
    int bb = (int)((sqrtf((float)rg * (float)rg + 1.0f) - 1.0f) / 2.0f + 0.5f);
    bb = bb < 1 ? 1 : bb;
    int pad = ro + lip + 3 * rg / 2 + 2, fw = w + 2 * pad, fh = h + 2 * pad;
    size_t n = (size_t)fw * fh;
    fx->gold = calloc(n, sizeof(uint32_t));
    fx->out = malloc(n * sizeof(uint32_t));
    fx->outline = calloc(n, 1);
    fx->lip = calloc(n, 1);
    fx->glow = calloc(n, 1);
    uint8_t *tmp = malloc(n);
    if (!fx->gold || !fx->out || !fx->outline || !fx->lip || !fx->glow || !tmp) {
        free(tmp);
        free(logo);
        launcher_logo_fx_free(fx);
        return -1;
    }
    fx->w = w;
    fx->h = h;
    fx->pad = pad;
    fx->fw = fw;
    fx->fh = fh;
    for (int y = 0; y < h; y++)
        memcpy(fx->gold + (size_t)(y + pad) * fw + pad, logo + (size_t)y * w, (size_t)w * sizeof(uint32_t));
    free(logo);
    /* outline: the thickened logo; relief: logo + outline, shifted down; glow: all of that blurred */
    for (size_t i = 0; i < n; i++)
        fx->outline[i] = (uint8_t)(fx->gold[i] >> 24);
    dilate(fx->outline, tmp, fw, fh, ro);
    for (int y = lip; y < fh; y++)
        memcpy(fx->lip + (size_t)y * fw, fx->outline + (size_t)(y - lip) * fw, (size_t)fw);
    for (size_t i = 0; i < n; i++)
        fx->glow[i] = fx->outline[i] > fx->lip[i] ? fx->outline[i] : fx->lip[i];
    blur(fx->glow, tmp, fw, fh, bb);
    free(tmp);
    return 0;
}

const uint32_t *launcher_logo_fx_render(LauncherLogoFx *fx, long tick)
{
    if (!fx->out)
        return NULL;
    /* pulsing glow: intensity from 0.3 to 0.55 over 3.2 s (96 steps); in the mock-up 0.45 to 0.75 (weaker at the
     * user's request on 2026-10-22) */
    float gi = 0.3f + 0.25f * (0.5f - 0.5f * cosf(6.2831853f * (float)(tick % 96) / 96.0f));
    /* shine: still from 0 to 35% of the 4.5 s (135 steps), crosses until 75% (eased at the ends); slanted band
     * (105 degrees, as in the mock-up), with a half-width of 0.24 of the logo width */
    float p = (float)(tick % 135) / 135.0f, t = p < 0.35f ? 0.0f : p > 0.75f ? 1.0f : (p - 0.35f) / 0.4f;
    t = t * t * (3.0f - 2.0f * t);
    float center = -1.5f + 4.0f * t, inv_w = 1.0f / (float)fx->w;
    for (int y = 0; y < fx->fh; y++)
        for (int x = 0; x < fx->fw; x++) {
            size_t i = (size_t)y * fx->fw + x;
            /* back to front, premultiplied: glow, relief, outline, logo */
            float a = (float)fx->glow[i] / 255.0f * gi, r = 255.0f * a, g = 186.0f * a, b = 60.0f * a;
            float la = (float)fx->lip[i] / 255.0f;
            r = 0x3A * la + r * (1.0f - la);
            g = 0x1C * la + g * (1.0f - la);
            b = b * (1.0f - la);
            a = la + a * (1.0f - la);
            float oa = (float)fx->outline[i] / 255.0f;
            r *= 1.0f - oa;
            g *= 1.0f - oa;
            b *= 1.0f - oa;
            a = oa + a * (1.0f - oa);
            uint32_t c = fx->gold[i];
            float ga = (float)(c >> 24) / 255.0f;
            if (ga > 0.0f) {
                float cr = (float)((c >> 16) & 0xFF), cg = (float)((c >> 8) & 0xFF), cb = (float)(c & 0xFF);
                float s = ((float)(x - fx->pad) * 0.966f + (float)(y - fx->pad) * 0.259f) * inv_w - center;
                float sh = 0.85f * (1.0f - fabsf(s) / 0.24f);
                if (sh > 0.0f) { /* "screen": lightens toward white */
                    cr += (255.0f - cr) * sh;
                    cg += (255.0f - cg) * sh;
                    cb += (240.0f - cb > 0.0f ? 240.0f - cb : 0.0f) * sh;
                }
                r = cr * ga + r * (1.0f - ga);
                g = cg * ga + g * (1.0f - ga);
                b = cb * ga + b * (1.0f - ga);
                a = ga + a * (1.0f - ga);
            }
            if (a <= 0.0f) {
                fx->out[i] = 0;
                continue;
            }
            uint32_t ua = (uint32_t)(a * 255.0f + 0.5f), ur = (uint32_t)(r / a + 0.5f), ug = (uint32_t)(g / a + 0.5f),
                     ub = (uint32_t)(b / a + 0.5f);
            fx->out[i] = (ua > 255 ? 255 : ua) << 24 | (ur > 255 ? 255 : ur) << 16 | (ug > 255 ? 255 : ug) << 8 |
                         (ub > 255 ? 255 : ub);
        }
    return fx->out;
}

void launcher_logo_rect(int aw, int ah, const Launcher *L, int *x, int *y, int *w, int *h)
{
    *w = aw * 32 / 100;
    *h = L->logo ? (int)((int64_t)*w * L->logo_h / L->logo_w) : 0;
    *x = (aw - *w) / 2;
    *y = ah * 9 / 1000;
}

int launcher_key(Launcher *L, LauncherKey k)
{
    if (L->msg == 1)
        return LAUNCHER_SFX_NONE; /* already loading */
    if (k == LAUNCHER_KEY_OK) {
        L->msg = launcher_games[L->sel].released ? 1 : 2;
        L->typed = 0;
        L->done_tick = 0;
        return L->msg == 1 ? LAUNCHER_SFX_OK : LAUNCHER_SFX_NO;
    }
    int n = (L->sel + (k == LAUNCHER_KEY_RIGHT ? 1 : LAUNCHER_GAMES - 1)) % LAUNCHER_GAMES;
    int sfx = n != L->sel ? LAUNCHER_SFX_MOVE : LAUNCHER_SFX_NONE;
    L->sel = n;
    L->msg = 0;
    return sfx;
}

int launcher_tick(Launcher *L)
{
    L->tick++;
    if (!L->msg)
        return 0;
    int len = (int)strlen(k_msg[L->msg]);
    if (L->typed < len)
        L->typed++;
    if (L->msg == 1 && L->typed == len && !L->done_tick)
        L->done_tick = L->tick;
    return L->msg == 1 && L->done_tick && L->tick - L->done_tick >= START_DELAY;
}

int launcher_starting(const Launcher *L)
{
    return L->msg == 1;
}

/* Game window: double border, blue in a dithered gradient */
static void window_box(uint32_t *out, int x, int y, int w, int h)
{
    fill2(out, x - 1, y - 1, w + 2, h + 2, RGB(0x000000));
    fill2(out, x, y, w, h, RGB(0xDADAFF));
    fill2(out, x + 1, y + 1, w - 2, h - 2, RGB(0x6D6D91));
    fill2(out, x + 2, y + 2, w - 4, h - 4, RGB(0x000000));
    static const uint32_t cols[4] = { 0x2448B6, 0x244891, 0x00246D, 0x000048 };
    for (int yy = 3; yy < h - 3; yy++) {
        float f = (float)(yy - 3) / (float)(h - 7) * (4 - 1.001f);
        int k = (int)f;
        float t = f - (float)k;
        for (int xx = 3; xx < w - 3; xx++)
            fill2(out, x + xx, y + yy, 1, 1, RGB(cols[k + (t * 16 > (float)k_bayer[yy & 3][xx & 3])]));
    }
}

void launcher_draw(const Launcher *L, uint32_t *out)
{
    long frame = L->tick;
    for (int y = 0; y < LAUNCHER_IMG_H; y++)
        for (int x = 0; x < LAUNCHER_IMG_W; x++)
            out[y * LAUNCHER_IMG_W + x] = L->bg[(y >> 1) * LAUNCHER_W + (x >> 1)];
    /* Twinkling stars, in the small pixels */
    for (int i = 0; i < L->star_count; i++) {
        int on = (int)((float)frame / 4.0f + (float)L->stars[i].phase) % 8;
        if (!on)
            continue;
        float c = L->stars[i].kind;
        uint32_t col = RGB(c < 0.15f ? 0xFFDA91 : on < 2 ? 0x6D6DB6 : 0xDADAFF);
        int x = L->stars[i].x, y = L->stars[i].y;
        fill(out, LAUNCHER_IMG_W, LAUNCHER_IMG_H, x, y, 2, 2, col); /* 2x2 (the 1st mock-up: 1) */
        if (c > 0.94f && on > 4) {
            fill(out, LAUNCHER_IMG_W, LAUNCHER_IMG_H, x - 4, y, 10, 2, col);
            fill(out, LAUNCHER_IMG_W, LAUNCHER_IMG_H, x, y - 4, 2, 10, col);
        }
    }
    /* Covers: the chosen one in color, 2 pixels higher, with a frame and a blinking arrow like the game cursor */
    for (int i = 0; i < LAUNCHER_GAMES; i++) {
        int x = COVER_X0 + i * (LAUNCHER_COVER_W + COVER_GAP), y = COVER_Y - (i == L->sel ? 2 : 0);
        int cw = LAUNCHER_COVER_W * 2, ch = LAUNCHER_COVER_H * 2;
        if (L->have_cover[i]) {
            const uint32_t *src = i == L->sel ? L->cover[i] : L->gray[i];
            for (int yy = 0; yy < ch; yy++)
                memcpy(out + (size_t)(y * 2 + yy) * LAUNCHER_IMG_W + x * 2, src + (size_t)yy * cw,
                       (size_t)cw * sizeof(uint32_t));
        } else {
            fill2(out, x, y, LAUNCHER_COVER_W, LAUNCHER_COVER_H, RGB(0x000024));
        }
        if (i != L->sel)
            continue;
        uint32_t c = RGB((frame >> 3) & 1 ? 0xFFFFFF : 0xFFDA48);
        fill2(out, x - 2, y - 2, LAUNCHER_COVER_W + 4, 1, c);
        fill2(out, x - 2, y + LAUNCHER_COVER_H + 1, LAUNCHER_COVER_W + 4, 1, c);
        fill2(out, x - 2, y - 2, 1, LAUNCHER_COVER_H + 4, c);
        fill2(out, x + LAUNCHER_COVER_W + 1, y - 2, 1, LAUNCHER_COVER_H + 4, c);
        int ax = x + LAUNCHER_COVER_W / 2 - 5, ay = y - 9 + (int)((frame >> 4) & 1);
        for (int r = 0; r < 6; r++)
            fill2(out, ax + r + 1, ay + r + 1, 11 - r * 2, 1, RGB(0x000000));
        for (int r = 0; r < 6; r++)
            fill2(out, ax + r, ay + r, 11 - r * 2, 1, RGB(0xFFDA48));
    }
    window_box(out, WIN_X, WIN_Y, WIN_W, WIN_H);
    /* Whole message typed: little arrow blinking in the corner, as in the game's dialogues */
    if (L->msg && L->typed == (int)strlen(k_msg[L->msg]) && (frame >> 4) & 1)
        for (int r = 0; r < 4; r++)
            fill2(out, WIN_X + WIN_W - 16 + r, WIN_Y + WIN_H - 10 + r, 7 - r * 2, 1, RGB(0xFFDA48));
}

/* Text in the menu's 8x8 font, with shadow; scale = pixels per font pixel; grad = color of each row of the
 * font (NULL = color); n = letters (-1 = all) */
static void text_big(uint32_t *out, int w, int h, const char *s, int n, int x, int y, uint32_t color, int scale,
                     const uint32_t *grad)
{
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (int i = 0; s[i] && (n < 0 || i < n); i++, cx += 8 * scale) {
            uint8_t rows[8];
            if (!menu_font_glyph(s[i], rows))
                continue;
            for (int yy = 0; yy < 8; yy++)
                for (int xx = 0; xx < 8; xx++)
                    if (rows[yy] & (0x80 >> xx))
                        fill(out, w, h, cx + xx * scale + (pass ? 0 : scale), y + yy * scale + (pass ? 0 : scale),
                             scale, scale, !pass ? RGB(0x000000) : grad ? RGB(grad[yy]) : color);
        }
    }
}

static void text(uint32_t *out, int w, int h, const char *s, int n, int x, int y, uint32_t color)
{
    text_big(out, w, h, s, n, x, y, color, 1, NULL);
}

/* Project name with the logo effects, weaker (request of 2026-10-22): golden glow around pulsing at the
 * same pace (3.2 s) and the same shine passing (4.5 s), in font pixels (the layer gets the filters). The
 * text (tw x 8 pixels at x0, y0) is already in out; the shadow is the black. */
static void title_shine(uint32_t *out, int w, int h, int x0, int y0, int tw, long tick)
{
    enum { R = 3, MW = 24 * 8 + 2 * R + 1, MH = 8 + 2 * R + 1 };
    int mw = tw + 2 * R + 1;
    if (mw > MW)
        return;
    uint8_t m[MH][MW] = { { 0 } }, t[MH][MW];
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < mw; x++) {
            int ox = x0 - R + x, oy = y0 - R + y;
            if (ox >= 0 && ox < w && oy >= 0 && oy < h && (out[oy * w + ox] & 0xFFFFFFu))
                m[y][x] = 255;
        }
    /* thickens by 1 pixel and blurs twice (3x3 average) */
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < mw; x++) {
            uint8_t v = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (y + dy >= 0 && y + dy < MH && x + dx >= 0 && x + dx < mw && m[y + dy][x + dx] > v)
                        v = m[y + dy][x + dx];
            t[y][x] = v;
        }
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < MH; y++)
            for (int x = 0; x < mw; x++) {
                int sum = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (y + dy >= 0 && y + dy < MH && x + dx >= 0 && x + dx < mw)
                            sum += pass ? m[y + dy][x + dx] : t[y + dy][x + dx];
                if (pass)
                    t[y][x] = (uint8_t)(sum / 9);
                else
                    m[y][x] = (uint8_t)(sum / 9);
            }
    /* glow from 0.2 to 0.4 (the logo's: 0.3 to 0.55); shine up to 0.5 (the logo's: 0.85) */
    float gi = 0.2f + 0.2f * (0.5f - 0.5f * cosf(6.2831853f * (float)(tick % 96) / 96.0f));
    float p = (float)(tick % 135) / 135.0f, tt = p < 0.35f ? 0.0f : p > 0.75f ? 1.0f : (p - 0.35f) / 0.4f;
    tt = tt * tt * (3.0f - 2.0f * tt);
    float center = -1.5f + 4.0f * tt;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < mw; x++) {
            int ox = x0 - R + x, oy = y0 - R + y;
            if (ox < 0 || ox >= w || oy < 0 || oy >= h)
                continue;
            uint32_t *px = &out[oy * w + ox];
            if (!*px) {
                uint32_t a = (uint32_t)((float)t[y][x] * gi + 0.5f);
                if (a)
                    *px = a << 24 | 0xFFBA3Cu;
            } else if (*px & 0xFFFFFFu) {
                float s = ((float)(x - R) * 0.966f + (float)(y - R) * 0.259f) / (float)tw - center;
                float sh = 0.5f * (1.0f - fabsf(s) / 0.24f);
                if (sh > 0.0f) {
                    float cr = (float)((*px >> 16) & 0xFF), cg = (float)((*px >> 8) & 0xFF), cb = (float)(*px & 0xFF);
                    cr += (255.0f - cr) * sh;
                    cg += (255.0f - cg) * sh;
                    cb += (cb < 240.0f ? 240.0f - cb : 0.0f) * sh;
                    *px = 0xFF000000u | (uint32_t)cr << 16 | (uint32_t)cg << 8 | (uint32_t)cb;
                }
            }
        }
}

int launcher_text_scale(int area_h, int part)
{
    /* Font at 3.1% of the height (project name and window) or 2.2% (help), as in the mock-up (8 font pixels
     * per character) */
    int k = (int)((float)area_h * (part == LAUNCHER_TEXT_HINT ? 0.022f : 0.031f) / 8.0f + 0.5f);
    return k < 1 ? 1 : k;
}

void launcher_draw_text(const Launcher *L, uint32_t *out, int w, int h, int part)
{
    memset(out, 0, (size_t)w * h * sizeof(uint32_t));
    if (part == LAUNCHER_TEXT_HINT) {
        /* Help line: keys in white, what they do in bluish gray */
        static const struct {
            const char *s;
            uint32_t c;
        } hint[] = { { "< >", 0xFFFFFF },  { " CHOOSE   ", 0x9191B6 }, { "RUN", 0xFFFFFF },
                     { " START   ", 0x9191B6 }, { "SELECT", 0xFFFFFF }, { " OPTIONS", 0x9191B6 } };
        int len = 0;
        for (size_t i = 0; i < sizeof(hint) / sizeof(hint[0]); i++)
            len += (int)strlen(hint[i].s);
        int hx = (w - len * 8) / 2;
        for (size_t i = 0; i < sizeof(hint) / sizeof(hint[0]); i++) {
            text(out, w, h, hint[i].s, -1, hx, h * 955 / 1000, RGB(hint[i].c)); /* below the window (94.9%) */
            hx += (int)strlen(hint[i].s) * 8;
        }
        return;
    }
    /* Project name: golden gradient (light on top, dark at the bottom), like the logo */
    static const char title[] = "PC-ENGINE RECOMP PROJECT";
    static const uint32_t gold[8] = { 0xFFFFB6, 0xFFFF6D, 0xFFDA48, 0xFFB624, 0xDA9124, 0xB66D00, 0x916D00, 0x916D00 };
    int t0x = (w - (int)(sizeof(title) - 1) * 8) / 2, t0y = h * 336 / 1000, tw = (int)(sizeof(title) - 1) * 8;
    text_big(out, w, h, title, -1, t0x, t0y, 0, 1, gold);
    title_shine(out, w, h, t0x, t0y, tw, L->tick);
    /* The 2 lines (13 pixels from one to the other, 9 with the shadow) centered on the height of the window interior
     * (rows 176-202 of 216: 87.5% in the middle), whatever the rounded font size */
    int tx = w * 86 / 1000, line = 13, ty = h * 875 / 1000 - (line + 9) / 2;
    if (L->msg) {
        /* Message letter by letter, wrapped at words inside the window */
        const char *m = k_msg[L->msg];
        int max = (w * (WIN_X + WIN_W - 3) / LAUNCHER_W - tx) / 8, left = L->typed, row = 0;
        while (*m && left > 0) {
            int len = (int)strlen(m), cut = len;
            if (len > max) {
                cut = max;
                while (cut > 0 && m[cut] != ' ')
                    cut--;
                if (!cut)
                    cut = max;
            }
            text(out, w, h, m, cut < left ? cut : left, tx, ty + row * line, RGB(0xFFFFFF));
            left -= cut;
            m += cut;
            while (*m == ' ') {
                m++;
                left--;
            }
            row++;
        }
    } else {
        const LauncherGame *g = &launcher_games[L->sel];
        text(out, w, h, g->name, -1, tx, ty, RGB(0xFFDA48));
        text(out, w, h, "Released", -1, tx, ty + line, RGB(0xFFFFFF));
        text(out, w, h, g->date, -1, tx + 9 * 8, ty + line, RGB(0xFFFFFF));
    }
}
