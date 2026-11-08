"""Test app_launcher_ys: the start menu before the game, with keys pressed automatically.

Runs `YsRecomp --config <ini> --launcher-test <folder> --frames 120 --turbo` twice (Ys III, Ys IV,
"not released", back to Ys I & II and start) in a new folder and checks that:
  - 1st time: the hidden console records the 3 Ys effects, stores launcher_sfx.bin and copies the menu font;
  - 2nd time: the effects come from launcher_sfx.bin (without the hidden console);
  - Ys I & II was chosen and the game ran the requested frames;
  - the 3 saved screens exist, have an image (are not black) and differ from each other.

Usage: run_launcher_test.py YsRecomp.exe DISC_FOLDER WORK_FOLDER
Without the disc in the folder: exit code 77 (skipped).
"""
import glob
import hashlib
import os
import shutil
import struct
import subprocess
import sys


def lit_fraction(bmp):
    """Fraction of pixels that are not black (SDL 24- or 32-bit BMP)."""
    with open(bmp, "rb") as f:
        d = f.read()
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    bp = struct.unpack("<H", d[28:30])[0] // 8
    stride = (w * bp + 3) & ~3
    lit = total = 0
    for y in range(0, abs(h), 4):
        row = d[off + y * stride: off + y * stride + w * bp]
        for x in range(0, w, 4):
            total += 1
            lit += sum(row[x * bp: x * bp + 3]) > 30
    return lit / max(total, 1)


def run(exe, ini, work):
    r = subprocess.run([exe, "--config", ini, "--launcher-test", work, "--frames", "120", "--turbo"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    print(r.stdout)
    print(r.stderr)
    return r


def main():
    exe, game_dir, work = sys.argv[1:4]
    if not glob.glob(os.path.join(game_dir, "*.cue")):
        print(f"No .cue in {game_dir}: test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ini = os.path.join(work, "YsRecomp.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; test configuration\nfullscreen=0\n")
        f.write(f"game_folder={os.path.abspath(game_dir)}\n")
    ok = True

    def need(cond, msg):
        nonlocal ok
        if not cond:
            print(f"FAILED: {msg}")
            ok = False

    r = run(exe, ini, work)
    need(r.returncode == 0, f"1st time exited with code {r.returncode}")
    need("recorded by the hidden console: 3 of 3" in r.stdout, "the hidden console did not record the 3 effects")
    need(os.path.isfile(os.path.join(work, "launcher_sfx.bin")), "launcher_sfx.bin was not written")
    need(os.path.isfile(os.path.join(work, "menu_font.bin")), "the menu font was not copied")
    need("Ys Book I & II chosen" in r.stdout, "Ys Book I & II was not chosen")
    need("Window closed after 120 frames" in r.stdout, "the game did not run after the launcher")
    need("options menu opened" in r.stdout and "options menu closed" in r.stdout,
         "the options menu did not open and close in the launcher")
    shots = [os.path.join(work, f"launcher_{t}.bmp") for t in (40, 70, 110, 150)]
    hashes = set()
    for s in shots:
        if not os.path.isfile(s):
            need(False, f"screen {s} was not saved")
            continue
        frac = lit_fraction(s)
        need(frac > 0.3, f"{os.path.basename(s)}: only {frac:.0%} of the screen has an image")
        with open(s, "rb") as f:
            hashes.add(hashlib.md5(f.read()).hexdigest())
    need(len(hashes) == 4, "saved screens are equal (the choice, the message or the menu did not change the image)")

    r = run(exe, ini, work)
    need(r.returncode == 0, f"2nd time exited with code {r.returncode}")
    need("Ys effects read from" in r.stdout and "hidden console" not in r.stdout,
         "2nd time: the effects did not come from launcher_sfx.bin")
    need("Ys Book I & II chosen" in r.stdout, "2nd time: Ys Book I & II was not chosen")
    print("OK: start menu, Ys effects and start of the game." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
