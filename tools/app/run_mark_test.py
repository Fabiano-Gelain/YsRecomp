"""Test app_mark_ys: F12 notes in the recorded session the frame that is on screen.

Runs the game recording the session (`--record FOLDER`) with `--mark-test Q`, which puts a real F12 in the window's
event queue when it shows frame Q. The recorded session must carry the line "# marks (F12): N" with N between Q and
Q + 20 (the frame shown when the event is read; in --turbo the console runs ahead of the window), and the session must
still be replayable (the script reads lines starting with "#" as comments).

Usage: run_mark_test.py EXE FOLDER FRAMES Q -- GAME_ARGUMENTS...
"""
import glob
import os
import re
import shutil
import subprocess
import sys


def main():
    sep = sys.argv.index("--")
    exe, folder, frames, q = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    game_args = sys.argv[sep + 1:]
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    r = subprocess.run([exe, *game_args, "--record", folder, "--turbo", "--frames", str(frames), "--mark-test", str(q)],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    print("\n".join(line for line in r.stdout.splitlines() if "F12" in line or "Window closed" in line))
    ok = r.returncode == 0
    if not ok:
        print(f"FAILED: exited with code {r.returncode}")
        print(r.stdout[-2000:], r.stderr[-2000:])
    files = sorted(glob.glob(os.path.join(folder, "session_*.txt")))
    if not files:
        print("FAILED: no session recorded")
        return 1
    text = open(files[-1], encoding="utf-8").read()
    m = re.search(r"^# marks \(F12\):(.*)$", text, re.M)
    marks = [int(v) for v in m.group(1).split()] if m else []
    print(f"session: {os.path.basename(files[-1])}; marks: {marks}")
    if len(marks) != 1 or not q <= marks[0] <= q + 20:
        print(f"FAILED: expected one mark between {q} and {q + 20}")
        ok = False
    print("OK: F12 marked the frame in the recorded session." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
