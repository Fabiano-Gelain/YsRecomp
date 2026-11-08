"""Test app_crop_ys: the border crop does not change the game and really crops.

Runs the same user session (button script + save) five times in YsRecomp, with the same number of
frames: "compact" (stretched, with CRT), "full" (gradient background) and "fill" with smooth motion, "frame" without
it and "wide" with the options menu opened and closed midway (--menu-test: the crop goes on, with the menu drawn on
top, smooth and with CRT). The console state at the end (RAM, VRAM, colors, CD RAM and registers) must be the same in
all of them, and the window must have seen the border and drawn cropped images (no crop, "off", no longer exists).
Each round starts from a fresh copy of the save. Runs with --turbo (without waiting for the clock): the state at the
end does not depend on pacing.

Usage: run_crop_test.py EXE CUE SESSION.txt SESSION.brm FRAMES FOLDER
"""
import os
import re
import shutil
import subprocess
import sys


def run(exe, args):
    r = subprocess.run([exe, *args], capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=900)
    lines = [l for l in r.stdout.splitlines() if "Border crop" in l or "Console state" in l or "Window closed" in l or "Menu font" in l]
    print("\n".join(lines))
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:])
    m = re.search(r"Console state: ([0-9A-F]{8})", r.stdout)
    c = re.search(r"Border crop: (\d+) of (\d+) frames with the border; (\d+) cropped images \((\d+) with the menu on "
                  r"top\)", r.stdout)
    return r, (m.group(1) if m else None), (tuple(int(x) for x in c.groups()) if c else None)


def main():
    exe, cue, session, bram, frames, work = sys.argv[1:7]
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    os.makedirs(work, exist_ok=True)
    ok = True
    states = {}
    menu_frame = str(int(frames) - 150)  # already in the game, with the border
    # Menu font: copied from VRAM in the 1st round and stored next to the .ini; the others read it from the file
    font = os.path.join(work, "menu_font.bin")
    if os.path.exists(font):
        os.remove(font)
    # smooth_motion: 2 = smooth motion assembled on the GPU, with the crop on top; 1 = the same
    # assembled on the CPU (--motion-cpu, like F6); 0 = off
    rounds = (("compact", "compact", 2, "aspect=stretch\ncrt=mask\n"), ("full", "full", 2, "hud_background=navy\n"),
              ("frame", "frame", 0, ""), ("fill", "fill", 2, "hud_background=ember\n"),
              ("menu", "wide", 1, "filter=smooth\ncrt=scanlines\nhud_background=wine\n"))
    for name, crop, smooth, image in rounds:
        ini = os.path.join(work, f"crop_{name}.ini")
        with open(ini, "w", encoding="utf-8") as f:
            f.write(f"; crop test\nfullscreen=0\ncrop={crop}\nsmooth_motion={min(smooth, 1)}\n{image}")
        save = os.path.join(work, f"crop_{name}.brm")
        shutil.copyfile(bram, save)
        print(f"--- crop={crop}, smooth_motion={smooth}")
        extra = (["--menu-test", menu_frame] if name == "menu" else []) + (["--motion-cpu"] if smooth == 1 else [])
        r, state, counts = run(exe, [cue, "--input", session, "--save", save, "--frames", frames, "--config", ini,
                                     "--turbo", *extra])
        if r.returncode != 0 or f"Window closed after {frames} frames" not in r.stdout:
            print(f"FAILED ({name}): the game did not reach {frames} frames (exit {r.returncode})")
            ok = False
        states[name] = state
        copied = "Menu font copied from the game" in r.stdout
        if copied != (name == "compact") or not os.path.exists(font) or os.path.getsize(font) != 1280:
            print(f"FAILED ({name}): menu font {'copied' if copied else 'not copied'} from VRAM; file "
                  f"{os.path.getsize(font) if os.path.exists(font) else 'missing'} (expected: copied only in the 1st "
                  f"round, 1280 bytes)")
            ok = False
        if smooth == 2 and not re.search(r"GPU: assembles the image; [1-9][0-9]* images assembled", r.stdout):
            print(f"FAILED ({name}): no image assembled on the GPU")
            ok = False
        if smooth == 1 and not re.search(r"GPU: .*; 0 images assembled", r.stdout):
            print(f"FAILED ({name}): with --motion-cpu, images assembled on the GPU")
            ok = False
        # With --turbo the game runs faster than the screen: the window sees ~1 in 8 frames (~50 with the border).
        if not counts or counts[0] < 20 or counts[2] < 20:
            print(f"FAILED ({name}): not enough border seen/cropped images: {counts}")
            ok = False
        elif name == "menu" and counts[3] < 10:
            print(f"FAILED: with the menu open, the crop should go on (menu on top): {counts}")
            ok = False
    if not states["compact"] or len(set(states.values())) != 1:
        print(f"FAILED: different console state: {states}")
        ok = False
    print("OK: the crop did not change the game and cropped the images with the border." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
