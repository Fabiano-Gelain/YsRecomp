/*
 * Reward for killing an enemy, read from the game (docs/ys_notes.md, "Enemies: HP, EXP and GOLD"). EXP and GOLD only
 * go up in one place, in both games: the "died" branch of the routine of Adol's hit on the enemy (main program, bank
 * $81 in window 3; X = $22 = slot of the walker hit). It takes the HP ($2A36,X), and if it reached 0 plays sound $17,
 * pays the walker's GOLD ($2976,X) and then the EXP ($29A6,X, halved for each level of Adol ($2C45) above
 * its own ($29D6,X), at least 1), each one through a far call ($4B69) to the add routines of bank $82 (EXP up to
 * 99999, GOLD up to 65535). The same adds also pay the event rewards (e.g. Ys I, Sara giving the crystal):
 * the area program (bank $86) calls the program vector ($4806 -> $4B69, target $4848 in Ys II) and adds the EXP
 * a little at a time (by 1, 10, 100 at a time, with sound), with no death before: those are only counted (events), not drawn. The rest that
 * writes to them (new game, save, Ys II starting, shops) does not go through the adds.
 * Hooks on instructions (checked against the code bytes; read only, the console does not change):
 *   died:          Ys I $81:7959, Ys II $81:7998 (LDA #$17 / JSR sound / LDX $22 / LDA #$02 / STA $2826,X);
 *   GOLD stored:   Ys I $82:7097, Ys II $82:7160 (STY $2C41 / STA $2C42: the new value in A:Y, the old one still in RAM);
 *   EXP stored:    Ys I $82:707C, Ys II $82:7145 (STY $2ABB / STX $2ABC / STA $2ABD: the new value in A:X:Y).
 * The adds always store (even at the maximum, with the same value): the gain is what really went up. The EXP is the last
 * store of a death: there the reward is complete and goes to the queue (the level-up waits come after).
 */
#ifndef PCE_APP_YS_REWARD_H
#define PCE_APP_YS_REWARD_H
#include <stdint.h>
#include "pce/pce.h"

#define YS_REWARD_MAX 8 /* rewards kept until the frame is prepared */

typedef struct YsRewardEvent {
    uint8_t walker; /* slot of the walker that died ($22) */
    uint8_t actor;  /* its character ($2736,X) */
    uint32_t exp, gold; /* what went up */
} YsRewardEvent;

typedef struct YsRewardFrame {
    int n;
    YsRewardEvent ev[YS_REWARD_MAX];
} YsRewardFrame;

typedef struct YsReward {
    int open;            /* died and the EXP has not been stored yet */
    YsRewardEvent cur;
    YsRewardFrame queue; /* complete rewards not yet delivered (ys_reward_take) */
    long kills, paid;    /* deaths; complete rewards */
    long events;         /* adds with no death before: the area's event rewards */
    long lost;           /* queue full */
} YsReward;

void ys_reward_init(YsReward *r);
/* Before each instruction */
void ys_reward_hook(YsReward *r, Pce *pce);
/* Appends to out (without erasing what is already there) the rewards since the last call */
void ys_reward_take(YsReward *r, YsRewardFrame *out);

#endif
