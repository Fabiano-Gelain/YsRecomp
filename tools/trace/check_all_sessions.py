"""Checks every recorded session not yet checked (acceptance).

For each session_*.txt in the folders (default: games/ys/sessions and dist/YsRecomp/sessions, where the player's
package records with record_sessions=1):
  1. Lockstep (ys_trace --lockstep): the recompiled code runs the whole session together with the interpreter and
     everything is compared at every instruction and frame. Also notes how many instructions fell into the
     interpreter (code the coverage has not seen yet or that the game rewrote).
  2. If the session was recorded with the real BIOS (header "# bios:" other than "-"): check_session.py, the
     interpreter against Geargrafx in image and sound of every frame. Without the BIOS (the player's case)
     Geargrafx is no reference: it needs the BIOS, and the C routines have slightly different timing
     (docs/ROADMAP.md); the lockstep holds, plus the C routines checked by run_bios_check.py.

Each session's result goes to <session>.conferencia (next to it) and an equal session is not checked again while
its number of frames does not change (--force checks everything; different ones are always checked again). With
YsRecomp open, sessions modified less than 3 minutes ago are skipped: the game may still be recording (the
recording is rewritten every minute).

Usage: python tools/trace/check_all_sessions.py [FOLDER ...] [--force] [--jobs N] (N sessions at the same time)
Exit: 0 = all equal, 1 = some different or with an error.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
YS_TRACE = os.path.join(ROOT, "build", "Release", "ys_trace.exe")
FOLDERS = (os.path.join(ROOT, "games", "ys", "sessions"), os.path.join(ROOT, "dist", "YsRecomp", "sessions"))
WORK = os.path.join(ROOT, "build", "sessions")
RECENT_S = 180


def game_running():
    """Is YsRecomp open (Windows: process list)?"""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq YsRecomp.exe", "/NH"], capture_output=True,
                             text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return True
    return "YsRecomp.exe" in out


sys.path.insert(0, HERE)
from check_session import read_header  # noqa: E402


def lockstep(session, info, frames):
    """Returns (ok, result text)."""
    name = os.path.splitext(os.path.basename(session))[0]
    work = os.path.join(WORK, name)
    os.makedirs(work, exist_ok=True)
    # Header paths relative to the project root (sessions recorded with --record games\...)
    cmd = [YS_TRACE, "--frame", "--lockstep", "--input", os.path.abspath(session)]
    if info.get("bios", "-") != "-":
        cmd += ["--bios", os.path.join(ROOT, info["bios"])]
    if info.get("save"):
        save = os.path.join(ROOT, info["save"])
        if not os.path.exists(save):  # copy whose header has only the save name: it sits next to the session
            save = os.path.join(os.path.dirname(os.path.abspath(session)), info["save"])
        cmd += ["--bram", save]
    cmd += [os.path.join(ROOT, info["game"]), str(frames), os.path.join(work, "lockstep.ppm")]
    t = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=work)
    out = r.stdout + r.stderr
    secs = time.time() - t
    ok_line = re.search(r"LOCKSTEP OK: (\d+) instructions and (\d+) frames", out)
    interp = re.search(r"interpreter: (\d+) instructions", out)
    if r.returncode != 0 or not ok_line:
        tail = "\n".join(out.strip().splitlines()[-15:])
        return False, f"lockstep DIFFERENT or error (code {r.returncode}, {secs:.0f} s):\n{tail}"
    total = int(ok_line.group(1))
    n_interp = int(interp.group(1)) if interp else 0
    pct = 100.0 * n_interp / total if total else 0.0
    # With the real BIOS, its code (no generated C for it) counts as interpreted
    note = " (includes the real BIOS)" if info.get("bios", "-") != "-" else ""
    return True, (f"lockstep OK: {total} instructions and {ok_line.group(2)} frames identical to the interpreter "
                  f"({secs:.0f} s); in the interpreter: {n_interp} instructions ({pct:.2f}%){note}")


def geargrafx(session):
    r = subprocess.run([sys.executable, os.path.join(HERE, "check_session.py"), os.path.abspath(session)],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    lines = (r.stdout + r.stderr).strip().splitlines()
    if r.returncode == 0:
        lines = [l for l in lines if l.startswith("EQUAL")] or lines[-1:]
    return r.returncode == 0, "Geargrafx: " + "\n".join(lines[-6:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="*")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--jobs", type=int, default=1, help="sessions checked at the same time")
    a = ap.parse_args()
    if not os.path.exists(YS_TRACE):
        print(f"{YS_TRACE} not found: build first (build.ps1)")
        return 1
    sessions, names = [], set()
    for folder in a.folders or FOLDERS:
        for s in sorted(glob.glob(os.path.join(folder, "session_*.txt"))):
            if os.path.basename(s) not in names:  # the same session copied to games/ys/sessions
                names.add(os.path.basename(s))
                sessions.append(s)
    if not sessions:
        print("No session found.")
        return 0
    running = game_running()
    todo = []
    for s in sessions:
        info = read_header(s)
        frames = int(info.get("frames", "0"))
        result_path = os.path.splitext(s)[0] + ".conferencia"
        label = os.path.basename(s)
        if not info.get("game") or frames <= 0:
            print(f"{label}: header without 'game' or 'frames', skipped")
            continue
        if running and time.time() - os.path.getmtime(s) < RECENT_S:
            print(f"{label}: recorded less than 3 min ago with YsRecomp open (may still be recording), skipped")
            continue
        if not a.force and os.path.exists(result_path):
            with open(result_path, encoding="utf-8") as f:
                old = f.read()
            if old.startswith(f"frames: {frames}\nEQUAL\n"):  # different: check again (it may have been fixed)
                print(f"{label}: already checked - EQUAL")
                continue
        todo.append((s, info, frames, result_path))

    def check(item):
        s, info, frames, result_path = item
        ok, text = lockstep(s, info, frames)
        lines = [text]
        if ok and info.get("bios", "-") != "-":
            ok, text = geargrafx(s)
            lines.append(text)
        verdict = "EQUAL" if ok else "DIFFERENT"
        with open(result_path, "w", encoding="utf-8") as f:
            f.write(f"frames: {frames}\n{verdict}\n" + "\n".join(lines) + "\n")
        return ok, (f"{os.path.basename(s)}: {frames} frames ({frames / 59.82 / 60:.1f} min of play)\n  {verdict}\n  " +
                    "\n  ".join("\n".join(lines).splitlines()))

    failed = checked = 0
    with concurrent.futures.ThreadPoolExecutor(max(1, a.jobs)) as ex:
        for ok, text in ex.map(check, todo):
            print(text, flush=True)
            failed += not ok
            checked += 1
    print(f"{checked} checked now; {failed} with a difference or error." if failed
          else f"{checked} checked now; all equal.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
