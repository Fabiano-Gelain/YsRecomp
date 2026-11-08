/* Ys panel read from VRAM: see ys_panel.h */
#include "app/ys_panel.h"

#include <string.h>

#include "app/ys_scroll.h"
#include "pce/pce.h"

/* The inner color of a full bar tile (506, 507), in palette p: as it comes out in the image */
static uint32_t bar_tile_rgb(const struct Pce *pce, int pal, int tile)
{
    const uint16_t *t = &pce->vdc.vram[(tile * 16) & (VDC_VRAM_SIZE - 1)];
    uint16_t a = t[4], b = t[4 + 8];
    int c = (a >> 7 & 1) | (a >> 15 & 1) << 1 | (b >> 7 & 1) << 2 | (b >> 15 & 1) << 3;
    return vce_rgb(pce->vce.color[(pal << 4 | c) & 0x1FF]);
}

void ys_panel_read(const struct Pce *pce, YsPanel *p)
{
    memset(p, 0, sizeof(*p));
    const uint8_t *ram = pce->ram; /* $2000- */
    p->next_level = (uint32_t)ram[0x2C4A - 0x2000] | (uint32_t)ram[0x2C4B - 0x2000] << 8 | (uint32_t)ram[0x2C4C - 0x2000] << 16;
    p->hp_max = ram[0x2C48 - 0x2000];
    int bxr = (ram[0x020E] | ram[0x020F] << 8) & 0x3FF, byr = (ram[0x0212] | ram[0x0213] << 8) & 0x1FF;
    int mwr = pce->vdc.reg[9];
    static const int k_bat_w[4] = { 32, 64, 128, 128 };
    int bw = k_bat_w[(mwr >> 4) & 3], bh = mwr & 0x40 ? 64 : 32;
    /* the map line shown on the panel's 1st line (the lower frame starts at YS_SCROLL_LOW_ROW) */
    int line = byr + 1 + (YS_PANEL_Y - YS_SCROLL_LOW_ROW);
    if ((line & 7) || (bxr & 7))
        return;
    for (int r = 0; r < YS_PANEL_ROWS; r++)
        for (int c = 0; c < YS_PANEL_COLS; c++) {
            uint16_t e = pce->vdc.vram[((line / 8 + r) % bh) * bw + (bxr / 8 + c) % bw];
            p->cell[r][c] = e;
            const uint16_t *t = &pce->vdc.vram[(YS_TILE(e) * 16) & (VDC_VRAM_SIZE - 1)];
            for (int y = 0; y < 8; y++) {
                uint16_t a = t[y], b = t[y + 8];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    p->pix[r * 8 + y][c * 8 + x] = (uint8_t)((a >> bit & 1) | (a >> (bit + 8) & 1) << 1 |
                                                             (b >> bit & 1) << 2 | (b >> (bit + 8) & 1) << 3);
                }
            }
        }
    int bar_pal = YS_PAL(p->cell[2][YS_BAR_FIRST_COL]);
    p->bar_yellow = bar_tile_rgb(pce, bar_pal, YS_TILE_BAR_YELLOW);
    p->bar_red = bar_tile_rgb(pce, bar_pal, YS_TILE_BAR_RED);
    p->valid = 1;
}

int ys_panel_lit(const YsPanel *p, int x, int y)
{
    x -= YS_PANEL_X;
    y -= YS_PANEL_Y;
    return p && p->valid && x >= 0 && y >= 0 && x < YS_PANEL_W && y < YS_PANEL_H && p->pix[y][x];
}
