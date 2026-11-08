/* Ys engine read by the PC: see ys_engine.h and docs/ys_notes.md ("Engine memory map"). */
#include "app/ys_engine.h"

#include <string.h>

YsEngine *g_ys_engine;

/* Console RAM ($2000-$3FFF) */
#define RAM(p, a) ((p)->ram[(a) - 0x2000])

/* Engine routines (bank $80 of CD RAM in window $4000-$5FFF) and the first bytes of each */
static const struct {
    uint16_t pc;
    uint8_t sig[5];
} k_hooks[] = {
    { 0x4D58, { 0xA9, 0x01, 0x4D, 0x15, 0x22 } }, /* start of the build: LDA #1 / EOR $2215 */
    { 0x4DAC, { 0xBD, 0x38, 0x2F, 0x89, 0x08 } }, /* a character (X): LDA $2F38,X / BIT #8 */
    { 0x4E60, { 0x8C, 0x02, 0x00, 0x8D, 0x03 } }, /* a piece (line already written): STY $0002 / STA $0003 */
    { 0x4DAA, { 0x58, 0x60, 0xBD, 0x38, 0x2F } }, /* end (after ex_sprdma): CLI / RTS */
};
enum { HOOK_START, HOOK_ACTOR, HOOK_PIECE, HOOK_END };

static uint8_t read_logical(Pce *pce, uint16_t addr)
{
    return pce_bus_read(pce, (uint32_t)pce->cpu.mpr[addr >> 13] * 0x2000 + (addr & 0x1FFF));
}

/* Ys I mine light: $86:7108 (window $6000) LDX $7673 / LDA $2F78,Y / STA $2F78,X ... (copies the position of
 * character Y to the light's) */
static const uint8_t k_copy_sig[6] = { 0xAE, 0x73, 0x76, 0xB9, 0x78, 0x2F };

static void copy_hook(YsEngine *e, Pce *pce)
{
    if (pce->cpu.pc != 0x7108 || pce->cpu.mpr[3] != 0x86)
        return;
    for (int k = 0; k < 6; k++)
        if (read_logical(pce, (uint16_t)(0x7108 + k)) != k_copy_sig[k])
            return;
    int x = read_logical(pce, 0x7673), y = pce->cpu.y;
    if (x < YS_ACTORS && y < YS_ACTORS && x != y) {
        e->copy_of[x] = (uint8_t)y;
        e->copies++;
    }
}

/* Adol is born: the routine that creates the hero in the gameplay program ($75A5 in Ys I, $75A7 in Ys II, window $6000): with
 * the new number in A, STA $76 / TAY / CLA / STA $2EF8,Y. $75 and $76 are only valid for the gameplay program: other programs
 * use them as scratch, which is why the number is noted here and not read from them later */
static const uint8_t k_hero_sig[7] = { 0x85, 0x76, 0xA8, 0x62, 0x99, 0xF8, 0x2E };

static void hero_hook(YsEngine *e, Pce *pce)
{
    uint16_t pc = pce->cpu.pc;
    if (pc != 0x75A5 && pc != 0x75A7)
        return;
    for (int k = 0; k < 7; k++)
        if (read_logical(pce, (uint16_t)(pc + k)) != k_hero_sig[k])
            return;
    e->hero = pce->cpu.a < YS_ACTORS ? pce->cpu.a : YS_NO_ACTOR;
}

static int which_hook(Pce *pce)
{
    uint16_t pc = pce->cpu.pc;
    if (pc < 0x4000 || pc >= 0x6000 || pce->cpu.mpr[2] != 0x80)
        return -1;
    for (int h = 0; h < (int)(sizeof(k_hooks) / sizeof(k_hooks[0])); h++) {
        if (k_hooks[h].pc != pc)
            continue;
        for (int k = 0; k < 5; k++)
            if (read_logical(pce, (uint16_t)(pc + k)) != k_hooks[h].sig[k])
                return -1;
        return h;
    }
    return -1;
}

/* n bytes from address ptr of window $8000-$9FFF with bank bank (CD RAM only, $80-$87); 0 = outside it */
static int read_banked(Pce *pce, int bank, uint16_t ptr, uint8_t *out, int n)
{
    uint32_t a = (uint32_t)bank * 0x2000 + (ptr & 0x1FFF);
    if (bank < 0x80 || a + (uint32_t)n > 0x88u * 0x2000)
        return 0;
    for (int i = 0; i < n; i++)
        out[i] = pce_bus_read(pce, a + (uint32_t)i);
    return 1;
}

/* Tables of the map's front cells in use (ys_engine.h, YsFront) */
static void read_front(Pce *pce, YsFront *f)
{
    f->base = (uint16_t)(RAM(pce, 0x205F) | RAM(pce, 0x2060) << 8);
    f->count = (uint16_t)(RAM(pce, 0x205D) | RAM(pce, 0x205E) << 8);
    uint16_t attr = (uint16_t)(RAM(pce, 0x2052) | RAM(pce, 0x2053) << 8);
    uint16_t bits = (uint16_t)(RAM(pce, 0x2C55) | RAM(pce, 0x2C56) << 8);
    if (!read_banked(pce, RAM(pce, 0x2C52), attr, f->attr, 256) ||
        !read_banked(pce, RAM(pce, 0x2C57), bits, f->bits, 256 * 8))
        f->count = 0;
}

void ys_engine_init(YsEngine *e)
{
    memset(e, 0, sizeof(*e));
    memset(e->copy_of, YS_NO_ACTOR, sizeof(e->copy_of));
    e->hero = YS_NO_ACTOR;
}

/* The VDC started copying the sprite table (from R13): the shown list becomes the newest one with that
 * address finished before the copy */
static void sat_copy(YsEngine *e, uint16_t base)
{
    e->shown_valid = 0;
    for (long k = 0; k < 4 && k < e->done_count; k++) {
        const YsList *l = &e->done[(e->done_count - 1 - k) & 3];
        if (l->base == base) {
            e->shown = *l;
            e->shown_valid = 1;
            return;
        }
    }
}

void ys_engine_hook(Pce *pce, int after)
{
    YsEngine *e = g_ys_engine;
    if (after || !e)
        return;
    /* By the VDC's counter: a long TIA may contain the whole copy, and the shown list went stale */
    if (pce->vdc.sat_copies != e->sat_copies) {
        e->sat_copies = pce->vdc.sat_copies;
        sat_copy(e, pce->vdc.sat_copy_base);
    }
    copy_hook(e, pce);
    hero_hook(e, pce);
    int h = which_hook(pce);
    if (h < 0)
        return;
    YsList *l = &e->cur;
    switch (h) {
    case HOOK_START:
        memset(l, 0, sizeof(*l));
        memset(l->owner, YS_NO_ACTOR, sizeof(l->owner));
        e->building = 1;
        e->cur_actor = YS_NO_ACTOR;
        break;
    case HOOK_ACTOR: {
        if (!e->building)
            break;
        int x = pce->cpu.x & (YS_ACTORS - 1);
        e->cur_actor = x;
        l->drawn[x] = 1;
        l->ax[x] = (int16_t)(RAM(pce, 0x2F78 + x) | RAM(pce, 0x2FB8 + x) << 8);
        l->ay[x] = (int16_t)(RAM(pce, 0x2FF8 + x) | RAM(pce, 0x3038 + x) << 8);
        l->flags[x] = RAM(pce, 0x2F38 + x);
        l->pose[x] = RAM(pce, 0x2EF8 + x);
        l->state[x] = RAM(pce, 0x2EB6 + x);
        break;
    }
    case HOOK_PIECE: {
        int slot = RAM(pce, 0x2216);
        if (e->building && slot < YS_ACTORS)
            l->owner[slot] = (uint8_t)e->cur_actor;
        break;
    }
    case HOOK_END:
        if (!e->building)
            break;
        e->building = 0;
        l->seq = ++e->done_count;
        l->count = RAM(pce, 0x2216);
        l->cam_x = (uint16_t)(RAM(pce, 0x2C33) | RAM(pce, 0x2C34) << 8);
        l->cam_y = (uint16_t)(RAM(pce, 0x2C35) | RAM(pce, 0x2C36) << 8);
        l->base = pce->vdc.reg[0x13];
        for (int i = 0; i < YS_ACTORS * 4; i++)
            l->sat[i] = pce->vdc.vram[(l->base + i) & (VDC_VRAM_SIZE - 1)];
        for (int w = 0; w < YS_WALKERS; w++) {
            l->w_actor[w] = RAM(pce, 0x2736 + w);
            l->w_vel[w] = RAM(pce, 0x2A66 + w);
            l->w_acc[w] = RAM(pce, 0x2ADF + w);
            l->w_left[w] = RAM(pce, 0x2B3F + w);
            l->w_mask[w] = RAM(pce, 0x2B9F + w);
            l->w_next[w] = RAM(pce, 0x2BCF + w);
            l->w_hp[w] = RAM(pce, 0x2A36 + w);
            l->w_hpmax[w] = RAM(pce, 0x28E6 + w);
        }
        l->exp = (uint16_t)(RAM(pce, 0x2ABB) | RAM(pce, 0x2ABC) << 8);
        l->gold = (uint16_t)(RAM(pce, 0x2C41) | RAM(pce, 0x2C42) << 8);
        l->unit = RAM(pce, 0x2092);
        read_front(pce, &l->front);
        memcpy(l->copy_of, e->copy_of, sizeof(l->copy_of));
        memset(e->copy_of, YS_NO_ACTOR, sizeof(e->copy_of));
        l->hero = e->hero;
        e->done[(l->seq - 1) & 3] = *l;
        /* ex_sprdma set this list's R13 and the copy started before the routine got here: sat_copy only had the
         * previous list with the same address, but the VDC is copying this one (it reads the table word by word, 1024 cycles) */
        if (pce->vdc.sat_transfer_pending > 0 && pce->vdc.reg[0x13] == l->base) {
            e->shown = *l;
            e->shown_valid = 1;
        }
        break;
    }
}

void ys_engine_frame(YsEngine *e, YsFrameList *out)
{
    out->kind = YS_LIST_NONE;
    out->shown_ok = 0;
    if (!e->shown_valid) {
        e->prev_shown = e->shown_seq = 0;
        return;
    }
    out->shown_ok = 1;
    out->shown = e->shown;
    out->shown_cam_x = e->shown.cam_x;
    out->shown_cam_y = e->shown.cam_y;
    long seq = e->shown.seq, want = e->shown_seq + 1;
    /* The list after the previous frame's, if the VDC repeated one (the next one finished after the copy) or if we were
     * already ahead of it: after a delay, the following ones also come out one late (the console shows 1, 1, 2,
     * 4: 3 never appears) until it skips one */
    const YsList *pick = &e->shown;
    if (e->shown_seq && want > seq && want - seq <= 2 && (seq == e->prev_shown || e->shown_seq > e->prev_shown)) {
        const YsList *c = &e->done[(want - 1) & 3];
        const YsList *held = &e->done[(e->shown_seq - 1) & 3];
        if (c->seq == want)
            pick = c;
        else if (e->shown_seq > seq && held->seq == e->shown_seq)
            pick = held; /* no new one: keeps the one already on screen */
    }
    out->kind = pick == &e->shown ? YS_LIST_SHOWN : YS_LIST_LATE;
    out->list = *pick;
    e->late_frames += out->kind == YS_LIST_LATE;
    e->shown_seq = pick->seq;
    e->prev_shown = seq;
}
