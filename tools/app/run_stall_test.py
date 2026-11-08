"""Test app_stall_ys: sound without dropouts when the window gets stuck (dragging, maximizing).

Runs the game with `--stall-window MS`: at 3 s the window thread stops responding for MS milliseconds,
as when Windows holds it. The game runs on its own thread, so it must keep sending
sound: the sound queue must not run dry even once, and the game must reach the requested number of
frames in the normal time (the stalled time must not be added to the game's).

With `device` (test app_stall_device_ys): `--stall-device MS`, the audio device stops along with it (as when
connecting a Bluetooth headset or controller). The sound drops out anyway then; what matters is that the game follows
the clock.

Usage: run_stall_test.py EXE FRAMES MS [device] -- GAME_ARGUMENTS...
Without an audio device: exit code 77 (skipped).
"""
import re
import subprocess
import sys
import time


def main():
    sep = sys.argv.index("--")
    exe, frames, stall_ms = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    game_args = sys.argv[sep + 1:]
    device = "device" in sys.argv[4:sep]
    t0 = time.time()
    r = subprocess.run([exe, *game_args, "--frames", str(frames),
                        "--stall-device" if device else "--stall-window", str(stall_ms)],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    elapsed = time.time() - t0
    print(r.stdout)
    print(r.stderr)
    if "No sound" in r.stdout:
        print("No audio device: test skipped.")
        return 77
    ok = r.returncode == 0
    if not ok:
        print(f"FAILED: exited with code {r.returncode}")
    if "Window stalled for" not in r.stdout:
        print("FAILED: the window was never stalled (did the game end before 3 s?)")
        ok = False
    m = re.search(r"the queue ran dry (\d+) time\(s\) and overflowed (\d+)", r.stdout)
    if not m:
        print("FAILED: no sound summary")
        ok = False
    elif int(m.group(1)) != 0 and not device:
        print(f"FAILED: the sound queue ran dry {m.group(1)} time(s) (audible dropout)")
        ok = False
    if f"Window closed after {frames} frames" not in r.stdout:
        print("FAILED: the game did not reach the requested number of frames")
        ok = False
    # Frames at ~60/s; if the game had stopped along with the window, it would add the stalled time. Measured by the
    # game itself, from the 1st frame to the end (program startup varies: controller connected, disc, busy computer)
    expected = frames / 60.0
    g = re.search(r"Game time: ([0-9.]+) s", r.stdout)
    game = float(g.group(1)) if g else elapsed - 2.0
    print(f"Total time: {elapsed:.1f} s; game: {game:.1f} s (expected: {expected:.1f} s; window stalled: "
          f"{stall_ms / 1000:.1f} s)")
    if game > expected + stall_ms / 1000.0 * 0.75:
        print("FAILED: the game stopped while the window was stalled")
        ok = False
    if device:
        print("OK: the game followed the clock with the window and the audio device stalled." if ok else "FAILED")
    else:
        print("OK: the sound went on without dropouts with the window stalled." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
