/* Framed screen read from the game: see ys_screen.h */
#include "app/ys_screen.h"

#include "app/intro_skip.h"

#include "app/crop.h"

#define LOADER_PC 0x40E7 /* program loader of the resident file (bank $80 in window 2) */

static const uint8_t k_loader_sig[4] = { 0xDA, 0x20, 0x65, 0x41 }; /* PHX; JSR $4165 */
/* The 1st program (the one the boot already put at $4800) is called directly, without the loader; also after a reset */
#define BOOT_PC 0x40DC
static const uint8_t k_boot_sig[3] = { 0x20, 0x00, 0x48 }; /* JSR $4800 */

void ys_screen_init(YsScreen *s)
{
    s->program = -1;
    s->loads = 0;
}

static uint8_t read_logical(Pce *pce, uint16_t addr)
{
    return pce_bus_read(pce, (uint32_t)pce->cpu.mpr[addr >> 13] * 0x2000 + (addr & 0x1FFF));
}

void ys_screen_hook(YsScreen *s, Pce *pce)
{
    if ((pce->cpu.pc != LOADER_PC && pce->cpu.pc != BOOT_PC) || pce->cpu.mpr[2] != 0x80)
        return;
    if (pce->cpu.pc == BOOT_PC) {
        for (int k = 0; k < 3; k++)
            if (read_logical(pce, (uint16_t)(BOOT_PC + k)) != k_boot_sig[k])
                return;
        s->program = -1;
        return;
    }
    for (int k = 0; k < 4; k++)
        if (read_logical(pce, (uint16_t)(LOADER_PC + k)) != k_loader_sig[k])
            return;
    s->program = pce->cpu.a;
    s->loads++;
}

int ys_screen_in_opening(int program, long frame)
{
    return program == 0x04 || (program == -1 && frame < INTRO_MAX_FRAMES);
}

int ys_screen_layout(int program)
{
    switch (program) {
    case 0x08: /* Ys I gameplay */
    case 0x30: /* Ys II gameplay */
        return CROP_SCREEN_GAME;
    case 0x01: /* load menu */
        return CROP_SCREEN_LOAD;
    default:
        return CROP_SCREEN_NONE;
    }
}
