#!/usr/bin/env python3
"""The mine light with a window open, in fill with the HUD at 3/4 (shrunken window, with the map from before it around it).

The game stays paused with the window open: Adol and the light do not move. So, outside the shrunken window, the image
must equal the one of the last frame before it. Without the fix, the game's character that pierces the light mask in
the window's cells (state $50, app/ys_engine.h) turned the circle into a square corner.

Usage: check_light_window.py YS_TRACE SESSION(no extension) CUE FOLDER BEFORE:OPEN [BEFORE:OPEN ...]
Fails if any pair has different points around the window, or if the window is not found.
"""
import os
import subprocess
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        end = pos
        while data[end] not in b" \t\r\n":
            end += 1
        if end > pos:
            fields.append(data[pos:end])
        pos = end + 1
    return int(fields[1]), int(fields[2]), data[pos:]


def shot(exe, base, cue, folder, frame):
    path = os.path.join(folder, f"light_window_{frame}.ppm")
    frame_ppm = os.path.join(folder, f"light_window_{frame}_frame.ppm")
    env = dict(os.environ, PCE_HIDDEN_WINDOW="1")
    cmd = [exe, "--frame", "--crop-log", "--crop-shot", "fill", "shadow", path, "--input", base + ".txt", "--bram",
           base + ".brm", cue, str(frame), frame_ppm]
    out = subprocess.run(cmd, capture_output=True, text=True, errors="replace", env=env).stdout
    for line in out.splitlines():
        if line.startswith("window characters outside the ring"):
            print(f"frame {frame}: {line}")
    return read_ppm(path)


def window_box(w, h, px, band_top):
    """The box holding the windows' white borders on the map: the long runs of white points on a line"""
    box = None
    for y in range(band_top):
        run = 0
        for x in range(w + 1):
            i = (y * w + x) * 3
            white = x < w and px[i] > 200 and px[i + 1] > 200 and px[i + 2] > 200
            if white:
                run += 1
                continue
            if run > w // 8:
                a, b = x - run, x - 1
                box = (a, y, b, y) if box is None else (min(box[0], a), box[1], max(box[2], b), y)
            run = 0
    return box


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    exe, base, cue, folder = sys.argv[1:5]
    os.makedirs(folder, exist_ok=True)
    failed = 0
    for pair in sys.argv[5:]:
        before, opened = (int(v) for v in pair.split(":"))
        w, h, a = shot(exe, base, cue, folder, before)
        w2, h2, b = shot(exe, base, cue, folder, opened)
        if (w, h) != (w2, h2):
            print(f"{pair}: different sizes {w}x{h} {w2}x{h2}: FAILED")
            failed += 1
            continue
        band_top = h * 5 // 6  # above the HUD band
        box = window_box(w, h, b, band_top)
        if box is None:
            print(f"{pair}: window not found: FAILED")
            failed += 1
            continue
        x0, y0, x1, y1 = box
        margin = 8  # the 1-point black outline (crop_view_state; 4 subpoints with the HUD at 3/4) and slack
        diff = 0
        first = None
        for y in range(band_top):
            for x in range(w):
                if x0 - margin <= x <= x1 + margin and y0 - margin <= y <= y1 + margin:
                    continue
                i = (y * w + x) * 3
                if a[i:i + 3] != b[i:i + 3]:
                    diff += 1
                    if first is None:
                        first = (x, y)
        print(f"{pair}: image {w}x{h}, window {x0},{y0}-{x1},{y1}: {diff} different points around it (1st: {first})")
        failed += diff != 0
    print("mine light with window:", "FAILED" if failed else "ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
