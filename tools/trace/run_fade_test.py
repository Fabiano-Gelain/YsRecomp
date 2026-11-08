"""Test of the CD volume fade (runtime/cd/cdrom.h) in Ys, with the script tests/input/ys_goddess.txt:
the game turns the fade on in the intro and turns it on again in the goddess menu.

  - default rule (Mednafen's): from the end of the fade to the end of the session the sound stays muted;
  - with --gg-fader (Geargrafx's rule): the music comes back for about 1 s after the 2nd Enter.
The second case shows that the test sees the difference.

Usage: python run_fade_test.py <pce_trace.exe> <bios> <cue> <script> <work folder>
Exit: 0 = passed, 1 = failed, 77 = BIOS or CD missing (test skipped).
"""
import array
import math
import os
import subprocess
import sys

FRAMES = 3600
VALUES_PER_FRAME = 1470  # 735 stereo samples per frame (approximate; only to find the stretch)
SILENT_FROM, SILENT_TO = 2650, 3600  # fade finished (Enter at frame 2400 + 2.5 s) until the end
BLIP_FROM, BLIP_TO = 3000, 3100      # right after the 2nd Enter (frame 3000)


def rms(samples, f0, f1):
    seg = samples[f0 * VALUES_PER_FRAME:f1 * VALUES_PER_FRAME]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))


def run(ours, bios, cue, script, work, name, extra):
    raw = os.path.join(work, f"{name}.raw")
    subprocess.run([ours, "--frame", "--audio", raw] + extra + ["--bios", bios, "--input", script, cue,
                    str(FRAMES), os.path.join(work, f"{name}.ppm")], check=True)
    with open(raw, "rb") as f:
        return array.array("h", f.read())


def main():
    if len(sys.argv) != 6:
        print(__doc__)
        return 2
    ours, bios, cue, script, work = sys.argv[1:]
    if not os.path.exists(bios) or not os.path.exists(cue):
        print("BIOS or CD not found: test skipped")
        return 77
    os.makedirs(work, exist_ok=True)
    fails = 0

    ours_audio = run(ours, bios, cue, script, work, "mednafen", [])
    v = rms(ours_audio, SILENT_FROM, SILENT_TO)
    print(f"Mednafen rule: volume in frames {SILENT_FROM}-{SILENT_TO} = {v:.0f} (expected 0)")
    fails += v != 0

    gg_audio = run(ours, bios, cue, script, work, "geargrafx", ["--gg-fader"])
    v = rms(gg_audio, BLIP_FROM, BLIP_TO)
    print(f"Geargrafx rule: volume in frames {BLIP_FROM}-{BLIP_TO} = {v:.0f} (expected > 0)")
    fails += v == 0

    print("PASSED" if fails == 0 else "FAILED")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
