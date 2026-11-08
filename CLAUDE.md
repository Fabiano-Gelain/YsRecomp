# YsRecomp — Ys Book I & II recompiled for PC (built on PCERecomp)

## Two projects

- **This folder (`D:\Projetos\YsRecomp`)**: the PC version of Ys (runtime/app, `main.c`, disc map, sessions, game tests,
  package). Paths in this file are relative to it unless they start with `PCERecomp/`.
- **`D:\Projetos\PCERecomp`**: the emulated console (`runtime/{cpu,pce,cd,bios,util}`), the recompiler (`tools/recomp`),
  the generic trace tools (`tools/trace`, `tools/cd`, `tools/reference_trace`), `docs/hardware` and `reference/`.
  The CMake here includes it (`PCERECOMP_DIR`: submodule `external/PCERecomp` or the sibling folder); Python scripts find
  it through `tools/pcerecomp_dir.py`. It builds and tests on its own: `..\PCERecomp\build.ps1 -Test`.
- The old repository (`D:\Projetos\pce_recomp`) is archived (it keeps the full Portuguese history, `docs/historico.md`).
  Both new ones go to the user's personal GitHub. **The AI never commits nor runs `git init` in them**: at the end of each
  step, give the user the commands to commit in both folders.

## Goal

Read the binary code of PC Engine / TurboGrafx-16 games and generate C code that, compiled with a runtime, runs the game
natively on PC (Windows). The generated C mimics the CPU instruction by instruction: it need not be readable, it must be
**correct**.

- Final target: an **enhanced PC version of *Ys Book I & II*** (CD-ROM²). The player only points at the bin/cue folder,
  **no BIOS file** (System Card routines rewritten in C). Enhancements: options menu, XInput, >60 FPS (smooth motion),
  border crop (Fill/Wide/HUD), smooth fades, CRT, equipment visible on Adol, instant dialog, launcher.
- During development the real BIOS also runs (`--bios`), as a reference checked against Geargrafx.

## About the user (important)

- Web developer; **does not write this project's code** (the AI writes everything). Does not know assembly, graphics or
  console hardware. **Speaks Portuguese: always talk to the user in Portuguese**, in plain language. Code, comments,
  docs and commit messages are in English.
- Role: set goals, run the game, describe what was seen, approve milestones.
- When a task is done, tell **exactly what to run and what should be seen**.

## Work rules

1. **Nothing moves forward without automatic verification.** Every milestone ends with a test that proves it works.
2. **Do not invent hardware details.** Check `PCERecomp/docs/hardware/INDEX.md` and `PCERecomp/reference/Geargrafx/src/`;
   if it is not there, tell the user and get the documentation first. Geargrafx is GPL-3: reference only, never copy.
3. **Small milestones.** One `docs/ROADMAP.md` step at a time; update its status at the end.
4. **Never version ROMs, ISOs, BIOS or code generated from them.**
5. One commit per milestone that passes the tests (made by the user, with the commands the AI gives).
6. Correctness before performance.
6b. **Complete solutions, no guessing or hacks** (user directive): every game quirk is solved by reading from the game
   itself what it does (hooks, disassembly, RAM), not by recognizing it in the image or patching case by case.
7. **Token economy:**
   - One conversation per milestone: when a milestone is done (commit commands given), say it is time for a new
     conversation and **always** give the ready-to-paste prompt (in Portuguese). Default: "Leia o CLAUDE.md e o
     docs/ROADMAP.md. Próximo passo: o primeiro marco ainda não concluído. Antes de programar, faça de uma vez as
     perguntas de decisão que precisar. Siga as regras de economia de tokens do CLAUDE.md. Ao terminar: rode os testes do
     grupo certo, atualize o roadmap e o docs/devlog.md, me dê os comandos do commit, me diga exatamente o que rodar e o
     que devo ver/ouvir, e me dê o prompt para a próxima conversa." Everything the next one needs must be in CLAUDE.md,
     ROADMAP.md or `docs/`.
   - Before investigating, look in `docs/` and ROADMAP.md for what was already found. Finished milestones: one or two
     lines each in `docs/devlog.md` (grep it, don't read it whole).
   - Prefer Grep and reading excerpts (offset/limit) over whole files; never re-read a file just edited.
   - Images: look as little as possible; use contact sheets (`PCERecomp/tools/trace/contact_sheet.py`) and numbers
     (`lit_pixels.py`, CRCs, `--hashes`) before opening PNGs.
   - Command output always filtered (tail/grep/Select-String), never the whole log.
   - Ask decision questions BEFORE programming.
   - Text with backslashes: only via Edit/Write (or a Python script written with Write), never via shell heredoc/sed.
   - Never rewrite files with PowerShell 5.1 `Get-Content`/`Set-Content`: it breaks UTF-8. Use Edit/Write or Git Bash `sed`.
   - `LNK1104`/.exe locked in `build\Release` may be the antivirus (Kaspersky) holding the file: find out who holds it,
     don't touch the antivirus.

## Where things are

| What | Where |
|---|---|
| Recompiler (Python) | `PCERecomp/tools/recomp/` |
| Console runtime (CPU, video, sound, CD, BIOS in C) | `PCERecomp/runtime/` |
| PC version (settings, disc, volumes, menu, intro, window) | `runtime/app/`, `runtime/main.c` |
| Game config, generated C (not versioned), recorded sessions | `games/ys/` (`disc.toml`, `sessions/`, `coverage/`) |
| Roadmap and status | `docs/ROADMAP.md`; finished milestones: `docs/devlog.md` |
| Ys research (memory, windows, scroll, fades, masks, sound, intro, disc) | `docs/ys_notes.md`; BIOS routines: `docs/ys_bios.md` |
| Hooks that read the game (engine, windows, scroll, fades, masks) | `runtime/app/ys_*.c`; what is read vs. still guessed: `docs/technical_notes.md` |
| Trace/image tools | generic: `PCERecomp/tools/trace/README.md` (`pce_trace` has an extension hook, `pce_trace.h`); Ys ones: `tools/trace/README.md` (`ys_trace_ext.c` = the Ys options of `pce_trace`/`ys_trace`, `check_session.py`, `check_all_sessions.py`, `run_bios_check.py`, `run_perf_check.py`, `run_fade_test.py`). All answer `--help` |
| Equipment on Adol (Python rules, HTML preview, frame generator; in game: `runtime/app/equip.c`) | `tools/equip/` |
| External sources (Geargrafx, HuC6280 tests, Mednafen) | `PCERecomp/reference/` (outside Git; restore: `PCERecomp/docs/hardware/INDEX.md`) |
| User's ROMs, disc and BIOS | `roms/` (never versioned) |

## Build and test

- Windows 11, PowerShell (Bash also available), Python 3.14, VS 2022 Build Tools (MSVC); the Build Tools CMake is not in
  PATH, `build.ps1` calls it. SDL3 3.2.30 is downloaded by CMake on the first build.
- `.\build.ps1` builds (Release, in `build\`). Quiet: only warnings/errors and the test summary; `-Verbose` shows all.
- **The user plays, records sessions and debugs with `dist\YsRecomp\`**: after any runtime change, run
  `.\build.ps1 -Package` and tell the user to run `dist\YsRecomp\YsRecomp.exe` (their sessions: `dist\YsRecomp\sessions\`).
- `.\build.ps1 -Package` tests (label `package`) and builds the player package in `dist\` (`YsRecomp\` + `.zip`; readme
  in `package/README.txt`, icon from `tools/package/make_icon.py`). Everything builds with `/MT`.
- `.\build.ps1 -Test` = label `quick` (~20 s, every change). `.\build.ps1 -TestAll` = everything but `history`, in
  parallel (~11 min, 88 tests; ~6 min are the real-time ones, one at a time): before committing milestones that touch the
  simulated console or the recompiler. `.\build.ps1 -TestHist` = label `history` (~6 min, 10 tests: old bugs of smooth
  motion, crop and sound in long sessions). During work: `ctest --test-dir build -C Release -L console|-L recomp|-R name`.
  A new test goes in a label (LABELS at the end of `CMakeLists.txt`); tests that measure real time or count what the
  window drew go in `TESTS_TEMPO_REAL` (run alone). No test shows a window or plays sound: ctest sets
  `PCE_HIDDEN_WINDOW=1` and `SDL_AUDIO_DRIVER=dummy`; set both when running an .exe by hand to check. `build.ps1` reruns
  once what fails.
- Geargrafx tests reuse its output while game, BIOS, script and `gg_trace` don't change (`PCERecomp/tools/trace/gg_cache.py`;
  `PCE_GG_CACHE=0` forces a rerun). A session's image and sound are checked in all frames at once (`run_session_test.py`,
  tests `session_ys_*`). The recompiled code is checked in lockstep against the interpreter (and the interpreter against
  Geargrafx): don't repeat traces/images of the recompiled code against Geargrafx. Window tests that don't measure time use
  `--turbo`.
- Changing `PCERecomp/tools/recomp/` or the disc map (`games/ys/disc.toml`) regenerates and recompiles the Ys C (~12 min):
  run it in the background. Runtime-only changes rebuild in ~12 s: the generated C (`ys_recomp`) is kept out of LTCG on
  purpose (with it, each link took ~10 min). Don't turn it back on.

## Running

- Player: `build\Release\YsRecomp.exe` with no arguments reads `YsRecomp.ini` next to the .exe (`game_folder`, volumes,
  `chip_filter`, `intro_to_menu`; display: `fullscreen`, `resolution`, `refresh_rate`, `vsync`, `window_scale`; image:
  `integer_scale`, `filter`, `crt`, `crt_brightness`, `aspect`, `crop`, `hud_background`, `crop_hud_names`; smooth motion:
  `smooth_motion` (1 = on, default: `runtime/app/motion.c`, assembled on the GPU by `runtime/app/gpu_draw.c`, frame redraw
  in `runtime/app/redraw.c`; fixes of game defects in `runtime/app/game_fixes.c`; F6 / `--motion-cpu`: GPU <-> CPU);
  `smooth_fades` (`runtime/app/fade.c`); `visual_equipment` (`runtime/app/equip.c`); `instant_dialog` (the only option
  that changes the game: `runtime/app/ys_dialog.c`, `Pce.game_patch`; the session notes `# dialog`); `pause_in_background`;
  `record_sessions=1` records every session in `sessions\` next to the .ini; its `# sound:` line notes sound failures),
  checks the disc, asks for the folder if needed, and opens the **launcher** (covers of the three Ys CD games: I & II, III, IV; Ys sound effects cached in
  `launcher_sfx.bin` next to the .ini; images in `launcher\` next to the .exe or `docs/`; `--launcher` forces it,
  `--launcher-test FOLDER` presses keys by itself, `--launcher-wav FOLDER` writes the effects and music to WAV; Esc/SELECT
  opens the options menu). Saves in `saves\` next to the .ini. `-Package` keeps the .ini, `saves\` and `sessions\` of
  `dist\YsRecomp\`. Esc = menu (Video; Image; Audio; Controls), blocked during the game's intro (`--menu-press` tests it).
  Alt+Enter = fullscreen. `--config FILE` uses another .ini (window tests: set `fullscreen=0`).
- Development: `YsRecomp.exe "roms\Ys Book I & II (USA).cue"` (no BIOS) or with
  `--bios "roms\[BIOS] TurboGrafx CD System Card (USA) (v2.0).pce"` (real BIOS; also for `pce_runtime`, `pce_trace`,
  `gg_trace`). `--gg-mix` = sound mixed as in Geargrafx. `--vrr` = V-Sync off (G-Sync/FreeSync). `--frame-timing` measures
  how long each frame stayed on screen (the user's monitor: 2560x1440, 180 Hz, locked at 120 Hz in Windows/driver, G-Sync;
  the window reads the real Windows rate, `runtime/app/vblank.c`; pacing: single clock, `runtime/app/pacing.c`;
  `PCE_PACE_LOG=1`). `--screen-dump FROM COUNT FOLDER` saves what the window showed (BMP; with the options menu open,
  `m<frame>_<n>.bmp`); `--window-size W H` opens the window at that size. `--audio-wav FILE` records the sound sent to the
  device (not with `--turbo`, which is silent). `--audio-hiccup MS` stops the sound device MS ms every 5 s.
- Sessions: `--record games\ys\sessions` records the buttons (rewritten every minute; F12 marks the frame on screen in the
  `# marks (F12):` line); `--input <session>` replays; check all new ones at once: `python tools\trace\check_all_sessions.py`
  (lockstep; with BIOS, also Geargrafx); one against Geargrafx: `python tools\trace\check_session.py <session.txt>`.
  A session that started from a save carries its `.brm` (`# save:` line, `--bram` in the tools). Game save:
  `saves\<cue name>.brm` (`--save FILE`).
- Check the C BIOS routines against the real one: `python tools\trace\run_bios_check.py` (`--routines E009,E012|all`).
- Ys C = **all the code on the disc** through the map (`games/ys/disc.toml`; `docs/ys_notes.md`, "Disc files and loader"),
  independent of the sessions. Coverage (`games/ys/coverage`) only checks: `python ..\PCERecomp\tools\recomp\recomp.py disc
  <bios> --cd <cue> --map games\ys\disc.toml --check games\ys\coverage [-v]` (test `recomp_disc_ys`). Record coverage of a
  new session (only if `--interp-log` shows missing code): `pce_trace --frame --coverage FILE.txt --input <session>
  [--bram <save>] [--bios ...] <cue> <frames> x.ppm`. What falls to the interpreter: `ys_trace ... --interp-log FILE`.
- Variables: `PCE_CD_LOG=1` (SCSI commands, ADPCM fade and rate), `PCE_AUDIO_LOG=1` (sound queue failures),
  `PCE_RING_LOG=FILE` (sprites that vanish around shrunk windows; the windows come from the game, `app/ys_windows.c`).

## Code conventions

- File paths are UTF-8: open with `u8_fopen` (`runtime/util/u8file.h`), not `fopen`.
- **Nothing about Ys in PCERecomp** (no code, no mention in its comments and docs; Ys only in its README, as the project
  that uses it). Game-specific things stay here and plug in through generic hooks: `pce_trace` extension (`TraceExt`),
  session events (`input_script_record_event`/`input_script_event`: `# menu`, `# dialog`), disc-map rules configurable in
  `disc.toml` (`script`, `routine_records`), `Pce.game_patch`, `Vdc.line_log`/`write_hook`. Check with Grep
  (`\bYs\b|ys_|app/|Adol`) in PCERecomp.
- Nothing that is "PC-side" touches the simulated console (exceptions noted in the session: menu music, `# menu`; instant
  dialog, `# dialog`). Sound mix: `Pce.mix` zeroed = Geargrafx sum (traces and tests); the window uses the Mednafen
  balance times the volumes (`PCERecomp/runtime/pce/mix.h`).
- New frame prepared on its own thread (`runtime/main.c`, "New-frame preparation"): fixes, crop, fades, bands, equipment,
  smooth motion and texture images go to a slot (`PrepSlot`); the window only uploads textures and draws reading the slot
  on screen (`ds`). State carried between frames (`g_prep`) only under `g_prep.lock` (order: `g_prep.lock`, `frame_lock`,
  `slot_lock`); no SDL on the prep thread. `--prep-inline` = the window prepares; `--prep-hashes FILE` writes one hash per
  prepared frame (test `app_prep_ys`: both ways equal). With F6 (motion on the CPU) the window prepares.
- Menu (`runtime/app/menu.c`) drawn over the image, game paused (`g_emu.paused`), with the game's 8x8 font copied from VRAM
  (`runtime/app/menu_font.h`, `menu_font.bin` next to the .ini).
- User directive: **the HUD and the menus (including the options menu) always get the image filters** (sharp/smooth,
  CRT) and the options menu has the game menu's scale (with the crop: `present_overlay`/`present_layer`).
