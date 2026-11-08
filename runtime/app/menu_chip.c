/* Chip music while the options menu is open. See menu_chip.h. */
#include "app/menu_chip.h"

#include <string.h>

#define MENU_CHIP_TICK 0xE6E8
#define MENU_CHIP_RET 0x3FFE /* return of the step (mirrored RAM the game does not execute): the copy stops here */

static void chip_tick(Pce *s)
{
    HuC6280 *c = &s->cpu;
    uint8_t p = c->p;
    hle_push(s, (uint8_t)((MENU_CHIP_RET - 1) >> 8));
    hle_push(s, (uint8_t)((MENU_CHIP_RET - 1) & 0xFF));
    c->p |= HU_FLAG_I; /* no interrupts: only the driver runs */
    c->pc = MENU_CHIP_TICK;
    for (int i = 0; i < 200000 && c->pc != MENU_CHIP_RET; i++)
        pce_step(s);
    c->p = p;
}

void menu_chip_play(MenuChip *m, const Pce *src, int16_t *buf, int n)
{
    if (!m->on) {
        if (pce_clone(&m->clone, src) != 0)
            return;
        if (src->hle) {
            m->hle = *src->hle;
            m->clone.hle = &m->hle;
        }
        m->clone.recomp_lookup = NULL; /* only the BIOS runs in the copy */
        m->clone.interp_hist = NULL;
        m->clone.psg.sfx_ctx = &m->clone;
        m->frac = 0.0;
        m->pieces = 0;
        m->samples = n;
        m->on = 1;
    }
    m->pieces++;
    Pce *s = &m->clone;
    s->mix = src->mix;
    psg_set_filter(&s->psg, s->mix.on && s->mix.chip_filter);
    m->frac += n * 21477270.0 / 44100.0;
    uint64_t end = s->master_cycles + (uint64_t)m->frac;
    m->frac -= (double)(uint64_t)m->frac;
    while (s->master_cycles < end) {
        uint64_t left = end - s->master_cycles;
        pce_advance_hardware(s, left > 256 ? 256 : left);
        int by_vblank = s->ram[0xE7] == 1;
        if (s->cpu.irq_request & HU_TIQ) {
            s->cpu.irq_request &= (uint8_t)~HU_TIQ; /* acknowledges the timer ($1403) */
            if (!by_vblank)
                chip_tick(s);
        }
        if (s->frame_ready) {
            s->frame_ready = 0;
            if (by_vblank)
                chip_tick(s);
        }
    }
    static int16_t chip[PSG_BUFFER_SIZE];
    int got = s->mix.on ? psg_end_frame_split(&s->psg, chip, s->mix.psg_music, s->mix.psg_sfx)
                        : psg_end_frame(&s->psg, chip);
    if (buf)
        for (int i = 0; i < n * 2; i++)
            buf[i] = got >= 2 ? chip[i < got ? i : got - 2 + (i & 1)] : 0; /* ±1 sample: repeats the last one */
}

long menu_chip_finish(MenuChip *m, Pce *dst)
{
    if (!m->on)
        return 0;
    m->on = 0;
    Pce *s = &m->clone;
    /* RAM: what the driver changed. The stack ($2100-$21FF) stays as it was: the copy only left the step returns there. */
    for (int i = 0; i < (int)sizeof(dst->ram); i++)
        if ((i < 0x100 || i > 0x1FF) && s->ram[i] != dst->ram[i])
            dst->ram[i] = s->ram[i];
    /* PSG: the copy's whole PSG (registers, channels, sampling clock and filters: the sound continues from it, without
     * the rest of the sound from before the menu), except the samples of the console's frame in progress and the pointers. */
    static Psg keep;
    keep = dst->psg;
    dst->psg = s->psg;
    Psg *p = &dst->psg;
    for (int c = 0; c < 6; c++)
        memcpy(p->ch[c].output, keep.ch[c].output, sizeof(p->ch[c].output));
    p->buffer_index = keep.buffer_index;
    memcpy(p->sfx_mask, keep.sfx_mask, sizeof(p->sfx_mask));
    p->extra_clock = keep.extra_clock;
    p->extra_sample = keep.extra_sample;
    p->extra_ctx = keep.extra_ctx;
    p->sfx_channels = keep.sfx_channels;
    p->sfx_ctx = keep.sfx_ctx;
    pce_free_clone(s);
    return m->pieces;
}

void menu_chip_replay(Pce *pce, long pieces, int n)
{
    static MenuChip m;
    for (long i = 0; i < pieces; i++)
        menu_chip_play(&m, pce, NULL, n);
    menu_chip_finish(&m, pce);
}
