# Ys Book I & II — research notes

Findings recorded so they don't have to be redone (Phase 8). BIOS routines used by Ys: see `docs/ys_bios.md`.

## Font and windows
- Ys **has no font of its own on the disc**: the letters come from the System Card's 12x12 font. The game reads the
  BIOS ROM directly (routine `$4057` → `$455B`, in CD RAM, same parameters as `ex_getfnt`: character in
  `$F8/$F9`, destination `$FA/$FB`, size `$FF`). Without the BIOS (bit 1 of `$2E` clear) it calls `ex_getfnt`
  ($E060) = our hand-drawn font, `runtime/bios/hle_font.c`.
- During the intro it calls `ex_getfnt` 67 times (26 uppercase + 26 lowercase + 15 symbols, at 16x16).
- Window text is drawn as an image into fresh tiles (8x16 cells, letters 12 lines tall). Code that writes the
  tiles: `$6318`-`$64AC` in bank `$81` (found with `pce_trace --watch-vram`).
- Choice window (e.g. Barbado's bar): black background, white border (color 15 of palette 0) 1 dot wide with a
  1-dot margin, white letters, a 4x7-dot white arrow left of the option, blinking 8 frames on and 8 off. The borders
  are simple tiles assembled by the game, not stored on the disc as a picture.
- **The game's 8x8 font** (2026-10-19): the game menu (SELECT: EQUIPMENT, INVENTORY, STATUS, LOAD, SAVE, SPEED;
  8 dots per line) and the panel (numbers) use a font of their own, uppercase, bold serif, 8x8, in background
  tiles (color 15): in the game's VRAM (session of 10/19, frame 3530), digits 0-9 = tiles 449-458, `:` = 459,
  `/` = 460, A-C = 461-463, 464 = solid tile, D-Z = 465-487; menu arrow = 504. "H.P", "EXP", "GOLD", "PLAYER",
  "ENEMY" are other tiles (colored, 257+). It is not on the disc in plain form (neither 4 planes nor 1 bit per
  dot): it must be compressed. The item window titles ("Ivory Key") use the System Card font.
  Checksum (CRC32 of the words, little-endian) of tiles 449-487 plus 504: `AADA7A4C`, the same in every gameplay
  frame checked (sessions of 10/19 and 10/20); before the game reaches the screen with the panel, the tiles are
  different. 7x7 letters with the 8th line empty; dots in color 15. The options menu copies it (`app/menu_font.h`).
- **Game windows on the map**: SELECT menu (33,36), INVENTORY (73,36), bar with portrait (window 153,28 to
  310,153, portrait outside it), conversations and GAME OVER all share the same white border (gray during fades)
  with the black margin; when switching from one window to another, one frame comes out with none. `crop_window`
  recognized them by shape.
- **The game's window system** (research; `pce_trace --bat-writers`, disassembled from CD RAM; the same in Ys I and
  Ys II, bank $81 in window 3, $80 in window 2). Top window in `$3F` column, `$40` row (8-dot cells of the
  **screen**, not the map), `$41` width, `$42` height, `$43` VRAM page where the background under it is saved (0 =
  not saved: portrait scenes, full screens). `$3E` = number of open windows (stack of up to 5; `$81:6000` pushes the
  5 bytes, `$81:600D` pops them; before gameplay, in the intro, `$3E` is something else). Open: `$81:6019` without
  saving (`$43` = 0) or `$81:6024` saving (`$81:6128` finds the page, `$81:60C2`/`$81:60CB` reads each map row and
  writes it to page `$43`); both continue at `$81:6031`: sound, `$81:6061` (border, row by row in `$81:607E`, tiles
  from `$307C` on), `$80:4F78` (rebuilds the walkers' masks), push, `$3E`+1.
  Close = `$81:604D`: with `$43`, `$81:60F5` restores the saved background; `$80:4F78` again. Screen -> map:
  `$80:54D4` (X column, Y row, plus the camera `$2C37`/`$2C39`; address in `$2DC0`); map row: `$80:5523`
  writes A words from `$2DC5` (wraps around the 64-wide map), `$80:55B9` reads; `$80:564D`/`$80:566F` = copy
  buffer <-> VRAM port. Letter: `$81:61FB` (character in `$F9`, cursor `$48`/`$49`, limits `$4B`/`$4D`); shop lists
  `$81:65AE`; menus (STATUS etc.) draw inside the window with bank $82 code called through
  `$80:4B63`/`$80:4B69`/`$80:4BB1` (call into another bank, with 2 bytes after the JSR). Outside the system: scroll and
  map update (`$80:4D58`, `$80:4DAC`, `$80:4C0F`; writes every frame, also with a window open), new map
  (`$83:A211`), full screens (`$81:6935`, `$81:612A`), map redraw by bank $82 (`$82:7C68`).
- **Window system check** (10 sessions, ~580 thousand frames, `$3E`/`$3F-$43` from `--state-log` against the windows
  found in the image by `--crop-log`): every window seen in the image was on the game's stack, except
  (a) the ~8 **closing** frames: `$3E` drops first, and the background comes back **one row per frame** (`$40` rises);
  (b) **portrait scenes** (bar, shops, ~32 frames on the switch), windows without a saved background; (c) 1 frame on
  opening (the border appears before `$3E` rises); (d) errors of the image detection itself. Game window with a saved
  background but no window in the image: 1 stray frame per opening (same ordering). With a window open (session
  `20261020_221359`, 3600-4290, `PCE_BW_DETAIL`), the map update **never** wrote to the window's cells; inside it only
  the window and menu code did.
- **Text program (dialogue)** (`pce_trace --log-calls 81:61F2`, `--dialog-log`). Letter in the window: `$81:61F2`
  (saves A/X/Y and calls `$81:61FB`; character in `$F8`/`$F9`), far-called from bank $80. The text (pointer `$67/$68`)
  goes either through a loop that writes everything at once (`$80:577F` in Ys II = `$80:5A79` in Ys I: reads a byte,
  translates it at `$5892`, 0 = end, `$578D` draws it or handles codes < $20 through the table at `$5852`) or through a
  **text task** (index X; start `$80:58A8` in Ys II / `$80:5BA2` in Ys I; resumes every frame at `$58C2` / `$5BBC`;
  pointer saved in `$2856,X/$2886,X`): every frame it adds the speed `$27F6,X` to the accumulator `$2826,X`; with speed 0
  it writes straight through (limited only by drawing time: ~15 letters per frame); otherwise it writes one letter each
  time the accumulator passes `$92` (a zero-page variable the map program also touches: `$40` at the start of Ys I, `$10`
  later) and waits for the next frame with `JSR $402A`. Script codes in the task: `$12 n` speed n; `$1E` speed 0; `$1D`
  speed 4; `$1B` speed 4 with sound `$13` on every letter (bit 7 of `$80`); `$13 n` / `$1C` pause of n / 16
  frames (`$4021`); `$14` waits for the other task (`$2796,X`); `$16` waits 4 frames with no button and opens another
  window (`$581D`: 4 bytes = `$3F-$42`, `$81:6024`); 0 = end. The speed test (`LDA $27F6,X; BEQ`) is at `$80:58D6`
  (Ys II and another program), `$80:5BD0` (Ys I) and `$80:5BD5` (another program): the same code in 4 disc files.
  In the 40 versioned sessions, slow text only appears in the Ys I program (46 stretches in 9 sessions, all without
  voice): Sara's speech at the start (speed `$0C`/`$04`, limit `$40`: one letter every ~5 frames) and the readings of
  the Books of Ys (portrait scene; session `20261020_221359`, 7463-10915; speed 4, limit `$10`: one letter every 4
  frames). Ordinary conversations already run at speed 0. The blinking arrow in choices is something else (`$81:6753`,
  one letter per frame: the cursor).
- **Windows and window sprites, from the source** (stage 1; `runtime/app/ys_windows.c`, `pce_trace --windows`,
  tests `janelas_sprites_ys`, `windows_ys1`). Hooks in bank $81 (identical in Ys I and Ys II): `$6031` = border
  starting (window already in `$3F-$43`; opening), `$6040` (`INC $3E`) = open, `$604D` (after `$6043`: `$3E`-1 and
  pop into `$3F-$43`) = closing, `$605D` = closed. The border (`$6061`) and the background restore (`$60F5`) advance
  row by row with `$40` (the rest of the window = `[row, $40)` when opening, `[$40, row+height)` when closing); almost
  every opening finishes within the same frame (27 sessions: opening in ~0.2% of window frames; closing ~2 frames per
  window). `$605D` calls the masks: `$80:4E2F` in **Ys I**, `$80:4F78` in Ys II (the rest of bank $80 differs); `$605A`
  is also called without closing anything. Window map cell = logic camera (`$2C37/$2C39`)/8 + column/row (`$80:54D4`;
  64x32-cell map); on the engine's screen (the actors'), x = cell*8 - camera of the displayed list.
  **Large menu screens (the save screen and others):** the game removes the scroll split: the whole image comes from a
  single scroll (BXR 0, camera 0,128: line 3 with BGY 104), with the border and the panel in the same map. On the way
  back, the logic camera is already the map's one frame before the scroll (e.g. `20261103_213911`, frame 26123: logic
  208,24, image still 0,128), and on the 1st frame with the split back the border and panel still have background and
  sprites enabled. The window stays at the same screen position in all those frames (white border in the image in sessions
  `20261103_194815`, `20261024_004620`, `20261024_195906`, `20261022_193936`): its box (`ys_windows_boxes`) comes from the
  scroll the image shows and from the map rows only, not from the logic camera. `bat_x/bat_y` (cells for
  `ys_windows_cells` and the tear fix) follow the logic. **The map moves within the background map** (session
  `20261103_232204`, frame 20618: the whole image comes out with BXR 0 and BGY = line + 101, camera 0,128, border in the
  same scroll; before, camera 64664,0): the VRAM from before the window only shows the map with the previous scroll
  (`redraw_ring`, `ring_one_scroll`), both for the floor and for the mask rule. Map figures and dots under the console's
  window (snow, stairs) appear in fill: they belong to the map.
  **The layout switch, in the code:** `$82:7AF6` enters the single scroll and `$82:7B2F` returns (in Ys II, `$7AA5` and
  `$7ADE`; same bytes, found on the disc). Entering: saves the map cells to RAM (`$7C06`), picks the interrupt line
  (`$2210` = `$68`), clears the map with `F100` (`$7BA7`), copies the panel and border from cells 41+ to cells 0-3 and
  **13-15** (`$7B5C`) and sets the logic camera to 0,128. Returning (`$2210` = `$148`): pops the camera and restores the
  cells (`$7C35`). **The border copied into cells 13-15 would land in the middle of the map with the current scroll**; on
  the next frame the new scroll already places it at the top of the image, and it never shows out of place on the
  console. So those writes are not window content (`YsWinFrame.layout_switch`, hook on the instructions that pick the
  line: `$7AFC`/`$7B32`, `$7AAB`/`$7AE1`). The same switch runs when loading a save and when changing areas: with the map
  black, it comes together with a page-0 window (`J0 2,0 36x16`).
  **Stack** (`$33C6` = bytes, `$33C7...`, `$80:53A5` pushes a byte, `$80:53B1` pops): opening pushes the 8 text bytes of
  the window below (`$2046-$204D`, `$81:6166`, only with `$3E` > 0), then the 5 of the new one (`$81:6000`); closing pops
  the 5 and the 8 (`$81:6173`; with `$3E` = 0, clears `$34C9`/`$3559`). It is shared with other uses (32 bytes of the
  inventory list on top of the window below; sometimes 1 byte before the 1st window): windows have no fixed place.
  **`$3E` can be dirty** from the game's intro (session `20261017_161018`, with the BIOS: 1 too many until the game
  clears it in a scene; while it is non-zero the game doesn't even play the open/close sound, `$81:6148`/`$6157`). So
  the list comes from the hooks; checked every frame (`$3E` minus its value with no window = open ones; the 5 bytes of
  each open one, in order, on the stack): **identical in every frame of the 27 versioned sessions** (~157 thousand frames
  with a window).
  **Window sprites** (cursor: 1 sprite; pink equipment frames: 4; Ys I inventory icons: 7-9): engine actors
  (`$2EF8` = 0, bits `$81`/`$89` = palette 1, priority; the cursor blinks with bit 3) **without bit 5** (not pinned to
  the map), at the start of the list. Inside a window open in the current frame **or the previous one** (the displayed
  list was built in the previous frame: when the equipment screen closes, the frames still show for 1 frame): across the
  27 sessions, ~73 thousand inside; the ones without bit 5 outside them were all scene figures. These **scene figures**
  also lack bit 5, outside the windows: the big Adol of the full equipment/inventory screens (window 18,0 20x16 without
  saved background, page 0; he stays ~60 frames after it closes), faces (session `20261023_203049`, the item window in
  front of the face), Ys I bosses and effects (no window), and a sprite at x 304 in Ys I. Rule: window sprite = no
  bit 5 **and** inside a window. **Portraits** in scenes (bar, shops): background; only the 4 border-corner sprites.
  **Masks on open/close** (`$80:4F78`): walks the actor list; those in state `$80` go through `$81:7DC3` (rebuilds the
  walker's mask from the current map, `$81:7ACF`), those in state `$50` through `$7F00` (A = 4). The window cells
  (letters and palette-15 background) produce a **full mask**: whoever is underneath disappears behind the black mask
  (e.g. session `20261023_230635`, frame 4200, Adol under the conversation with the masks visible, bits $22).
- **Opening and closing while the image is drawn**: between the close hook (`$81:604D`) and the background-restored hook
  (`$6057`) the game rewrites the window cells row by row **while the image is being output** (session
  `20261023_203049`, frame 6478: from image line 25 to 171); when opening, the border comes out between `$6031` and
  `$6037`. Anything that needs the window per image line (the window in front of sprites) follows the game's writes to
  the map (`ys_windows_cells`/`ys_windows_write`). The sprite table shown at the start of the image may be the engine
  list from the previous frame: window sprites are searched for in both.
- **Windows from the source vs. the old crop** (stage 2; `ys_windows_snapshot`/`ys_windows_boxes`, `RedrawFrame.wins`,
  `tools\trace\check_windows.py` and `PCE_WIN_CMP`, deleted in stage 3: in the Git history). Routine code:
  `$6031 JSR $6148` (sound), `$6034 JSR $6061` (border), `$6037 JSR masks`, `$603A JSR $617F`, `$603D JSR $6000`
  (push), `$6040 INC $3E`; close: `$6043` (`$3E`-1, `JSR $600D`), `$604D JSR $61DE`, `$6050 LDA $43 / BEQ $605A`,
  `$6054 JSR $60F5` (background), `$6057 JSR $6143`, `$605A JSR $6157` (sound), `$605D JSR masks / RTS`. The game
  **saves the background and draws the border in the same frame**, each one stepping `$40` row by row and **setting it
  back to the 1st row at the end**: "opening with `$40` = 1st row" may mean the border is done; hence the hooks `$6037`
  (border done) and `$6057` (background restored). The **map cell** comes from the **current** logic camera, every frame:
  with the SELECT menu open, the camera jumps (session `20261031_000404`, frame 8200: 80 dots when the 2nd window opens)
  and the game redraws the map and the window below at the new place; the window is pinned to the screen (the
  background comes back using the current camera). The image is output during the frame: each hook records the image
  line (`vce.vpos`); the window that counts is the one at the 1st image line or at the end of the frame, like the crop
  (`win_src`/`win_src_end`). The crop finds each window **1 dot inward** on each side.
  **27 sessions** (160 thousand frames with a window in the fill crop): boxes identical dot for dot in 92,415; scene in
  both 40,542; full screen without saved background (window 2,0 36x16 page 0: Ys II equipment/inventory), which the
  crop doesn't find, 19,322 (same for `redraw_ring`: no box); different edges only 66. Differences, all guesses by the
  crop: (a) **sticky "scene"** (6,129): after a portrait the crop held the scene ~30 frames into the fade-out (the
  portrait is not a game window: it stays drawn until the fade), and after a tall window (inventory 6,0 28x16, session
  `20261024_004620`, frame 33506) it stayed in scene for ~1,000 frames during the following conversation (rule "the
  scene continues while there is a window", from Feena's book); (b) **count** (1,534): the crop with two almost
  identical boxes for the same window (1 dot apart, e.g. session `20261024_004620`, frame 34688) and open/close it sees
  1 frame late (or never, for map-wide windows); (c) **sprites only from the crop** (193): actors (bit 5) entirely under
  the window in the frame it opens, before the masks: the crop took them for a cursor and erased them around the
  shrunk window; (d) **sprites only from the game** (336, nearly all in session `20261024_195906`, frames
  28668-28834): the 2 sprites of an item icon in the Ys II inventory, inside the window, that the crop doesn't
  recognize (grouped with another one touching outside): around the shrunk window they should come out loose (not
  checked in the image). Engine list different from the displayed table in 2,303 frames (late list: sprites not
  checked in those).
- **Crop from the game's windows** (stage 3; image-based detection deleted: `crop_window`, `win_src`,
  `check_windows.py`, `PCE_WIN_CMP`). `main.c` (and `pce_trace --crop-log`) passes the crop the boxes from
  `ys_windows_boxes` (`crop_set_windows`, 1 dot inward, as before); window sprites = those in the engine list marked in
  `wins.own`, found in the displayed table by **content** (the list may be late); masks (bit 7 of priority clear) that
  touch a window also come out around it. **Scene** = window without saved background (page 0: portraits 18,0 20x16;
  Ys II full screens 2,0 36x16, which become a scene with the HUD, user's decision) **or** a window at the map edge /
  larger than the strip (as before). After the page-0 window closes, the scene **holds until the fade-out** (or the
  screen stops being gameplay, the flash, or 600 frames). Measured over the 27 sessions: 344 scene ends, 263 already on
  black (darkened 7, 0 frames after the window), 81 during the fade (darkened 2, 10-11 frames after); none reached 600.
  347 starts: 312 on black and 35 visible (Ys II full screens appear at once). The old rule "the scene continues while
  there is a window" (scene stuck ~1,000 frames) was removed. **Correction (user report, 2026-11-02)**: ending at the
  1st fade step (darkened 2) made the switch from the whole map to the fill strip look like a zoom; now the scene only
  ends when the image is **fully black** (`darken` 7 and the smooth fade in 7 steps): in session `20261023_230635`, from
  5688 to 5708 (test `cena_retrato_ys`).

## Game fades (read from the code, 2026-11-03)
Tools: `pce_trace --watch-vce FROM TO` (who writes the colors and requests copies) and `--fades` (the steps read by
the hooks in `runtime/app/ys_fade.c`).
- **GAME OVER (session `20261103_224821`, frames 7447-7521):** the map goes to level 0 (7447), and on the next frame
  the game opens the page-0 window and lights 1 color in each palette 0 to 7 **without the fade routine**: the text is
  lit while the level is still 0. With RUN the window closes (7489), the fade restarts with only those 8 colors
  (level 7 at 7492, 0 at 7520) and only then does the image go black. "Level 0" alone doesn't mean a black screen:
  `YsFadeFrame.map_stray` counts the lit VCE colors that didn't come from the routine, in the palettes of the last step
  (the border and the panel are in palettes outside the fade).
- **The game never writes colors directly to the VCE** (only the BIOS, at boot: `$E889`). It builds the **displayed
  colors in RAM**, `$3C00-$3DFF` (background) and `$3E00-$3FFF` (sprites), 2 bytes each, and asks the BIOS to copy
  them: `$221F` = 2 (load), `$2220/$2221` and `$2223/$2224` = the pointers, `$2222`/`$2225` = blocks of 16 colors; the
  game's VBlank calls `ex_colorcmd` (`$E0E4`) at `$80:4A1B`, which copies with a 512-color TIA (the color address
  comes back to the same place). The **full colors** are at `$3800-$3BFF` (the programs' fade routines read from
  there; in the intro, from `$2ABF`).
- **Fade routine** (one copy per program, at different addresses; found by its bytes, `ys_fade.c`):
  `STX zp; STA zp+1; TXA; AND #7; ASL x3; ORA level; TAX; LDA table,X ...`: input color in X (low byte) and A
  (high), level in a zero-page byte (`$8B` in the game programs, `$41`/`$32`/`$68` in the scene programs); each channel
  comes from **table[channel * 8 + level]** (64 bytes, the same in every program: proportional, level 7 = the color,
  0 = black; not "subtract 1 from each channel"). Scene family (intro, Ys I ending, Ys II start): also adds a
  **brightening** (another zero-page byte, right after the level) and clamps at 7. Callers (`$82:7867` in Ys I,
  `$82:78AC` in Ys II, `$82:7B7F` in the load menu) read the color from `($10),Y`, call, and store to `($12),Y-1`
  (`DEY; STA ($12),Y`; in scenes `LDA zp; DEY; STA ($12),Y`). Source `$3800`, destination `$3C00`. In the intro there
  is another call (`$80:53F5`) with a different destination: it stays without an owner (`unknown_dest`).
- **Two paths for the copy to the VCE**: the game requests it with `$221F`; it is serviced by `ex_colorcmd` (`$E0E4`,
  in the game's VBlank, `$80:4A1B`) or, when the VBlank is handled by the BIOS (`$F5` without bit 1: when loading a save
  from the game menu), by the BIOS interrupt itself (`$E889`, which internally calls `$E522`, bypassing `$E0E4`). Both
  clear `$221F` at the end: that is how the hook sees the copy. Previously only the 1st path was seen and those fades
  were missed (session `20261027_182704`, frames 13325-13349).
- **The fade is an actor** (`$401E`, see "Actors" under "Disc files and loader"): `$77FB` registers it; the step
  (`$82:7834` darkens, `$781E` brightens) computes the colors at level `$2946,X` and then adds/subtracts 1 (ends at -1
  or 8: requests the last copy, `DEC $31BE`, `JMP $4030`); the next step comes after `$2A66,X` frames (4). Palettes in
  the fade: 32 bits in `$2826/$2856/$2886/$28B6,X` (background 0-7, 8-15, sprites 0-7, 8-15); the pattern
  `FF 7F FF 7F` leaves palette 15 out; areas use others (e.g. `$86:6CF4` in Ys I, only palettes 0 and 2).
  **Fade-out**: levels 7, 6, ..., 0 every 4 frames (the 1st, 7, changes nothing). Palettes that an old fade left at
  some level stay there (e.g. the border's on black after the load menu): the "map level" is that of the colors of the
  **last step** (`YsFadeFrame.map_level`).
- **Black screen without a fade**: when a program starts (e.g. the Ys I game after the load menu or the intro) the game
  blanks the screen without the fade routine: video off (VDC CR without background or sprites; e.g. $0C) or all 256
  background colors black (the sprite colors remain from the previous scene), for 1-4 s, before the 1st fade (level 0
  upward). The map level there is 0.
- **Ys I ending flash** (area, `$86:616C`, every 6 frames, 81331-81367 of session `20261023_000904`): adds 1 to each
  channel of every displayed color (`$3C00-$3FFF`, clamping at 7) in place; after the white comes an ordinary fade with
  a white source. So every displayed color = per channel **min(7, table[source][level] + brightening)**.
- **Other writes** to `$3C00-$3FFF` (not fades; they take effect immediately): animated area colors (`$86:6D1B`,
  blinking and cycling colors), palettes set directly (`$80:5747`, ...).
- **What the PC does** (`ys_fade.c`): hooks at the fade routine entry (source, level, brightening, destination from the
  stack and from the caller's `STA ($12),Y`), in the flash loop and at `$E0E4` (what goes to the VCE); each frame, each
  VCE color gets its source, level and brightening **only if the computation gives exactly the VCE color**. Compared
  with the old image-based approach in 6 sessions: the old one missed the 1st step of almost every fade and, in dark
  rooms, several.

## CD music (read from the code, 2026-11-04)
- **Playing a track:** resident routine `$80:4242/$4244` (`PHX; PHA; CLA; JSR $E02D`): `cd_fade(0)` and right after
  `cd_play` (`$E012`), the same in Ys I and Ys II. The CD's D8 command only arrives ~10 to ~40 ms later: with a fade
  active, the old track comes back at full volume in that window (that was the GAME OVER noise; the same with the real
  BIOS and in Geargrafx, it's the game's).
- **Fading the music out:** `cd_fade($0D)` at `$82:76EB` (on death), `$82:7D14`, `$80:5CE1` and others.
- **Restoring the volume without changing track:** scene command `$80:5CF1` (`$80:5CF6`, `JMP $E02D`; session
  `20261021_214858`, frame 22166): the music continues on purpose. The old time-based approach (0.1 s) cut its start;
  now it comes back immediately.
- **What the PC does** (`ys_music.c`): hook on the `JSR` at `$80:4247`; with a fade active and a track playing, the CD
  output is muted until the next music command (`CdAudio.pc_mute`). If the fade has just started (frame 59944 of the
  same session), it only loses those ms of the old track, which the command would cut anyway. Without a fade, the volume
  doesn't change and nothing is muted.

## Mid-frame scroll changes
- Ys changes the scroll (BXR, BYR in two bytes, CR) via a line interrupt at the start of the map (image line 27:
  before it, the border with BXR 0, BYR ~349-353, background only; after it, the map, background and sprites) and at
  the end (155). With a lot to do (the Ys I centipede boss, which makes the screen shake), the interrupt arrives 1-2
  lines late: the first map lines show the continuation of the border (colored dots of the panel stored in VRAM) and
  the whole map shifts down. The registers land on different lines: sometimes only the CR (sprites) is late, or BXR has
  already switched and BYR hasn't, or BYR is half-written (new low byte, old high byte: 424 = $1A8). In the session of
  10/19, 347 frames, 323 in the centipede fight. Only the PC window fixes it (now from the game: `fix_scroll`).
- **The game's routine** (disassembled from the generated C; the same in Ys I and Ys II): VBlank `$80:4A10` -> `$4A48`
  (top border: BXR `$220C`, BYR `$2210`) and `$4AA3` (camera copied if the list is ready; `$31C4` = 0; RCR = $57). Line
  interrupt `$80:4AC6` (5 PHA/PLA of delay), by the counter `$31C4`: 0 -> `JSR $4A5B` at `$4AE1` (BXR = `$2C2F/$2C30`,
  BYR = `$2C31` - 1, or $1FF if 0; SMB6 `$F3` = sprites; if `$2C31` >= $81, RCR = $157 - `$2C31` and 1 comes next,
  otherwise 2 and RCR = $D7); 1 -> `JSR $4A88` at `$4B05` (ring wrap: BYR = $1FF, BXR = `$2C2F` or 0 with bit 6 of
  `$82`; RCR $D7); 2+ -> `JSR $4A75` at `$4AD8` (BXR `$220E`, BYR `$2212`; RMB6 `$F3`). At the end of every interrupt,
  CR = `$F3/$F4` (`$4A33`). On time, the switch is made on image lines 26 and 154 and shows on the next (27 and 155).
  `$4B2F`/`$4B3A` disable the switches. Hooks on the three calls: `app/ys_scroll.c`; fix: `fix_scroll`
  (`app/game_fixes.c`); `pce_trace --scroll-log`. Since the CPU/BIOS-in-C timing changes (Phase 8), the centipede
  session of 10/19 has no late switch at the start of the map anymore (previously 344 frames); 1 remains at the end
  (frame 80666).
- At the end of the map (line 155) the bottom part of the border and the panel are stored from VRAM line 256 on
  (BXR 0, CR $80, background only; the map uses CR $C0). In the temple (Ys I), the switch sometimes arrives 1 line late
  (the border and the panel shift down 1 line for a frame; sometimes only the CR is late) and, with the map scrolled to
  the end, its last line is 255: it continues straight into the border's line 256 (same BXR). Only the PC window fixes
  it (`fix_scroll`).
- **Mine darkness** (Mine Depths, Ys I): the light circle is 12 black 16x16/16x32 sprites in front (patterns $2C0,
  $2C2, $2CA, palette C, mirrored) around the hero, and the lit floor is redrawn in the map (outside it, black tiles)
  when the hero walks. Interpolating the sprites over the already-redrawn floor would show lit floor outside the
  circle: in those frames (~12% of the frames walking in the mine) and for 1 s afterwards, whatever is dark in A and in
  B (5x5 square) stays dark and the rest is interpolated.
- With the SELECT menu open, the game sometimes changes the scroll and the map together (identical image, scroll 8 dots
  different): smooth motion doesn't interpolate that frame. Until 2026-10-20 those frames went out without
  interpolation, and walking in the mine stuttered.

## BIOS sound driver (PSG)
- Two programs, music and effects, 6 channels each; the effect has priority on the channel (`runtime/bios/hle_psg.c`).
- RAM: `$22DF` = programs enabled (0 = music only, 1 = effects only, 2 = both); `$247C+x` = music channel x
  playing; `$2607+x` = 1 when the effect owns channel x. Ys plays the chip music through this driver (on all 6 channels
  during gameplay); effects in ~11% of the samples of a 12-minute session.
- Play (function $0B; `pce_trace --log-calls E0D8`, $20F8 = number): music $02, $05, $06, $0B; effects $12
  (confirm/open and close window), $13 (cursor), $14-$19, $1A (repeated every 5 frames at a boss), $1B, $1D,
  $1F, $23, $25, $2A, $2B. The game's tables reach the driver (functions $03-$09) on entering the playable part:
  ~frame 2681 via Continue, ~10414 via Beginning watching the prologue, ~3705 pressing RUN every 40 frames.

## Intro (BIOS-less build, no buttons)
- Frames: loading (black) 0-252 → "from NEC" 253 → "Ys Book I & II" logo 482 (more than 1000 lit dots) → dark 1443 →
  copyright 1633 → "Ancient Ys Vanished" and video → goddess (~8700) → back to the copyright. Music: CD track 21 from
  ~24 s.
- RUN does nothing on NEC and the logo. From the copyright on, the game itself skips: copyright/video → goddess (even
  in the middle of her animation) → main menu (Beginning/Continue).
- Fast-forward: originally ~600 frames/s, later ~950 frames/s; today "Skip intro" reaches the menu in ~4.6 s.
- With RUN (or I, which does the same here) measured by script: RUN on the copyright (1640) → dark (1650) → goddess
  starts appearing (1940: 5 dots; 1950: 145) → RUN accepted as early as 1960 → full goddess → "Ancient Ys Vanished"
  title (red, then blue background) → main menu Beginning/Continue lighting up at ~2334 (58690 lit dots, complete at
  ~2372) → menu routine at 2386. Without the 2nd RUN the goddess stays still (~2290) until pressed.
- **The intro program (`$04`, read 2026-11-04; hook: `runtime/app/ys_intro.c`).** Runs from frame 60 on; the logic is a
  **script** in its own bytecode, run by an actor (`$6339`), pointer in `$62/$63`, loop at `$5A98`, operation table at
  `$5AD5` (the code is the index; returns the length in A). End of script (`$FF`): `$2EF6` = 4, and the program picks
  the next one by the cursor `$3B` (table `$489C`: `$28` Beginning, `$01` Continue; `$52` if bit 6 of `$2E`).
  - Operations seen: `$04 p lo hi` registers an actor, `$0E n` puts `n` in `$2EF5` (2 = the script waits; `$0C` = jumps
    to the pointer in `$2EF7/8`; 0 = continues), `$10 n` waits `n` frames, `$14 lo hi` calls a routine, `$18 ...` screen
    fade, `$1A lo hi 00` repeats until that address is zero (seen like that in the sessions; `$2EE5` = fades in progress,
    `$2EED` = another counter of the same kind, not read), `$FF` end.
  - **Each screen starts with the script's fade** (the 1st light comes 9 to 15 frames later): "from NEC" `$4C4A` (frame
    244), logo `$4C6D` (473; "from NEC" stays on screen), copyright `$4D3D` (1618). The goddess comes from script
    `$4F78` (actors `$49E7`, `$4A7C`, `$4BB8`; her wait is `$4913`); the title, from script `$5023`.
  - **Main menu = routine `$494C`** (registered at `$509D`): mask `$2EBF` = `$08` (RUN only), cursor in `$3B`.
  - **Accepting buttons**: routine `$48A1` every frame. With `$3A` = 0, reads `$2228`, ANDs it with `$2EBF` and, if
    anything is left, sets bit 0 of `$24` (the "hurry"). `$2EBF` is only written at `$48CA` and `$4915` (`$0F`) and
    `$494E` (`$08`); until frame 1458 it is 0 (that's why NEC and the logo ignore RUN). `$3A`: `$FF` = locked (`$4C03`),
    `$1E` = 30 frames (`$4BFF`).
  - **What the hurry does**: the scheduler (`$4288`) stops counting every actor's waits while the bit is set
    (`BBS0 $24`), and the goddess routines (`$4A8C`, `$4BC0`...) jump to the end. RUN pressed on the copyright: `$48C8`
    blanks the screen and jumps to script `$4F78`. Before the goddess: it is kept until `$4913`, which jumps to the
    title.
  - Hurry on the very 1st copyright frame (RUN at 1619, 2nd at 1748) reaches the menu at 2175, with the same image as
    the slower path (RUN at 1640 and 1960, frame 2391) and the same music commands.
  - **RUN during the animated scene:** black screen with green specks that grow until the next scene. It's the game's:
    identical in Geargrafx with the real BIOS (RUN at 3201 and 7533; in the BIOS-less session `session_20261104_221925`,
    frames ~1950-2010).

## Sound mix (Mednafen balance)
- Relative to Geargrafx: chip ×0.678, CD music ×0.5, ADPCM ×0.684 (in CD games Mednafen lowers the chip; the
  "50%/43%" in its code are not relative to the chip). Math in `runtime/pce/mix.h`.

## Buttons (game manual, supplied by the user on 2026-10-18)

- D-pad: moves Adol and navigates menus.
- I: selects, confirms and casts magic (Ys II). In the controls menu: "Confirm/Magic".
- II: uses the equipped item and cancels/exits a menu or the inventory. "Cancel/Item".
- SELECT: pauses and opens the command window. "Commands".
- RUN: starts the game. "Start". (The user's recorded sessions also have RUN in the middle of gameplay; the manual
  doesn't say what it does there. Don't merge it with I: pressing I would send both.)

## Motion: scroll and sprites (measured 2026-10-18, for smooth motion)
- Measured in session `session_20261017_183502` (5900 frames), scroll at image line 60 (the top part is the map; the
  bottom panel, H.P/EXP/GOLD, has its own scroll and doesn't move). At the end of the frame the BXR/BYR registers
  already hold the panel's value: the map scroll is only visible line by line.
- The map scrolls **2 dots per frame for 4 frames and stops on the 5th** ("2-2-2-2-0", 8 dots every 5 frames =
  96 dots/s), horizontally and vertically. It's not slowdown: the sprites keep moving on the 5th frame.
  Big jumps (more than 16 dots) = room/screen change.
- Sprites: most move 1-2 dots per frame (some 3-8). The hero stays still on screen while the camera follows him. The
  golden border around the map doesn't move.
- **Map ring** (measured 2026-10-18, session `session_20261017_183502`, frame 3854): the background map is 64x64 tiles
  (MWR $5A) and Ys keeps the visible map in a ring of lines 0-255 (32 tile rows); lines 256+ hold the border (BYR 328..
  at the top, 256.. at the bottom) and the panel. Where the ring wraps, the game changes BYR mid-map: image lines
  27-144 with BYR 138..255 and 145-154 with 512..521 (= 0..9). The switch line moves with the vertical scroll.
  Horizontally there is no mid-image switch.
- The game copies graphics into VRAM while the image is being output (mid-frame): only with the VRAM at the start plus
  the line-by-line writes does the redraw reproduce 99.9% of frames. The few remaining ones change colors mid-frame
  (not logged).

## Engine memory map (measured 2026-10-24)
Sessions: `20261017_183502` (Ys I, field, with BIOS), `20261024_160656` (Ys II, cave, with BIOS) and
`20261024_084643` (Ys II, cave, without BIOS, SLOW and FAST). The engine lives in CD RAM, bank $80 in the window
$4000-$5FFF; Ys I and Ys II use the same routines at the same addresses. Tool: `pce_trace --actors`
(`tools/trace/ys_actors.c`, hooks at these addresses, checked against the code bytes; also works in `ys_trace`).
Tests `actors_ys` and `actors_ys1`.
- **Main loop** ($80:49F2): wait for the frame ($4B13) → logic ($4012) → $4EAD → build the sprite list ($4D58) →
  loop. The end of the list build marks the end of that iteration's logic.
- **Wait** ($80:4B13): compares `$2241` (frame counter, which the game itself increments in VBlank, $80:4A27) with the
  last one seen (stored at $80:4B12). If the logic overran the frame, it returns immediately: the game **does not slow
  down**.
- **VBlank** ($80:4A10, branch on bit 5 of the VDC status; runs at VCE line 259): at $4AA3, if bit 1 of `$2080` is set
  (list ready), clears it and copies the camera `$2C33/$2C35` to `$2C2F/$2C31`, the map scroll (put into BXR/BYR by TIA
  at the map line, $4A5B).
- **Camera** (16 bits, dots of the map ring: X 0-511, Y 0-255; movement = difference modulo 512/256):
  `$2C37/$2C39` = the logic's; `$2C33/$2C35` = the built list's (copied from `$2C37` at the end of the build);
  `$2C2F/$2C31` = the displayed scroll's. In every gameplay frame of the 3 sessions, the camera of the displayed list =
  the map scroll: sprites and background never get out of sync.
- **Actors** (up to 64; 64-byte tables, index X < $40): `$2E74,X` = next in the linked list (starts at `$2EB4`;
  ≥ $40 = end; it is the drawing order); `$2EB6,X` = state ($8x = active, cleared when leaving the list);
  `$2EF8,X` = drawing (pose); `$2F38,X` = bits (0-2 = drawing palette, 3 = hidden, 5 = pinned to the map, 7 =
  priority); `$2F78/$2FB8,X` = X **on screen** (signed 16 bits; 0 = 1st map column); `$2FF8/$3038,X` = Y on screen
  (0 = 1st map line). There is no "world" position: when the camera moves, routine $80:4F24 adds the offset (with the
  sign flipped) to every actor with bit 5. Map position = screen + camera (`$2C33`). In Ys II Adol is actor 0 (still at
  144,56 while the camera follows him).
- **Sprite list** ($80:4D58): alternates the VRAM table (`$2215`: $0E00/$0F00), starts at sprite `$2E4A` (= 4: sprites
  0-3 are the 4 fixed corners of the map border, from another routine; they are the only ones without an owner in
  gameplay frames) and walks the linked list. Each actor ($4DAC) without bit 3: sprite X = $20 + screen X + piece dx,
  Y = $58 + screen Y + dy; a piece outside (Y ≥ $D8 or X ≥ $160) is skipped; stops at 64 (`$2216` = counter). The
  pieces: pointer ($10) chosen by the drawing and the palette; segments [count, palette] + count × [dy, dx, drawing,
  attribute] until count 0. At the end it clears the rest, copies the camera, sets bit 1 of `$2080` and requests the
  table copy (ex_sprdma, R13).
- **When it shows**: the VDC copies the table (SATB) at the start of VBlank, VCE line 257 (the image ends at 256): the
  list built in one frame shows in the next. Checked: the drawn table = the copied list's in every gameplay frame.
- **Which actor is Adol**: the routine that creates the hero in the game program (`$75A5` in Ys I, `$75A7` in Ys II,
  window `$6000`, after `LDA #$81 / JSR $4CF5 / STA $2736,X`) stores the actor in `$76` and the walker in `$75`, and the
  game program reads Adol from there (`LDY $76`). Other programs (banks `$104`-`$10A`) use `$75`/`$76` as scratch, so the
  number is recorded at creation (`YsEngine.hero`). In the 129 sessions he is always actor 0. Adol's walker is **not**
  fixed: `X=$0A`/`$0C` below hold in part of the game; in others `$2736,$0A` is another actor or `$FF`.
- **When the VDC copies the list**: the copy takes 1024 VDC cycles, word by word. A long TIA can contain the whole copy
  (between one instruction and the next `sat_transfer_pending` is already 0) and `ex_sprdma` can set R13 right before
  the copy starts, with the list routine still before its end: `Vdc.sat_copies` counts the copies (observers only) and
  the end of the list corrects the displayed one if the copy in progress is its own. In the 129 sessions the list
  matches the frame's table in every gameplay frame except 93, in 3 sessions: they are not frames where Adol changes (no
  Adol sprite differs); not investigated further.
- **Walkers** (another table, with its own index: Adol is X=$0A in Ys I and X=$0C in Ys II): `$2736,X` = actor it
  moves; `$2B0F,X` = direction (dx/dy at $80:5143); `$2A66,X` = speed (32 for Adol); `$2ADF,X` = accumulator;
  `$2B3F,X` = dots left in the step; `$2092` = unit. Every frame ($81:765E): accumulator += speed; while
  accumulator ≥ unit: − unit, move 1 dot ($80:5111), remaining − 1. The hero moves in **8-dot steps** and, on the frame
  after a step ends, decides the next one and doesn't move: that's the "0" of the rhythm. Unit = table $60E5 (bank $82)
  [`$31C1` & 1]: **16** (SLOW: 2-2-2-2-0, 8 dots in 5 frames = 1.6 per frame) or **12** (FAST: 2.67 = 2-3-3-0, 8 dots
  in 4 frames = 2 per frame). The option only has these two values.
- **Late frames**: the logic overran the frame (the list finished after line 257). The screen repeats the previous list
  (and the scroll, since the camera is only copied with the list ready), the late list **never shows** and the next one
  already comes out on the right frame. Walking in FAST: 3, 0, 0, 5, 3 instead of 3, 0, 3, 2, 3 (the "2-0-4" pattern).
  It's the game's: the same in Geargrafx (session with BIOS identical in every frame). Measured ("logic" = from the
  return of the wait to the end of the list, in lines; the frame has 263):

| Session | Gameplay frames | Repeated | Lists that overran | Would fit with a CPU 1.1x / 1.25x / 1.5x faster |
|---|---|---|---|---|
| Ys I field (183502, BIOS) | 2934 | 12 | 9 (372-791 lines) | 0 / 0 / 1 |
| Ys II cave (160656, BIOS) | 1746 | 16 | 17 (255-344) | 12 / 14 / 17 |
| Ys II (084643, no BIOS) | 9495 | 173 | 236 (255-808) | 143 / 196 / 215 |
| same, frames 5600-6399 (cave, FAST) | 800 | 43 | 63 (255-342) | 43 / 61 / 63 |

  In the crowded cave in FAST, the heavy iteration is the decision one (every 4 frames, ~277 lines).
- **Chained delays**: after a late frame, the following lists also come out one behind until the VDC skips one: e.g.
  the console shows 2024, 2024, 2025, 2027 (2025 was the late one; 2026, the decision one, never shows). To show them
  all, the list following the previous frame's is returned while the VDC is behind (`runtime/app/ys_engine.c`,
  `ys_engine_frame`). Cave FAST (5600-6399): 55 frames swapped.
- **Walkers in practice** (measured in the FAST cave): they all move in 8-dot steps with the decision frame
  (`$2B3F` = 8 and no movement); Adol v32, enemies v16 (1.33 dots/frame in FAST: 1,3,4,5,7,8 in 6 frames). Frames
  moving in the step = ceil((8 x unit - accumulator) / speed). **In the scenes seen, every actor has bit 5** (Adol too:
  the game moves him and the camera pushes him back); world position = screen + camera holds for all of them.
  Whom the camera follows: the walker that stays still on screen on an axis in **every** frame of the step (an enemy
  may come back to the same height by chance).
- **Border sides on the map**: on the map lines, the border's sides are tiles of the background map itself, in the
  columns just outside the visible map (CROP_MAP_X..+CROP_MAP_W), rewritten by the game when the camera moves. The map
  shown shifted from B brings a strip of those columns (old, dark border) at the edge. That strip now comes from map A
  (where those columns were still visible map); repeating the edge column left stretched stripes, visible in FAST (up
  to ~3 dots).
- **Ys I mine light**: the black mask around the circle is an actor (a2, 12 sprites, bits $A3) always at Adol's (a0)
  position, without a walker; the lit floor is redrawn on the map every frame around Adol.
  Code (mine area program in bank $86, window $6000): $86:721F creates the light actor (`JSR $4851`, drawing 0, bits
  $A3) and stores its number in `$7673`; every frame $86:7108 (`LDX $7673 / LDA $2F78,Y / STA $2F78,X`...) copies into
  it the position of actor Y (Adol) and $86:7130 picks the drawing from the position (`$56/$58`). The PC engine sees
  this copy through a hook (`YsList.copy_of`, `runtime/app/ys_engine.c`): the light moves with map B (the lit floor is
  B's).
  **Window over the mask** (read from the code, session `20261103_194815`, frame 10400): the mask is a sprite in front of
  the background and would cover the window. For the window to show, the game puts an actor in **state `$50`**
  (`$2EB6,X`; bits `$25`, drawing 0, 8 sprites behind the background) before the mask in the list, at the same
  position. Path: `$81:6148` (`$3E` = 0 and bit 0 of `$4E`, 1st window) far-calls `$7F03` of the area program =
  `$86:7322` (reserves 5 VRAM pages at `$7677`, clears them, `LDA #$50 / JSR $4851`, number in `$7321`); `$81:6157`
  (last window) calls `$7F06` = `$86:7367` (deletes); on open/close, `$80:4F78` sends the state-`$50` actors to `$7F00`
  (A = 4) = `$86:737B`, which rebuilds the patterns from the map: 20 16x16 blocks in the ring around Adol (columns
  `$7468`, rows `$747C`, in cells), each cell opaque if the tile number, minus `$5F/$60`, is not less than `$5D/$5E` (in
  the mine, tiles $340-$3BF = lit floor are transparent; the others, the window's, opaque). With no opaque cell, hidden
  (bit 3). A sprite behind the background and in front of the mask: where it is opaque, it beats the mask and the
  background (the window) shows. Measured: the opaque blocks are only those inside the window. In the 32 versioned
  sessions (660 thousand frames), state `$50` in 7525 frames (mine only) and **never** without an open window
  (`pce_trace --windows`: "state $50 characters").
  **1st frame** (the game's own defect, the same on the console): the map already has the window and the sprite table
  is still the old one; it comes out pierced by the mask and with Adol on top for 1 frame. In fill with the HUD at 3/4
  it doesn't show (`redraw_ring`, `fix`).
  **Game running late on open/close** (session `20261103_203809`, frame 4762): building the window takes more than one
  frame (list done at 2862 lines) and the console repeats the sprite table. In the mine, enemies in the dark only
  disappear through full masks (cells `E3F9`, palette 14, outside the floor range); without the mask, they appear in the
  dark.
  Note: here `$81:605D` is `JSR $4F78`, which sends state-`$80` actors to `$81:7D61` (the text in "The game's window
  system" mentions `$80:4E2F` in Ys I and `$81:7DC3`; not investigated, the hooks don't depend on these bytes).
- **Roofs, walls and bushes: masks** (session `20261025_003515`, Ys II village): Adol is a sprite "in front of" the
  background (attribute $108D); the villagers are "behind" ($100C). To hide Adol under a roof, the game places mask
  actors (bits $22, drawings $40-$4E; e.g. a1 and a18) on the **8-dot map cells** around him, before him in the list
  and behind the background: the mask beats Adol and the opaque background covers the mask. Every frame they jump cells
  following Adol (e.g. 120 -> 136), while he moves 2 dots. In the image between A and B, using only B's masks left a
  1-2 dot strip of him above the roof (the far side). The first fix (A's masks pinned to the map) was replaced by the
  game's own rule (below; `runtime/app/ys_masks.c`).
- **How the game builds the masks** (code at $81:7A6D-7D40, checked with the disassembler): each walker X has the mask
  for the current position (`$2B9F,X`) and the one for the next step (`$2BCF,X`; at the end of the step the current one
  goes away and the next becomes the current). The walker calls $81:7A75 (block type 1 = 16x32 dots, 2 cells x 4 rows);
  other types (16x16, 32x16, 32x32, 32x64) via $81:7A71 and via the vector $80:485D. Mask drawing = $40 + slot (type 1),
  bits $22 (bit 3 = hidden). Block: X/Y of the checked position (the actor's + 8 in the step direction,
  $81:7DFE/$7E07) plus the fine camera (`$2C3B/$2C3C`), /8; the mask sits 8 dots below that position. Top row of the
  block with limit 3, each row below 1 less (the feet row: 0). Per cell (2 map entries per row, $80:55B9): tile of
  palette 14/15 (`>= $E000`) or outside `base..base+count` (`$5F/$60`, `$5D/$5E`) = **full mask** ($81:7E18, 8 x $FF:
  the darkness of the caves); inside but beyond the first 256 = empty ($81:7E10); otherwise bits = table (`($52/$53)` in
  bank `$2C52`, e.g. $83:81AC in the village): height (bits 0-2) < limit = empty, otherwise the tile's 8x8 drawing
  (`($2C55/$2C56)+tile*8` in bank `$2C57`, bit 7 = leftmost dot; it can have dots where the background is transparent:
  the mask is black, sprite palette C all $000). `$70` starts at $FF; a tile outside the table clears bit 4, a front cell
  ANDs its bits; if bit 4 remains, the mask is hidden (the game hides nothing). The drawing goes straight to VRAM (one
  plane), in a slot marked in `$364B`. In the frames, the two masks cover everything the rule says; in the dark cave the
  game redraws the lit floor every step and the masks become "stale" relative to the map.
  More details (read from the code): $81:7A75 = mask at the start of the step (A = 4, no offset); at the step decision,
  `$2B3F` = 8 and $81:7A77 (A = direction `$35`) builds the end-of-step one ($81:772C -> `$2BCF`); at the end of the
  step ($81:735C) the start one is released and the end one becomes the start one (`$2BCF` = $FF until the next
  decision). Directions 0-8 ($81:7DFE/$7E07): (0,+8), (0,-8), (+8,0), (-8,0), (0,0), (+8,+8), (-8,+8), (-8,-8), (+8,-8).
  Types 0-4 ($7E2A/$7E2F/$7E34: extra column/row, top limit; $7E25: drawing $00/$40/$60/$80/$90 + slot): 16x16
  (0,-1,1), **16x32 (0,-2,3)**, 32x16 (-1,-1,1), 32x32 (-1,-2,3), 32x64 (-1,-4,7). The mask is the actor at
  (P.x, P.y + 8) and its sprite sits at that position. With bit 5 of `$4E` and bit 6 of `$3D` (bits that are carried
  into the mask's bits), the tables move to the following ones (`$2C56` + 8, `$53` + 1). The game also reads cells
  outside the displayed part of the map (above and below the screen, in the 256-line ring). Actors off screen have no
  sprites (nor do their masks). Measured in the roof scenes: the rule (starting from the position of the start-of-step
  mask, on the step grid) reproduces dot for dot every visible mask of the game and which ones it hides; it only fails
  for some masks above the top edge of the screen (not investigated: they don't show).
  **Ys I** (read from the code in the mine, session `20261102_212804`): bank $81 is different, same logic. Entries:
  $81:7A13 (`LDA #4 / LDY #1`, start-of-step mask; called at $81:75D4 `JSR $7A13 / STA $2B9F,X`), $81:7A15
  (A = direction `$35`, at the decision; $81:772A `JSR $7A15 / STA $2BCF,X`), $81:7A0B (type in `$3078`; vector
  $80:485D); X = walker slot (`$2736,X` = the actor); creates the mask actor ($4CF5, A = $80; `$307A` = its number or
  $FF), position P + 8 in `$2FF8/$3038`, drawing `$365B` + `$7DC3,X`, bits $22 | `$3D`; at the end (`$81:7AB4`) bit 3 of
  `$2F38,Y` by the bit-4 rule ($70); returns the number in A, which the walker writes to its tables. Why the rule didn't
  reproduce all of them (186 "refusals" in the mine): (a) the game builds the mask with the map of that moment and then
  redraws the lit floor around Adol (the cells change: the mask becomes "stale"); (b) the end-of-step mask below the map
  area gets no sprite (actor/mask off screen), without bit 3. That's why the PC uses the **dots the game drew** (the
  mask's sprites), not rebuilt ones. At the step decision the game already shows the end-of-step mask (it covers the
  actor's head, above the start-of-step mask): it goes along in every image of the frame.
  **Window open**: the window cells (palette 15) produce a full mask; whoever walks under it during the conversation
  gets full masks. In fill (shrunk window) the PC shows the map from before the window there: those masks (type 1 ones
  touching a window) are rebuilt by the same rule with that map (`ys_masks_unwindowed`).
- **Floor redrawn while the image is output** (session `20261024_190512`, Ys II dark cave): every step the game writes
  floor tiles near Adol on the map during the image; the frame comes out "torn" in 42-111 dots (the same size as the
  conversation window's, 48-156). **Ys II sewer** (session `20261103_224821`, 9031-10097): the area program
  (`$86:73D2`, via `$80:4012`/`$80:4288`) writes ~76 cells around Adol per frame while walking; at the step decision
  (`$2B3F` goes from 0 to 8, nothing moves; logic of 220-230 lines) there are 113 or more, plus the new column
  (`$80:557A`, 32 cells), already while the image is being output. Every 4 steps (20 frames) the logic exceeds 263 lines
  and the console repeats a list (the "2-0-4"; `motion_fix_late` fixes it: 61 of 61). The tear says nothing about the
  image: frames with 34 thousand torn dots can be identical to the previous one (the game swaps the map with the screen
  already dark).
- **16-bit camera X**: `$2C33` goes past 511 (e.g. 64792 = -744 in FAST in the field); the ring only uses the low
  9 bits.
- **Player numbers** (measured 2026-10-27 with `--dump-state` on 3 Ys I frames and 4 Ys II frames, checked against the
  panel): the same addresses in both games for **EXP** (`$2ABB-$2ABD`, 24 bits), **next level** (`$2C4A-$2C4C`, 24
  bits: `$82:6FB7`, `CMP/SBC/SBC`; Ys II: 30000 -> 33000 -> 36000 on level-up, with the level in `$2C45`: 10, 11, 12)
  and **GOLD** (`$2C41`, 16 bits). H.P at `$2C43`: 16 bits in Ys I (`$2C44` = 0), 8 bits in Ys II, with M.P at `$2C44`;
  max H.P at `$2C48` (Ys I: 16 bits; Ys II: 8 bits, with max M.P at `$2C49`; equal in the 4 frames). The Ys I panel
  shows `current/next EXP`; Ys II's, only EXP, and H.P without the maximum. The H.P bar on the panel has **1 dot per
  hit point**: the lit part (yellow + red) is the max H.P in both games (up to 255, columns 72-326). The crop's HUD reads
  NEXT from RAM (`crop_next_exp`) and the bar's maximum.

## Enemies: HP, EXP and GOLD (measured 2026-10-27)

- **Each enemy's HP**: walker tables (index X, like `$2736,X`): `$2A36,X` = current HP, `$28E6,X` = max HP (8 bits;
  0 = no HP: Adol, actors that don't fight). The same addresses in both games. Ys I, session `20261017_153505`
  (frames 24000-39000, the opening field): 67 kills, in all of them exactly one slot reaches 0 and its actor
  (`$2736,X`) leaves; HP 16 (hits of 8) and 50 (hits of 10), max = the starting value in 64 of the 66 (the other 2 had
  already been hit before the measured window). Ys II, session `20261024_084643`: 5 kills, max 219, hits of 32. The
  panel's ENEMY bar keeps what it shows in `$366C` (HP) and `$366D` (max).
- **Gains**: EXP (`$2ABB`) and GOLD (`$2C41`) rise on the same frame the HP reaches 0 (Ys I: +1 to +3 EXP, +6 to +14
  GOLD in the field; Ys II: +45/+45). They also rise when loading a save (thousands, together with the room change):
  not a gain.
- **Reward routine** (read from the code; `runtime/app/ys_reward.h`): Adol's hit on the enemy is a routine of the main
  program in bank $81 (window 3; Ys I from $81:7901, Ys II from $81:794B), with X = `$22` = slot of the walker hit:
  damage = Adol's strength (`$2C46`) - enemy defense (`$2946,X`), minimum 0 (Ys I: doubled without magic, `$2C74` = 0,
  up to 255); HP `$2A36,X` -= damage. If it reached 0 ($81:7959 Ys I, $81:7998 Ys II): sound $17, state `$2826,X` = 2,
  `$2B6F,X` = 5, and it pays out, via the far call `$4B69` (bank `$30` = $82 in window 3;
  `JSR $4B69 / BRA *+4 / .dw target`), first the walker's GOLD (`$2976,X`, 8 bits, in `$00/$01`; target $6FE9 Ys I,
  $7030 Ys II) and then the EXP (`$29A6,X`, halved for each level of Adol (`$2C45`) above the enemy's level
  (`$29D6,X`), minimum 1; target $6FA7 Ys I, $6FEC Ys II). The bank $82 sums: 24-bit EXP up to 99999 (`$01869F`),
  stored at $82:707C (Ys I) / $82:7145 (Ys II) with `STY $2ABB / STX $2ABC / STA $2ABD`; GOLD up to 65535, stored at
  $82:7097 / $82:7160 (`STY $2C41 / STA $2C42`). After the EXP comes the level-up (sound $25, HP filling up, frame
  waits). Searched across the whole disc: these are the only calls to those sums; everything else that writes EXP/GOLD
  is a new game (GOLD 1000, EXP 0), Ys II starting from the Ys I save (EXP above 99999 becomes 10000), the shops (GOLD
  minus the price) and the loaded save. The same sums pay **event rewards**: the area program (bank $86) calls the
  program vector `JSR $4806` (-> `$4B69`; target `$4848` in Ys II, which jumps to $6FEC) and adds the EXP gradually, by
  1, 10, 100 at a time, one step every 5 frames with sound $1A and $1B at the end (Ys I: Sara giving the crystal,
  session `20261017_153505`, frames 43162-43252, $86:7A57; Ys II: $86:68E6, session `20261024_004620`, frame 60531).
  These have no kill before them (the hook only counts them).
  In the 30 sessions: 771 gains identical to the old approach (same value and place, 1 frame earlier), 1 to 2 points
  of difference, and 6 times two enemies dying on the same frame: the old one merged them into a single gain (+2 EXP),
  the new one shows one on each enemy.
- **Screen position**: sprite at X = x0 + (table X - 32) and Y = (table Y - 64) - raster0 (x0 = 8 and raster0 = 13 on
  the map: the border corner, sprite at 32,88, lands at column 8, line 11). The actor may have 16x32 sprites with an
  empty top part: the enemy's top comes from the lit dots (`runtime/app/battle.c`).

## Equipment on Adol (measured 2026-10-25)

- **Equipped**: `$2C71` sword, `$2C72` shield, `$2C73` armor, `$2C74` magic (Ys II); number of the piece within the
  type (0, 1, ...), `$FF` = none. Before the game starts these bytes hold garbage (intro): they are only valid with the
  engine running. Ys I, session `20261017_153505`: all `$FF` at the start; sword 0 at frame 21166, armor 0 at 21460,
  shield 0 at 41259. Found via the Ys II equipment screen (session `20261023_230635`, frame 7373): routine $82:62AB reads
  `$2C71,X` (X = type, `$2C77`) and adds the type's first item (table $82:6321: 0, 6, 12, 18) into `$33`; $82:6BF0 draws
  the icon of item `$33`. Items the player owns: bits in `$2ABF` (tested at $80:5A42).
- **Order of the pieces** (name table on the disc, track 2: Ys I at 0x34259C, Ys II at 0x6B14A3; common words like
  SWORD/SHIELD/SILVER/BATTLE come from a list and are inserted by number): Ys I swords Short, Long, Talwar, Silver,
  Flame; shields Small, Middle, Large, Silver, Battle; armors Chain Mail, Plate Mail, Reflex, Silver Armor, Battle
  Armor. Ys II swords Short, Long, Talwar, Hyper Cutter, Battle, Clelia; shields Wood, Small, Large, Iron, Battle,
  Clelia; armors Chain Mail, Breast Plate, Plate Mail, Reflex, Battle Armor, Clelia Armor. The equipment screen shows
  English names with different spelling ("Chain-Armor"), written in the game's font (they can't be found as text on
  the disc).
- **Small Adol** (actor 0): one 16x32 sprite (attribute $108D: sprite palette 13, in front of the background), hair and
  face on lines 5-12, body 13-22, shadow 22-26 (outline and shadow in color 14). Colors: blue armor 1, 8, 5, 7 (+ 6 for
  highlights), red clothing 9, 4, 2 (shoulders, sleeves), skin 10, blade 11. Drawing (`$2EF8`) = direction x 4 + step;
  standing, step 1, standing, step 2. Ys I: the 12 frames are in VRAM as drawings $D0-$E5 (front $D1/$D0/$D4, back
  $D8/$D5/$D9, facing right $DD/$DC/$E0, left $E4/$E1/$E5; drawings $28-$5F use the same ones, with masks). Ys II: the
  game copies only the current ones to $180-$185.
- **The game already swaps Adol according to the equipment**: three sets of 12 frames, with nothing (start of Ys I),
  with a generic sword (blade in color 11, 3 to 10 dots, with outline; hilt in color 3 next to the hand) and with sword
  and shield (Ys I, session `20261017_153505` at frame 43950, after the Small Shield: drawings $180-$195, the same as
  $D0-$E5 plus $B0). The game's shield: large from the front (on the left arm, ~5x9, face 1/8/5, rim 7, middle band
  13/3/12), from the back only the outline (on the left), from the side a curved profile in front of the body (5/8/7,
  crossing the sword). In step $184 the blade comes in color 7 instead of 11. The armor never changes (always the blue
  one). Ys II uses the same with-sword frames as Ys I (the with-shield set was not checked in II).
  Error corrected on 2026-10-27: this used to say the shield never appears (only the first two frame sets had been
  seen).
- **Equipment screen icons**: when the menu opens, the game decompresses the 16x16 icons into VRAM (4 tiles each, in
  the order top-left, top-right, bottom-left, bottom-right; general routine $80:5000-50CD), all in background palette
  10 (BAT = $A000 + tile). Ys II (session `20261023_230635`, frame 7450): from tile $650, 6 swords, 6 armors, 6 shields,
  6 magics (item -> icon table at $82:6EA3). Ys I (session `20261021_214858`, menu open at frame ~22560; found by
  watching `$2C77`, which only the equipment screen uses): from tile $670, 5 swords, 5 shields, 5 armors (the equipped
  sword, 2, matches icon 2). Same-named icons differ between the two games (e.g. Ys I's Chain Mail has no red
  shoulder pads; Ys I's Reflex is blue with gold, Ys II's a light-blue crystal). Adol's swords and shields were
  hand-drawn based on them (`tools/equip/equip_art.py`; to see the icons: the preview `equip_preview.html`, which puts
  them next to each piece).
- **In the PC window (measured 2026-10-27)**: Adol's frames are **not loose on the disc** (128 bytes of tiles searched
  in the data tracks: nothing; they must be compressed). With a shield, VRAM $D0-$E5 no longer holds the sword-only
  frames (other drawings), so the shield's dots can't be extracted on the fly: that's why the 36 frames are taken from
  the disc at build time (`tools/equip/equip_frames.py`, session `20261017_153505`). **Ys II uses the same frames**
  (with shield, checked in session `20261024_160656`, frame 5000: drawing $EC, identical to Ys I's $194). **Which
  game**: the table of each type's 1st item at $82:6321 (CD RAM, position $4321) is 00 05 0A 0F in Ys I and 00 06 0C 12
  in Ys II (before gameplay: zeros). In the left step with shield (drawing $195) the sword is entirely behind the body
  (no blade dots).
  Any combination works (including no piece): once the frame is recognized, the window draws from the frame set of
  what is in RAM (no sword / with sword / with shield; with shield and no sword, the with-shield one without the blade),
  in the same direction and step; it also covers piece swaps, when VRAM changes after RAM. To see it on the game's
  frame: `ys_trace --equip --equip-force E,S,A` (255 = none; only changes the drawing).
- **Read from the game (`equip_colors`, `redraw_equip`)**: the sprite is the hero actor's (`YsList.hero`, in "Engine
  memory map"); the drawing is still compared with the 36 frames, because that tells which direction and step he is in
  and whether he is in normal form. Colors: full color or a single level and brightening from the fade routine, with the
  source equal to the reference palette (each channel value comes from the game's table, including those the palette
  doesn't use); all 15 colors equal (Adol all white or black): the equipment too; any other palette: Adol comes out as
  on the console. Checked with `--equip-log` over the 129 sessions (2,804,504 frames): Adol is the same in the 2,263,160
  frames in which he appears; no value the old approach knew changed; 16,571 frames differ only in values it deduced.
- **Other forms and effects of Adol** (identified by the user, 2026-11-04; Adol comes out as on the console, as
  before): **blue monster with red hair** (the Ys II curse, ice town and Solomon temple scene, undone with the chalice;
  attribute $108D; session `20261031_195127`, frames 3658-9693) and **yellow monster** (the staff; attribute $108D,
  drawings `$E0`/`$E5`; session `20261103_232204`, up to frame 23401). When returning from the yellow one, normal Adol
  appears for a few frames with the monster's palette (`1FF 1F8 000 090...`). **Glow at the statue** (session
  `20261024_004620`, frames 18807-18887): the game swaps colors every 6 frames, outside the fade routine; during it the
  console's Adol is shown, without the pieces. **Ice reflection**: see the item below.
- **How the game sets Adol's form** (research of 2026-11-05, session `20261031_195127`; not yet used by the window,
  which recognizes the form by the 36 frames in VRAM): the hero's pose (`$2EF8`) and bits (`$2F38`) are **the same** in
  normal and monster form (pose 00-0F, bits `$20`); no byte of main RAM changes with the curse, except the **drawing
  base of set 0** (`$2E4C/$2E54,Y`: `$01B0` normal, `$02A0` monster; changes at frames 3644 and 10074, the start and end
  of the curse). Who writes the base: `$80:4CCD` (STA `$2E54,Y` / `$2E4C,Y`, Y = set), called at `$80:5002` after
  decompression. The routine that loads a set's drawings is `$80:4FBD` (programs `$08` and `$30`, loads 103 and 108;
  `$0E` = set, `$0F` = palette, `$1E/$1F` = 6-byte descriptor: tile table, palette, drawing source; the pointer goes
  through `$4054`: bank = `$FFF5` + 3 + bits 5-7, offset = bits 0-4, window `$8000`); decompression is `JSR $5693` at
  `$4FFC`, with the source in `$10/$11` (converted again at `$507B`). The curse (area code at `$86:6200`, called from
  `$81:7050-7163`) goes straight into decompression: the descriptor (`$3173`, bank `$84`) and the source (`$86B7`) are
  **the same** as normal Adol's; what changes is the file the game put into CD RAM in place of the source. To do: read
  `$507B` to find the physical address of the source and the disc file the curse loads there (and the yellow
  monster's); with that the window would know the form from what the game loaded, and the direction and step would come
  from the pose (`equip_ice_pose` already does this on ice).
- **Ice reflection** (measured 2026-11-04 in session `20261023_000904`, Ys I: stretches 12652-12862, 12947-16060,
  38798-40632, 40717-41091 and 58026-63767, all on the ice floors of Darm Tower): there is no separate sprite. The
  reflection is lines 24-30 of Adol's own 16x32 sprite, always tied to the drawing the engine requests:
  - **How the game requests it**: the hero actor goes from drawing `$2EF8,X` = 00-0F to **10-1F** (same direction and
    step: direction = bits 2-3, step = bits 0-1). The tile table ($80:4DDB, pointers in `$2E5C/$2E64,Y`, relocated to CD
    RAM: position = `$6000` + 16-bit address; sprite pattern = tile + base `$2E4C/$2E54,Y`; palette = value +
    `$2E6C,Y`) gives frame base+`$18` and palette 14 instead of 13 (attribute `$108E`). The base changes with the scene:
    `$D0`, `$148` or `$170` (hence drawings `$E8`, `$164`, `$188`...). In other scenes the table is different and 10-1F
    are other drawings, in palette 13 (seen in session `20261017_153505`: one shifted piece or three pieces; not
    investigated what they are).
  - **The frame**: apart from the retouches, it is the normal frame without the shadow (outline dots of lines 21-23 and
    all of the bottom shadow), with color 11 (blade) as 13 and 13 as 1 (blade index 7 painted in color 7 also becomes
    13); in lines 24-30, seven lines of Adol himself without shadow, bottom to top, with no column mirroring: 24←22,
    25←19, 26←17, 27←14, 28←11, 29←9, 30←6, remapping the index: 1→8, 2→B, 3→C, 4→2, 5→7, 6→6, 7→7, 8→5, 9→4, A→A, B→D,
    C→6, D→1, E→1. Palette 14:
    `0DB 087 070 0F9 068 187 1FE 1D7 10F 020 17C 0B8 138 1FF 00B 000`. Color 0 doesn't appear (transparent).
  - **Checked** (`tools/equip/equip_ice_preview.py`): in the 12 with-sword frames (already in the field's VRAM,
    `$E8`-`$FD`, sword only) and the 12 with sword and shield (only in the ice VRAM), the rule gives the disc frame
    minus 7 hand-retouched dots (`ICE_RETOUCHES`). **Without a sword**: no session on ice; it's unknown which drawings
    the game loads there.
  - **In the game** (`equip.c`: `equip_ice_pose`, `equip_ice`): Adol is recognized by the drawing the engine requests
    and by the palette, not by the dots: the same rule with the equipment image. Piece colors: the game's by index; the
    others via `ice_top_color` (green +1) and `ice_reflect_color` (blue: green +3, red +1; red: red +2, green +1; green:
    all three +1), our own choice imitating the game, approved by the user. Checked with `--equip-log` on the session:
    11,272 frames, never more than 3 dots off the rule (the retouches). On certain edges the game itself hides the legs
    and the reflection with masks (e.g. frame 39934): the same in the window.
  - **Gap in Adol facing away**: in the standing back drawing the body ends on line 22 and the reflection starts on 24:
    it's the game's.

## Armor and shields: drawing rules
- **Face:** eyes at (7, 11-12) and (10, 11-12) from the front; the chin reaches column 10 on line 13. The game's shield
  starts next to it, at column 10-11; in a front step it goes down one dot to the left. That's why the skin and the
  outline around it (`head_guard`) always come out as in the game.
- **Body parts:** they can't be deduced from the colors (red is clothing, arm or leg depending on the frame);
  `ARMOR_ROLES` marks them by hand in the 12 frames. The 3 frame sets have the same body; anything left over goes by
  line (`armor_role`).
- **Shades:** index 1 is the dark that separates the legs, and 5 on the chest, with 7, forms the sprite's light "Y":
  merging 1 with 8 or erasing 5 removes the body's shape (measured against the original: light and shadow correlation
  from 0.22-0.53 to 0.30-0.72).
- **Blade:** from the front and back it runs 2 columns from the body, with 1 outline column between. Shoulder pads,
  spikes and wide blades can't occupy that column.
- **Ice:** the game's table (`ICE_TOP`, `ICE_REFLECT`) only applies to dots that remain the game's; a piece with a color
  equal to one of its palette goes by the piece rule. Yellow `(7, 6, 0)` is index 12, which the game swaps for orange
  on ice.

## Border and panel on screen (measured 2026-10-18, for the crop)
- Game image: 341x242 dots (7.16 MHz mode). Measured in the 3 recorded sessions (6913 frames): the border is identical
  in every gameplay frame, in every area with scrolling and on room changes (it doesn't darken).
- Lines 0-10, columns 0-7 and 328-340: always black. Border outline (a single color, $B69100): lines 11, 25,
  156, 170 and columns 8, 327. Black line around the map: lines 26 and 155, columns 23 and 312.
- Map: columns 24-311 (288) x lines 27-154 (128). H.P/EXP/GOLD panel: lines 179-219, columns 8-327 ("H.P" starts at
  column 8 and the bars reach 327: as wide as the border); 171-178 and 220-234 black.
- Overscan: lines 235-241 (7) repeat the top of the border (11-17).
- The load game screen uses the same golden top, but the rest is different (border all the way down, no panel).
- **Panel: the cells the game writes** (read from VRAM on 2026-11-03: Ys I `20261017_153505` frame 30000, Ys II
  `20261024_084643` frame 20000, save screen `20261020_221359` frame 3850). The image from line 155 on comes from
  background map line `$2212` + 1 (255 + 1 = 256), column `$220E` (0); the panel is the cells of map rows 35-40
  (64x64), columns 0-39 (image: lines 179-226, columns 8-327). Tiles (palette 15), identical in both games:
  empty 256; text (row 35): H . P = 257 258 259, E X P = 260 261 259, G O L D = 262-265, M . P = 266 258 259,
  digits 0-9 = 449-458, ":" = 459, "/" = 460 (Ys I: `020/020`, `00000/00050`; Ys II: `047`, `158/160`);
  row 36: top outline (279, 280 x 32: the cell's last line); 37: PLAYER 267-272, side 282 in cells 7 and 39
  (lit dot in the last column), inside 1 dot per hit point: 506 = 8 yellow, 507 = 8 red, 508 = the tip one (redone by
  the game: yellow/red/off); 38: 284 (corner), 285 (PLAYER's bottom outline on the 1st line and ENEMY's top on the
  last); 39: ENEMY 273-278 and the sides; 40: 288 (bottom outline on the 1st line) in cells 8-39: cell 7 stays empty
  (the missing corner of the ENEMY bar). On the save screen ("Which to save?") the game rewrites the digits in
  **palette 5** (green; the "/" stays in 15): that's how the HUD knows they belong to the selected save.
  `app/ys_panel.c` stores the cells and their dots every frame; the HUD (`crop.c`) finds labels, numbers and bars from
  them (formerly, from the image colors). With max HP 255 the bar reaches the last cell, and its tile (511) is the tip
  joined with the side, in place of 282. Checked over the 142 sessions (`pce_trace --crop-log`, "panel from the game";
  ~3 million gameplay frames): identical to the old approach 98.4%; different with the screen darkened ~30 thousand
  (white stops being white: the old one lost the bars and thought it was the save screen) and where the old one found
  nothing ~18 thousand; with the screen lit ~2 thousand, all errors of the old one: white flash in combat (e.g. session
  `20261017_181314` frame 62588: the panel background goes white and the white numbers vanish, ~1,570; the new HUD
  draws only the tiles' dots), scenes with dimmed colors (white becomes gray, ~400), the scroll switch 1 line late
  (~30) and ~80 single frames where the game changes the enemy bar or the digit palette after the image has passed the
  panel (the cells are read at the end of the frame).
- **Save screen: what the game does with the numbers** (measured 2026-11-04 with `pce_trace --panel-log`, Ys I
  `20261020_221359` frames 3807-4249, Ys II `20261103_213911` frames 25831-26400):
  - **While listing the saves** (about 8 frames, 1-2 per save, while writing the names), the game passes each save
    through the **player's** H.P (`$2C43`), EXP (`$2ABB`) and GOLD (`$2C41`) and restores the player's at the end. The
    panel doesn't change, and neither do the next level (`$2C4A`) and the max (`$2C48`). Anyone reading those three
    from RAM in those frames sees another save's numbers: the HUD's NEXT came from RAM and flickered (Ys I: 197 ->
    1619, 912, 224; Ys II: 2110 -> 52087). NEXT is now `$2C4A` (3 bytes) minus the EXP written on the panel.
  - **With the cursor on a save**, the digits are in palette 5 and the game leaves that save's next level in `$2C4A`
    and max H.P in `$2C48` (Ys I: 16 bits; Ys II: H.P in `$2C48`, M.P in `$2C49`; a Ys I save listed in Ys II: 145 in
    `$2C48`, 0 in `$2C49`), on the same frame as the digits, and restores them on leaving. `$2C43`, `$2ABB` and `$2C41`
    remain the player's. The game does **not** change the bar: on the console, 024/065 appears with the player's bar
    (79 of 79). The HUD (compact, wide, fill) draws the bar from the save's HP, in the colors of the game's bar (tiles
    506/507 in the cell's palette: `bar_yellow`, `bar_red` in `ys_panel_read`), and NEXT in the usual place, in green.
    Full and Frame modes show the console's panel.
- **What says the screen has a border (read 2026-11-03)**: the disc program that is running. The resident loader
  (`$80:40E7`, `PHX; JSR $4165`) receives in A the number of the next program; the 1st (the boot one) is called
  directly at `$40DC` (`JSR $4800`). Programs seen in the 30 sessions: `$04` intro, `$01` load menu (border down to line
  234), `$28` Ys I start, `$08` Ys I game, `$20`/`$24` Ys I ending, `$30` Ys II game. Only `$08` and `$30` show the map
  with the border and the panel, and in all their lit frames the image has that shape, except: the late scroll switch
  (centipede boss), the border being drawn on entry (only the side bars, 1 frame) and, at the end of Ys I, the white of
  the flash (33 frames, still in `$08`, before `$20`). In `$01`, only the last 4 frames (the border being erased) don't
  have the shape. The old approach (by the image, with 30 frames of tolerance) left the first 26 frames of the Ys I start
  (`$28`) cropped like the load screen.
- **Screen split without the game's interrupt**: in gameplay the BIOS vectors are `$2202` = `$4A0D` (the game's
  interrupt), `$2208` = `$4B2F` and `$220A` = `$4B3A`. In some stretches the game's VBlank (`$4A18`) and its line
  interrupt (`$4AC6`) don't run (e.g. session `20261020_205639`, frames 3046-3130, right after entering, with the map
  black and `$2210` changed from $148 to $68 on the previous frame) and the border and the panel stay on screen: the
  `ys_scroll.c` hooks see nothing (that's why the split can't tell whether the screen has a border). Not checked:
  whether it's the BIOS that splits the screen there (bit 1 of `$F5` clear; it uses `$220C/$2210`, `$220E/$2212` and
  calls the hooks `$2208`/`$220A`). The load menu has its own vectors (`$489B`, `$49AB`, `$49B6`).

## Disc files and loader (read from the code, 2026-11-02)
Basis of the session-independent recompiler (`games/ys/disc.toml`, `tools/recomp/pcerecomp/discmap.py`).
- **Boot**: the IPL (sector 1 of the data track, LBA 3597) asks to load 2 sectors of file 0 at $4000 (banks $80-$84 in
  windows 2-6) and jump to $4000. The real BIOS first runs the code of sectors 0-1 at $2800 (calls $2B26). The data
  track has a 2nd identical copy starting at sector +4096, which the game doesn't read.
- **File 0 (resident, RAM $0000-$0FFF = $4000)**: jump table at $4000-$406B (the game's system routines) and the
  **loader**: file N = sector 3596 + (N + 2) * 32 (blocks of 32 sectors); size in sectors in the 92-byte table at $4181
  (routine $4165). Entries: $40E7 program (loads at $4800, banks base+1..+4 in windows 3-6, and jumps to $4800; the
  program returns in A the number of the next one: loop at $40E2), $410B area (loads at logical $6000 with banks
  $83-$86), $4143/$41FE VRAM, $414D ADPCM. `$FFF5` (BIOS ROM) = $80.
- **Families** (by size in the table): 19 sectors = main programs ($01, $04, $08, $20, $24, $28, $2C, $30, $52, $55;
  RAM $0800-$9FFF; bank 80 in window 2, 81 and 82 alternating in 3, 83 in 5). 16 sectors = areas (code in bank 86, in
  window 3; pairs: even number = code, odd = graphics; $0C-$1E Ys I, $34-$5A Ys II). The sessions up to 2026-10-31 ran
  code from 7 programs and 21 areas.
- **Actors**: $401E registers an actor (routine in $20/$21, bank = the one in window 3). The dispatcher jumps through
  `JMP ($26A6,X)`/`JMP ($26D5,X)` at $42A0/$42A3. "Wait for the next frame": `JSR $4024` (continues after the JSR),
  `JSR $4021` (with a wait), `JSR $402A / BRA *+4 / .dw routine` (continues at the routine). Area actor list (routine
  $578A of the Ys I program): records [routine, priority (FF = end), bank - $80, ...].
- **Far call** in the large programs: `JSR $4B63|$4B69|$4B6F / BRA *+4 / .dw routine` maps into window 3 the bank from
  $2F (=$81), $30 (=$82) or $31 (=$86, area) and calls the routine (`JMP ($44)` at $4BB1). Areas have fixed routines at
  the end of their code ($7F03, $7F15, ...) called this way.
- **Area events**: records with two consecutive routine addresses (check and action) and the position (e.g. area $3C
  from $028A of the file). The scene scripts of the small programs ($04, $20, $24, $28) are their own bytecode:
  `14 lo hi` and `04 xx lo hi` call a routine (the full format was not read).
- **Computed addresses**: an actor's 2nd phase sometimes starts right after the `JMP`/`RTS` of the 1st (e.g. $7C97 in
  bank 82 of the program) without being written anywhere; the recompiler tries the address after every
  `RTS/RTI/JMP/BRA` (a guess, verified). Areas also call routines of the loaded program (`JSR $490B`).
- **Session-independent recompiler**: with these rules, of the 56,631 instructions the 55 coverage sessions ran, only
  10 are not found (program $20, bank 81 at $6000); 220 thousand instructions in total.
