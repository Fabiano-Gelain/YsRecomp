"""Test app_menu_image_ys: the image under the options menu is the last one the window showed, with what the
window adds (equipment on Adol, enemy bars and rewards), and not the console's raw frame (which may already be
the next one). Runs the session with `--menu-test Q` and PCE_MENU_HIDDEN=1 (the menu is not drawn) and `--screen-dump`:
each image with the menu open (m*.bmp) must equal the last one shown before it (q*.bmp). Without smooth motion (with
it the screen shows images between two frames), with the crop off and in wide.
Usage: run_menu_under_test.py EXE FRAMES Q FOLDER -- GAME_ARGUMENTS...
"""
import os
import shutil
import subprocess
import sys


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    off = int.from_bytes(data[10:14], "little")
    return data[18:26], data[off:]  # width/height and the pixels


def main():
    sep = sys.argv.index("--")
    exe, frames, menu_frame, work = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    game_args = sys.argv[sep + 1:]
    ok = True
    for crop in ("off", "wide"):
        out = os.path.join(work, crop)
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        ini = os.path.join(out, "menu.ini")
        with open(ini, "w", encoding="utf-8") as f:
            f.write(f"fullscreen=0\nwindow_scale=2\nsmooth_motion=0\ncrop={crop}\nvisual_equipment=1\n"
                    "enemy_hp_bars=1\nreward_popups=1\n")
        game = list(game_args)
        if "--save" in game:  # the game may write to the save: each round with a copy
            i = game.index("--save") + 1
            shutil.copy(game[i], os.path.join(out, "save.brm"))
            game[i] = os.path.join(out, "save.brm")
        args = [exe, *game, "--config", ini, "--turbo", "--frames", frames, "--menu-test", str(menu_frame),
                "--screen-dump", str(menu_frame - 60), "120", out]
        r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900,
                           env={**os.environ, "PCE_MENU_HIDDEN": "1"})
        if r.returncode != 0 or "--menu-test: menu closed" not in r.stdout:
            print(r.stdout[-3000:])
            print(f"FAILED ({crop}): the game or the menu script did not run to the end (code {r.returncode})")
            ok = False
            continue
        names = sorted((n for n in os.listdir(out) if n.endswith(".bmp")), key=lambda n: (int(n[1:7]), int(n[8:-4])))
        first = next((i for i, n in enumerate(names) if n[0] == "m"), None)
        if not first:
            print(f"FAILED ({crop}): no images of the menu or of the session before it: {names[:5]}")
            ok = False
            continue
        last = names[first - 1]
        ref = read_bmp(os.path.join(out, last))
        menu = [n for n in names if n[0] == "m"]
        newer = sum(int(n[1:7]) > int(last[1:7]) for n in menu)
        bad = [n for n in menu if read_bmp(os.path.join(out, n)) != ref]
        print(f"{crop}: {len(menu)} images with the menu against {last}; {len(bad)} different; "
              f"{newer} with the console on a newer frame than the one shown")
        if bad:
            print(f"FAILED ({crop}): image under the menu differs from the last one shown: {bad[:5]}")
            ok = False
    print("OK: under the menu, the last image shown." if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
