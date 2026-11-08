"""Test app_menu_chip_ys: with the options menu open, the chip sound goes on (the sound driver runs on a copy
of the console). Runs the session without the BIOS with `--menu-test Q` (the menu opens right after an effect starts)
and `--audio-wav`, with CD music at 0: in the first frames with the menu open the effect must still sound (before, the
output went to zero at once). On closing, the effect must not restart (the console receives the sound state from the
copy; before, the first frames after the menu repeated those at its start), the session recorded alongside
(`--record`) notes the menu, and replaying it (`--turbo`) reaches the same console state; without the menu line, a
different one.
Usage: run_menu_chip_test.py EXE FRAMES Q FOLDER -- GAME_ARGUMENTS...
"""
import array
import math
import os
import re
import shutil
import subprocess
import sys
import wave


def main():
    sep = sys.argv.index("--")
    exe, frames, menu_frame, work = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    game = sys.argv[sep + 1:]
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ini = os.path.join(work, "menu.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("fullscreen=0\nwindow_scale=2\nvolume_music=0\n")
    if "--save" in game:  # the game may write to the save: use a copy
        i = game.index("--save") + 1
        shutil.copy(game[i], os.path.join(work, "save.brm"))
        game[i] = os.path.join(work, "save.brm")
    wav = os.path.join(work, "sound.wav")
    rec = os.path.join(work, "session")
    args = [exe, *game, "--config", ini, "--frames", frames, "--menu-test", menu_frame, "--audio-wav", wav,
            "--record", rec]
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
    m = re.search(r"menu open at samples (\d+) to (\d+)", r.stdout)
    if r.returncode != 0 or not m or "--menu-test: menu closed" not in r.stdout:
        print(r.stdout[-3000:])
        print(f"FAILED: the game or the menu script did not run to the end (code {r.returncode})")
        return 1
    start, end = int(m.group(1)), int(m.group(2))
    w = wave.open(wav)
    data = array.array("h", w.readframes(w.getnframes()))

    def rms_frames(at):  # 6 frames of sound from sample at
        out = []
        for k in range(6):
            s = data[(at + k * 735) * 2:(at + (k + 1) * 735) * 2]
            out.append(round(math.sqrt(sum(v * v for v in s) / max(1, len(s)))))
        return out

    rms, after = rms_frames(start), rms_frames(end)
    print(f"chip sound in the first frames with the menu: {rms}; after closing: {after}")
    if min(rms) < 300:
        print("FAILED: the chip sound stopped with the menu open")
        return 1
    if after[0] > rms[0] // 2:
        print("FAILED: after the menu the effect restarted (the console did not receive the menu's sound)")
        return 1

    # The session recorded with the menu replays the same
    state = re.search(r"Console state: (\w+)", r.stdout).group(1)
    names = [f for f in os.listdir(rec) if f.endswith(".txt")]
    session = os.path.join(rec, names[0])
    text = open(session, encoding="utf-8").read()
    if not re.search(r"^# menu \d+ [1-9]\d* \d+$", text, re.M):
        print("FAILED: the recorded session did not note the menu")
        return 1
    no_menu = os.path.join(work, "no_menu.txt")
    with open(no_menu, "w", encoding="utf-8") as f:
        f.write("".join(l for l in text.splitlines(True) if not l.startswith("# menu ")))
    states = []
    for script in (session, no_menu):
        save = os.path.join(work, "replay.brm")
        shutil.copy(session[:-4] + ".brm", save)
        replay = [exe, game[0], "--input", script, "--save", save, "--config", ini, "--frames", frames, "--turbo"]
        r2 = subprocess.run(replay, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
        m2 = re.search(r"Console state: (\w+)", r2.stdout)
        states.append(m2.group(1) if m2 else None)
    print(f"console state: in the window {state}; replaying the session {states[0]}; without the menu line {states[1]}")
    if states[0] != state:
        print("FAILED: the session recorded with the menu does not replay the same")
        return 1
    if states[1] == state:
        print("FAILED: without the menu line the console reached the same state (did the menu not change the console's sound?)")
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())