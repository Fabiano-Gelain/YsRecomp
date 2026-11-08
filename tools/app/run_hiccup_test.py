"""Test app_hiccup_ys: sound without dropouts when the audio device stalls briefly now and then.

Runs the game with `--audio-hiccup MS`: every 5 s the audio device stops for MS milliseconds and the game goes on.
The sound queue grows at each stall; the sound speed adjustment must bring it back down before the next one. It used
to go only up to 0.5% and the queue went past 15 frames and was discarded (a dropout every ~10 s in the lava cave, at
180 Hz). The queue must not overflow even once.

Usage: run_hiccup_test.py EXE FRAMES MS -- GAME_ARGUMENTS...
Without an audio device: exit code 77 (skipped).
"""
import re
import subprocess
import sys


def main():
    sep = sys.argv.index("--")
    exe, frames, ms = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    r = subprocess.run([exe, *sys.argv[sep + 1:], "--frames", str(frames), "--audio-hiccup", str(ms)],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    if "No sound" in r.stdout:
        print("No audio device: test skipped.")
        return 77
    m = re.search(r"the queue ran dry (\d+) time\(s\) and overflowed (\d+) time\(s\); the console waited for the queue (\d+)",
                  r.stdout)
    if r.returncode != 0 or not m:
        print(r.stdout[-2000:])
        print(f"FAILED: exited with code {r.returncode} or without the sound summary")
        return 1
    under, over, holds = (int(g) for g in m.groups())
    print(f"Audio device stalled {ms} ms every 5 s: queue ran dry {under}, overflowed {over}, console waited {holds}")
    if over:
        print("FAILED: the sound queue overflowed (sound dropout)")
        return 1
    print("OK: the sound went on without dropouts.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
