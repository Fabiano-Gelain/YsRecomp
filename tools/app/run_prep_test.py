"""Test app_prep_ys: the new frame prepared on its own thread comes out equal to the one prepared by the window.

What a new console frame needs before it shows (game fixes, crop, fades, bars, equipment, smooth motion and the GPU
texture images) is prepared on its own thread, while the previous frame's images are on screen; with --prep-inline,
the window itself prepares it at display time (as before). Runs the same user session both ways, in real time (the
window releases the console one frame at a time: each frame is prepared once, in both), with the user's .ini (fill,
TV, CRT, equipment, bars, smooth fades) in a 960x540 window (HUD at 3/4: the game's windows shrunk) and the options
menu opened and closed midway (--menu-test: after it the window prepares the frame again, with the current options).
--prep-hashes writes, for each prepared frame, a sum of the texture images and of what its images read (frame B, the
smooth motion positions at 5 fractions, the frame prepared for the GPU). From frame 600 on, every frame must be
prepared in both and with the same sum (the last one, if it came out twice: the menu frame). At the start of the
session one console round sometimes publishes two frames in a row (before the window releases one at a time), and
which of them is prepared depends on timing, in both ways; the smooth fade (which carries colors from one frame to
the next) carries that difference through the black screen at the start (up to frame ~200).

Usage: run_prep_test.py EXE CUE SESSION.txt SESSION.brm FRAMES MENU_FRAME FOLDER
"""
import os
import re
import shutil
import subprocess
import sys

START = 600  # the first 10 s are left out (see above)


def run(exe, cue, session, bram, frames, menu, work, name, extra):
    save = os.path.join(work, name + ".brm")
    shutil.copyfile(bram, save)
    hashes = os.path.join(work, name + ".hash")
    ini = os.path.join(work, "prep.ini")
    r = subprocess.run([exe, cue, "--input", session, "--save", save, "--frames", str(frames), "--config", ini,
                        "--window-size", "960", "540", "--menu-test", str(menu), "--prep-hashes", hashes, *extra],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
    with open(os.path.join(work, name + ".txt"), "w", encoding="utf-8") as f:
        f.write(r.stdout)
    if r.returncode != 0 or f"Window closed after {frames} frames" not in r.stdout:
        print(r.stdout[-2000:])
        print(f"FAILED ({name}): the game did not reach {frames} frames (exit {r.returncode})")
        return None, r.stdout
    if "--menu-test: menu opened" not in r.stdout:
        print(f"FAILED ({name}): the options menu did not open")
        return None, r.stdout
    with open(hashes, encoding="utf-8") as f:
        lines = [tuple(l.split()) for l in f if l.strip()]
    return lines, r.stdout


def main():
    exe, cue, session, bram, frames, menu, work = sys.argv[1:8]
    frames, menu = int(frames), int(menu)
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    with open(os.path.join(work, "prep.ini"), "w", encoding="utf-8") as f:
        f.write("; new-frame preparation test\nfullscreen=0\ncrop=fill\naspect=tv\nsmooth_motion=1\n"
                "filter=sharp\ncrt=mask\nvisual_equipment=1\nenemy_hp_bars=1\nreward_popups=1\n"
                "smooth_fades=1\n")
    par, out_par = run(exe, cue, session, bram, frames, menu, work, "own_thread", [])
    inl, out_inl = run(exe, cue, session, bram, frames, menu, work, "window", ["--prep-inline"])
    if par is None or inl is None:
        return 1
    m = re.search(r"New-frame preparation: (\d+) on its own thread, (\d+) on the window's", out_par)
    ok = True
    if not m or int(m.group(1)) < frames * 0.9:
        print(f"FAILED: the session without --prep-inline did not prepare on its own thread ({m.group(0) if m else 'line missing'})")
        ok = False
    else:
        print(m.group(0))
    # The last sum of each frame (the menu frame may come out twice in one of them: prepared before the menu opens and
    # again after it). The early ones are left out: the black screen before the window releases the console one by one
    # comes out according to timing
    a = {int(s): h for s, h in par if int(s) >= START}
    b = {int(s): h for s, h in inl if int(s) >= START}
    common = sorted(set(a) & set(b))
    only = sorted(set(a) ^ set(b))
    diff = [s for s in common if a[s] != b[s]]
    print(f"Prepared frames: {len(par)} on its own thread, {len(inl)} on the window; from {START} on: {len(common)} "
          f"in both, {len(diff)} different; only in one: {only[:10]}")
    if diff:
        print(f"FAILED: different sum (frame, own thread, window): {[(s, a[s], b[s]) for s in diff[:5]]}")
        ok = False
    if only or len(common) < (frames - START) * 0.99:
        print("FAILED: frame prepared in only one of them (outside the start)")
        ok = False
    print("OK: the frame prepared on its own thread equals the one prepared by the window." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
