/* Ys's mid-image scroll changes, read from the game: see ys_scroll.h */
#include "app/ys_scroll.h"

#include <string.h>

#include "pce/vce.h"

#define RAM(p, a) ((p)->ram[(a) - 0x2000])

enum { HOOK_MAP, HOOK_WRAP, HOOK_LOW };

static const struct {
    uint16_t pc;
    uint8_t sig[3]; /* JSR of the change */
} k_hooks[] = {
    { 0x4AE1, { 0x20, 0x5B, 0x4A } },
    { 0x4B05, { 0x20, 0x88, 0x4A } },
    { 0x4AD8, { 0x20, 0x75, 0x4A } },
};

static uint8_t read_logical(Pce *pce, uint16_t addr)
{
    return pce_bus_read(pce, (uint32_t)pce->cpu.mpr[addr >> 13] * 0x2000 + (addr & 0x1FFF));
}

static int which_hook(Pce *pce)
{
    uint16_t pc = pce->cpu.pc;
    if (pc < 0x4AD8 || pc > 0x4B05 || pce->cpu.mpr[2] != 0x80)
        return -1;
    for (int h = 0; h < 3; h++) {
        if (k_hooks[h].pc != pc)
            continue;
        for (int k = 0; k < 3; k++)
            if (read_logical(pce, (uint16_t)(pc + k)) != k_hooks[h].sig[k])
                return -1;
        return h;
    }
    return -1;
}

static void clear(YsScrollFrame *f)
{
    memset(f, 0, sizeof(*f));
    f->row_map = f->row_wrap = f->row_low = -1;
}

void ys_scroll_init(YsScroll *s)
{
    clear(&s->cur);
}

/* Image line the console is drawing (-1 outside it) */
static int image_row(Pce *pce)
{
    int r = pce->vce.vpos - VCE_ACTIVE_FIRST_LINE;
    return r >= 0 && r < VCE_ACTIVE_LINES ? r : -1;
}

void ys_scroll_hook(YsScroll *s, Pce *pce)
{
    int h = which_hook(pce);
    if (h < 0)
        return;
    YsScrollFrame *f = &s->cur;
    int row = image_row(pce);
    switch (h) {
    case HOOK_MAP:
        clear(f);
        f->map_x = (uint16_t)(RAM(pce, 0x2C2F) | RAM(pce, 0x2C30) << 8);
        f->map_y = RAM(pce, 0x2C31);
        f->cr = RAM(pce, 0x20F3);
        f->row_map = (int16_t)row;
        break;
    case HOOK_WRAP:
        if (f->row_map < 0)
            break;
        f->wrap = 1;
        f->wrap_x = (RAM(pce, 0x2082) & 0x40) ? 0 : (uint16_t)(RAM(pce, 0x2C2F) | RAM(pce, 0x2C30) << 8);
        f->row_wrap = (int16_t)row;
        break;
    case HOOK_LOW:
        if (f->row_map < 0 || f->row_low >= 0)
            break;
        f->low_x = (uint16_t)(RAM(pce, 0x220E) | RAM(pce, 0x220F) << 8);
        f->low_byr = (uint16_t)(RAM(pce, 0x2212) | RAM(pce, 0x2213) << 8);
        f->row_low = (int16_t)row;
        f->valid = 1;
        break;
    }
}

void ys_scroll_frame(YsScroll *s, YsScrollFrame *out)
{
    *out = s->cur;
    clear(&s->cur);
}
