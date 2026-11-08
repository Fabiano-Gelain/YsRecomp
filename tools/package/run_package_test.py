"""Test app_package_ys: the player package runs in a clean folder.

Builds the package (make_package.py) and checks that the folder and the .zip hold only YsRecomp.exe and README.txt.
Then extracts the .zip into a Windows temporary folder (outside the project: no reference/, roms/, build/),
puts next to the .exe a YsRecomp.ini with only the disc folder (as it is after the player picks it; the
folder picker window would block the test) and runs the .exe with no game arguments, from inside that folder:
  - the disc is recognized and the recompiled code runs 600 frames;
  - the save goes to saves/ next to the .exe (2 KB);
  - with record_sessions=1, the recorded session goes to sessions/ next to the .exe.
Also checks that building the package again keeps YsRecomp.ini, saves/, sessions/ and menu_font.bin.

Usage: run_package_test.py YsRecomp.exe DISC_FOLDER WORK_FOLDER
Without the disc in the folder: checks only the package and exits with 77 (skipped).
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_package  # noqa: E402


def main():
    exe, game_dir, work = sys.argv[1:4]
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    r = make_package.make(exe, work)
    if not r:
        print("FAILED: the package was not built")
        return 1
    folder, zip_path = r
    images = make_package.launcher_images()
    expected = sorted(list(make_package.FILES) + (["launcher"] if images else []))
    if sorted(os.listdir(folder)) != expected:
        print(f"FAILED: the package folder has {sorted(os.listdir(folder))}, expected {expected}")
        return 1
    with zipfile.ZipFile(zip_path) as z:
        names = sorted(z.namelist())
    want = sorted([f"YsRecomp/{n}" for n in make_package.FILES] + [f"YsRecomp/launcher/{n}" for n in images])
    if names != want:
        print(f"FAILED: the zip has {names}")
        return 1
    print(f"OK: package with {expected} ({os.path.getsize(zip_path) // 1024} KB zipped)")
    # Build again on top of a folder that was played in: configuration, saves and sessions stay
    for sub in ("saves", "sessions", "stale"):
        os.makedirs(os.path.join(folder, sub))
        open(os.path.join(folder, sub, "x"), "w").close()
    for name in ("YsRecomp.ini", "menu_font.bin", "launcher_sfx.bin", "stale.txt"):
        open(os.path.join(folder, name), "w").close()
    make_package.make(exe, work)
    kept = sorted(os.listdir(folder))
    if kept != sorted(expected + list(make_package.KEEP)):
        print(f"FAILED: after building again, the folder has {kept}")
        return 1
    print("OK: building again keeps the player's YsRecomp.ini, saves/, sessions/, menu_font.bin and launcher_sfx.bin")

    if not glob.glob(os.path.join(game_dir, "*.cue")):
        print(f"No .cue in {game_dir}: running-package test skipped.")
        return 77
    clean = tempfile.mkdtemp(prefix="ysrecomp_package_")
    try:
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(clean)
        game = os.path.join(clean, "YsRecomp")
        ini = os.path.join(game, "YsRecomp.ini")
        with open(ini, "w", encoding="utf-8") as f:
            f.write(f"fullscreen=0\nrecord_sessions=1\ngame_folder={os.path.abspath(game_dir)}\n")
        r = subprocess.run([os.path.join(game, "YsRecomp.exe"), "--frames", "600", "--turbo"], cwd=game,
                           capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
        out = r.stdout + r.stderr
        ok = True
        if r.returncode != 0:
            print(out[-3000:])
            print(f"FAILED: exited with code {r.returncode}")
            ok = False
        for line in ("Running the recompiled code", "Window closed after 600 frames"):
            if line not in out:
                print(f"FAILED: '{line}' missing from the output")
                ok = False
        saves = glob.glob(os.path.join(game, "saves", "*.brm"))
        if len(saves) != 1 or os.path.getsize(saves[0]) != 2048:
            print(f"FAILED: save expected in saves/ next to the .exe (2048 bytes); found: {saves}")
            ok = False
        # record_sessions=1: the session goes to sessions/ next to the .exe (without the recording's .tmp)
        rec = sorted(os.listdir(os.path.join(game, "sessions"))) if os.path.isdir(os.path.join(game, "sessions")) else []
        txt = [n for n in rec if n.endswith(".txt")]
        if len(txt) != 1 or len(rec) != 1:
            print(f"FAILED: expected 1 session in sessions/ next to the .exe; found: {rec}")
            ok = False
        else:
            with open(os.path.join(game, "sessions", txt[0]), encoding="utf-8") as f:
                head = f.read()
            if "# frames: 600\n" not in head:
                print(f"FAILED: the recorded session does not have '# frames: 600':\n{head[:500]}")
                ok = False
        print("OK: the package ran in a clean folder and reached the game." if ok else "FAILED")
        return 0 if ok else 1
    finally:
        shutil.rmtree(clean, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
