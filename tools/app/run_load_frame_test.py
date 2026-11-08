"""Test app_load_no_border_ys: the screen edge does not flash on the black screen while loading.

On entering the scroll-only screen (and on returning from it) the game copies the border into cells 13-15 of the map;
it only shows in the right place with the next frame's scroll. The torn-frame fix brought those writes forward and the
border appeared for 1 frame in the middle of the black map, when loading a save and when changing area. Runs the user's
session (session_20261104_192718, without the BIOS) in real time (in --turbo the window may skip exactly that frame),
with his fill and TV pixels, saving what the window showed: with the black map, the image must be all black.

Usage: run_load_frame_test.py EXE CUE SESSION.txt SESSION.brm FOLDER
"""
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

FIRST, COUNT = 3085, 780  # from loading the save to the end of the 1st area change
FRAMES = FIRST + COUNT
BLACK = [(3090, 3100), (3847, 3860)]  # the whole image black: border copied (3093, 3844) and after it
FADE_END = (3843, 3846)  # end of the smooth fade of the 1st area change: only the very dark map, never the lit border
FADE_MAX = 60


def brightest(path):
    with open(path, "rb") as f:
        d = f.read()
    off = struct.unpack_from("<I", d, 10)[0]
    step = struct.unpack_from("<H", d, 28)[0] // 8
    px = d[off:]
    return max(max(px[0::step]), max(px[1::step]), max(px[2::step]))


def main():
    exe, cue, session, bram, work = sys.argv[1:6]
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    dump = os.path.join(work, "images")
    os.makedirs(dump)
    ini = os.path.join(work, "window.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; border on the black screen test\nfullscreen=0\ncrop=fill\naspect=tv\nsmooth_motion=1\nfilter=sharp\n"
                "crt=mask\nsmooth_fades=1\n")
    save = os.path.join(work, "window.brm")
    shutil.copyfile(bram, save)
    r = subprocess.run([exe, cue, "--input", session, "--save", save, "--frames", str(FRAMES), "--config", ini,
                        "--window-size", "480", "270", "--screen-dump", str(FIRST), str(COUNT), dump],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
    if r.returncode != 0 or f"Window closed after {FRAMES} frames" not in r.stdout:
        print(f"FAILED: the game did not reach {FRAMES} frames (exit {r.returncode})")
        print(r.stdout[-2000:])
        return 1
    max_of = {}
    for path in glob.glob(os.path.join(dump, "q*.bmp")):
        q = int(re.search(r"q(\d+)_", os.path.basename(path)).group(1))
        if any(a <= q <= b for a, b in BLACK) or FADE_END[0] <= q <= FADE_END[1]:
            max_of[q] = max(max_of.get(q, 0), brightest(path))
    ok = True
    for a, b in BLACK:
        seen = [q for q in max_of if a <= q <= b]
        lit = sorted(q for q in seen if max_of[q] > 0)
        print(f"frames {a}-{b}: {len(seen)} with an image, {len(lit)} with something lit {lit[:8]}")
        if not seen or lit:
            ok = False
    fade = {q: v for q, v in max_of.items() if FADE_END[0] <= q <= FADE_END[1]}
    print(f"end of the fade {FADE_END[0]}-{FADE_END[1]}: maximum brightness {sorted(fade.items())}")
    if not fade or max(fade.values()) > FADE_MAX:
        ok = False
    print("OK: on the black screen the border does not show." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
