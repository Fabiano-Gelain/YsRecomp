/* Ys characters: see ys_actors.h and docs/ys_notes.md ("Engine memory map"). */
#include "ys_actors.h"

#include <string.h>

YsActors *g_ys_actors;

/* Console RAM ($2000-$3FFF) */
#define RAM(p, a) ((p)->ram[(a) - 0x2000])

/* Engine routines (bank $80 of the CD RAM in the $4000-$5FFF window) and the first bytes of each */
static const struct {
    uint16_t pc;
    uint8_t sig[5];
} k_hooks[] = {
    { 0x4D58, { 0xA9, 0x01, 0x4D, 0x15, 0x22 } }, /* start of the build: LDA #1 / EOR $2215 */
    { 0x4DAC, { 0xBD, 0x38, 0x2F, 0x89, 0x08 } }, /* a character (X): LDA $2F38,X / BIT #8 */
    { 0x4E60, { 0x8C, 0x02, 0x00, 0x8D, 0x03 } }, /* a piece (line already written): STY $0002 / STA $0003 */
    { 0x4DAA, { 0x58, 0x60, 0xBD, 0x38, 0x2F } }, /* end (after ex_sprdma): CLI / RTS */
    { 0x4B1B, { 0xAD, 0x41, 0x22, 0x8D, 0x12 } }, /* end of the wait for the next frame: LDA $2241 */
    { 0x4AA3, { 0x1F, 0x80, 0x14, 0x17, 0x80 } }, /* VBlank: copies the camera of the finished list to the scroll */
};
enum { HOOK_START, HOOK_ACTOR, HOOK_PIECE, HOOK_END, HOOK_WAIT, HOOK_VBLANK };

static uint8_t read_logical(Pce *pce, uint16_t addr)
{
    return pce_bus_read(pce, (uint32_t)pce->cpu.mpr[addr >> 13] * 0x2000 + (addr & 0x1FFF));
}

/* VCE line (0-262; the video may be behind: Pce.video_pending) */
static int vce_line(const Pce *pce)
{
    return pce->vce.vpos + (int)((pce->vce.hpos + pce->video_pending) / 1365);
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

void ys_actors_init(YsActors *ya, FILE *out)
{
    memset(ya, 0, sizeof(*ya));
    ya->out = out;
    ya->frame = 1;
    ya->first_bad = -1;
    ya->vbl_line = -1;
}

/* The VDC started copying the sprite table (from R13): the shown list becomes the newest one with that
 * address; the ones finished before it and never copied were skipped */
static void sat_copy(YsActors *ya, uint16_t base)
{
    ya->shown_valid = 0;
    for (long k = 0; k < 4 && k < ya->done_count; k++) {
        const YsBuild *b = &ya->done[(ya->done_count - 1 - k) & 3];
        if (b->base != base)
            continue;
        if (ya->shown.seq && b->seq > ya->shown.seq + 1)
            ya->skipped += b->seq - ya->shown.seq - 1;
        ya->shown = *b;
        ya->shown_valid = 1;
        return;
    }
}

void ys_actors_hook(Pce *pce, int after)
{
    YsActors *ya = g_ys_actors;
    if (after || !ya)
        return;
    int copying = pce->vdc.sat_transfer_pending != 0;
    if (copying && !ya->dma_seen)
        sat_copy(ya, pce->vdc.reg[0x13]);
    ya->dma_seen = copying;
    int h = which_hook(pce);
    if (h < 0)
        return;
    YsBuild *b = &ya->cur;
    switch (h) {
    case HOOK_START:
        memset(b, 0, sizeof(*b));
        memset(b->owner, YS_NO_OWNER, sizeof(b->owner));
        b->first = RAM(pce, 0x2E4A);
        ya->building = 1;
        ya->cur_actor = YS_NO_OWNER;
        break;
    case HOOK_ACTOR: {
        if (!ya->building)
            break;
        int x = pce->cpu.x & (YS_SLOTS - 1);
        ya->cur_actor = x;
        if (b->actors < YS_SLOTS)
            b->order[b->actors++] = (uint8_t)x;
        b->ax[x] = (int16_t)(RAM(pce, 0x2F78 + x) | RAM(pce, 0x2FB8 + x) << 8);
        b->ay[x] = (int16_t)(RAM(pce, 0x2FF8 + x) | RAM(pce, 0x3038 + x) << 8);
        b->pose[x] = RAM(pce, 0x2EF8 + x);
        b->flags[x] = RAM(pce, 0x2F38 + x);
        break;
    }
    case HOOK_PIECE: {
        int slot = RAM(pce, 0x2216);
        if (!ya->building || slot >= YS_SLOTS)
            break;
        uint16_t ptr = (uint16_t)(RAM(pce, 0x2010) | RAM(pce, 0x2011) << 8);
        b->owner[slot] = (uint8_t)ya->cur_actor;
        b->dy[slot] = (int8_t)read_logical(pce, ptr);
        b->dx[slot] = (int8_t)read_logical(pce, (uint16_t)(ptr + 1));
        break;
    }
    case HOOK_END:
        if (!ya->building)
            break;
        ya->building = 0;
        b->seq = ++ya->done_count;
        b->frame = ya->frame;
        b->line = vce_line(pce);
        b->work = ya->wait_master ? (int)((pce->master_cycles - ya->wait_master) / 1365) : -1;
        if (b->work >= 0) {
            ya->work_hist[b->work < 132 ? 0 : b->work < 210 ? 1 : b->work < 263 ? 2 : 3]++;
            if (b->work > ya->work_max) {
                ya->work_max = b->work;
                ya->work_max_frame = ya->frame;
            }
        }
        b->count = RAM(pce, 0x2216);
        b->cam_x = (uint16_t)(RAM(pce, 0x2C33) | RAM(pce, 0x2C34) << 8);
        b->cam_y = (uint16_t)(RAM(pce, 0x2C35) | RAM(pce, 0x2C36) << 8);
        b->base = pce->vdc.reg[0x13];
        for (int i = 0; i < YS_SLOTS * 4; i++)
            b->sat[i] = pce->vdc.vram[(b->base + i) & (VDC_VRAM_SIZE - 1)];
        ya->done[(b->seq - 1) & 3] = *b;
        if (ya->frame_builds < 4)
            ya->frame_list[ya->frame_builds] = b->seq;
        ya->frame_builds++;
        break;
    case HOOK_VBLANK:
        ya->vbl_line = vce_line(pce);
        ya->vbl_copy = (RAM(pce, 0x2080) & 0x02) != 0;
        break;
    case HOOK_WAIT: {
        int delta = (RAM(pce, 0x2241) - pce->cpu.a) & 0xFF;
        ya->frame_waits++;
        ya->frame_delta = delta;
        ya->waits++;
        ya->wait_hist[delta < 1 ? 0 : delta > 4 ? 3 : delta - 1]++;
        ya->wait_master = pce->master_cycles;
        break;
    }
    }
}

void ys_actors_frame(YsActors *ya, const uint16_t *drawn)
{
    ya->frames++;
    ya->builds_hist[ya->frame_builds < 3 ? ya->frame_builds : 3]++;
    const YsBuild *b = ya->shown_valid ? &ya->shown : NULL;
    if (b && memcmp(b->sat, drawn, sizeof(b->sat)) != 0) {
        ya->frames_differ++; /* the drawn table is not the copied list's */
        b = NULL;
    }
    int used = 0;
    for (int s = 0; s < YS_SLOTS; s++)
        if (drawn[s * 4] || drawn[s * 4 + 1] || drawn[s * 4 + 2] || drawn[s * 4 + 3])
            used = s + 1;
    if (ya->out)
        fprintf(ya->out, "f%ld", ya->frame);
    if (!b) {
        ya->sprites_free += used;
        if (ya->out)
            fprintf(ya->out, " no engine list: %d sprites without owner\n", used);
        ya->prev_seq = 0;
    } else {
        ya->frames_engine++;
        /* repeated frame: the same list as the previous frame while the game is building lists (not a pause) */
        int repeated = b->seq == ya->prev_seq && ya->frame_builds + ya->prev_builds > 0;
        if (repeated) {
            if (ya->repeated < 64)
                ya->repeated_list[ya->repeated] = ya->frame;
            ya->repeated++;
        }
        ya->prev_seq = b->seq;
        if (ya->out) {
            fprintf(ya->out, " cam %d,%d list %ld (f%ld line %d, logic %d lines)%s", b->cam_x, b->cam_y, b->seq,
                    b->frame, b->line, b->work, repeated ? " repeated" : "");
            for (int k = 0; k < ya->frame_builds && k < 4; k++) {
                const YsBuild *m = &ya->done[(ya->frame_list[k] - 1) & 3];
                fprintf(ya->out, "%s%ld (line %d, logic %d)", k ? ", " : " built ", m->seq, m->line, m->work);
            }
            if (ya->frame_waits)
                fprintf(ya->out, " wait %d", ya->frame_delta);
            if (ya->vbl_line >= 0)
                fprintf(ya->out, " vblank line %d%s", ya->vbl_line, ya->vbl_copy ? " copy" : "");
        }
        for (int i = 0; i < b->actors; i++) {
            int a = b->order[i];
            int any = 0;
            for (int s = 0; s < b->count && s < YS_SLOTS; s++) {
                if (b->owner[s] != a)
                    continue;
                int y = drawn[s * 4] & 0x3FF, x = drawn[s * 4 + 1] & 0x3FF;
                int ey = (0x58 + b->ay[a] + b->dy[s]) & 0x3FF, ex = (0x20 + b->ax[a] + b->dx[s]) & 0x3FF;
                if (x != ex || y != ey) {
                    ya->sprites_bad++;
                    if (ya->first_bad < 0)
                        ya->first_bad = ya->frame;
                }
                ya->sprites_owned++;
                if (ya->out && any)
                    fprintf(ya->out, ",%d", s);
                else if (ya->out)
                    fprintf(ya->out, " | a%d %d,%d p%02X f%02X: s%d", a, b->ax[a], b->ay[a], b->pose[a], b->flags[a], s);
                any = 1;
            }
            if (!any && ya->out)
                fprintf(ya->out, " | a%d %d,%d p%02X f%02X: -", a, b->ax[a], b->ay[a], b->pose[a], b->flags[a]);
        }
        int free_n = 0;
        for (int s = 0; s < used; s++)
            if (s >= b->count || b->owner[s] == YS_NO_OWNER)
                free_n++;
        ya->sprites_free += free_n;
        if (ya->out)
            fprintf(ya->out, " | no owner: %d\n", free_n);
    }
    ya->frame++;
    ya->prev_builds = ya->frame_builds;
    ya->frame_builds = 0;
    ya->frame_waits = 0;
    ya->vbl_line = -1;
}

void ys_actors_summary(const YsActors *ya)
{
    printf("actors: %ld frames, %ld with the engine list (%ld with the table different from the copied list); "
           "%ld character sprites, %ld without owner; %ld out of the character position (1st: %ld)\n",
           ya->frames, ya->frames_engine, ya->frames_differ, ya->sprites_owned, ya->sprites_free, ya->sprites_bad,
           ya->first_bad);
    printf("actors: lists finished per frame: 0 in %ld, 1 in %ld, 2 in %ld, 3+ in %ld; waits for the "
           "next frame: %ld (of 1 frame: %ld, 2: %ld, 3: %ld, 4+: %ld)\n",
           ya->builds_hist[0], ya->builds_hist[1], ya->builds_hist[2], ya->builds_hist[3], ya->waits, ya->wait_hist[0],
           ya->wait_hist[1], ya->wait_hist[2], ya->wait_hist[3]);
    printf("actors: logic (from the wait to the end of the list, in lines of 263 per frame): up to half a frame %ld, "
           "half to 80%% %ld, 80-100%% %ld, more than 1 frame %ld; the longest: %d lines (list of frame %ld)\n",
           ya->work_hist[0], ya->work_hist[1], ya->work_hist[2], ya->work_hist[3], ya->work_max, ya->work_max_frame);
    printf("actors: %ld late frames (previous frame's list repeated), %ld lists never shown",
           ya->repeated, ya->skipped);
    if (ya->repeated) {
        printf("; first:");
        for (long k = 0; k < ya->repeated && k < 64; k++)
            printf(" %ld", ya->repeated_list[k]);
    }
    printf("\n");
}
