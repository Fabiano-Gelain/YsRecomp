/*
 * The game's windows read by the PC: Ys I and Ys II open and close the windows (menus,
 * conversations, shops, messages) through the same routines of bank $81 (docs/ys_notes.md, "The game's window system").
 * Hooks on instructions (like app/ys_engine.h; checked against the code bytes; read only, the console does not change):
 *   $81:6031 starts drawing the frame (the window already at $3F-$43: column, line, width, height in 8-pixel
 *            screen cells; VRAM page with the saved background, 0 = none) -> opening;
 *   $81:6040 INC $3E -> open;  $81:604D (after $3E - 1 and popping) -> closing (the saved background comes back
 *            one line at a time, $40 going down); $81:605D -> closed (leaves the list; $81:605A, which goes through it, is also
 *            called without closing: it only removes the one that is closing). The bytes of the masks' address at $605D do not
 *            count ($80:4E2F in Ys I, $80:4F78 in Ys II).
 * The hooks $81:6037 (frame ready: at the end it sets $40 back to the 1st line) and $81:6057 (saved background restored)
 * mark the end of each drawing. The map cell (64x32 BAT) of each window comes from the logic's camera ($2C37/$2C39)
 * of now, every frame, as $80:54D4 does (the window is pinned to the screen). The game's stack (`$33C6` = how many bytes, `$33C7...`) is shared with other uses (inventory list,
 * 1 byte before the 1st window etc.): the windows have no fixed place in it, and $3E may come dirty; that is why the list comes from the
 * hooks, and the stack and $3E serve only for checking.
 *
 * Window sprites (cursor, pink equipment frames): engine characters (app/ys_engine.h) without bit 5 of
 * $2F38 (not pinned to the map) and inside an open window (of the frame or the previous one: the shown list was built
 * in it). Scene figures (the big Adol of the full screens, faces, bosses) also lack bit 5: outside the windows.
 * The portraits of the portrait scenes are background, not sprites.
 */
#ifndef PCE_APP_YS_WINDOWS_H
#define PCE_APP_YS_WINDOWS_H
#include <stdint.h>
#include <stdio.h>
#include "app/ys_engine.h"
#include "pce/pce.h"

#define YS_WIN_MAX 5 /* the game halts (BRK) on the 6th */
enum { YS_WIN_OPENING = 1, YS_WIN_OPEN, YS_WIN_CLOSING };

typedef struct YsWindow {
    uint8_t col, row, w, h, page; /* $3F-$43 */
    uint8_t bat_x, bat_y;         /* map cell of the top left corner (0-63, 0-31), by the logic's camera */
    uint8_t state;                /* YS_WIN_* */
    uint8_t vis0, vis1;           /* window lines (screen cells, row..row+h) already drawn: [vis0, vis1) */
    uint8_t done;                 /* opening: frame ready ($81:6037); closing: background restored ($81:6057) */
    uint8_t chg_row;              /* image line where it changed in this frame (opened, frame ready, background restored,
                                   * closed): the ones above still show it as it was; 0 = no change in the image */
    uint8_t start_row;            /* image line where a line-by-line change started in this frame (opening
                                   * $81:6031, closing $81:604D): the ones above still show it as it was; 0 = none */
} YsWindow;

typedef struct YsWindows {
    YsWindow win[YS_WIN_MAX]; /* from bottom to top */
    int count;
    int n3e_extra;   /* $3E when opening the 1st: may come dirty from the game's intro (session 20261017_161018: 1 until the
                      * game clears it by itself, in a scene); the game then does not even play the open/close sound ($81:6148) */
    int last_ok;     /* the last frame's check matched (or was not done) */
    long opens, closes;
    long frames, mismatch; /* frames checked and with the list different from $3E or from the start of the game's stack */
    long first_mismatch;
    int prev_n;                      /* ys_windows_snapshot: the previous frame's rectangles (engine screen pixels) */
    int prev_r[YS_WIN_MAX][4];
    int snap_n;                      /* the windows of the last ys_windows_snapshot */
    YsWindow snap_win[YS_WIN_MAX];
    int gone_n;                      /* the ones that closed since the last ys_windows_snapshot */
    int layout_switch;               /* YsWinFrame.layout_switch since the last ys_windows_snapshot */
    YsWindow gone[YS_WIN_MAX];
    int last_sprites_ok;             /* the window sprites of the last ys_windows_snapshot (YsWinFrame.prev_own) */
    uint8_t last_own[YS_ACTORS];
    uint16_t last_sat[YS_ACTORS * 4];
} YsWindows;

/* The windows of one frame, copied for whoever draws (RedrawFrame.wins) */
typedef struct YsWinFrame {
    int count;               /* windows (opening, open, closing), from bottom to top */
    YsWindow win[YS_WIN_MAX];
    int prev_count;          /* the ones at the end of the previous frame (the image is drawn during the frame: at the top, the lines
                              * may have gone out before a window opened or closed) */
    YsWindow prev[YS_WIN_MAX];
    int gone_n;              /* the ones that closed in this frame (with the line where they disappeared) */
    YsWindow gone[YS_WIN_MAX];
    int scene;              /* some without saved background (page 0): portrait scene or full screen */
    int layout_switch;       /* the game switched in this frame between the scroll split and scroll only (ys_windows.c) */
    int sprites_ok;          /* own is valid: there was an engine list in the frame */
    uint8_t own[YS_ACTORS];  /* window sprites by number in the engine list (without bit 5 and inside a window
                              * of the frame or the previous one) */
    uint16_t sat[YS_ACTORS * 4]; /* that list's table (to check that it is the shown one) */
    int prev_sprites_ok;         /* the same for the previous frame (the shown table may be its) */
    uint8_t prev_own[YS_ACTORS];
    uint16_t prev_sat[YS_ACTORS * 4];
} YsWinFrame;

void ys_windows_init(YsWindows *w);
/* Instruction hook (Pce.on_instruction, before the instruction) */
void ys_windows_hook(YsWindows *w, Pce *pce);
/* End of a console frame (frame = its number): drawn lines of the opening/closing windows and the check
 * against the game ($3E = how many open; the bottom one = the first 5 bytes of the stack) */
void ys_windows_frame(YsWindows *w, Pce *pce, long frame);
/* Rectangle of the window's drawn lines, in engine screen pixels (0,0 = 1st map pixel; the position of the
 * characters, ys_engine.h), with the camera cam_x, cam_y of the shown list: x0, y0, x1, y1 (x1, y1 exclusive).
 * Returns 0 with no drawn line */
int ys_window_rect(const YsWindow *win, uint16_t cam_x, uint16_t cam_y, int r[4]);
/* Window sprites in an engine list: mine[i] = 1 on sprite i of a character without bit 5. Returns how many */
int ys_window_sprites(const YsList *l, uint8_t mine[YS_ACTORS]);
/* Box of sprite i of the list in engine screen pixels (x0, y0, x1, y1) */
void ys_sprite_box(const YsList *l, int i, int r[4]);

/* End of the frame, after ys_windows_frame: the windows and their sprites in out (eng: the frame's engine list,
 * NULL or YS_LIST_NONE = without the sprites). Once per frame (stores the rectangles for the next one) */
void ys_windows_snapshot(YsWindows *w, const YsFrameList *eng, YsWinFrame *out);
/* Boxes of the windows in console image pixels, like crop_ring_boxes (x0, y0, x1, y1, inclusive), by the scroll
 * of each map line (rows: the h lines of the image; only the map ones with background and sprites on): the cell of each
 * window by the camera the image shows (not bat_x/bat_y: the logic's may be ahead, as when coming back from the
 * save screen) appears where the map line and the scroll put it. The window counts as it was on the 1st line of the
 * image or at the end of the frame (like the crop; the line of the change: YsWindow.chg_row). With a scene (without saved
 * background) in this frame, none (*scene = 1), as the crop does in scenes. Returns how many */
int ys_windows_boxes(const YsWinFrame *f, const VdcLineInfo *rows, int h, int boxes[][4], int max, int *scene);
/* VDC map cells (tx x ty) of the game's windows at the start of the image, cells[line * tx + column]:
 *   YS_CELL_WIN: window (drawn line, vis0..vis1, at the end of the previous frame and of this one);
 *   YS_CELL_UNTIL | k: was a window and stops being one in this frame (background coming back, window closing): window until the game
 *     rewrites the cell;
 *   YS_CELL_FROM | k: becomes a window in this frame (frame coming down): window when the game writes the cell;
 *   (k: the window, in the order win and then gone of YsWinFrame). Whoever draws passes the game's writes to the map, in order,
 * to ys_windows_write. A cell is a window if != 0 and without YS_CELL_FROM. Returns how many cells marked */
#define YS_CELL_WIN 0x01
#define YS_CELL_UNTIL 0x40
#define YS_CELL_FROM 0x80
int ys_windows_cells(const YsWinFrame *f, int tx, int ty, uint8_t *cells);
/* The game wrote VRAM entry addr on image line row: the window cell that changes with the write (made
 * after the window started changing: YsWindow.start_row) becomes a window or stops being one */
void ys_windows_write(const YsWinFrame *f, uint8_t *cells, int tx, int ty, uint32_t addr, int row);

#endif
