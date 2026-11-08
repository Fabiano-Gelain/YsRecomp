# Roadmap

Each item is a verifiable milestone. Mark `[x]` only when its test passes. Finished milestones: one or two lines each in
`docs/devlog.md` (what was done, what was found). Here: decisions, pending milestones in full, and a pointer per phase.

## Phases 0-7 — done

HuC6280 CPU, video, sound and input (1); disassembler (2); C generator (3); lockstep (4); CD-ROM² with the real BIOS,
identical to Geargrafx (5); Ys recompiled (6); Ys without the BIOS file, routines rewritten in C and checked against the
real ones (7). Details in `docs/devlog.md`.

## Phase 8 — PC version of Ys I & II — done (2026-10-18 to 2026-11-06)

User decisions that still hold:

- Settings and saves are **portable**: next to `YsRecomp.exe` (nothing in AppData).
- Menu **in English**, with the **look and font of Ys itself** (read from the player's disc at run time; the package ships
  `menu_font.bin`, see Phase 9).
- **No save states**: only the game's own save.
- **Esc opens the menu**; "Quit" is inside it and asks for confirmation.
- Default display: **fill the screen keeping 4:3, sharp pixels**. Integer scale, bilinear filter and CRT are menu options.
- Executable: **`YsRecomp.exe`**, with an Adol icon drawn by the user (`docs/Icone_novo.png`, outside Git;
  `tools/package/make_icon.py`).
- Rule for the whole PC version: nothing "PC-side" (menu, volume mixer, screen scale, controls, pacing) touches the
  simulated console. The menu is drawn over the game image and the game is paused while it is open, so the usual tests
  (traces, sessions against Geargrafx, lockstep) keep holding. Exceptions are noted in the session (`# menu`, `# dialog`).
- Every game quirk is solved by reading the game (hooks, code, RAM), never by guessing from the image (lists 1-3 of
  "guesses to replace", 8.69-8.88).

### Optional, after publishing (3rd list of what is still guessed; full list in `docs/technical_notes.md`)

Each needs research in the game's routines before programming:

- [ ] **8.89 — Adol's shape, direction and step from the game (equipment).** Today: the 36 frames compared in VRAM
      (`equip_frame_of`) and Adol normal/on ice by palette number (13/14). Research done (`ys_notes.md`, "How the game sets
      Adol's shape"): pose and bits are the same in monster form; what changes is the file the game decompresses for set 0
      (routine `$80:4FBD`, decompression at `$4FFC`, base at `$4CCD`). Missing: read the conversion at `$507B`, find the
      files of normal Adol, the blue and the yellow monster; then the shape from what the game loaded and direction and step
      from the pose (`$2EF8`, as on ice).
- [ ] **8.90 — Equipment colors without a reference palette** (`color_set`): find who writes the colors during the statue
      glow (every 6 frames, outside the fade routine) and when the yellow monster turns back.
- [ ] **8.91 — Windows and scenes without fixed timeouts**: the window box kept for 8 frames (`CROP_WINDOW_OFF_FRAMES`: when
      switching windows one frame has none; find in the window system the sign that another one will open), the scene held
      up to 600 frames (`CROP_SCENE_HOLD_MAX`), the scene by window touching the map edge, and the ghosts of the pink
      borders (`RING_GHOST_FRAMES`: use the windows of the frame whose shown list was built, not 8 frames by equality).
- [ ] **8.92 — Leftovers**: room change by the game's routine instead of a jump over 16 points (`MOTION_JUMP`); black screen
      and GAME OVER text by the routine that writes the colors (`ys_fade.c`, `map_stray`); the area (file `$12`) that one
      session placed outside the disc map's spot (~300 instructions in the interpreter, `86:10E4`, session
      `20261102_202756`).

## Phase 9 — Publish on GitHub (requested 2026-11-05)

Two projects derived from the old `pce_recomp` repository:

- **`PCERecomp`**: the recompilation tool (Python recompiler, console runtime: CPU, video, sound, CD, BIOS in C, checking
  tools), generic, with nothing about Ys.
- **`YsRecomp`**: the PC version of Ys Book I & II (disc map, hooks, crop, HUD, smooth motion, equipment, launcher,
  package), using PCERecomp.

User decisions (2026-11-06):

- Names `PCERecomp` and `YsRecomp`, on the user's **personal** GitHub. The user authorized finishing Phase 9 without
  interaction, so links use the `GITHUB_USER` placeholder (replace before publishing).
- **MIT license on both** (Geargrafx was only consulted). `tools/reference_trace` (links Geargrafx) stays GPL-3 apart.
- New folders `D:\Projetos\PCERecomp` and `D:\Projetos\YsRecomp`, fresh history; YsRecomp uses PCERecomp as a **Git
  submodule** (`external/PCERecomp`; until published, CMake finds `..\PCERecomp` through `PCERECOMP_DIR`). **The AI does not
  commit nor `git init`**: it gives the user the commands. The old repo `D:\Projetos\pce_recomp` is archived.
- **`YsRecomp.exe` ships ready in the Releases** (the `.zip` from `build.ps1 -Package`), **with the launcher images** (made
  by the user from the covers) **and the menu font** (`menu_font.bin`). The generated C only exists with the disc: the
  Release is built on the user's computer, not in CI.
- Go public: recorded sessions (`.txt`), saves (`.brm`), coverage, screenshots/GIFs on the page, third-party docs of
  `docs/hardware` (with their origin), and the **launcher images** (`docs/*.jpg`, user decision 2026-11-06). Never: ROMs,
  BIOS, generated C.
- History: `docs/devlog.md` (English summary). The full Portuguese history and the old roadmap are kept locally in
  `docs/pt/` (ignored by Git) and belong in the archived repo.
- Pages: **README** in PCERecomp; **README + GitHub Pages site** in YsRecomp, the Ys one polished: all features (menu
  options, Fill/Wide, HUD, smooth motion above 60 FPS, smooth fades, CRT, equipment on Adol, life bars, rewards, launcher,
  no BIOS...) and the work done (static recompilation with lockstep against the interpreter and Geargrafx, disc map, BIOS
  rewritten in C, "read it from the game" instead of guessing).
- **After publishing, work continues in the new repos, in English** (code and docs; conversation with the user stays in
  Portuguese).

Steps (each ends with both folders building and the tests passing):

- [x] **9.1 — Split into two folders.** 2026-11-06.
- [x] **9.2 — Nothing about Ys in PCERecomp.** 2026-11-06.
- [x] **9.3 — PCERecomp in English: runtime.** 2026-11-06.
- [x] **9.4 — PCERecomp in English: tools and docs.** 2026-11-06.
- [x] **9.5 — YsRecomp in English: runtime.** 2026-11-07.
- [x] **9.6 — YsRecomp in English: tools, tests and game files.** 2026-11-07.
- [x] **9.7 — YsRecomp docs in English** 2026-11-07 (`ys_notes.md`, `ys_bios.md`, `technical_notes.md`, `CLAUDE.md`, this roadmap,
      `devlog.md`).
- [x] **9.8 — Pages** 2026-11-07: README of PCERecomp; README and GitHub Pages site of YsRecomp with every option and the work done,
      screenshots and GIFs made with `--screen-dump`.
- [x] **9.9 — Final check and publishing** 2026-11-07: a clean copy of both folders builds and passes the tests; nothing forbidden in
      Git (ROMs, BIOS, generated C); commands for the user to create the repositories, the submodule, the Release and Pages.

## Out of scope for now

- Dragon Slayer: The Legend of Heroes (dropped by the user on 2026-10-17; focus on Ys).
