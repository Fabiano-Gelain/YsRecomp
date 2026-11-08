"""Test app_rhythm_sewer_ys: Adol walks without stops, through the window path (frame preparation, smooth motion).

In the Ys II sewer the game redraws the floor while the image is being output at the step decision. The garbage frame
(since removed) took that frame for garbage and hid it: the image stopped for a frame, jumped, and the whole step came
out without a constant speed (19 stops and 48 frames off the rhythm in 9031-10097 of session_20261103_224821). Runs the
session in real time, alone (with --turbo and a busy computer the preparation skips frames, and the rhythm is counted
per prepared frame), and checks --walk-rhythm: every frame walking at the rhythm, no stops.

Usage: run_walk_rhythm_test.py EXE CUE SESSION.txt SESSION.brm FROM TO WALKING RHYTHM FOLDER
"""
import os
import re
import shutil
import subprocess
import sys


def main():
    exe, cue, session, bram, de, ate, andando, ritmo, work = sys.argv[1:10]
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ini = os.path.join(work, "rhythm.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; Adol rhythm test\nfullscreen=0\ncrop=fill\naspect=tv\nsmooth_motion=1\nfilter=sharp\ncrt=mask\n"
                "visual_equipment=1\nenemy_hp_bars=1\nreward_popups=1\nsmooth_fades=1\n")
    save = os.path.join(work, "rhythm.brm")
    shutil.copyfile(bram, save)
    r = subprocess.run([exe, cue, "--input", session, "--save", save, "--frames", ate, "--config", ini,
                        "--walk-rhythm", de, ate],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
    if r.returncode != 0 or f"Window closed after {ate} frames" not in r.stdout:
        print(f"FAILED: the game did not reach {ate} frames (exit {r.returncode})")
        print(r.stdout[-2000:])
        return 1
    m = re.search(r"Adol rhythm .*: (\d+) walking, (\d+) at the rhythm of ([\d.]+), (\d+) stops midway", r.stdout)
    if not m:
        print("FAILED: no rhythm line")
        return 1
    print(m.group(0))
    ok = m.group(1) == andando and m.group(2) == andando and m.group(3) == ritmo and m.group(4) == "0"
    print("OK: Adol walks without stops and at the rhythm." if ok else
          f"FAILED: expected {andando} walking, all at the rhythm of {ritmo}, no stops")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
