# Ys trace tools

The generic tools (trace, frames, sound, button script, CD, BIOS routines in C, image study, profiler) are in
`PCERecomp/tools/trace/README.md`. The Ys ones live here: this project's `pce_trace`/`ys_trace` is PCERecomp's with
the Ys extension (`ys_trace_ext.c`; `pce_trace --help` lists the generic options and the Ys ones), plus the scripts
that read the Ys sessions.

| Tool | What it does |
|---|---|
| `build\Release\pce_trace.exe` | PCERecomp's `pce_trace` + the Ys extension (interpreter) |
| `build\Release\ys_trace.exe` | the same with the game's generated C (`--lockstep`, `--interpret`, `--equip`) |
| `tools\trace\check_session.py`, `check_all_sessions.py` | check recorded sessions (below) |
| `tools\trace\run_session_test.py` | `session_ys_*` tests: image of every frame and all of the sound against Geargrafx |
| `tools\trace\run_fade_test.py` | `fade_ys` test: CD volume fade (intro and goddess menu) |
| `tools\trace\run_bios_check.py` | `bios_c_ys` test: BIOS routines in C against the real one over the sessions |
| `tools\trace\run_perf_check.py`, `run_dialog_test.py`, `check_light_window.py` | performance, instant dialog, mine light |

Tests against Geargrafx: `trace_ys_boot`, `trace_ys_run` (PCERecomp's `run_trace_test.py`), `session_ys_boot`
(script `tests/input/ys_boot.txt`), `session_ys_game` (script `tests/input/ys_game.txt`; the sound includes CD music
and ADPCM) and `session_ys_save`.

## Recorded sessions
`YsRecomp ... --record FOLDER` (or `record_sessions=1` in `YsRecomp.ini`: FOLDER = `sessions\` next to it)
records the buttons pressed frame by frame in `FOLDER/session_YYYYMMDD_HHMMSS.txt` (PCERecomp's script format),
rewritten every minute of play and when the window closes. **F12** notes the frame on screen (`# marks (F12): F1
F2...`): the player presses it on seeing a defect, and the analysis starts right at those frames (`--smooth-frames
F-10 20 FOLDER`, `--screen-dump`). Test `app_mark_ys`.
`python tools\trace\check_all_sessions.py [FOLDER ...]` checks every session not yet checked
(default: `games/ys/sessions` and `dist/YsRecomp/sessions`): lockstep of the recompiled code against the interpreter
over the whole session (with the fraction of instructions in the interpreter) and, if recorded with the real BIOS,
`check_session.py`; result in `<session>.conferencia`. Sessions without the BIOS do not have Geargrafx as a
reference (it needs the BIOS; the C routines have slightly different timing).
`python tools\trace\check_session.py <session.txt>` replays the session on Geargrafx and on our runtime at the same
time (`--hashes`) and reports the first different frame, saving its images.
Ys events in the session (script lines `# <name> ...`): `# menu F PIECES SAMPLES` (options menu opened with sound;
the chip sound played in the menu goes to the console) and `# dialog F 0|1` (instant dialog on or off).
`YsRecomp --input` and `pce_trace`/`ys_trace --frame` replay them; Geargrafx does not (it diverges from there on).

## BIOS routines in C
- `python tools\trace\run_bios_check.py [sessions] [--routines E009,E012|all]` (test `bios_c_ys`): runs PCERecomp's
  `bios_check.exe` on the Ys sessions.
- Test `hle_timing_ys` (PCERecomp's `run_hle_timing_test.py`): the session `session_20261020_221359` (recorded with the
  BIOS), replayed with `--hle`, has the image equal to the real BIOS one in every frame from 1 to 11195, except 2881-4248
  (see the ROADMAP).
- `pce_trace --dialog-log`: the text stretches the game writes slowly (text task with speed != 0,
  `app/ys_dialog.h`) and, at the end, the per-letter waits and the speeds zeroed by instant dialog
  (`--instant-dialog` turns it on from reset; the session turns it on with the line `# dialog <frame> <0|1>`).
- `--log-calls 81:61F2`: the font in the windows.

## Ys options in pce_trace --frame
`--bat-writers FILE --bat-routine 81:607E`: the border of the game's window system (test `game_windows_ys`).
`--equip-log` (Adol and the equipment colors from the game against the old way, the stretches with a drawing outside
the 36 frames and a summary; how many frames of Adol on the ice and how many break the reflection rule;
`PCE_EQUIP_SAT_LOG=1` lists the frames with the engine list different from the sprite table), `--windows FILE|-`
(game windows by hooks in the open/close routines, `runtime/app/ys_windows.c`: per frame, each window (cells, page of
the saved background, opening/open/closing, lines drawn, engine screen points) and the window sprites, inside/outside;
`e50`, the state `$50` characters; at the end, the summary, with the check against `$3E` and the start of the game
stack; `--crop-log` turns the windows on by itself (the crop uses the game ones) and notes when the scene starts and
ends and when the crop turns on/off with the game program (`runtime/app/ys_screen.c`); at the end, "screen from the
game": each frame's screen from the program against the border shape in the image (`crop_detect`, the old way), the
black ones with the HUD band lit and what is left on each side (`PCE_CROP_DIFF=1` lists each different frame);
"panel from the game": the HUD from the panel cells (`app/ys_panel.c`) against the old way from the image colors
(`tools/trace/hud_old.c`), equal and different frames (`PCE_HUD_DIFF=1` lists each one); `--panel-log` (without
needing `--crop-log`) notes each panel change: the text written in the cells, what the HUD concludes, H.P, EXP, next
level and GOLD from RAM, the HUD's NEXT (from the panel) against the one computed from RAM, and the bar color read
from the palette against the image one; at the end, also "around the shrunken windows": how many game masks touching
a window were remade by the rule with the map from before it, in how many frames, how many character points they hid
and the frames without the engine list), `--watch-vce FROM TO` (generic: who writes the colors), `--fades` (Ys fades
read from the game, `runtime/app/ys_fade.c`: each step of the map fade, "level N brightening N", with the colors in
the fade and the steps; at the end, the calls seen; `PCE_FADES_DETAIL=1` shows the levels of each palette;
`--crop-log` uses the same levels), `--scroll-log FILE|-` (map/border scroll changes read from the game's line
interrupt, `runtime/app/ys_scroll.c`: per late frame, fixed by `fix_scroll` or not matching, the lines of the changes;
at the end, how many frames were fixed and how many do not match with the screen blanked/lit), `--actors FILE|-`
(`ys_actors.c`: Ys characters read from the game engine; per frame, the camera, the shown list and the built ones, the
logic duration, each character with position, drawing, bits and sprites; at the end, the summary with the late
frames; see `docs/ys_notes.md`, "Engine memory map"), `--game-fixes` (the fixes the window always makes:
counts the frames with the item window put in front of the face; `--crop-shot` comes out with it in front; turns on
the game windows and lists the torn frames inside them, which the window fixes (`fix_torn_window`), and how many still
have a tear inside them after the fix; with `--volumes`, `--audio` without the old CD track; `PCE_TORN_LOG=1` lists
each torn frame with the points outside and inside the windows and the 1st line; `PCE_TORN_SHOT=FOLDER` saves each
fixed frame, `t<frame>.ppm`), `--walk-rhythm FROM TO` (also in `YsRecomp`: how many frames Adol walked, how many at
the rhythm and the stops in the middle of the walk, as smooth motion shows it), `--smooth-check` (smooth motion,
`runtime/app/motion.c`, reading the Ys engine through `runtime/app/ys_engine.c`; the old one, which guessed from the
frame, is gone: the "smooth motion" summary counts the frames the redraw, `runtime/app/redraw.c`, reproduces exactly,
and fails below 99%; the frames with the engine list, the late ones swapped for the list the console did not show,
the 4x image at f = 1 without constant speed, which must be the frame, the late scroll changes fixed at the top and
at the bottom of the map, `runtime/app/game_fixes.c`, the border/panel, outside the map lines, equal to the frame in
every image, and the light out of place, map lit where A and B are dark) and `--smooth-frames FROM COUNT FOLDER` (the
same with 4 images per frame; saves the 4x ones of each frame FROM..FROM+COUNT-1; without `--smooth-check`, smooth
motion is only computed from 30 frames before FROM, fast; FOLDER = `-` does not save the images; in those frames it
also counts the odd points, of a color that neither A nor B has in a 5x5 square, also looked for in B shifted by the
shown camera, and the images with more than 1000, a misplaced piece; `PCE_ODD_LOG=1` lists them all; and the camera
and hero jerks, step |x| + |y| changing by more than 0.05 point, turning is not a jerk; `PCE_JERK_LOG=1` lists;
`PCE_MOTION_LOG=1` lists the shown camera in each image and whom it follows; the masks by the game's rule, roofs and
walls: in how many frames, how many hidden walkers, the blocks with the points the game drew in B, in the 8 frames
before, and by the rule (the game showed none there: a character entering from the screen edge) and in how many images
these were used (`PCE_MASK_RULE_LOG=1` lists; `PCE_MASK_LOG=1` lists every walker in each image, with the blocks and
the game sprites that go out), how many 4x points they change against the game ones only and whether the character
goes out of them more than out of the game ones in B (FAILED); `PCE_BEHIND_LOG=1` lists; `PCE_NO_BEHIND=1` turns them
off; the "mine light" line counts the copies of Adol's position seen by the hook (`YsList.copy_of`); and the map
edges: with the camera moving sideways, without constant speed, the image at f = 0 in the first and last 6 columns of
the map, outside sprites, must be A; `PCE_EDGE_LOG=1` lists). With `--gpu` as well (opens a hidden window), each image
is also built on the GPU (`runtime/app/gpu_draw.c`) and read back: the "GPU" line says in how many the image at f = 1
came out equal to the frame, in how many the 4x one came out equal to the CPU one, the ones that could not be built
(mode change mid-image: they go to the CPU) and the time of one image at 2560x1440 (`PCE_GPU_LOG=1` lists the
different ones, with the rectangle of the differences; `PCE_GPU_TIME_LOG=1`, the ones that took more than 3 ms on the
GPU; the number varies a lot from one run to another: for the real pacing, replay the session in the window in full
screen with `--frame-timing`).

| Tool | What it does |
|---|---|
| `build\Release\app_tests.exe menufont FONT FOLDER` | options menu pages with the game font (`menu_font.bin` that YsRecomp writes next to the .ini; e.g. `build/app_output/recorte/menu_font.bin` after `app_crop_ys`) |

## Performance
- `ys_trace ... --profile FILE` (PCERecomp's profiler). E.g. intro up to the main menu:
  `ys_trace --frame --profile p.txt --input build\app_output\abertura\direto_ao_menu.txt <cue> 2175 x.ppm`.
- In the window: `PCE_PROFILE=FILE` in `YsRecomp`/`pce_runtime` records the same profiler on the window thread (run
  the one in `build\Release`, with the `.pdb`); `--frame-timing` ends with the drawing cost ("Drawing, ...": before the
  new frame, new frame, whole image and the pieces, average, max and how many above 3 ms).
- `python tools\trace\run_perf_check.py --save build\perf_ref\hle` **before** changing the simulated console, and
  `--check build\perf_ref\hle` afterwards: the user's sessions, frame by frame (image and sound CRC) and the final
  state, equal to the reference (`--bios ...` for the path with the real BIOS, `--frames N` to cut).
  The `perf_ys` test does the `--check` of the first 4000 frames (skipped without a reference).
- Smooth motion: in the window,
  `--frame-timing` with `PCE_TIMING_ALL=1` lists every frame off the pacing (otherwise only the first 20); a session
  can be replayed in the window with `--input`, `--save` (copy of the `.brm`), `--config` and `--frames`. The
  `--frame-timing` output ends with the "Single clock" line (`runtime/app/pacing.h`): images, delivered late, without
  the frame ready (the console did not finish in time: the image repeats), skips (the console ran ahead: window stuck)
  and the estimated drawing (2nd largest of the last 32 images); `PCE_PACE_LOG=1` lists each one with the frame, the
  console frames that took more than 10 ms and the console more than 30 ms without being released by the window. The
  recorded session notes the same (`# clock:`).
