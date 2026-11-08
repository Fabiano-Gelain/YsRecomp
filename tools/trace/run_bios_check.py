"""Checks the BIOS routines rewritten in C (runtime/bios) against the real BIOS over several sessions.

Runs bios_check (see PCERecomp/tools/trace/bios_check.c) on every recorded session, all at the same time, and
merges the results: per routine, how many calls were checked and how many differed.

Usage: python tools/trace/run_bios_check.py [sessions ...] [--routines E009,E012|all] [--max N]
                                            [--frames N] [--bios-check <exe>]
       (no sessions: all of games/ys/sessions/; game, BIOS and save come from each one's header)
Exit: 0 = all equal, 1 = some difference, 2 = usage error.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EXE = os.path.join(ROOT, "build", "Release", "bios_check.exe")


def read_header(path):
    info = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("#"):
                continue
            key, _, value = line[1:].partition(":")
            info[key.strip()] = value.strip()
    return info


ROW = re.compile(r"^(\S+)\s+\$([0-9A-F]{4})\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sessions", nargs="*")
    ap.add_argument("--routines", default="all")
    ap.add_argument("--max", type=int, default=2000)
    ap.add_argument("--frames", type=int, help="limits the number of frames of each session")
    ap.add_argument("--bios-check", default=EXE)
    a = ap.parse_args()

    sessions = a.sessions or sorted(glob.glob(os.path.join(ROOT, "games", "ys", "sessions", "*.txt")))
    if not sessions:
        print("no session found")
        return 2
    if not os.path.exists(a.bios_check):
        print(f"{a.bios_check} not found: build first (build.ps1)")
        return 2

    procs = []
    for s in sessions:
        info = read_header(s)
        game, bios = info.get("game"), info.get("bios")
        frames = int(info.get("frames", "0"))
        if a.frames:
            frames = min(frames, a.frames)
        if not game or not bios or frames <= 0:
            print(f"{s}: header without 'game', 'bios' or 'frames'")
            return 2
        if not os.path.exists(os.path.join(ROOT, game)) or not os.path.exists(os.path.join(ROOT, bios)):
            print(f"missing {game} or {bios}: test skipped")
            return 77
        cmd = [a.bios_check, "--input", s, "--routines", a.routines, "--max", str(a.max)]
        if info.get("save"):
            cmd += ["--bram", info["save"]]
        cmd += ["--bios", bios, game, str(frames)]
        procs.append((s, subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          text=True, encoding="utf-8", errors="replace")))
    print(f"Checking {len(procs)} session(s) ...", flush=True)

    totals = {}
    bad = False
    for s, p in procs:
        out, _ = p.communicate()
        name = os.path.basename(s)
        lines = out.splitlines()
        if p.returncode not in (0, 1) or not any(l.startswith("RESULT") for l in lines):
            print(f"== {name}: bios_check failed (code {p.returncode})")
            print("\n".join(lines[-20:]))
            bad = True
            continue
        for i, line in enumerate(lines):
            m = ROW.match(line)
            if not m:
                continue
            key = (m.group(2), m.group(1))
            t = totals.setdefault(key, [0, 0, 0, 0])
            t[0] += int(m.group(3))  # calls
            t[1] += int(m.group(4))  # checked
            t[2] += int(m.group(5))  # different
            t[3] += int(m.group(6))  # sensitive
            if int(m.group(5)):
                bad = True
                print(f"== {name}: {m.group(1)} different")
                for extra in lines[i + 1:]:
                    if not extra.startswith("    "):
                        break
                    print(extra)
    print(f"{'routine':12s} {'entry':7s} {'calls':>10s} {'checked':>10s} {'different':>10s} {'sensitive':>9s}")
    for (entry, name), t in sorted(totals.items()):
        print(f"{name:12s} ${entry:6s} {t[0]:10d} {t[1]:10d} {t[2]:10d} {t[3]:9d}")
    print("RESULT: THERE ARE DIFFERENCES" if bad else "RESULT: all equal in every session")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
