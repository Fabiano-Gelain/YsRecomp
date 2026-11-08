"""Test app_menu_ys: opening and closing the options menu in the middle of a session does not change the game.

Runs the same session (button script) twice in YsRecomp, with the same number of frames: without the
menu and with `--menu-test Q` (at frame Q the menu opens, the music volume goes to 90%, "Quit game" is opened
and cancelled, and the menu closes with "Resume"). The console state at the end (RAM, VRAM, colors, CD RAM and
registers) must be the same, and the new volume must have been written to the configuration.

Usage: run_menu_test.py EXE FRAMES Q FOLDER -- GAME_ARGUMENTS...
"""
import os
import re
import subprocess
import sys


def run(exe, args):
    r = subprocess.run([exe, *args], capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=600)
    print(r.stdout)
    print(r.stderr)
    m = re.search(r"Console state: ([0-9A-F]{8})", r.stdout)
    return r, (m.group(1) if m else None)


def main():
    sep = sys.argv.index("--")
    exe, frames, menu_frame, work = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    game_args = sys.argv[sep + 1:]
    os.makedirs(work, exist_ok=True)
    ini = os.path.join(work, "menu_test.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; menu test\nvolume_music=100\nfullscreen=0\n")

    plain, state_plain = run(exe, [*game_args, "--frames", frames])
    menu, state_menu = run(exe, [*game_args, "--frames", frames, "--config", ini, "--menu-test", menu_frame])
    ok = plain.returncode == 0 and menu.returncode == 0
    if not ok:
        print(f"FAILED: exit codes {plain.returncode} and {menu.returncode}")
    for name, r in (("without menu", plain), ("with menu", menu)):
        if f"Window closed after {frames} frames" not in r.stdout:
            print(f"FAILED ({name}): the game did not reach {frames} frames")
            ok = False
    if "--menu-test: menu opened" not in menu.stdout or "menu closed; music 90%" not in menu.stdout:
        print("FAILED: the menu script did not run to the end")
        ok = False
    if not state_plain or state_plain != state_menu:
        print(f"FAILED: different console state (without menu {state_plain}, with menu {state_menu})")
        ok = False
    with open(ini, encoding="utf-8") as f:
        saved = f.read()
    if "volume_music=90" not in saved:
        print("FAILED: the new volume was not written to the configuration:\n" + saved)
        ok = False
    print("OK: the menu opened and closed without changing the game, and the volume was saved." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
