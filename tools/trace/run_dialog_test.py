"""Test dialog_ys: instant dialog.

The text of the lines is written by the game's text program, which waits for the next frame for each letter when
the task speed is not 0 (app/ys_dialog.h). The option zeroes the speed in its test (hook checked by the code bytes)
and changes the game itself; the session notes the option ("# dialog <frame> <0|1>").

1. The user's session as recorded (no such line: option off) has slow text (Sara's line, at the start of Ys I):
   --dialog-log counts per-letter waits.
2. The same session with "# dialog 0 1": no per-letter waits, speeds zeroed, and the recompiled code equal to the
   interpreter with the option on both (lockstep).
3. YsRecomp records the configuration's option in the session (instant_dialog=1, the default: "# dialog 0 1"; 0: no
   line).

Usage: run_dialog_test.py YS_TRACE YSRECOMP CUE BIOS SESSION.txt FRAMES FOLDER
"""
import glob
import os
import re
import shutil
import subprocess
import sys


def run(cmd, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def summary(out):
    m = re.search(r"dialog: (\d+) slow stretches, (\d+) frames; per-letter waits: (\d+); zeroed speeds: (\d+) "
                  r"\(option (\w+)\)", out)
    return tuple(int(m.group(i)) for i in range(1, 5)) + (m.group(5),) if m else None


def record(exe, cue, bios, work, name, on):
    folder = os.path.join(work, name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    ini = os.path.join(folder, "test.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write(f"fullscreen=0\ninstant_dialog={on}\n")
    code, out = run([exe, cue, "--bios", bios, "--config", ini, "--record", folder, "--turbo", "--frames", "120",
                     "--save", os.path.join(folder, "test.brm")], timeout=300)
    files = sorted(glob.glob(os.path.join(folder, "session_*.txt")))
    if code != 0 or not files:
        print(out[-1500:])
        print(f"FAILED: YsRecomp did not record the session (instant_dialog={on}, exit {code})")
        return None
    with open(files[-1], encoding="utf-8") as f:
        return [line.strip() for line in f if line.startswith("# dialog")]


def main():
    if len(sys.argv) != 8:
        print(__doc__)
        return 2
    ys_trace, ysrecomp, cue, bios, session, frames, work = sys.argv[1:]
    ys_trace, ysrecomp = os.path.abspath(ys_trace), os.path.abspath(ysrecomp)
    os.makedirs(work, exist_ok=True)
    ok = True

    # 1. as on the console
    base = [ys_trace, "--frame", "--dialog-log", "--bios", bios]
    code, out = run(base + ["--input", session, cue, frames, os.path.join(work, "original.ppm")])
    s = summary(out)
    print(f"session as recorded: {s}")
    if code != 0 or not s or s[0] < 1 or s[2] < 100 or s[3] != 0 or s[4] != "off":
        print(out[-1500:])
        print("FAILED: expected slow text (stretches and per-letter waits) with the option off")
        ok = False

    # 2. with the option in the session, and the lockstep
    copy = os.path.join(work, "with_dialog.txt")
    with open(session, encoding="utf-8") as f:
        lines = f.readlines()
    head = [i for i, line in enumerate(lines) if not line.startswith("#")][0]
    lines.insert(head, "# dialog 0 1\n")
    with open(copy, "w", encoding="utf-8") as f:
        f.writelines(lines)
    code, out = run(base + ["--lockstep", "--input", copy, cue, frames, os.path.join(work, "dialog.ppm")])
    s = summary(out)
    print(f"with '# dialog 0 1': {s}")
    if "LOCKSTEP OK" not in out:
        print(out[-2500:])
        print("FAILED: the recompiled code diverged from the interpreter with the option on")
        ok = False
    if not s or s[2] != 0 or s[3] < 1 or s[4] != "on":
        print(out[-1500:])
        print("FAILED: with the option on the text still waited letter by letter (or nothing was zeroed)")
        ok = False

    # 3. the session recorded by YsRecomp notes the configuration's option
    on = record(ysrecomp, cue, bios, work, "record_on", 1)
    off = record(ysrecomp, cue, bios, work, "record_off", 0)
    print(f"recorded with instant_dialog=1: {on}; with 0: {off}")
    if on != ["# dialog 0 1"] or off != []:
        print("FAILED: the recorded session did not note the option as it should")
        ok = False

    print("dialog_ys: OK" if ok else "dialog_ys: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
