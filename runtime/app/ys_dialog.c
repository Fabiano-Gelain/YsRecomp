/* Instant dialog: see ys_dialog.h */
#include "app/ys_dialog.h"

#include "cd/cdrom.h"

#define SPEED 0x27F6 /* speed of the text task, index X */

/* From the accumulator's LDA $27F6,X to the JSR $402A (wait for the next frame); the speed test is byte 10:
 * LDA $27F6,X; CLC; ADC $2826,X; STA $2826,X; LDA $27F6,X; BEQ +$1D; LDA $2826,X; SEC; SBC $92; BCS +$12; JSR $402A */
static const uint8_t k_sig[26] = { 0xBD, 0xF6, 0x27, 0x18, 0x7D, 0x26, 0x28, 0x9D, 0x26, 0x28, 0xBD, 0xF6, 0x27,
                                   0xF0, 0x1D, 0xBD, 0x26, 0x28, 0x38, 0xE5, 0x92, 0xB0, 0x12, 0x20, 0x2A, 0x40 };
#define SIG_TEST 10

#define SIG_WAIT 23 /* the JSR $402A: the text waits for the next frame for the next letter */

/* test = address of the speed test in one of the three disc programs that have the text program (bank $80
 * in window 2), checked against the bytes */
static int sig_ok(const Pce *pce, uint16_t test)
{
    if ((test != 0x58D6 && test != 0x5BD0 && test != 0x5BD5) || pce->cpu.mpr[2] != 0x80 || !pce->cd)
        return 0;
    for (int k = 0; k < (int)sizeof(k_sig); k++)
        if (pce->cd->ram[(test - SIG_TEST + k) & 0x1FFF] != k_sig[k])
            return 0;
    return 1;
}

int ys_dialog_at(const Pce *pce)
{
    return sig_ok(pce, pce->cpu.pc);
}

int ys_dialog_wait_at(const Pce *pce)
{
    return sig_ok(pce, (uint16_t)(pce->cpu.pc - (SIG_WAIT - SIG_TEST)));
}

void ys_dialog_patch(Pce *pce)
{
    if (!ys_dialog_at(pce))
        return;
    uint32_t addr = 0xF8u * 0x2000 + ((SPEED + pce->cpu.x) & 0x1FFF);
    if (pce_bus_read(pce, addr) != 0)
        pce_bus_write(pce, addr, 0);
}

void ys_dialog_set(Pce *pce, int on)
{
    pce->game_patch = on ? ys_dialog_patch : NULL;
}
