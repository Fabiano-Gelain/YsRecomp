"""Checks a recorded session (pce_runtime --record) against Geargrafx.

Runs the session's button script on Geargrafx (gg_trace) and on our runtime (pce_trace), both at the
same time, recording a "fingerprint" (CRC) of the image and sound of every frame (--hashes).
Then compares frame by frame and reports the first different frame. If there is a difference, saves
both images of that frame (and the difference) as PNG.

Our side's sound uses Geargrafx's fade (--gg-fader), so the comparison is valid (see runtime/cd/cdrom.h).
Geargrafx's output is reused while nothing changes (gg_cache.py). The session_* tests use compare()
(tools/trace/run_session_test.py).

Usage: python tools/trace/check_session.py <session.txt> [--frames N] [--work <folder>]
       (game, BIOS and number of frames come from the session header; --frames limits)
Exit: 0 = all equal, 1 = different, 2 = usage error.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import pcerecomp_dir  # noqa: E402
GG = pcerecomp_dir.GG_TRACE
OURS = os.path.join(ROOT, "build", "Release", "pce_trace.exe")
sys.path.insert(0, HERE)
sys.path.insert(0, pcerecomp_dir.TRACE_TOOLS)
import gg_cache  # noqa: E402


def read_header(path):
    info = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("#"):
                continue
            key, _, value = line[1:].partition(":")
            info[key.strip()] = value.strip()
    return info


def dialog_on_frame(path):
    """First frame where the session turns instant dialog on ("# dialog <frame> 1"), or None."""
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.split()
            if len(p) == 4 and p[0] == "#" and p[1] == "dialog" and p[3] == "1":
                return int(p[2])
    return None


def read_hashes(path):
    with open(path) as f:
        return [line.split() for line in f if line.strip()]


def first_audio_diff(ref_path, our_path, our_hashes):
    """Frame (ours) of the first different sound value, or None if the streams are equal."""
    chunk = 1 << 20
    offset = 0
    with open(ref_path, "rb") as fr, open(our_path, "rb") as fo:
        while True:
            r, o = fr.read(chunk), fo.read(chunk)
            if r != o:
                n = min(len(r), len(o))
                i = next((k for k in range(n) if r[k] != o[k]), n)
                value = (offset + i) // 2
                total = 0
                for h in our_hashes:
                    total += int(h[3])
                    if total > value:
                        return int(h[0])
                return int(our_hashes[-1][0]) if our_hashes else 0
            if not r:
                return None
            offset += len(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "sessions"))
    ap.add_argument("--ours", default=OURS,
                    help="our executable (default: pce_trace, interpreter only; e.g. build/Release/ys_trace.exe "
                         "for the recompiled code)")
    ap.add_argument("--hle", action="store_true",
                    help="our side uses the BIOS routines rewritten in C (runtime/bios/hle.h)")
    ap.add_argument("--hle-only", help="only these C routines (hex entries separated by commas)")
    a = ap.parse_args()
    ours_exe = os.path.abspath(a.ours)

    info = read_header(a.session)
    game = info.get("game")
    bios = info.get("bios", "-")
    frames = a.frames or int(info.get("frames", "0"))
    if not game or frames <= 0:
        print("session header without 'game' or 'frames'")
        return 2
    for exe in (GG, ours_exe):
        if not os.path.exists(exe):
            print(f"{exe} not found: build first (build.ps1 and PCERecomp\\tools\\reference_trace\\build.ps1)")
            return 2

    # Instant dialog changes the game itself; with it on, Geargrafx (the real console) is no reference.
    # The check holds up to the frame where the session turned it on (the lockstep still holds for all of it).
    on = dialog_on_frame(a.session)
    if on is not None:
        if on <= 0:
            print("EQUAL: not checked (instant dialog on from the start: the game changes, Geargrafx is no reference;"
                  " only the lockstep checks this session)")
            return 0
        if on < frames:
            print(f"instant dialog turned on at frame {on}: the check against Geargrafx holds up to it")
            frames = on

    name = os.path.splitext(os.path.basename(a.session))[0]
    work = os.path.join(a.work, name)
    common = ["--input", a.session]
    if bios != "-":
        common += ["--bios", bios]
    if info.get("save"):
        common += ["--bram", info["save"]]  # the session started from this save (CD save memory)
    print(f"Checking {frames} frames ({frames / 59.82 / 60:.1f} min of play) of {a.session} ...", flush=True)
    ours_opts = ["--gg-fader"] + (["--hle"] if a.hle else []) + (["--hle-only", a.hle_only] if a.hle_only else [])
    return compare(GG, ours_exe, game, frames, common, ours_opts, work)


def compare(gg_exe, ours_exe, game, frames, common, ours_opts, work):
    """Runs the session on both (common: --input/--bios/--bram) and compares frame by frame."""
    os.makedirs(work, exist_ok=True)
    common = ["--frame"] + common
    ref_h, our_h = os.path.join(work, "hashes_ref.txt"), os.path.join(work, "hashes_ours.txt")
    ref_a, our_a = os.path.join(work, "som_ref.raw"), os.path.join(work, "som_ours.raw")
    ref_fim = os.path.join(work, "ref_fim.ppm")
    gg = gg_cache.start([gg_exe, "--hashes", ref_h, "--audio", ref_a] + common + [game, str(frames), ref_fim],
                        [ref_h, ref_a, ref_fim], cwd=ROOT)
    ours = subprocess.Popen([ours_exe, "--hashes", our_h, "--audio", our_a] + ours_opts + common +
                            [game, str(frames), os.path.join(work, "ours_end.ppm")], cwd=ROOT)
    if gg.wait() != 0 or ours.wait() != 0:
        print("one of the tools failed (see the messages above)")
        return 1

    ref, our = read_hashes(ref_h), read_hashes(our_h)
    # Image: frames where the game changes resolution mid-screen are drawn by Geargrafx 1024 wide
    # and by us with the new width; those cannot be compared pixel by pixel.
    first_image = None
    mixed = 0
    for r, o in zip(ref, our):
        if r[1:3] == o[1:3]:
            continue
        if r[1].startswith("1024x"):
            mixed += 1
            continue
        first_image = int(r[0])
        break
    # Sound: compared as a single stream (the split between frames sometimes differs by a few values).
    first_audio = first_audio_diff(ref_a, our_a, our)
    if len(ref) != len(our):
        print(f"different number of frames: Geargrafx {len(ref)}, ours {len(our)}")
    if mixed:
        print(f"({mixed} frames with a mid-screen resolution change not compared)")
    if first_image is None and first_audio is None and len(ref) == len(our):
        print(f"EQUAL: {len(ref)} frames, image and sound identical to Geargrafx")
        return 0

    if first_audio is not None:
        print(f"SOUND different from frame {first_audio} ({first_audio / 59.82:.1f} s)")
    if first_image is not None:
        n = first_image
        print(f"IMAGE different from frame {n} ({n / 59.82:.1f} s); saving the images ...")
        ref_ppm, our_ppm = os.path.join(work, f"ref_{n}.ppm"), os.path.join(work, f"ours_{n}.ppm")
        subprocess.run([gg_exe] + common + [game, str(n), ref_ppm], stdout=subprocess.DEVNULL, cwd=ROOT, check=True)
        subprocess.run([ours_exe] + ours_opts + common + [game, str(n), our_ppm], cwd=ROOT, check=True)
        subprocess.run([sys.executable, os.path.join(pcerecomp_dir.TRACE_TOOLS, "compare_frames.py"), ref_ppm, our_ppm,
                        "--png", os.path.join(work, f"frame_{n}")])
    return 1


if __name__ == "__main__":
    sys.exit(main())
