/* Equipment on Adol: writes the frame table (app/equip.h, generated from the disc) and the C drawing
 * (equip_compose) of each frame with every combination of pieces in both games, for tools/equip/equip_c_test.py
 * to check against the Python rules (equip_art.compose).
 *
 * Usage: equip_check OUTPUT.bin
 * File (16-bit little-endian): palette (16), ice palette (16); for each of the 36 frames: frame set, direction,
 * 512 indices, mask (32), blade7 (32), under (512), ice_shadow (32); then, for Ys I and Ys II, sword x shield x
 * armor (0xFF first, then 0, 1, ...) x the 36 frames: 512 colors (0xFFFF = transparent); in Ys I, each one
 * followed by the same image on the ice (equip_ice: 512 colors). */
#include <stdio.h>

#include "app/equip.h"
#include "util/u8file.h"

static void put(FILE *f, int v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

int main(int argc, char **argv)
{
    if (argc != 2 || !g_equip_table) {
        fprintf(stderr, "usage: equip_check OUTPUT.bin (with the frames from the disc)\n");
        return 2;
    }
    FILE *f = u8_fopen(argv[1], "wb");
    if (!f)
        return 1;
    const EquipTable *t = g_equip_table;
    for (int c = 0; c < 16; c++)
        put(f, t->pal[c]);
    for (int c = 0; c < 16; c++)
        put(f, t->ice_pal[c]);
    for (int i = 0; i < EQUIP_FRAMES; i++) {
        const EquipFrame *fr = &t->frame[i];
        put(f, fr->set);
        put(f, fr->dir);
        for (int y = 0; y < EQUIP_H; y++)
            for (int x = 0; x < EQUIP_W; x++)
                put(f, fr->px[y][x]);
        for (int y = 0; y < EQUIP_H; y++)
            put(f, fr->mask[y]);
        for (int y = 0; y < EQUIP_H; y++)
            put(f, fr->blade7[y]);
        for (int y = 0; y < EQUIP_H; y++)
            for (int x = 0; x < EQUIP_W; x++)
                put(f, fr->under[y][x]);
        for (int y = 0; y < EQUIP_H; y++)
            put(f, fr->ice_shadow[y]);
    }
    long images = 0;
    static EquipImage img;
    for (int game = EQUIP_GAME_YS1; game <= EQUIP_GAME_YS2; game++) {
        int ns = equip_count(game, 0), nh = equip_count(game, 1), na = equip_count(game, 2);
        for (int s = -1; s < ns; s++)
            for (int h = -1; h < nh; h++)
                for (int a = -1; a < na; a++)
                    for (int i = 0; i < EQUIP_FRAMES; i++) {
                        EquipInfo info = { (uint8_t)(s & 0xFF), (uint8_t)(h & 0xFF), (uint8_t)(a & 0xFF),
                                           (uint8_t)game };
                        equip_compose(&t->frame[i], t->pal, &info, img);
                        for (int k = 0; k < EQUIP_H * EQUIP_W; k++)
                            put(f, img[k] < 0 ? 0xFFFF : img[k]);
                        images++;
                        if (game == EQUIP_GAME_YS1) {
                            equip_ice(&t->frame[i], t->pal, t->ice_pal, img);
                            for (int k = 0; k < EQUIP_H * EQUIP_W; k++)
                                put(f, img[k] < 0 ? 0xFFFF : img[k]);
                        }
                    }
    }
    fclose(f);
    printf("equip_check: %ld images\n", images);
    return 0;
}
