/* Intro screen and RUN accepted, read from the intro program: see ys_intro.h */
#include "app/ys_intro.h"

#include <string.h>

#define INTRO_PROGRAM 0x04

/* Script loop: LDA ($62); CMP #$FF, with the pointer to the next operation at $62/$63 (bank $80 in window 2) */
#define SCRIPT_PC 0x5AA4
static const uint8_t k_script_sig[4] = { 0xB2, 0x62, 0xC9, 0xFF };
/* Main menu routine: LDA #$08; STA $2EBF (only RUN counts) */
#define MENU_PC 0x494C
static const uint8_t k_menu_sig[5] = { 0xA9, 0x08, 0x8D, 0xBF, 0x2E };

/* The script operation that starts each screen: the fade that lights up the image */
static const struct {
    uint16_t addr;
    IntroScreen screen;
    uint8_t op[9];
} k_screens[] = {
    { 0x4C4A, INTRO_SCREEN_NEC, { 0x18, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00, 0x02, 0x00 } },
    { 0x4C6D, INTRO_SCREEN_LOGO, { 0x18, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00 } },
    { 0x4D3D, INTRO_SCREEN_COPYRIGHT, { 0x18, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00, 0xFF, 0x00 } },
};

/* Game variables in RAM ($2000 = ram[0]) */
#define RAM_LOCK 0x003A /* frames without accepting buttons */
#define RAM_FLAGS 0x0024 /* bit 0: button accepted, the game in a hurry */
#define RAM_MASK 0x0EBF /* $2EBF: buttons that count */

void ys_intro_init(YsIntro *s)
{
    s->screen = INTRO_SCREEN_LOADING;
}

static int code_is(Pce *pce, uint16_t addr, const uint8_t *bytes, int n)
{
    for (int k = 0; k < n; k++) {
        uint16_t a = (uint16_t)(addr + k);
        if (pce_bus_read(pce, (uint32_t)pce->cpu.mpr[a >> 13] * 0x2000 + (a & 0x1FFF)) != bytes[k])
            return 0;
    }
    return 1;
}

void ys_intro_hook(YsIntro *s, Pce *pce, int program)
{
    uint16_t pc = pce->cpu.pc;
    if ((pc != SCRIPT_PC && pc != MENU_PC) || program != INTRO_PROGRAM || pce->cpu.mpr[2] != 0x80)
        return;
    if (pc == MENU_PC) {
        if (code_is(pce, MENU_PC, k_menu_sig, sizeof(k_menu_sig)))
            s->screen = INTRO_SCREEN_MENU;
        return;
    }
    if (!code_is(pce, SCRIPT_PC, k_script_sig, sizeof(k_script_sig)))
        return;
    uint16_t op = (uint16_t)(pce->ram[0x62] | pce->ram[0x63] << 8);
    for (size_t i = 0; i < sizeof(k_screens) / sizeof(k_screens[0]); i++)
        if (op == k_screens[i].addr && k_screens[i].screen > s->screen &&
            code_is(pce, op, k_screens[i].op, sizeof(k_screens[i].op)))
            s->screen = k_screens[i].screen;
}

int ys_intro_run_accepted(const Pce *pce, int program)
{
    return program == INTRO_PROGRAM && (pce->ram[RAM_MASK] & PCE_PAD_RUN) && pce->ram[RAM_LOCK] == 0 &&
           !(pce->ram[RAM_FLAGS] & 1);
}
