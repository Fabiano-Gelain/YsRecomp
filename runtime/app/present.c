#include "app/present.h"

#include "app/redraw.h"

#define PRESENT_LINE_ROWS 6 /* steps of each line's profile (the linear filter joins one to the next) */

/* [0] = normal, [1] = bright (crt_brightness=bright: softer lines, lighter grille, more added brightness;
 * white stays near full instead of ~80%) */
/* Brightness along one image line, top to bottom (255 = full) */
static const Uint8 k_line_profile[2][PRESENT_LINE_ROWS] = { { 115, 217, 255, 255, 217, 115 },
                                                            { 150, 228, 255, 255, 228, 150 } };
/* Grille: red, green, blue */
static const Uint32 k_mask[2][3] = { { 0xFFFFB0B0u, 0xFFB0FFB0u, 0xFFB0B0FFu },
                                     { 0xFFFFC8C8u, 0xFFC8FFC8u, 0xFFC8C8FFu } };
static const float k_glow[2] = { 0.2f, 0.25f }; /* how much of the image is added back on top of the CRT */
/* How much of the diffuse glow is added; in the values with it, the sharp image comes back with half of k_glow (otherwise
 * white fills the lines and they vanish) */
static const float k_bloom[2] = { 0.25f, 0.3f };
static const float k_slot_gap[2] = { 0.72f, 0.82f }; /* how much of the grille remains in the gap between slot bricks */

enum { MASK_NONE, MASK_APERTURE, MASK_SLOT, MASK_SHADOW };

static int crt_mask_kind(int crt)
{
    switch (crt) {
    case VIDEO_CRT_MASK:
    case VIDEO_CRT_GLOW_MASK: return MASK_APERTURE;
    case VIDEO_CRT_SLOT:
    case VIDEO_CRT_GLOW_SLOT: return MASK_SLOT;
    case VIDEO_CRT_SHADOW: return MASK_SHADOW;
    default: return MASK_NONE;
    }
}

/* Screen pixels per grille unit. The columns (Lines+mask, Glow+mask) always have 1 pixel per color, at any
 * resolution: user preference (at 1440p, 2 pixels looked coarse). Slot and shadow: by the height of the
 * output (the window, not the layer being assembled). */
static int mask_unit(SDL_Renderer *r, int kind)
{
    if (kind == MASK_APERTURE)
        return 1;
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(r, &ow, &oh);
    if (kind == MASK_SHADOW)
        return oh >= 2000 ? 2 : 1;
    return oh >= 1260 ? 2 : 1;
}

static void mask_size(int kind, int unit, int *w, int *h)
{
    *w = (kind == MASK_APERTURE ? 3 : 6) * unit;
    *h = (kind == MASK_APERTURE ? 1 : 4) * unit;
}

static Uint32 dim(Uint32 c, float f)
{
    Uint32 out = 0xFF000000u;
    for (int s = 0; s < 24; s += 8)
        out |= (Uint32)((float)(c >> s & 0xFF) * f + 0.5f) << s;
    return out;
}

/* One tile of the grille, w x h (mask_size). Aperture: R G B in columns. Slot: the columns in bricks 4 units
 * tall with a 1-unit gap; the 2nd triad offset by 2 units. Shadow: each color 2 units wide, bands
 * 2 units tall, each half a triad (3 units) ahead: dots in a triangle. */
static void mask_pixels(int kind, int unit, int bright, Uint32 *px, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int ux = x / unit, uy = y / unit;
            Uint32 c;
            if (kind == MASK_SHADOW)
                c = k_mask[bright][((ux + 3 * (uy / 2)) % 6) / 2];
            else {
                c = k_mask[bright][ux % 3];
                if (kind == MASK_SLOT && uy == (ux < 3 ? 3 : 1))
                    c = dim(c, k_slot_gap[bright]);
            }
            px[y * w + x] = c;
        }
}

static SDL_Texture *make_texture(SDL_Renderer *r, int w, int h, const Uint32 *pixels, SDL_ScaleMode scale)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t)
        return NULL;
    SDL_UpdateTexture(t, NULL, pixels, w * (int)sizeof(Uint32));
    SDL_SetTextureScaleMode(t, scale);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_MOD);
    return t;
}

/* CRT lines and grille over dst (the h-line image already drawn). *lines / *lines_h: the cached line
 * profile (one for the image, another for the present_overlay layer). The grille starts at screen column
 * mask_x (the same as the image's, so the layer continues its grille). */
static void draw_crt(SDL_Renderer *r, const SDL_FRect *dst, int h, int crt, int bright, SDL_Texture **lines,
                     int *lines_key, SDL_Texture **mask, int *mask_key, float mask_x, float mask_y)
{
    if (dst->h >= 3.0f * h) {
        int rows = h * PRESENT_LINE_ROWS, key = rows * 2 + bright;
        if (!*lines || *lines_key != key) {
            if (*lines)
                SDL_DestroyTexture(*lines);
            *lines_key = key;
            Uint32 *col = SDL_malloc((size_t)rows * sizeof(Uint32));
            *lines = NULL;
            if (col) {
                for (int i = 0; i < rows; i++) {
                    Uint32 b = k_line_profile[bright][i % PRESENT_LINE_ROWS];
                    col[i] = 0xFF000000u | b << 16 | b << 8 | b;
                }
                *lines = make_texture(r, 1, rows, col, SDL_SCALEMODE_LINEAR);
                SDL_free(col);
            }
        }
        if (*lines)
            SDL_RenderTexture(r, *lines, NULL, dst);
    }
    int kind = crt_mask_kind(crt);
    if (kind == MASK_NONE)
        return;
    int unit = mask_unit(r, kind), mw = 0, mh = 0, key = kind * 16 + bright * 8 + unit;
    mask_size(kind, unit, &mw, &mh);
    if (!*mask || *mask_key != key) {
        if (*mask)
            SDL_DestroyTexture(*mask);
        *mask_key = key;
        *mask = NULL;
        Uint32 *px = SDL_malloc((size_t)mw * (size_t)mh * sizeof(Uint32));
        if (px) {
            mask_pixels(kind, unit, bright, px, mw, mh);
            *mask = make_texture(r, mw, mh, px, SDL_SCALEMODE_NEAREST);
            SDL_free(px);
        }
    }
    if (*mask) {
        /* Tiled starts at the rectangle's corner: start earlier, at the phase of (mask_x, mask_y), clipped to dst */
        int sx = ((int)(dst->x - mask_x) % mw + mw) % mw, sy = ((int)(dst->y - mask_y) % mh + mh) % mh;
        SDL_FRect t = { dst->x - (float)sx, dst->y - (float)sy, dst->w + (float)sx, dst->h + (float)sy };
        SDL_Rect clip = { (int)dst->x, (int)dst->y, (int)dst->w, (int)dst->h };
        SDL_SetRenderClipRect(r, sx || sy ? &clip : NULL);
        SDL_RenderTextureTiled(r, *mask, NULL, 1.0f, &t);
        SDL_SetRenderClipRect(r, NULL);
    }
}

static SDL_Texture *bloom_texture(SDL_Renderer *r, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w < 1 ? 1 : w, h < 1 ? 1 : h);
    if (t)
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

/* Copies the rectangle (0, 0, from_w, from_h) of from to (0, 0, to_w, to_h) of the target to, cleared to black first (the
 * linear filter at the edge picks up black, not leftovers from a previous time) */
static void bloom_pass(SDL_Renderer *r, SDL_Texture *from, const SDL_FRect *from_src, SDL_Texture *to, float to_w,
                       float to_h)
{
    SDL_FRect d = { 0, 0, to_w, to_h };
    SDL_SetRenderTarget(r, to);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderTexture(r, from, from_src, &d);
}

/* Adds (ADD) from_src of from into rectangle to of the current target, times alpha */
static void bloom_add(SDL_Renderer *r, SDL_Texture *from, const SDL_FRect *from_src, const SDL_FRect *to, float alpha)
{
    SDL_SetTextureBlendMode(from, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaModFloat(from, alpha);
    SDL_RenderTexture(r, from, from_src, to);
    SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaModFloat(from, 1.0f);
}

/* Diffuse glow over dst (on the current target): shown (the image drawn in dst) halved 3 times,
 * squared (only bright spreads much), downscaled 2 more times (1/16, 1/32) and the 3 combined (half of the 1/8 one, a
 * quarter of each of the others, upscaled with a linear filter), added: bright bleeds ~2 image pixels strongly and ~6
 * weakly. Only downscales and upscales (nothing at half a pixel): same on the GPU and in the tests' renderer.
 * Textures at output size (smaller layers use a piece of them, without recreating them for every image). */
static void draw_bloom(Presenter *p, SDL_Renderer *r, SDL_Texture *shown, const SDL_FRect *shown_src,
                       const SDL_FRect *dst, int bright)
{
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(r, &ow, &oh);
    int bw = (ow + 1) / 2, bh = (oh + 1) / 2;
    if (bw <= 0 || bh <= 0 || dst->w <= 0 || dst->h <= 0)
        return;
    if (!p->bloom[0] || p->bloom_w != bw || p->bloom_h != bh) {
        for (int i = 0; i < PRESENT_BLOOM_TEX; i++)
            if (p->bloom[i])
                SDL_DestroyTexture(p->bloom[i]);
        p->bloom_w = bw;
        p->bloom_h = bh;
        p->bloom[0] = bloom_texture(r, bw, bh);
        p->bloom[1] = bloom_texture(r, (bw + 1) / 2, (bh + 1) / 2);
        p->bloom[2] = bloom_texture(r, (bw + 3) / 4, (bh + 3) / 4);
        p->bloom[3] = bloom_texture(r, (bw + 3) / 4, (bh + 3) / 4);
        p->bloom[4] = bloom_texture(r, (bw + 7) / 8, (bh + 7) / 8);
        p->bloom[5] = bloom_texture(r, (bw + 15) / 16, (bh + 15) / 16);
    }
    for (int i = 0; i < PRESENT_BLOOM_TEX; i++)
        if (!p->bloom[i])
            return;
    SDL_Texture *target = SDL_GetRenderTarget(r);
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_ScaleMode scale = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(shown, &scale);
    float w = SDL_min(dst->w * 0.5f, (float)bw), h = SDL_min(dst->h * 0.5f, (float)bh);
    SDL_FRect a = { 0, 0, w, h }, b = { 0, 0, w * 0.5f, h * 0.5f }, c = { 0, 0, w * 0.25f, h * 0.25f };
    SDL_FRect e = { 0, 0, w * 0.125f, h * 0.125f }, f = { 0, 0, w * 0.0625f, h * 0.0625f };
    /* 1st downscale: a layer's transparent part becomes black (blended over black) */
    SDL_SetTextureScaleMode(shown, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(shown, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaModFloat(shown, 1.0f);
    bloom_pass(r, shown, shown_src, p->bloom[0], a.w, a.h);
    SDL_SetTextureBlendMode(shown, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(shown, scale);
    for (int i = 0; i < PRESENT_BLOOM_TEX; i++) {
        SDL_SetTextureBlendMode(p->bloom[i], SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaModFloat(p->bloom[i], 1.0f);
        SDL_SetTextureColorModFloat(p->bloom[i], 1.0f, 1.0f, 1.0f);
    }
    bloom_pass(r, p->bloom[0], &a, p->bloom[1], b.w, b.h);
    bloom_pass(r, p->bloom[1], &b, p->bloom[2], c.w, c.h);
    bloom_pass(r, p->bloom[2], &c, p->bloom[3], c.w, c.h);
    SDL_SetTextureBlendMode(p->bloom[2], SDL_BLENDMODE_MOD); /* squared: [3] = Q */
    SDL_RenderTexture(r, p->bloom[2], &c, &c);
    SDL_SetTextureBlendMode(p->bloom[2], SDL_BLENDMODE_NONE);
    bloom_pass(r, p->bloom[3], &c, p->bloom[4], e.w, e.h); /* Q/16 */
    bloom_pass(r, p->bloom[4], &e, p->bloom[5], f.w, f.h); /* Q/32 */
    /* Combined, back to front (in the leftover textures): [1] = (Q/16 + Q/32) / 2; [0] = (Q + [1]) / 2 */
    SDL_SetTextureColorModFloat(p->bloom[4], 0.5f, 0.5f, 0.5f);
    bloom_pass(r, p->bloom[4], &e, p->bloom[1], e.w, e.h);
    SDL_SetTextureColorModFloat(p->bloom[4], 1.0f, 1.0f, 1.0f);
    bloom_add(r, p->bloom[5], &f, &e, 0.5f);
    SDL_SetTextureColorModFloat(p->bloom[3], 0.5f, 0.5f, 0.5f);
    bloom_pass(r, p->bloom[3], &c, p->bloom[0], c.w, c.h);
    SDL_SetTextureColorModFloat(p->bloom[3], 1.0f, 1.0f, 1.0f);
    bloom_add(r, p->bloom[1], &e, &c, 0.5f);
    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    bloom_add(r, p->bloom[0], &c, dst, k_bloom[bright != 0]);
}

/* The whole CRT over dst (the h-line image already drawn in it, from shown/shown_src): lines and grille,
 * the image lightly added back and, in the values with glow, the diffuse glow */
static void crt_apply(Presenter *p, SDL_Renderer *r, SDL_Texture *shown, const SDL_FRect *shown_src,
                      const SDL_FRect *dst, int h, const VideoSettings *v, SDL_Texture **lines, int *lines_key,
                      float mask_x, float mask_y)
{
    if (v->crt <= VIDEO_CRT_OFF || v->crt >= VIDEO_CRT_COUNT)
        return;
    int bright = v->crt_bright != 0;
    draw_crt(r, dst, h, v->crt, bright, lines, lines_key, &p->mask, &p->mask_key, mask_x, mask_y);
    SDL_SetTextureBlendMode(shown, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaModFloat(shown, k_glow[bright] * (v->crt >= VIDEO_CRT_GLOW ? 0.5f : 1.0f));
    SDL_RenderTexture(r, shown, shown_src, dst);
    SDL_SetTextureBlendMode(shown, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaModFloat(shown, 1.0f);
    if (v->crt >= VIDEO_CRT_GLOW)
        draw_bloom(p, r, shown, shown_src, dst, bright);
}

void present_draw(Presenter *p, SDL_Renderer *r, SDL_Texture *frame, int w, int h, int sub, int divider,
                  const VideoSettings *v)
{
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r, &ow, &oh);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (ow <= 0 || oh <= 0 || w <= 0 || h <= 0)
        return;
    VideoRect d;
    int kx, ky;
    video_layout(ow, oh, w, h, divider, v, &d, &kx, &ky);
    SDL_FRect src = { 0, 0, (float)(w * sub), (float)(h * sub) }; /* w x h image, sub times larger (smooth motion) */
    SDL_FRect dst = { (float)d.x, (float)d.y, (float)d.w, (float)d.h };
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaModFloat(frame, 1.0f);
    SDL_Texture *shown = frame; /* what was drawn in dst (and is added back in the CRT) */
    const SDL_FRect *shown_src = &src;
    if (v->smooth || v->integer_scale) {
        SDL_SetTextureScaleMode(frame, v->smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(r, frame, &src, &dst);
    } else {
        if (!p->big || p->big_w != w * kx || p->big_h != h * ky) {
            if (p->big)
                SDL_DestroyTexture(p->big);
            p->big_w = w * kx;
            p->big_h = h * ky;
            p->big = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, p->big_w, p->big_h);
            if (p->big)
                SDL_SetTextureScaleMode(p->big, SDL_SCALEMODE_LINEAR);
        }
        if (!p->big)
            return;
        SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(p->big, SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaModFloat(p->big, 1.0f);
        SDL_SetRenderTarget(r, p->big);
        /* The frame's pixels come with alpha 0: copied as they are, the CRT's add (source times alpha) would see nothing. Only the
         * color is copied; alpha stays the clear's, opaque */
        SDL_RenderClear(r);
        SDL_SetTextureBlendMode(frame, SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO,
                                                                  SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                                                  SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD));
        SDL_RenderTexture(r, frame, &src, NULL);
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(r, NULL);
        SDL_RenderTexture(r, p->big, NULL, &dst);
        shown = p->big;
        shown_src = NULL;
    }
    p->mask_x = dst.x;
    p->mask_y = dst.y;
    crt_apply(p, r, shown, shown_src, &dst, h, v, &p->lines, &p->lines_h, p->mask_x, p->mask_y);
}

void present_canvas_scale(SDL_Renderer *r, int w, int h, int divider, const VideoSettings *v, int *kx, int *ky)
{
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r, &ow, &oh);
    VideoRect d;
    *kx = *ky = REDRAW_SUB;
    if (ow > 0 && oh > 0 && w > 0 && h > 0 && !v->smooth)
        video_layout(ow, oh, w, h, divider, v, &d, kx, ky);
}

void present_draw_canvas(Presenter *p, SDL_Renderer *r, SDL_Texture *canvas, int w, int h, int kx, int ky,
                         int divider, const VideoSettings *v)
{
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r, &ow, &oh);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (ow <= 0 || oh <= 0 || w <= 0 || h <= 0)
        return;
    VideoRect d;
    int lx, ly;
    video_layout(ow, oh, w, h, divider, v, &d, &lx, &ly);
    SDL_FRect src = { 0, 0, (float)(w * kx), (float)(h * ky) };
    SDL_FRect dst = { (float)d.x, (float)d.y, (float)d.w, (float)d.h };
    /* Integer scale: same size; Sharp: the image upscaled by integers, downscaled with a linear filter; Smooth:
     * bilinear from the 4x image */
    SDL_SetTextureScaleMode(canvas, v->integer_scale && !v->smooth ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaModFloat(canvas, 1.0f);
    SDL_RenderTexture(r, canvas, &src, &dst);
    p->mask_x = dst.x;
    p->mask_y = dst.y;
    crt_apply(p, r, canvas, &src, &dst, h, v, &p->lines, &p->lines_h, p->mask_x, p->mask_y);
}

/* CRT on the layer: lines at its scale (h lines), the grille continuing the image's, and the added brightness */
static void overlay_crt(Presenter *p, SDL_Renderer *r, SDL_Texture *shown, const SDL_FRect *shown_src,
                        const SDL_FRect *d, int h, const VideoSettings *v)
{
    crt_apply(p, r, shown, shown_src, d, h, v, &p->over_lines, &p->over_lines_h, p->mask_x, p->mask_y);
}

void present_overlay(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, const VideoRect *dst,
                     const VideoSettings *v)
{
    present_overlay_lines(p, r, tex, w, h, h, dst, v);
}

void present_overlay_lines(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, int lines,
                           const VideoRect *dst, const VideoSettings *v)
{
    if (w <= 0 || h <= 0 || lines <= 0 || dst->w <= 0 || dst->h <= 0)
        return;
    SDL_FRect src = { 0, 0, (float)w, (float)h }, d = { (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h };
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    SDL_Texture *shown = tex; /* what was drawn in d (added back in the CRT) */
    const SDL_FRect *shown_src = &src;
    if (v->smooth || (dst->w % w == 0 && dst->h % h == 0)) {
        SDL_SetTextureScaleMode(tex, v->smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(r, tex, &src, &d);
        overlay_crt(p, r, shown, shown_src, &d, lines, v);
        return;
    }
    /* Sharp: upscale by integers without smoothing until past the size, then downscale with a linear filter */
    int kx = (dst->w + w - 1) / w, ky = (dst->h + h - 1) / h;
    if (!p->over || p->over_w != w * kx || p->over_h != h * ky) {
        if (p->over)
            SDL_DestroyTexture(p->over);
        p->over_w = w * kx;
        p->over_h = h * ky;
        p->over = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, p->over_w, p->over_h);
        if (p->over)
            SDL_SetTextureScaleMode(p->over, SDL_SCALEMODE_LINEAR);
    }
    if (!p->over)
        return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(p->over, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(r, p->over);
    SDL_RenderTexture(r, tex, &src, NULL);
    SDL_SetRenderTarget(r, NULL);
    SDL_RenderTexture(r, p->over, NULL, &d);
    overlay_crt(p, r, p->over, NULL, &d, lines, v);
}

void present_layer(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, const VideoRect *dst,
                   const VideoSettings *v)
{
    present_layer_lines(p, r, tex, w, h, h, dst, v);
}

void present_layer_lines(Presenter *p, SDL_Renderer *r, SDL_Texture *tex, int w, int h, int lines,
                         const VideoRect *dst, const VideoSettings *v)
{
    if (w <= 0 || h <= 0 || dst->w <= 0 || dst->h <= 0)
        return;
    /* Assembled separately, at dst's size and with transparency kept: the CRT (SDL's MOD and ADD keep the
     * destination alpha) applies only to what the layer draws; then it goes on top with blending */
    if (!p->lay || p->lay_w != dst->w || p->lay_h != dst->h) {
        if (p->lay)
            SDL_DestroyTexture(p->lay);
        p->lay_w = dst->w;
        p->lay_h = dst->h;
        p->lay = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, dst->w, dst->h);
        if (p->lay)
            SDL_SetTextureScaleMode(p->lay, SDL_SCALEMODE_NEAREST);
    }
    if (!p->lay)
        return;
    SDL_FRect src = { 0, 0, (float)w, (float)h }, d = { 0, 0, (float)dst->w, (float)dst->h };
    SDL_Texture *shown = tex;
    const SDL_FRect *shown_src = &src;
    SDL_SetRenderTarget(r, p->lay);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    if (v->smooth || (dst->w % w == 0 && dst->h % h == 0)) {
        SDL_SetTextureScaleMode(tex, v->smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(r, tex, &src, &d);
    } else {
        /* Sharp: upscale by integers without smoothing until past the size, then downscale with a linear filter */
        int kx = (dst->w + w - 1) / w, ky = (dst->h + h - 1) / h;
        if (!p->lay_big || p->lay_big_w != w * kx || p->lay_big_h != h * ky) {
            if (p->lay_big)
                SDL_DestroyTexture(p->lay_big);
            p->lay_big_w = w * kx;
            p->lay_big_h = h * ky;
            p->lay_big = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, p->lay_big_w,
                                           p->lay_big_h);
            if (p->lay_big)
                SDL_SetTextureScaleMode(p->lay_big, SDL_SCALEMODE_LINEAR);
        }
        if (!p->lay_big) {
            SDL_SetRenderTarget(r, NULL);
            return;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(p->lay_big, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(r, p->lay_big);
        SDL_RenderTexture(r, tex, &src, NULL);
        SDL_SetRenderTarget(r, p->lay);
        SDL_RenderTexture(r, p->lay_big, NULL, &d);
        shown = p->lay_big;
        shown_src = NULL;
    }
    crt_apply(p, r, shown, shown_src, &d, lines, v, &p->lay_lines, &p->lay_lines_h, p->mask_x - (float)dst->x,
              p->mask_y - (float)dst->y);
    SDL_SetRenderTarget(r, NULL);
    SDL_SetTextureBlendMode(p->lay, SDL_BLENDMODE_BLEND);
    SDL_FRect out = { (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h };
    SDL_RenderTexture(r, p->lay, NULL, &out);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
}

void present_free(Presenter *p)
{
    if (p->lay)
        SDL_DestroyTexture(p->lay);
    if (p->lay_big)
        SDL_DestroyTexture(p->lay_big);
    if (p->lay_lines)
        SDL_DestroyTexture(p->lay_lines);
    if (p->over)
        SDL_DestroyTexture(p->over);
    if (p->over_lines)
        SDL_DestroyTexture(p->over_lines);
    if (p->big)
        SDL_DestroyTexture(p->big);
    if (p->lines)
        SDL_DestroyTexture(p->lines);
    if (p->mask)
        SDL_DestroyTexture(p->mask);
    for (int i = 0; i < PRESENT_BLOOM_TEX; i++)
        if (p->bloom[i])
            SDL_DestroyTexture(p->bloom[i]);
    SDL_zerop(p);
}
