"""Test app_menu_intro_ys: the options menu does not open during the intro and the image shows on the path without smooth motion.

1. Replays a session with a real Esc (`--menu-press`) on frames of the intro and of the game: while the game's program is
   the intro's ($04) or the boot, the menu stays blocked; afterwards it opens. (The launcher menu has its own loop.)
2. With the sharp filter, no smooth motion and the CRT mask (the image goes through the intermediate texture): the image
   shows and the CRT brightness is added (white pixel above what the mask alone would allow, 200 at crt_brightness=bright).

Usage: run_menu_opening_test.py EXE CUE SESSION.txt SAVE.brm FOLDER
"""
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

PRESSES = "300,1000,2000,3500"
FRAMES = "3700"


def run(exe, args):
    return subprocess.run([exe, *args], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)


def write_ini(path, extra):
    with open(path, "w", encoding="utf-8") as f:
        f.write("fullscreen=0\n" + extra)


def bmp_stats(path):
    """(lit pixels, the highest "lowest channel" of a pixel)"""
    with open(path, "rb") as f:
        data = f.read()
    offset, width, height, bpp = (struct.unpack_from("<I", data, 10)[0], struct.unpack_from("<i", data, 18)[0],
                                  abs(struct.unpack_from("<i", data, 22)[0]), struct.unpack_from("<H", data, 28)[0])
    if bpp != 32:
        raise ValueError(f"{path}: {bpp} bits")
    lit = white = 0
    for i in range(offset, offset + width * height * 4, 4):
        b, g, r = data[i], data[i + 1], data[i + 2]
        if r or g or b:
            lit += 1
            white = max(white, min(r, g, b))
    return lit, white


def main():
    exe, cue, session, save, work = sys.argv[1:6]
    if not os.path.exists(cue):
        print(f"No disc ({cue}): test skipped.")
        return 77
    os.makedirs(work, exist_ok=True)
    ok = True

    ini = os.path.join(work, "menu.ini")
    write_ini(ini, "")
    copy = os.path.join(work, "menu.brm")
    shutil.copyfile(save, copy)
    r = run(exe, [cue, "--config", ini, "--save", copy, "--input", session, "--turbo", "--frames", FRAMES,
                  "--menu-press", PRESSES])
    seen = re.findall(r"--menu-press: frame (\d+), program (-?\d+): menu (\w+)", r.stdout)
    print("\n".join(" ".join(s) for s in seen))
    expected = len(PRESSES.split(","))
    if r.returncode != 0 or len(seen) != expected:
        print(f"FAILED: code {r.returncode}, {len(seen)} Esc of {expected}\n{r.stdout[-600:]}\n{r.stderr[-300:]}")
        ok = False
    for frame, program, result in seen:
        want = "blocked" if int(program) in (-1, 4) else "opened"
        if result != want:
            print(f"FAILED: frame {frame}, program {program}: {result}, expected {want}")
            ok = False
    if not any(s[2] == "blocked" for s in seen) or not any(s[2] == "opened" for s in seen):
        print("FAILED: the test needs to see the menu blocked and opened")
        ok = False

    ini = os.path.join(work, "image.ini")
    write_ini(ini, "smooth_motion=0\ncrop=off\nfilter=sharp\ncrt=mask\ncrt_brightness=bright\n")
    dump = os.path.join(work, "image")
    shutil.rmtree(dump, ignore_errors=True)
    os.makedirs(dump)
    r = run(exe, [cue, "--config", ini, "--save", os.path.join(work, "image.brm"), "--turbo", "--frames", "600",
                  "--window-size", "960", "540", "--screen-dump", "590", "1", dump])
    shots = sorted(glob.glob(os.path.join(dump, "*.bmp")))
    if r.returncode != 0 or not shots:
        print(f"FAILED: no image (code {r.returncode})\n{r.stderr[-300:]}")
        ok = False
    else:
        lit, white = bmp_stats(shots[0])
        print(f"No smooth motion, sharp, with the mask: {lit} lit pixels, white {white}")
        if lit < 10000 or white <= 200:
            print("FAILED: black image or without the CRT brightness")
            ok = False

    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
