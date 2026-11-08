"""Test app_intro_ys: skipping the intro with fast forward (screens read from the game).

Runs YsRecomp (no-BIOS version) twice up to the same frame: watching the intro and with "Enter"
simulated (`--skip-at`) on the loading screen, on "from NEC" and on the logo. Checks:
  - fast forward stops on the frame in which the game's script starts each next screen ("from NEC", logo,
    copyright);
  - the console state at the end is the same as watching it all without pressing anything;
  - skipping, it gets to the end much faster.
Then, the "Skip intro" option (`--intro-to-menu`, straight to the title menu): the automatic RUN lands where the game
accepts it (on the copyright and before the goddess), the fast forward stops when the game starts the title menu
routine, and the console state is the same as a session (`--input`) that presses RUN on those same frames.

Usage: run_intro_test.py EXE CUE FOLDER
"""
import os
import re
import subprocess
import sys
import time

FRAMES = 1800
SKIPS = "100,300,600"
EXPECTED = [(100, 245), (300, 474), (600, 1619)]  # (request, frame in which the script starts the next screen)
MENU_FRAMES = 2400
MENU_RUNS = [1619, 1620, 1748, 1749]  # RUN pressed and released, twice: when the game accepts it
MENU_STOP = 2175  # the game starts the title menu routine ($494C)


def run(exe, args):
    t0 = time.time()
    r = subprocess.run([exe, *args], capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=600)
    elapsed = time.time() - t0
    print(r.stdout)
    print(r.stderr)
    m = re.search(r"Console state: ([0-9A-F]{8})", r.stdout)
    return r, (m.group(1) if m else None), elapsed


def main():
    exe, cue, work = sys.argv[1], sys.argv[2], sys.argv[3]
    if not os.path.exists(cue):
        print(f"No disc ({cue}): test skipped.")
        return 77
    os.makedirs(work, exist_ok=True)
    base = [cue, "--save", os.path.join(work, "intro.brm"), "--frames", str(FRAMES)]
    watch, state_watch, t_watch = run(exe, base)
    skip, state_skip, t_skip = run(exe, [*base, "--skip-at", SKIPS])
    ok = watch.returncode == 0 and skip.returncode == 0
    stops = [(int(a), int(b)) for a, b in re.findall(r"Fast forward: from frame (\d+) to frame (\d+)", skip.stdout)]
    if stops != EXPECTED:
        print(f"FAILED: stops {stops}, expected {EXPECTED}")
        ok = False
    if "Fast forward" in watch.stdout:
        print("FAILED: fast forward without a request")
        ok = False
    if not state_watch or state_watch != state_skip:
        print(f"FAILED: different console state (watching {state_watch}, skipping {state_skip})")
        ok = False
    print(f"Time: watching {t_watch:.1f} s, skipping {t_skip:.1f} s")
    if t_skip > t_watch - 10:
        print("FAILED: skipping was not faster")
        ok = False
    if ok:
        print("OK: each request stopped on the 1st frame of the next screen and the game stayed the same.")

    # Straight to the title menu
    base = [cue, "--save", os.path.join(work, "intro.brm"), "--frames", str(MENU_FRAMES)]
    to_menu, state_menu, t_menu = run(exe, [*base, "--intro-to-menu"])
    presses = [(kind, int(q)) for kind, q in re.findall(r"Straight to menu: RUN (pressed|released) on frame (\d+)",
                                                         to_menu.stdout)]
    m = re.search(r"Straight to menu: stopped on frame (\d+)", to_menu.stdout)
    if to_menu.returncode != 0 or not m or [k for k, _ in presses] != ["pressed", "released"] * 2:
        print(f"FAILED: straight to menu did not press RUN twice and stop ({presses})")
        return 1
    (a, b, c, d), stop = [q for _, q in presses], int(m.group(1))
    print(f"Straight to menu: RUN {a}-{b} and {c}-{d}, stopped on frame {stop}, {t_menu:.1f} s")
    if [a, b, c, d] != MENU_RUNS or stop != MENU_STOP:
        print(f"FAILED: RUN {[a, b, c, d]} and stop {stop}, expected {MENU_RUNS} and {MENU_STOP}")
        ok = False
    script = os.path.join(work, "straight_to_menu.txt")
    with open(script, "w", encoding="utf-8") as f:
        f.write(f"{a} RUN\n{b} -\n{c} RUN\n{d} -\n")
    same, state_same, _ = run(exe, [*base, "--input", script, "--turbo"])  # state only: without waiting for the clock
    if not state_menu or state_menu != state_same:
        print(f"FAILED: state differs from the session with RUN on the same frames ({state_menu}, {state_same})")
        ok = False
    print("OK: straight to menu stopped on the title menu and the game stayed the same." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
