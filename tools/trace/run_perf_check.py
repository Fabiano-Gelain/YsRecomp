"""Performance without changing the game: runs the user's sessions in ys_trace and compares, frame by
frame, the CRC of image and sound (--hashes) and the console state at the end (--dump-state: VRAM, colors, VDC,
RAM, CD RAM) with a reference recorded before the optimizations. Also shows frames per second.

    python tools/trace/run_perf_check.py --save REF   records the reference (before changing anything)
    python tools/trace/run_perf_check.py --check REF  checks against it (afterwards)

Options: --exe (default build/Release/ys_trace.exe), --cue, --bios <system card> (real BIOS instead of the C
routines), --frames N (cuts each session at N frames), --only <part of the name>.
Without the disc: exits with 77 (skipped).
"""
import argparse
import glob
import hashlib
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def session_frames(path):
    last = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"(\d+)", line)
            if m:
                last = int(m.group(1))
    return last + 120


def session_save(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("# save:"):
                return os.path.join(ROOT, line.split(":", 1)[1].strip().replace("\\", "/"))
    return None


def run_one(args, work, session):
    name = os.path.splitext(os.path.basename(session))[0]
    frames = session_frames(session)
    if args.frames:
        frames = min(frames, args.frames)
    hashes = os.path.join(work, name + ".hashes")
    state = os.path.join(work, name + ".state")
    cmd = [args.exe, "--frame", "--input", session, "--hashes", hashes, "--dump-state", state]
    save = session_save(session)
    if save:
        cmd += ["--bram", save]
    if args.bios:
        cmd += ["--bios", args.bios]
    cmd += [args.cue, str(frames), os.path.join(work, name + ".ppm")]
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    secs = time.time() - t0
    if r.returncode != 0:
        return name, frames, secs, None, None, (r.stdout + r.stderr)[-500:]
    with open(state, "rb") as f:
        digest = hashlib.sha1(f.read()).hexdigest()
    with open(hashes, encoding="utf-8") as f:
        lines = f.read().splitlines()
    os.remove(state)
    os.remove(os.path.join(work, name + ".ppm"))
    return name, frames, secs, lines, digest, None


def main():
    ap = argparse.ArgumentParser()
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--save")
    mode.add_argument("--check")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "Release", "ys_trace.exe"))
    ap.add_argument("--cue", default=os.path.join(ROOT, "roms", "Ys Book I & II (USA).cue"))
    ap.add_argument("--bios")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--only")
    args = ap.parse_args()
    if not os.path.exists(args.cue):
        print(f"No disc ({args.cue}): skipped.")
        return 77
    ref = args.save or args.check
    if args.check and not glob.glob(os.path.join(ref, "*.hashes")):
        print(f"No reference in {ref} (record it with --save before changing anything): skipped.")
        return 77
    work = os.path.join(ref, "atual") if args.check else ref
    os.makedirs(work, exist_ok=True)
    sessions = sorted(glob.glob(os.path.join(ROOT, "games", "ys", "sessions", "*.txt")))
    if args.only:
        sessions = [s for s in sessions if args.only in s]
    if args.check:
        # Sessions recorded after the reference have nothing to compare with.
        new = [s for s in sessions if not os.path.exists(os.path.join(ref, os.path.basename(s)[:-4] + ".hashes"))]
        if new:
            print(f"{len(new)} session(s) without a reference (recorded after it): not checked")
        sessions = [s for s in sessions if s not in new]
    if not sessions:
        print("No session to check: skipped.")
        return 77
    with ThreadPoolExecutor(max_workers=min(len(sessions), os.cpu_count() or 4)) as pool:
        results = list(pool.map(lambda s: run_one(args, work, s), sessions))
    ok = True
    total_frames = total_secs = 0
    for name, frames, secs, lines, digest, err in results:
        if err:
            print(f"{name}: FAILED to run\n{err}")
            ok = False
            continue
        total_frames += frames
        total_secs += secs
        line = f"{name}: {frames} frames in {secs:.1f} s ({frames / secs:.0f} frames/s)"
        digest_path = os.path.join(ref, name + ".state.sha1")
        if args.save:
            with open(digest_path, "w") as f:
                f.write(digest)
            print(line + ", reference recorded")
            continue
        with open(os.path.join(ref, name + ".hashes"), encoding="utf-8") as f:
            want = f.read().splitlines()
        with open(digest_path) as f:
            want_digest = f.read().strip()
        cut = args.frames is not None and len(lines) < len(want)  # --frames: only the start, no final state
        diff = next((i for i, (a, b) in enumerate(zip(lines, want)) if a != b), None)
        if diff is None and len(lines) != len(want) and not cut:
            diff = min(len(lines), len(want))
        if diff is not None:
            print(f"{line}: DIFFERENT at frame {diff + 1}")
            ok = False
        elif cut:
            print(f"{line}: equal in the first {len(lines)} frames")
        elif digest != want_digest:
            print(f"{line}: image and sound equal, but the console state at the end is different")
            ok = False
        else:
            print(f"{line}: equal")
    print(f"Total: {total_frames} frames, sum of the times {total_secs:.1f} s "
          f"({total_frames / max(total_secs, 1e-9):.0f} frames/s per session, in parallel)")
    if args.check:
        print("OK: every session equal to the reference." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
