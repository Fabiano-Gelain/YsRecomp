# YsRecomp development log

YsRecomp is a native Windows version of *Ys Book I & II* (PC Engine CD-ROM², 1989) built by **static
recompilation**: the game's HuC6280 machine code is read from the player's own disc image and translated into C,
which is compiled together with a runtime that simulates the rest of the console (video, sound, CD drive). The
player only points the program at a bin/cue folder; **no System Card BIOS file is needed**, because the BIOS
routines the game calls were rewritten in C and checked call by call against the real BIOS.

How it was built:

- **An AI wrote all the code**; the project owner set the goals, played the game, recorded sessions, described
  what he saw and approved each milestone. Every milestone ends with an automated test that proves it works.
- **Correctness first.** The interpreter was checked instruction by instruction, frame by frame and sample by sample
  against the Geargrafx emulator; the recompiled code is checked in **lockstep** against the interpreter (CPU state
  after every instruction, memories, image and sound after every frame). Recorded play sessions are replayed through
  this chain as regression tests.
- **Read it from the game instead of guessing.** The PC-side enhancements (cropping, smooth motion above 60 FPS,
  HUD, smooth fades, visible equipment) started out as heuristics on the output image; over time almost all of them
  were replaced by hooks that read what the game itself is doing (its window system, scroll interrupts, fade
  routines, actor tables, reward routine).
- The PC layer never changes the simulated console (with two documented exceptions, recorded in the session
  files), so the old traces and comparisons stay valid.

Milestone numbers below match `docs/ROADMAP.md`; the full (Portuguese) text of each milestone is in
`docs/historico.md`. Since Phase 9 the generic parts (CPU, video, sound, CD, BIOS in C, the recompiler and the
trace tools) live in the separate **PCERecomp** repository; paths such as `runtime/pce/` or `tools/recomp/` below
refer to it.

## Phases 0-7 — From an empty folder to Ys without a BIOS (until 2026-10-18)

- **Phase 0 — Foundation.** Project layout, hardware documentation (`docs/hardware/INDEX.md` in PCERecomp) and
  reference sources gathered; *Bomberman* (HuCard) picked as the first, simpler target to validate the pipeline.
- **Phase 1 — CPU and minimal emulator (C + SDL3).** HuC6280 interpreter passing all 640,000 Mesen CPU tests
  (all 256 opcodes), plus the HuC6280-only instructions (TAM/TMA, ST0-2, block transfers, CSL/CSH). Banked memory
  map, VCE, VDC (background, sprites, interrupts, timer), joypad and PSG. Verified against Geargrafx: millions of
  identical trace instructions, title-screen frames identical pixel by pixel, 60 s of sound identical sample by
  sample.
- **Phase 2 — Disassembler and analysis (Python).** ROM loader, recursive disassembly from the vectors with bank
  tracking, function detection and `JMP (abs,X)` jump tables read from the ROM, a per-game config file. A coverage
  recorder in the interpreter cross-checks the static analysis (Bomberman: 665 functions, 0 conflicts).
- **Phase 3 — C generator.** One C function per game function; every instruction calls the same opcode routine the
  interpreter uses (so the C inherits the validated CPU), branches become `goto`. A dispatcher looks up
  (bank, address) and falls back to the interpreter for code the game copies to RAM.
- **Phase 4 — Lockstep validation.** Recompiled vs. interpreted execution compared after every instruction (and RAM,
  VRAM, sprites, palette, image and sound every frame), stopping at the first divergence. Bomberman ran ~1.4 billion
  instructions without divergence; scope was reduced on purpose (world 2-2), as it was only the pipeline test.
  Bomberman was removed from the project on 2026-10-18.
- **Phase 5 — CD-ROM².** BIN/CUE reader (43 tracks of Ys, table of contents and per-track CRC identical to
  Geargrafx); System Card 2.0, 64 KB CD RAM and save RAM; CD controller (SCSI commands with seek timing, interrupts),
  CD audio (play/pause/fade) and ADPCM. Ys became playable in the interpreter with the real BIOS: a first 12-minute
  session matched Geargrafx in image and sound over 43,953 frames.
- **Phase 6 — Ys recompiled.** Ys loads its code from the CD into RAM, so the runtime tracks the **disc origin of
  every RAM byte** (drive -> registers -> RAM) instead of hashing. Each load becomes a "virtual bank" analysed like
  ROM; generated functions first check their signature bytes in RAM and drop back to the dispatcher when the game
  rewrites its own code. Five user sessions (50 minutes, boss defeated) ran 5.03 billion instructions and 182,000
  frames in lockstep with no divergence, and matched Geargrafx in image and sound.
- **Phase 7 — No BIOS: just the bin/cue.** Profiling showed Ys uses 43 BIOS routines plus 3 interrupt handlers.
  All were rewritten in C (`runtime/bios/`) and checked by `bios_check`: at each call in the recorded sessions the C
  version runs from the same state and must leave identical registers, RAMs, VRAM, VDC, palette and the same order of
  hardware writes (~200,000 calls checked). A synthetic BIOS image (vectors and sound-driver tables only, no code)
  and a C boot sequence run the disc's own boot program. Known differences: our own font for the save menu and our
  own approximation of the sound driver's factory waveforms.

## Phase 8 — The PC version (plan approved 2026-10-18; game finished by the owner 2026-11-05)

User decisions for this phase: portable config and saves next to the `.exe`; an English options menu with the look
and font of Ys itself (read from the player's disc, never shipped); no save states; Esc opens the menu; default
display fills the screen at 4:3 with sharp pixels.

### First playable PC build (2026-10-18 to 2026-10-21)

- **8.1 — Import the game.** `YsRecomp.exe` without arguments asks for the bin/cue folder once and checks the disc.
- **8.2 — Separate volumes.** CD music, chip effects and voices with their own volume; the window uses Mednafen's
  channel balance, while traces keep the Geargrafx mix (`runtime/pce/mix.c`).
- **8.3 — Options menu with the Ys look**, drawn over the paused game (`runtime/app/menu.c`).
- **8.3b — Skip intros** one screen at a time, with fast-forward.
- **8.4 — Controls.** XInput through SDL and configurable keys.
- **8.5 — Fullscreen and VRR** (G-Sync/FreeSync).
- **8.6 — Performance.** Profiling showed the game code barely matters: ~83% of the time was the hardware clock
  (dot-by-dot video, sound and CD stepping every instruction). Video now runs "lazily" until something observable
  happens, with identical cycle-by-cycle results: 419 -> 890 frames/s on a played session; intro skip 5.5 s -> 2.5 s.
- **8.7 — No sound dropouts when dragging the window**: the console runs on its own thread, the window only shows
  the newest frame.
- **8.8 — Image options**: integer scale, bilinear filter, CRT effect (`runtime/app/present.c`).
- **8.9 — Package**: icon, `YsRecomp.exe` + readme, nothing from the disc; everything built with `/MT`.
- **8.11 — Crop the frame and overscan** (replacing a widescreen idea that was dropped: Ys only updates the map
  columns that enter its 256-pixel view, and enemies off-screen do not exist). Measured: the game's frame border is
  identical in 814 of 814 gameplay frames; the HUD is redrawn as a band (Fill/Wide/Original modes).
- **E2 — More than 60 FPS (smooth motion).** Ys scrolls in a 2-2-2-2-0 pixel rhythm per frame (2-3-3-0 in FAST
  mode); intermediate images interpolate scroll and objects at constant speed with ~1 frame of latency.
- **8.12 — Faster tests**: Geargrafx output cached, Bomberman removed, recompiled-vs-Geargrafx comparisons dropped
  (the lockstep already chains recompiled -> interpreter -> Geargrafx).
- **8.14 — Options menu styled after the game's own menu**, with the game's 8x8 font copied from VRAM.
- **8.15 / 8.16 — BIOS-in-C timing.** The torn half-window frame during portrait dialogues exists with the real BIOS
  too; how much garbage shows depends on where the music timer interrupt lands. The CD-drive handshake loops of the
  C BIOS were rewritten as one BIOS instruction per pseudo-instruction so their timing follows the real one.
- **8.17 — More HUD backgrounds** (later restricted to top-to-bottom dark gradients that suit the gold HUD).

### Bug reports from play (2026-10-21 to 2026-10-25)

- **8.17b — Black screen in Gaban's hut** (no BIOS): the game streams ADPCM straight from the CD with `ad_cplay`
  ($E03F), a routine that had not been needed before; written in C (`runtime/bios/hle_adpcm.c`). **8.17c**: the
  owner confirmed it in play.
- **8.18 — Hide the garbage frame on window swaps** (later replaced by real fixes, 8.72b and 8.81).
- **8.19 — Pop on GAME OVER: it is the game.** It fades CD audio to silence, restores full volume and only ~40 ms
  later sends the new play command, so 40 ms of the old track plays at full volume (same on the real BIOS and
  Geargrafx). Muted on the PC output only (later done via the game's routine, 8.84).
- **8.20 — Fill-mode HUD covering the map**: smaller band with the HUD drawn at 3/4 scale.
- **8.21 — Jump when opening the save menu**: the C save-RAM routine is one long pseudo-instruction, the frame ended
  inside it and the VDC line log was overwritten by the next frame's first lines; the log is now closed at end of
  frame.
- **8.22 — Wobbling light circle in the mine**: the light is black sprites fixed on screen while the lit floor is in
  the scrolled map; interpolation now keeps both in step.
- **8.23 — Launcher**: a start screen in the game's style (pixel starfield, 8x8 font, double-border window) with the
  covers of the three PC Engine Ys games; its sound effects are played by the C sound driver. **8.29 / 8.30 /
  8.47b**: bigger, gold logo, release years only.
- **8.24 — Hiss in Luta Gemma's voice.** ADPCM at 16,050 Hz does not divide 44,100 Hz, so point-sampling (as
  Geargrafx does) holds each value 2 or 3 samples irregularly; the PC mix now averages over time and uses Mednafen's
  2-pole filter. **8.32 — Clicks in the same voice**: the ADPCM decoder wraps around at 12 bits (as in Geargrafx and
  Mednafen); the PC mix clamps at 0 and 4095 instead.
- **8.26 — Music during voiced dialogue: it is the game.** The music is recorded inside the ADPCM voice data on the
  disc; left as original.
- **8.27 / 8.35 — Windows vs. sprites**: the item window stays in front of the tower woman's face, but intentional
  sprites on windows (the pink selection frame in Ys II's equipment screen) stay visible.
- **8.28 — ENEMY bar the same size as H.P in Fill.**
- **8.31 / 8.36 / 8.37 — Smooth motion hitches.** FAST mode's 2-3-3-0 rhythm, and frames the game delays with many
  sprites (2-0-4), made the averaged speed oscillate; the cave "performance" report was really this (the console
  ran at 813 frames/s). Fixed by detecting whole rhythms and straight-line walking.
- **8.33 — Ys I ending flash breaking the crop**: the whole screen brightens one level every 6 frames; the crop now
  recognises the flash.
- **8.34 — Artifacts in the Ys II intro**, caused by smooth motion in the in-between images only (the console's own frames were clean); fixed.

### Smooth motion rebuilt by reading the engine (2026-10-24 to 2026-10-25)

- **8.39 — Memory map of the Ys engine** (`docs/ys_notas.md`). One engine for Ys I and II: camera in three stages
  (`$2C37` logic -> `$2C33` built list -> `$2C2F` displayed scroll, copied in VBlank only when the list is ready, so
  sprites and background never disagree); up to 64 actors with **screen** positions (no world coordinates; the
  camera pushes map-bound actors), drawing `$2EF8`, linked list `$2E74`.
- **8.40 — New smooth motion reading the game**: camera and every actor interpolated from the engine's own tables
  (`runtime/app/ys_engine.c`, `motion.c`); a late frame is replaced by the list the console did not get to show,
  even with smooth motion off.
- **8.41 — Image composed on the GPU at screen resolution** (`runtime/app/gpu_draw.c`): background strips, 64
  sprites and VDC priority rebuilt as textured rectangles.
- **8.42 — A single clock** for any refresh rate, G-Sync/FreeSync or windowed: each image is planned for the time it
  will appear (refresh timing from DWM), and the window paces the console (`runtime/app/pacing.c`).
- **8.43 — Old code deleted** (1,415 lines of image-based smoothing). **8.44**: a CPU-overclock option turned out
  unnecessary.
- **8.45 / 8.48 / 8.48b — Bits of Adol (and his shadow) showing over roofs and walls** during interpolation; fixed
  with the game's mask sprites.
- **8.46 — Strange left map edge scrolling right in FAST**; **8.47** — refresh-rate readings that oscillate only
  change the rhythm after 1 s.
- **8.49 — Chopped monsters in the basement: it is the game** (stacked enemies' masks cut each other on the real
  console too).
- **8.50 — Flicker when choosing in NPC dialogues**: the hidden torn frame was drawn through the wrong path with
  sharp + CRT.
- **8.51 — HUD in houses and shops** in Wide and Fill too.
- **Cleanup (2026-10-25)**: generated C left out of LTCG (runtime relinks in ~12 s instead of ~10 min per exe).

### New features: equipment, HUD, fades, menu audio (2026-10-25 to 2026-10-31)

- **8.52a / 8.52b — Visible equipment on Adol.** An HTML preview to design each armor, shield and sword, then the
  in-game option (`runtime/app/equip.c`, art generated by `tools/equip/`). **8.70 / 8.86**: hand-drawn shapes for
  every sword and shield, armor by body part with the icon colors.
- **8.53 — Same HUD in Ys I and Ys II** (NEXT, max H.P, margins).
- **8.54 — Enemy HP bars and "+EXP/+GOLD" popups** (`runtime/app/battle.c`).
- **8.55 — Equipment and bars stay visible under the options menu** (it shows the last displayed image).
- **8.56 / 8.60 / 8.65 — Music keeps playing with the menu open.** CD music continues and resumes where it was; for
  chip music only the game's sound driver runs on a copy of the console, and the console then receives that sound
  (the session records `# menu` so replays do the same).
- **8.57 — Hiss on the cursor beep**: optional chip filter (PSG integrated at 4 x 44,100 Hz with a 20 kHz low-pass).
- **8.58 — Smooth fades**: palette, pixels and frame move continuously between the game's fade steps.
- **8.59 — Two broken window tests** (test defects, not game defects).
- **8.61 — CD audio dropouts**: not reproducible; sessions now log sound-queue failures. Withdrawn.
- **8.62 — Stalls when a Bluetooth device connects**: with window and audio device both stalled the console
  follows the clock.
- **8.63 / 8.64 / 8.66b / 8.66d — 180 Hz and fullscreen pacing**: highest recent refresh reading wins, draw-time
  estimates per image type, presenting on the refresh even with Windows composition (NVIDIA overlay, Game Bar);
  above-normal process priority; F12 marks a frame in the recorded session.
- **8.66 — Game windows at 3/4 scale in Fill**, with the map around them redrawn from before the window opened;
  menu sound effects using the game's own effects; sound that survives an audio device pause.
- **8.66c — Cut enemies in the Ys II temple**: closed, resolved by 8.69-8.75.
- **8.67 / 8.67b — More CRT presets** (slot mask, shadow mask, glow).

### Replacing guesses with the game's own data (2026-11-01 to 2026-11-04)

- **8.68 — Research: the Ys window system** (window stack `$3E`, window `$3F-$43`, saved background kept in VRAM by
  the game) and who writes the background map; `pce_trace --bat-writers`, VDC write hook.
- **8.69 — Windows from the source**: hooks on the game's open/close routines drive the crop and smooth motion
  instead of image detection (`runtime/app/ys_windows.c`).
- **8.13 / 8.71 — All Ys code recompiled from a disc map.** Instead of growing coverage session by session, the
  disc's file table and loader were reverse-engineered (resident file 0 at `$4000`, file N = sector
  3596 + (N+2)*32, program/area families by size, far-call stubs `JSR $4B63/$4B69/$4B6F`) into
  `games/ys/disc.toml`; the generated C is split into 24 parts. Of 56,631 instructions seen in sessions only 10 were
  missing; ~0.001% interpreted in long sessions. Coverage is now just a check.
- **8.72 — Mid-frame scroll changes from the source** (hooks in the game's raster interrupt).
  **8.72b**: the torn frame inside game windows is repaired rather than hidden.
- **8.73 / 8.73b — Masks and mine light by the game's rule**: masks for hidden walkers rebuilt on the grid around the
  displayed position; the mine light recognised by a hook on its position copy (`$86:7108`).
- **8.74 — Fades, flash and darkening from the source**: hooks on each program's fade routine and on the copy to the
  VCE; every color's origin and level checked against the VCE.
- **8.75 — Frame preparation on its own thread**: off-rhythm images in the temple at 180 Hz went from 25.8% to 0.0%.
- **8.76 — Lean tests** with a hidden window and dummy audio.
- **8.77 — Crop enabled by the running disc program** (hook on the loader `$80:40E7`), not by recognising the frame.
- **8.78 — Window hooks everywhere** (sprites in front of windows, bars hidden by windows), checked on 120 sessions.
- **8.79 — HUD from the panel cells the game writes in VRAM** (labels, numbers, bars), checked on 142 sessions.
- **8.80 / 8.80b-f — Rewards and edge cases**: "+EXP/+GOLD" from the game's reward routine (23 tests that passed on a
  truncated expression were fixed along the way); mine light with an open window; enemies flickering in the dark;
  quick zooms after the save screen and GAME OVER; NEXT not blinking while the save list is read.
- **8.81 / 8.81b — End of the garbage-frame filter**: it guessed from tearing and caused hitches in the Ys II sewer;
  every frame now reaches the window.
- **8.82 — Adol and equipment darkening from the game** (hero creation hook, fade routine). Found on the way: sprite
  copies detected by a VDC counter (a long TIA hid the copy) and fades with VBlank served by the BIOS.
- **8.83 — Intro skip by the intro program's own state** (program `$04`), no pixel counting.
- **8.84 / 8.84b — GAME OVER silence via the game's music routine** (`$80:4244`); border flicker on load screens fixed
  with a hook on the layout switch (`$82:7AF6`).
- **8.85 — Equipment reflected in the ice of Darm Tower**, built with the game's reflection rule.
- **8.87 — Options menu locked during the intro**; image vanishing with sharp filter + CRT grid fixed (alpha-0
  pixels). The green dots after RUN in the intro are the game's own (same in Geargrafx).

### Acceptance and last features (2026-11-05 to 2026-11-06)

- **8.10 — Acceptance**: the owner finished Ys I and Ys II with the PC version **without the BIOS** (2026-11-05);
  sessions recorded automatically and checked with `tools/trace/check_all_sessions.py`.
- **8.88 — Self-modifying jump targets**: JSR/JMP/branch targets the game rewrites are read from RAM at run time
  (the drawing routine at `$612A`/`$61EB` went from ~26,000 interpreted instructions per session to 0). Smooth-motion
  camera follows the game's hero; Ys II max H.P from `$2C48`.
- **8.88b — Screen bouncing on every line of the Ys II ending** in Fill; default settings for distribution
  (Fill, HUD Shadow, no CRT). Darm's projectile speed confirmed as original.
- **8.93 — Instant dialog**: the game's text task with its speed zeroed (the game's own path for instant text). The
  first option that changes the game: applied through `Pce.game_patch` on both sides of the lockstep and recorded in
  the session (`# dialog`).

Still open (optional, after publication): 8.89-8.92, the remaining guesses (Adol's shape and direction from the
loaded files, equipment colors without a reference palette, fixed timeouts in window and scene handling, room-change
detection).

## Phase 9 — Publishing on GitHub (from 2026-11-06)

The old single repository was split into two projects with fresh history, MIT licensed (the Geargrafx bridge in
`tools/reference_trace` stays GPL-3 on its own), with code and docs translated to English.

- **9.1 — Split into two folders** (2026-11-06). **PCERecomp** got the console runtime
  (`runtime/{cpu,pce,cd,bios,util}`), the recompiler (`tools/recomp`), the generic trace and CD tools,
  `tools/reference_trace`, the CPU/console tests and `docs/hardware`. **YsRecomp** kept `runtime/app`, `main.c`,
  `games/ys`, the Ys tools, tests, package and Ys docs; its CMake includes PCERecomp (submodule `external/PCERecomp`
  or the sibling folder via `PCERECOMP_DIR`).
- **9.2 — Nothing about Ys in PCERecomp** (2026-11-06): game-specific behavior enters through generic hooks
  (`pce_trace` extensions, session events, configurable disc-map rules, `Pce.game_patch`, VDC line log/write hook);
  the Ys trace options moved to `tools/trace/ys_trace_ext.c`.
- **9.3 / 9.4 — PCERecomp in English** (2026-11-06): runtime comments and messages, then tools and docs; code proven
  token-for-token identical by `tools/code_same.py` / `tools/py_same.py`.
- **9.5 — YsRecomp runtime in English** (2026-11-07); all 88 tests still pass.
- **9.6 — YsRecomp tools, tests and game files in English** (2026-11-07): `disc.toml` (families `boot`, `resident`,
  `program`, `area`), recorded sessions in `games/ys/sessions/session_*.txt` with an English header (`# game:`,
  `# frames:`, `# motion:`, `# display:`, `# clock:`, `# sound:`, `# marks (F12):`, `# dialog`), save paths made
  relative to the repo, English test names and labels (`quick`, `history`, `package`), `run_bios_check.py --routines`.
  Proof that only text changed: `code_same.py`/`py_same.py`. Tests: 88/88, history 10/10, package 2/2.
- **9.7 — YsRecomp docs in English** (2026-11-07): `ys_notes.md`, `ys_bios.md`, `technical_notes.md`, `CLAUDE.md`, the
  roadmap and this log (the full Portuguese history stays with the archived repository).
- **9.8 — Pages** (2026-11-07): full READMEs for both projects, `LICENSE` and `THIRD_PARTY_NOTICES.md` for YsRecomp,
  and a GitHub Pages site (`docs/index.html`, images in `docs/site/img/` captured from the real game with
  `--screen-dump`, an animated WebP comparing the original rhythm with smooth motion).
- **9.9 — Final check** (2026-11-07): a clean copy holding only what Git would get (plus the user's `roms/` and
  PCERecomp's `reference/`) builds from scratch, regenerates the Ys C and passes: PCERecomp 5/5, YsRecomp 88/88 (the
  Geargrafx-reference tests skip without the optional `gg_trace` build) and package 2/2. Nothing forbidden goes to Git.
