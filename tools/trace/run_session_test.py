"""Session test: runs N frames with a button script on Geargrafx (gg_trace) and on our runtime
(pce_trace), at the same time, and requires the image of every frame and all of the sound to be identical (CRC per
frame, --hashes; sound compared as a single stream). At the first different frame, saves both images as PNG.
Same check as check_session.py, with the game and the buttons given on the command line.

Usage: python run_session_test.py <gg_trace.exe> <pce_trace.exe> <game> <work folder> <frames>
                                  [--input <script>] [--bios <system card>] [--bram <save>]
(with --bios, <game> is the .cue CD image; our side uses Geargrafx's fade, --gg-fader)
Exit: 0 = equal, 1 = different, 77 = game, BIOS or gg_trace missing (test skipped).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_session  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gg")
    ap.add_argument("ours")
    ap.add_argument("game")
    ap.add_argument("work")
    ap.add_argument("frames", type=int)
    ap.add_argument("--input")
    ap.add_argument("--bios")
    ap.add_argument("--bram")
    a = ap.parse_args()

    for path, what in ((a.game, "game"), (a.bios, "BIOS"), (a.bram, "save")):
        if path and not os.path.exists(path):
            print(f"{what} not found ({path}): test skipped")
            return 77
    if not os.path.exists(a.gg):
        print(f"gg_trace not built ({a.gg}): run PCERecomp\\tools\\reference_trace\\build.ps1. Test skipped")
        return 77
    common = []
    if a.input:
        common += ["--input", os.path.abspath(a.input)]
    if a.bios:
        common += ["--bios", os.path.abspath(a.bios)]
    if a.bram:
        common += ["--bram", os.path.abspath(a.bram)]
    return check_session.compare(os.path.abspath(a.gg), os.path.abspath(a.ours), os.path.abspath(a.game),
                                 a.frames, common, ["--gg-fader"], os.path.abspath(a.work))


if __name__ == "__main__":
    sys.exit(main())
