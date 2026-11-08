#include "app/fade.h"

#include <string.h>

#include "app/redraw.h"
#include "app/ys_fade.h"

int fade_track(FadeTrack *t, int level, int max_d)
{
    int q = level * FADE_Q;
    if (t->age < 255)
        t->age++;
    if (!t->valid || q != t->target) {
        int d = q - t->target;
        if (t->valid && d != 0 && d <= max_d * FADE_Q && d >= -max_d * FADE_Q) {
            /* the time between this color's steps (the 1st step: FADE_PERIOD), from what is on screen */
            int n = t->valid == 2 && t->age >= 2 && t->age <= FADE_MAX_PERIOD ? t->age : FADE_PERIOD, e = q - t->shown;
            t->target = (int16_t)q;
            t->step = (int16_t)((e + (e > 0 ? n - 1 : e < 0 ? 1 - n : 0)) / n);
            t->valid = 2;
        } else {
            t->shown = t->target = (int16_t)q;
            t->step = 0;
            t->valid = 1;
        }
        t->age = 0;
    }
    if (t->shown != t->target) {
        int v = t->shown + t->step;
        t->shown = (int16_t)((t->step > 0 && v > t->target) || (t->step < 0 && v < t->target) ? t->target : v);
    }
    return t->shown;
}

static int q_to_8bit(int q) { return q * 255 / (7 * FADE_Q); } /* at integer levels, the same as vce_rgb */

int fade_colors(FadePalette *p, const uint16_t color[512], const YsFadeFrame *yf, uint32_t rgb[512], int on)
{
    int moving = 0;
    for (int i = 0; i < 512; i++) {
        uint16_t c = color[i] & 0x1FF;
        if (!on) {
            p->ch[i][0].valid = p->ch[i][1].valid = p->ch[i][2].valid = 0;
            rgb[i] = vce_rgb(c);
            continue;
        }
        /* the game's fade step: each channel stays or moves 1 level (the game's table), all in the same direction */
        int step = yf && yf->step[i];
        int b = fade_track(&p->ch[i][0], c & 7, step);
        int r = fade_track(&p->ch[i][1], (c >> 3) & 7, step);
        int g = fade_track(&p->ch[i][2], (c >> 6) & 7, step);
        moving += b != p->ch[i][0].target || r != p->ch[i][1].target || g != p->ch[i][2].target;
        rgb[i] = 0xFF000000u | (uint32_t)q_to_8bit(r) << 16 | (uint32_t)q_to_8bit(g) << 8 | (uint32_t)q_to_8bit(b);
    }
    return moving;
}

void fade_palette(FadePalette *p, RedrawFrame *f, const YsFadeFrame *yf, int on)
{
    uint32_t rgb[512];
    fade_colors(p, f->color, yf, rgb, on);
    if (!on)
        return; /* f->rgb keeps the console's colors (redraw_capture) */
    memcpy(f->rgb, rgb, sizeof(rgb));
    /* The equipment's direct colors: each level of Adol's sprite palette (13) as it is on screen */
    uint8_t seen[3][8] = { { 0 } };
    for (int i = 256 + 13 * 16 + 1; i < 256 + 14 * 16; i++)
        for (int k = 0; k < 3; k++) {
            int l = (f->color[i] >> (3 * k)) & 7;
            if (!seen[k][l]) {
                seen[k][l] = 1;
                f->chan[k][l] = (uint8_t)q_to_8bit(p->ch[i][k].shown);
            }
        }
}

void fade_pixels(FadePixels *s, uint32_t *px, const uint16_t *index, int w, int h, const uint16_t color[512],
                 const YsFadeFrame *yf, int on)
{
    if (!fade_colors(&s->pal, color, yf, s->rgb, on) || w <= 0 || h <= 0 || w > VCE_MAX_WIDTH || h > VCE_ACTIVE_LINES)
        return;
    /* Only the pixels of the color in transition and with the end-of-frame color (a change mid-image stays as it is) */
    uint32_t now[512];
    for (int i = 0; i < 512; i++)
        now[i] = vce_rgb(color[i] & 0x1FF);
    int n = w * h;
    for (int i = 0; i < n; i++) {
        uint16_t k = index[i];
        if (k & VCE_PIXEL_BLACK)
            continue;
        k &= 0x1FF;
        if (s->rgb[k] != now[k] && px[i] == now[k])
            px[i] = s->rgb[k];
    }
}
