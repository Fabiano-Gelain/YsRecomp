# Technical notes (details moved out of CLAUDE.md)

Look things up here when needed; day-to-day information is in `CLAUDE.md`. Ys research from Phase 8: `docs/ys_notes.md`.

## Phase history (summary; details in `docs/devlog.md`)
- Phases 1-4: validated with Bomberman (HuCard; code and tests removed on 2026-10-18, kept in Git history).
- Phase 5 (CD-ROM²): BIN/CUE, CD-ROM² console, SCSI, CD music and ADPCM; with the real BIOS, Ys runs from the
  intro to Esteria harbor identically to Geargrafx.
- Phase 6: Ys recompiled (`YsRecomp.exe`, formerly `ys.exe`); 5 of the user's sessions (50 min, one boss) identical
  to Geargrafx and in lockstep. ~5% of instructions still run in the interpreter, but they account for ~1% of the time.
- Since Phase 8: time comes from the hardware clock, not from the game's code. Deferred video: `Pce.video_pending`
  only advances up to `vce_quiet_cycles` (next event/phase/transfer/HSYNC); **any new I/O that reads or changes the
  VDC/VCE must call `pce_sync_video` before (and after, if it changes state)**. Check with
  `run_perf_check.py` (tools/trace/README.md).
- Phase 7: Ys without the BIOS file (synthetic BIOS `runtime/bios/synth.c` + routines in C, `runtime/bios/hle.h`);
  boot skips the System Card screen. The generated C for Ys does not include the BIOS banks.
- Phase 8 (PC version, in progress): menu, controls, fullscreen/VRR, border crop, smooth motion, smooth fades, CRT,
  new HUD, equipment on Adol, health bars, launcher; all of the disc's code recompiled from the disc map. Everything
  the window shows differently from the console is **read from the game** (section below).

## PC version: what is read from the game (state as of 2026-11-05)
Rule 6b of `CLAUDE.md`: whatever the PC version changes in the image comes from what the game does (read-only hooks,
RAM), not from recognizing the image. The emulated console never changes; everything lives in `runtime/app/`.

| What | How it is read | Where |
|---|---|---|
| Camera, characters, owner of each sprite, late frame | game engine: the sprite list it builds | `ys_engine.c`, `motion.c` |
| Windows (menus, dialogs, shops): opening, open, closing | hooks on the bank $81 routines | `ys_windows.c` |
| Window in front of sprites, and bars hidden while a window is open | the game's window cells and their sprites | `ys_windows_cells`, `redraw_windows`, `battle.c` |
| Screen with a border (gameplay, load screen) for the crop | disc program running: hook on the loader `$80:40E7` | `ys_screen.c`, `crop.c` |
| Late map/border scroll switch | hooks on the line interrupt `$80:4AC6` | `ys_scroll.c`, `fix_scroll` |
| Torn frame inside windows | per-line VRAM writes + the game's windows; off on frames where the game switches layout (`$82:7AF6`/`$7B2F`) | `game_fixes.c` (`fix_torn_window`), `ys_windows.c` (`layout_switch`) |
| Silencing the old track on a music change | the game's routine that plays a song (`$80:4244`) | `ys_music.c`, `cdaudio.c` (`pc_mute`) |
| Instant dialog (**changes the game**: the only exception to the rule above) | the text task's speed test (`$80:5BD0`/`$58D6`/`$5BD5`, verified by the bytes) zeroed; `Pce.game_patch`, also on the lockstep reference; the session records `# dialog` | `ys_dialog.c` |
| Map around shrunken windows (fill) | VRAM and scroll of the frame before them | `redraw.c` (`redraw_ring`), `crop.c` (`crop_ring`) |
| Roofs, walls and mine light in smooth motion | the game's rule for the masks | `ys_masks.c` |
| Fades, flash and darkening | each program's fade routine and the game's table; copy to the VCE via request `$221F` | `ys_fade.c`, `fade.c`, `crop.c` |
| Enemy health, EXP, GOLD, equipment | RAM | `battle.c`, `equip.c` (`equip_read`) |
| Armor by body part, broad blade, protected face, ice | hand-made map of each of the 12 frames + icon colors; not read from the game at runtime, it is artwork | `tools/equip/equip_art.py` (generates `equip_art.h`), `equip.c` |
| Adol's sprite and equipment colors | character recorded when the game creates the hero; fade routine; direction and step still from the drawing in VRAM (the 36 frames); on ice, from the drawing the engine requests (`$2EF8`) | `ys_engine.c` (`hero_hook`), `equip.c` (`equip_colors`), `redraw.c` (`redraw_equip`) |
| Kill rewards (+EXP/+GOLD): which enemy and how much | hooks on the hit routine and on the game's additions (`$81:7959/7998`, `$82:707C/7097/7145/7160`) | `ys_reward.c`, `battle.c` |
| New HUD and panel bars: labels, numbers, bars, save screen | cells the game writes to the panel, read from VRAM | `ys_panel.c`, `crop.c` |
| Skipping the intro: which screen it is on and when it accepts RUN | the intro program's script and its variables | `ys_intro.c`, `intro_skip.c` |

How the image reaches the screen: the game runs on one thread; each new frame is prepared on another (fixes, crop,
fades, bars, equipment, smooth motion, `PrepSlot` in `runtime/main.c`); the window only uploads the textures and
composes each image on the GPU (`gpu_draw.c`) at the moment it is shown (single clock, `pacing.c`/`vblank.c`), at any
refresh rate.

**Still guessed** (survey of 2026-11-05; the 2nd list is done; the 3rd is in `ROADMAP.md`):
Adol's shape, direction and step for the equipment from the 36 frames in VRAM, and normal/on-ice Adol from the palette
number (`equip_frame_of`, `equip_sprite`; research in `ys_notes.md`, "How the game sets Adol's shape"); equipment colors
compared against a reference palette (`color_set`: disappears in the statue's glow); fixed crop timeouts (window kept
for `CROP_WINDOW_OFF_FRAMES` frames, scene held up to `CROP_SCENE_HOLD_MAX`) and for the pink-border ghosts
(`RING_GHOST_FRAMES`, by equality of the 4 words); the late-scroll fix that gives up when two probe lines do not match
(`fix_scroll`, `FIX_SCROLL_SETTLE`/`FIX_LATE_MAX`); room change by a jump of more than 16 pixels (`MOTION_JUMP`); scene
by a window touching the map edge; black screen by all background colors at 0, and the GAME OVER text by lit colors
outside the fade (`ys_fade.c`, `map_stray`). Removed on 2026-11-05: the smooth-motion camera follows the hero recorded
by the game (before: the lowest-numbered walker standing still on screen), Adol without a health bar is the hero
(before: character 0), and Ys II's max H.P in the HUD is `$2C48` (before: the lit pixels of the bar).
Not guesses (verified): `ring_one_scroll` (reads the scroll the game put in the VDC on each line; in split mode the
border comes from lines 256+ and the map from 0-255, so the test cannot match by chance), palette 5 of the save numbers
(it is the cell the game writes to the panel), the transparent black panel in "Original" (presentation) and the check
of the redrawn frame against the console's (`exact`: turns smooth motion off for that frame, guesses nothing).

## Recompiler and generated code
- Each generated instruction calls the same `hu_op_XX` as the interpreter (`runtime/cpu/huc6280_exec.h`); function
  exits return to the dispatcher (`pce_step`), which checks the actual bank.
- Ys: code is loaded from disc into RAM in "loads" that take turns at the same address; each is identified by its
  position on disc (coverage v2, `runtime/cd/origin.c`); the recompiler reads the bytes straight from the image
  (`tools/recomp/pcerecomp/cdimage.py`). Checking coverage:
  `python tools\recomp\recomp.py coverage "roms\[BIOS] ...pce" games\ys\coverage --cd "roms\Ys ...cue"`.
- Loads and entry points come from the **disc map** (`games/ys/disc.toml`, `discmap.py`), not from coverage: the
  game's file table, each file's family (where it goes in RAM, window of each bank), certain entry points (program
  entry, jump table, far call, `JSR x / BRA *+4 / .dw routine`, `LDA #n / CLC / ADC $FFF5 / TAM`) and **guesses**
  (addresses in the areas' data, immediates before a JSR/JMP, tables loaded into zero page, cutscene scripts),
  accepted only if the code starting at them decodes completely with no invalid opcode or BRK
  (`Analysis._plausible`). Without coverage nothing is "proven": overlapping instructions are all kept, and each
  instruction has a single owner (`functions._partition`, certain entry points first; a function may be entered at
  any point of its body). Code the game rewrites: a write to a fixed address inside an operand of the same load
  (`Analysis._self_modified`); the operand of those instructions is read from RAM at run time and left out of the
  signature (`codegen._live_operand`), also for branches: JSR/BSR/JMP/BRA and conditional branches (the target read
  goes to the dispatcher or is checked). Until 2026-11-05 only for ordinary instructions: Ys changes the target of
  the `JSR $530D` in the drawing routine `$612A`/`$61EB` (`STA $613C`... before calling it), the signature never
  matched and the whole routine fell back to the interpreter (~26 thousand instructions in every session; 287
  thousand in one). Remaining: ~300 instructions of an area (file `$12`) that one session placed outside where the
  map knows it (`86:10E4`). The C is emitted in 24 parts (`codegen.PARTS`, `/MP`).

## CD
- Code in `runtime/cd/` (cdimage, cdrom, scsi, adpcm, cdaudio). Unimplemented parts call `cdrom_unimplemented`;
  `pce_trace` fails in that case, so no test can pass by imitating hardware without a reference.
- CD volume fade follows Mednafen, not Geargrafx (the user confirmed in Mednafen that the goddess menu is silent).
  Sound tests against Geargrafx use `--gg-fader`; `fade_ys` checks the new rule.
- The `cd_image` test needs `gg_cdinfo` (`.\tools\reference_trace\build.ps1`) and takes ~20 s.

## Game files (in `roms/`, never versioned)
- Ys Book I & II (USA): `.cue` + 43 `.bin` (track 2 = MODE1/2352 data, the other 42 = audio). TOC: 326,566
  sectors; data at LBA 3596-11998 (pregap from 3365). Sector 1: "PC Engine CD-ROM SYSTEM", "Ancient Ys Vanished".
- BIOS: `[BIOS] TurboGrafx CD System Card (USA) (v2.0).pce` (256 KB, no header, CRC32 `FF2A5EC3`, reset
  $E0F3). `syscard3.pce` (3.0, 512-byte header, CRC32 without header `6D9A73EF`) for Super CD.

## Reference emulators
- Mednafen 1.32.1 at `D:\Projetos\mednafen\mednafen.exe` (outside the repository), for visual comparison; its source
  in `reference/mednafen_src/` (the whole `.tar.xz` is there; extract only the file you need).
- Geargrafx as a library for traces (`tools/reference_trace/`, build with
  `.\tools\reference_trace\build.ps1`). See `tools/trace/README.md`.
