/* Ys fades read from the game: see ys_fade.h and docs/ys_notes.md ("Game fades"). */
#include "app/ys_fade.h"

#include <string.h>

#include "cd/cdrom.h"

#define RAM_COLORS 0x3C00 /* shown colors in RAM: $3C00-$3DFF background, $3E00-$3FFF sprites */

static uint8_t read_logical(const Pce *pce, uint16_t addr)
{
    uint8_t bank = pce->cpu.mpr[addr >> 13];
    if (bank >= 0x80 && bank <= 0x87 && pce->cd)
        return pce->cd->ram[(bank - 0x80) * 0x2000 + (addr & 0x1FFF)];
    if (bank == 0xF8)
        return pce->ram[addr & 0x1FFF];
    return pce_bus_read((Pce *)pce, (uint32_t)bank * 0x2000 + (addr & 0x1FFF));
}

static uint16_t ram_word(const Pce *pce, uint16_t addr) /* logical address $2000-$3FFF */
{
    return (uint16_t)(pce->ram[addr & 0x1FFF] | pce->ram[(addr + 1) & 0x1FFF] << 8);
}

static uint16_t ram_color(const Pce *pce, uint16_t addr)
{
    return (uint16_t)(ram_word(pce, addr) & 0x1FF);
}

/* Code bytes at pc: sig with 0xFFFF = any byte */
static int code_is(const Pce *pce, uint16_t pc, const uint16_t *sig, int n)
{
    for (int k = 0; k < n; k++)
        if (sig[k] != 0xFFFF && read_logical(pce, (uint16_t)(pc + k)) != sig[k])
            return 0;
    return 1;
}

/* Start of the fade routine (both families):
 *   STX zp_lo; STA zp_lo+1; TXA; AND #7; ASL; ASL; ASL; ORA level; TAX; LDA table,X
 * then, in the programs' one: STA $88 (0x85); in the scenes' one: ADC brightening (0x65 zp) */
static const uint16_t k_transform[] = { 0x86, 0xFFFF, 0x85, 0xFFFF, 0x8A, 0x29, 0x07, 0x0A, 0x0A, 0x0A,
                                        0x05, 0xFFFF, 0xAA, 0xBD, 0xFFFF, 0xFFFF };
#define TRANSFORM_LEN 16

/* Flash loop (end-of-Ys-I area): LDA (zp); TAX; AND #7; TAY; LDA table,Y; STA $00; TXA; AND #$38; CMP #$38;
 * BEQ +2; ADC #8 */
static const uint16_t k_flash[] = { 0xB2, 0xFFFF, 0xAA, 0x29, 0x07, 0xA8, 0xB9, 0xFFFF, 0xFFFF, 0x85, 0x00,
                                    0x8A, 0x29, 0x38, 0xC9, 0x38, 0xF0, 0x02, 0x69, 0x08 };
#define FLASH_LEN 20

void ys_fade_init(YsFade *f)
{
    memset(f, 0, sizeof(*f));
}

uint16_t ys_fade_color(const uint8_t table[64], uint16_t src, int level, int add)
{
    uint16_t out = 0;
    for (int sh = 0; sh < 9; sh += 3) {
        int c = (src >> sh) & 7;
        int v = level >= YS_FADE_FULL ? c : table[c * 8 + (level < 0 ? 0 : level)];
        v += add;
        out |= (uint16_t)((v > 7 ? 7 : v) << sh);
    }
    return out;
}

int ys_fade_channel(const uint8_t table[64], int ch, int level_q, int add_q, int q)
{
    if (level_q < 0)
        level_q = 0;
    if (level_q > 7 * q)
        level_q = 7 * q;
    int l0 = level_q / q, frac = level_q % q;
    int v0 = l0 >= 7 ? ch : table[ch * 8 + l0];
    int v1 = l0 >= 7 ? v0 : l0 + 1 >= 7 ? ch : table[ch * 8 + l0 + 1];
    int v = v0 * q + (v1 - v0) * frac + add_q;
    return v > 7 * q ? 7 * q : v < 0 ? 0 : v;
}

/* The stored color is still the one the calculation gives (the game did not change it some other way) */
static int color_ok(const YsFade *f, const YsFadeColor *c, uint16_t shown)
{
    if (!c->on || (c->level < YS_FADE_FULL && !f->table_ok))
        return 0;
    return ys_fade_color(f->table, c->src, c->level, c->add) == shown;
}

/* Entry of the fade routine: the color in X/A at the current level and brightening goes to the caller's ($zp),Y-1 */
static void on_transform(YsFade *f, Pce *pce, uint16_t pc)
{
    f->transforms++;
    uint8_t level = pce->ram[read_logical(pce, (uint16_t)(pc + 11))];
    uint16_t table = (uint16_t)(read_logical(pce, (uint16_t)(pc + 14)) | read_logical(pce, (uint16_t)(pc + 15)) << 8);
    int add = 0;
    if (read_logical(pce, (uint16_t)(pc + 16)) == 0x65) /* scenes family: ADC brightening */
        add = pce->ram[read_logical(pce, (uint16_t)(pc + 17))];
    if (!f->table_ok) {
        for (int i = 0; i < 64; i++)
            f->table[i] = read_logical(pce, (uint16_t)(table + i));
        f->table_ok = 1;
    }
    /* The caller: the JSR left on the stack the address of its last byte; after it comes (LDA zp;) DEY; STA (dest),Y */
    uint8_t s = pce->cpu.s;
    uint16_t ret = (uint16_t)((pce->ram[0x100 + (uint8_t)(s + 1)] | pce->ram[0x100 + (uint8_t)(s + 2)] << 8) + 1);
    if (read_logical(pce, ret) == 0xA5)
        ret = (uint16_t)(ret + 2);
    if (read_logical(pce, ret) != 0x88 || read_logical(pce, (uint16_t)(ret + 1)) != 0x91) {
        f->unknown_dest++;
        return;
    }
    uint8_t zp = read_logical(pce, (uint16_t)(ret + 2));
    uint16_t dest = (uint16_t)(ram_word(pce, (uint16_t)(0x2000 + zp)) + pce->cpu.y - 1);
    if (dest < RAM_COLORS || dest >= RAM_COLORS + 0x400 || (dest & 1))
        return;
    YsFadeColor *c = &f->ram[(dest - RAM_COLORS) / 2];
    c->src = (uint16_t)((pce->cpu.x | pce->cpu.a << 8) & 0x1FF);
    c->level = level > YS_FADE_FULL ? YS_FADE_FULL : level;
    c->add = (uint8_t)(add > 7 ? 7 : add);
    c->on = 1;
    c->when = f->frames;
}

/* Flash loop: the color at ($zp) gains 1 level in each channel */
static void on_flash(YsFade *f, Pce *pce, uint16_t pc)
{
    f->flashes++;
    uint8_t zp = read_logical(pce, (uint16_t)(pc + 1));
    uint16_t dest = ram_word(pce, (uint16_t)(0x2000 + zp));
    if (dest < RAM_COLORS || dest >= RAM_COLORS + 0x400 || (dest & 1))
        return;
    YsFadeColor *c = &f->ram[(dest - RAM_COLORS) / 2];
    uint16_t cur = ram_color(pce, dest);
    if (color_ok(f, c, cur)) {
        if (c->add < 7)
            c->add++;
    } else {
        c->src = cur;
        c->level = YS_FADE_FULL;
        c->add = 1;
        c->on = 1;
    }
    c->when = f->frames;
}

/* The request was served: the requested colors went from RAM to the VCE */
static void on_upload(YsFade *f, Pce *pce, uint8_t req)
{
    if (req & 1)
        return; /* read request (from the VCE to RAM) */
    f->uploads++;
    for (int part = 0; part < 2; part++) {
        uint16_t ptr = ram_word(pce, (uint16_t)(0x2220 + 3 * part));
        int n = pce->ram[0x222 + 3 * part] * 16;
        for (int i = 0; i < n && i < 256; i++) {
            uint16_t a = (uint16_t)(ptr + 2 * i);
            YsFadeColor *v = &f->vce[part * 256 + i];
            if (a >= RAM_COLORS && a < RAM_COLORS + 0x400 && !(a & 1)) {
                const YsFadeColor *r = &f->ram[(a - RAM_COLORS) / 2];
                *v = *r;
                v->on = (uint8_t)color_ok(f, r, ram_color(pce, a));
            } else {
                v->on = 0;
            }
        }
    }
}

void ys_fade_hook(YsFade *f, Pce *pce)
{
    /* The video turned on by the game (VDC's CR: background, sprites) while the image is drawn */
    int row = pce->vce.vpos - VCE_ACTIVE_FIRST_LINE;
    if (row >= 0 && row < VCE_ACTIVE_LINES) {
        f->display_on |= (uint8_t)(pce->vdc.reg[5] & 0xC0);
        f->display_seen = 1;
    }
    /* The copy to the VCE is seen through the request ($221F) cleared at its end, and not through the ex_colorcmd entry: with
     * VBlank handled by the BIOS ($F5 without bit 1), the interrupt routine does the copy internally, without going through $E0E4 */
    uint8_t req = pce->ram[0x21F];
    if (f->req && !req)
        on_upload(f, pce, f->req);
    f->req = req;
    uint16_t pc = pce->cpu.pc;
    uint8_t bank = pce->cpu.mpr[pc >> 13];
    if (bank >= 0x80 && bank <= 0x87) {
        uint8_t op = read_logical(pce, pc);
        if (op == 0x86) {
            if (code_is(pce, pc, k_transform, TRANSFORM_LEN))
                on_transform(f, pce, pc);
        } else if (op == 0xB2) {
            if (code_is(pce, pc, k_flash, FLASH_LEN))
                on_flash(f, pce, pc);
        }
    }
}

void ys_fade_frame(YsFade *f, const Pce *pce, YsFadeFrame *out)
{
    memcpy(out->color, pce->vce.color, sizeof(out->color));
    memcpy(out->table, f->table, sizeof(out->table));
    out->table_ok = f->table_ok;
    int lmin = YS_FADE_FULL, amax = 0, n = 0;
    uint32_t last = 0;
    f->frames++;
    for (int i = 0; i < 512; i++) {
        YsFadeColor *v = &f->vce[i];
        if (v->on && !color_ok(f, v, (uint16_t)(pce->vce.color[i] & 0x1FF)))
            v->on = 0; /* the VCE has another color (written some other way) */
        const YsFadeColor *p = &f->prev[i];
        int dl = v->level - p->level, da = v->add - p->add;
        int step = v->on && p->on && v->src == p->src && (dl < 0 ? -dl : dl) + (da < 0 ? -da : da) == 1;
        out->step[i] = (uint8_t)step;
        out->c[i] = *v;
        f->prev[i] = *v;
        if (i < 256 && v->on && v->src && v->when > last)
            last = v->when;
    }
    /* the game's last step in the background colors */
    for (int i = 0; i < 256; i++) {
        const YsFadeColor *v = &out->c[i];
        if (v->on && v->src && v->when == last) {
            n++;
            lmin = v->level < lmin ? v->level : lmin;
            amax = v->add > amax ? v->add : amax;
        }
    }
    /* The whole screen black without the fade routine (when starting a program, before the 1st fade), as at the end of a fade:
     * level 0. By the game: the video off (background and sprites) during the whole image, or, with no fade colors, the VCE's 256
     * background colors black (the frame and the panel are background: the HUD strip, which stands in for the panel, goes
     * black with it; the sprite ones stay from old scenes, e.g. the beginning of Ys I with the BIOS) */
    int lit = 0;
    for (int i = 0; i < 256 && !lit && !n; i++)
        lit = (pce->vce.color[i] & 0x1FF) != 0;
    if ((f->display_seen && !f->display_on) || (!n && !lit)) {
        lmin = 0;
        amax = 0;
    }
    /* Colors lit outside the routine in the palettes of the last step (the GAME OVER text, after the fade to black) */
    uint8_t in_fade[16] = { 0 };
    int stray = 0;
    for (int i = 0; i < 256 && n; i++)
        if (out->c[i].on && out->c[i].src && out->c[i].when == last)
            in_fade[i >> 4] = 1;
    for (int i = 0; i < 256 && n; i++)
        stray += in_fade[i >> 4] && !out->c[i].on && (pce->vce.color[i] & 0x1FF) != 0;
    f->display_on = 0;
    f->display_seen = 0;
    out->map_level = lmin;
    out->map_add = amax;
    out->map_colors = n;
    out->map_stray = stray;
}
