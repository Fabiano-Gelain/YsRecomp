"""Test app_start_ys: YsRecomp opens the way the player opens it, with no game arguments.

Creates a test configuration (YsRecomp.ini) pointing at the disc folder, runs
`YsRecomp --config <ini> --frames N` and checks that:
  - the disc was recognized from the folder (no folder picker window: it would block the test);
  - the game ran with the recompiled code until the end of the frames;
  - the save was written to saves/ next to the configuration (portable version), 2 KB.

Usage: run_app_start_test.py YsRecomp.exe DISC_FOLDER WORK_FOLDER
Without the disc in the folder: exit code 77 (skipped).
"""
import glob
import os
import shutil
import subprocess
import sys


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

    r = subprocess.run([exe, "--config", ini, "--frames", "600", "--turbo"], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=300)
    print(r.stdout)
    print(r.stderr)
    ok = True
    if r.returncode != 0:
        print(f"FAILED: exited with code {r.returncode}")
        ok = False
    if "Running the recompiled code" not in r.stdout:
        print("FAILED: did not run the recompiled code")
        ok = False
    if "Window closed after 600 frames" not in r.stdout:
        print("FAILED: did not reach 600 frames")
        ok = False
    saves = glob.glob(os.path.join(work, "saves", "*.brm"))
    if len(saves) != 1 or os.path.getsize(saves[0]) != 2048:
        print(f"FAILED: save expected in {work}/saves (2048 bytes); found: {saves}")
        ok = False
    with open(ini, encoding="utf-8") as f:
        text = f.read()
        if os.path.abspath(game_dir) not in text:
            print("FAILED: the configuration lost the game folder")
            ok = False
        # display options: the missing ones are written; the one the test set (windowed) stays
        for key in ("fullscreen=0", "resolution=desktop", "refresh_rate=auto", "vsync=1", "window_scale=3"):
            if key not in text.splitlines():
                print(f"FAILED: the configuration does not have {key}")
                ok = False
    print("OK: the game opened from the folder stored in the configuration." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
