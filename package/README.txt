YsRecomp - Ys Book I & II for PC
================================

YsRecomp runs Ys Book I & II (TurboGrafx-CD) natively on Windows. The game's
original code was translated to PC code ahead of time, so there is no emulator
and no System Card BIOS file is needed. The game itself is NOT included: you
need your own copy of the disc.

FOR PERSONAL USE ONLY. This program contains a translation of the game's code.
Use it only with a disc you own, and please do not redistribute it.


What you need
-------------
- Windows 10 or 11, 64-bit.
- A BIN/CUE image of Ys Book I & II (USA), made from your own disc: one .cue
  file and its .bin file(s) in one folder (for example the Redump set
  "Ys Book I & II (USA)", 1 .cue + 43 tracks).
- Optional: an Xbox controller (or any controller Windows recognizes).


First start
-----------
1. Put the YsRecomp folder anywhere you like (it does not need to be installed).
2. Run YsRecomp.exe.
3. A window asks for the folder with your .cue/.bin files. Choose it.
   The disc is checked; if it is a different version or a file is missing or
   damaged, a message tells you what is wrong.
4. The start menu shows the covers of Ys Book I & II, Ys III and Ys IV (only
   Ys Book I & II can be played). Press Run (Enter) to start; Select (Space)
   opens the options menu. The folder is remembered for the next time.


Controls (default)
------------------
                    Keyboard        Controller
  D-pad             Arrow keys      D-pad
  Button I          X               A
  Button II         Z               B
  Select            Space           Y
  Run (Start)       Enter           X
  Options menu      Esc             Menu (Start)
  Fullscreen        Alt+Enter

During the intro, Enter (or A) skips to the next scene. Keys and buttons can be
changed in the options menu (Controls).


Options menu (Esc)
------------------
The game is paused while the menu is open.
- Video: fullscreen, resolution, refresh rate, V-Sync, window size and Smooth
  motion. Screen mode changes take effect with "Apply"; if you do not answer
  "Keep these settings?" within 10 seconds, the old mode comes back.
- Image: integer scale, sharp or smooth filter, CRT effect (Scanlines,
  Lines+mask, Slot mask, Shadow mask, Glow, Glow+mask, Glow+slot; the mask
  size follows your screen) and CRT brightness (Normal or Bright), pixel
  aspect (TV, square, or Stretch to fill the whole screen), Crop border
  (Original keeps the game's own status panel; Full, Compact, Wide and Fill
  show a new status bar under the map, the same in Ys I and Ys II: H.P with
  its maximum, M.P in Ys II, NEXT = experience still needed for the next
  level, and GOLD), HUD background (black or one of 14 dark gradients behind
  the new status bar), Visual equipment. The status bar and the menus always
  get the same filter and CRT effect as the picture.
- Visual equipment (on by default): on the map, Adol wears the sword, shield
  and armor you have equipped, each with its own shape and colors (in the
  original game he always looks the same).
- Smooth motion (on by default): the game still runs at 60 steps per second,
  but the map and characters are drawn between pixels at your monitor's
  refresh rate (120 Hz, 144 Hz...), and the map scrolls at an even speed
  instead of the original 2-2-2-2-0 rhythm. The picture is about 1/60 s
  behind the controls; turn it Off for the original 60 frames per second.
- Smooth fades (on by default): the game darkens and brightens the screen in
  7 visible steps; with this On each step blends smoothly into the next.
- Half-drawn frames (the game sometimes shows a picture it is still drawing,
  which also happens on the real console): inside the game's windows (talk,
  menus, shops) the whole window is shown, old or new.
- Audio: master volume and separate volumes for CD music, chip music, sound
  effects and voices; Chip filter (on by default) removes the harsh hiss the
  sound chip makes on high notes (turn it Off for the raw sound).
- Controls: keyboard and controller buttons, controller layout.
- Gameplay: Skip intro (start straight at the title menu) and Pause in
  background (on by default: the game pauses while the window is not in
  front, e.g. after Alt+Tab); Enemy HP bars (a small yellow/red bar over an
  enemy you hit, for 3 seconds) and Reward popups ("+EXP" and "+GOLD" where
  an enemy dies: Off, Large or Small letters). Both are on by default and
  hide while a game window (talk, item) is open. Instant dialog (on by
  default): the few texts the game types out letter by letter (Sara at the
  start of Ys I, the Books of Ys) appear whole at once; press the button to
  go on, as in any other talk.
- Quit game.

The menu uses the game's own letters. They are copied from your disc the
first time the game screen appears (until then the menu uses simpler
letters) and kept in menu_font.bin, next to YsRecomp.ini.


Files
-----
Everything stays inside the YsRecomp folder (nothing goes to AppData):
- YsRecomp.ini     your settings (created on first start; delete it to reset).
- saves\           the game's save memory (your saved games). Back it up!
- menu_font.bin    the game's letters for the options menu (from your disc).
- launcher_sfx.bin the game's sound effects for the start menu and the
                   options menu (recorded from your disc).
- sessions\        recorded play sessions (only with record_sessions=1 in
                   YsRecomp.ini; used to check the game against the original).

To use a different disc folder, delete the "game_folder" line in YsRecomp.ini;
the folder window appears again on the next start. If you move or rename the
disc folder, YsRecomp notices and asks for it again.


Troubleshooting
---------------
- The disc is refused: the message says which file is missing or what is
  wrong. Check that the .cue and every .bin file it lists are in the folder
  and that it is the USA version.
- Black screen or wrong size in fullscreen: press Alt+Enter for a window, then
  pick another resolution in Options > Video.
- Choppy picture on a G-Sync/FreeSync monitor: try V-Sync Off in Options > Video.
- Slow PC: turn Smooth motion Off in Options > Video.
