"""Test app_torn_window_ys: torn frame with the game's window shrunk.

In Fill with the HUD at 3/4 the game's windows shrink, and around them the map comes out without the window's own
sprites (the pink equipment frames, the cursor: RedrawRing.skip). Runs the user's session (session_20261031_000404,
without the BIOS) in real time until past frame 3834 (torn only inside the windows, with the equipment screen open),
with the user's .ini (fill, TV, CRT, smooth motion on the GPU) and the window at 960x540 (HUD at 3/4): the window must
report that it received at least one frame torn inside the windows (fix_torn_window shows the whole window from the end
of the image), and no image with a shrunk window may come out without its sprites outside (the window releases the
console one frame at a time: the counters do not depend on timing).

Usage: run_torn_ring_test.py EXE CUE SESSION.txt SESSION.brm FOLDER
"""
import os
import re
import shutil
import subprocess
import sys

TORN = 3834  # torn with the equipment screen open (pce_trace --game-fixes; ring_log: pink frames)
FRAMES = 3845


def main():
    exe, cue, session, bram, work = sys.argv[1:6]
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ini = os.path.join(work, "window.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; torn frame with a shrunk window test\nfullscreen=0\ncrop=fill\naspect=tv\n"
                "smooth_motion=1\nfilter=sharp\ncrt=mask\nvisual_equipment=1\n")
    save = os.path.join(work, "window.brm")
    shutil.copyfile(bram, save)
    r = subprocess.run([exe, cue, "--input", session, "--save", save, "--frames", str(FRAMES), "--config", ini,
                        "--window-size", "960", "540"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
    print("\n".join(l for l in r.stdout.splitlines() if "torn frames" in l or "GPU:" in l))
    if r.returncode != 0 or f"Window closed after {FRAMES} frames" not in r.stdout:
        print(f"FAILED: the game did not reach {FRAMES} frames (exit {r.returncode})")
        print(r.stdout[-2000:])
        return 1
    ok = True
    m = re.search(r"Images with a shrunk window missing its outside sprites: (\d+); torn frames inside the game's "
                  r"windows: (\d+) \((\d+) fixed\)", r.stdout)
    if not m:
        print("FAILED: no torn frames line")
        return 1
    # how many the drawing fixed depends on which frames the window catches in time (here, with the window out of focus,
    # it skips frames); how many the console sent as torn inside the windows does not
    if int(m.group(2)) < 1:
        print(f"FAILED: no frame torn inside the windows (expected {TORN})")
        ok = False
    if int(m.group(1)) != 0:
        print(f"FAILED: {m.group(1)} image(s) with a shrunk window without its sprites outside")
        ok = False
    print("OK: with the torn frame fixed, the shrunk window still has no sprites of its own outside." if ok
          else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
