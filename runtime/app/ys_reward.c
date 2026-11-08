/* Reward for killing an enemy, read from the game: see ys_reward.h */
#include "app/ys_reward.h"

#include <string.h>

#define RAM(p, a) ((p)->ram[(a) - 0x2000])

enum { HOOK_KILL, HOOK_GOLD, HOOK_EXP };

static const struct {
    uint16_t pc;
    uint8_t bank; /* in window 3 */
    uint8_t kind;
    uint8_t n;
    uint8_t sig[12];
    uint16_t skip; /* bits of the bytes that do not count */
} k_hooks[] = {
    /* LDA #$17 / JSR $5E06|$5E0B (sound) / LDX $22 / LDA #$02 / STA $2826,X */
    { 0x7959, 0x81, HOOK_KILL, 12, { 0xA9, 0x17, 0x20, 0x06, 0x5E, 0xA6, 0x22, 0xA9, 0x02, 0x9D, 0x26, 0x28 }, 1 << 3 },
    { 0x7998, 0x81, HOOK_KILL, 12, { 0xA9, 0x17, 0x20, 0x0B, 0x5E, 0xA6, 0x22, 0xA9, 0x02, 0x9D, 0x26, 0x28 }, 1 << 3 },
    /* STY $2C41 / STA $2C42 / RTS */
    { 0x7097, 0x82, HOOK_GOLD, 7, { 0x8C, 0x41, 0x2C, 0x8D, 0x42, 0x2C, 0x60 }, 0 },
    { 0x7160, 0x82, HOOK_GOLD, 7, { 0x8C, 0x41, 0x2C, 0x8D, 0x42, 0x2C, 0x60 }, 0 },
    /* STY $2ABB / STX $2ABC / STA $2ABD / RTS */
    { 0x707C, 0x82, HOOK_EXP, 10, { 0x8C, 0xBB, 0x2A, 0x8E, 0xBC, 0x2A, 0x8D, 0xBD, 0x2A, 0x60 }, 0 },
    { 0x7145, 0x82, HOOK_EXP, 10, { 0x8C, 0xBB, 0x2A, 0x8E, 0xBC, 0x2A, 0x8D, 0xBD, 0x2A, 0x60 }, 0 },
};

static uint8_t read_logical(Pce *pce, uint16_t addr)
{
    return pce_bus_read(pce, (uint32_t)pce->cpu.mpr[addr >> 13] * 0x2000 + (addr & 0x1FFF));
}

static int which_hook(Pce *pce)
{
    uint16_t pc = pce->cpu.pc;
    if (pc < 0x707C || pc > 0x7998)
        return -1;
    for (int h = 0; h < (int)(sizeof(k_hooks) / sizeof(k_hooks[0])); h++) {
        if (k_hooks[h].pc != pc || pce->cpu.mpr[3] != k_hooks[h].bank)
            continue;
        for (int k = 0; k < k_hooks[h].n; k++)
            if (!(k_hooks[h].skip >> k & 1) && read_logical(pce, (uint16_t)(pc + k)) != k_hooks[h].sig[k])
                return -1;
        return k_hooks[h].kind;
    }
    return -1;
}

void ys_reward_init(YsReward *r)
{
    memset(r, 0, sizeof(*r));
}

void ys_reward_hook(YsReward *r, Pce *pce)
{
    int h = which_hook(pce);
    if (h < 0)
        return;
    if (h == HOOK_KILL) {
        uint8_t w = RAM(pce, 0x2022);
        r->open = 1;
        r->kills++;
        memset(&r->cur, 0, sizeof(r->cur));
        r->cur.walker = w;
        r->cur.actor = w < 0x30 ? RAM(pce, 0x2736 + w) : 0xFF;
        return;
    }
    if (!r->open) { /* event reward, paid by the area program (not an enemy death) */
        r->events++;
        return;
    }
    if (h == HOOK_GOLD) {
        uint32_t now = (uint32_t)(pce->cpu.y | pce->cpu.a << 8);
        uint32_t old = (uint32_t)(RAM(pce, 0x2C41) | RAM(pce, 0x2C42) << 8);
        r->cur.gold = now > old ? now - old : 0;
        return;
    }
    uint32_t now = (uint32_t)(pce->cpu.y | pce->cpu.x << 8 | pce->cpu.a << 16);
    uint32_t old = (uint32_t)(RAM(pce, 0x2ABB) | RAM(pce, 0x2ABC) << 8 | RAM(pce, 0x2ABD) << 16);
    r->cur.exp = now > old ? now - old : 0;
    r->open = 0;
    r->paid++;
    if (r->queue.n < YS_REWARD_MAX)
        r->queue.ev[r->queue.n++] = r->cur;
    else
        r->lost++;
}

void ys_reward_take(YsReward *r, YsRewardFrame *out)
{
    for (int i = 0; i < r->queue.n; i++) {
        if (out->n < YS_REWARD_MAX)
            out->ev[out->n++] = r->queue.ev[i];
        else
            r->lost++;
    }
    r->queue.n = 0;
}
