#!/usr/bin/env python3
"""Late frame that could not switch lists, with a shrunk window (fill, HUD at 3/4; in the window, with the real frame
preparation, in real time).

On opening or closing a window the game runs late: the console repeats the sprite table and the PC tries the next list
(motion_fix_late). With the window being painted during the image the switch fails and the frame has no list for smooth
motion. The ring around the window uses the list the console showed: the masks touching the window are redone and the
enemy does not appear in the dark.

Checks in PCE_RING_LOG, on frames with lista=2:
- on the EXPECTED frames, there are redone masks (m > 0);
- the sprite layer equals that of a normal neighboring frame with the same windows (the enemy did not appear).
And that no frame with a window stayed outside the engine (lista=0).

Usage: run_ring_shown_list_test.py YSRECOMP CUE SESSION.txt SESSION.brm FOLDER FRAMES EXPECTED[,EXPECTED...]
"""
import os
import re
import shutil
import subprocess
import sys


def main():
    exe, cue, session, bram, work, frames, expected = sys.argv[1:8]
    if not os.path.exists(cue):
        print(f"No Ys disc ({cue}): test skipped.")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ini = os.path.join(work, "window.ini")
    with open(ini, "w", encoding="utf-8") as f:
        f.write("; ring with the list the console showed test\nfullscreen=0\ncrop=fill\naspect=tv\nsmooth_motion=1\n"
                "filter=sharp\ncrt=off\npause_in_background=0\n")
    save = os.path.join(work, "window.brm")
    shutil.copyfile(bram, save)
    log = os.path.join(work, "ring.txt")
    env = dict(os.environ, PCE_RING_LOG=log)
    r = subprocess.run([exe, cue, "--input", session, "--save", save, "--frames", frames, "--config", ini, "--window-size",
                        "1280", "720"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900,
                       env=env)
    if r.returncode != 0 or f"Window closed after {frames} frames" not in r.stdout:
        print(f"FAILED: the game did not reach {frames} frames (exit {r.returncode})")
        print(r.stdout[-1500:])
        return 1
    rows = {}
    cur = None
    with open(log, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"F (\d+) (.*) lista=(\d) m=(\d+) sat=([0-9A-F]+)", line)
            if m:
                cur = {"seq": int(m[1]), "boxes": m[2], "lista": int(m[3]), "m": int(m[4]), "sat": m[5], "spr": None}
                rows[cur["seq"]] = cur
            elif line.startswith(" S ") and cur:
                cur["spr"] = line.split()[1]
    fails = 0
    shown = sorted(q for q, v in rows.items() if v["lista"] == 2)
    outside = sorted(q for q, v in rows.items() if v["lista"] == 0)
    same = 0
    print(f"{len(rows)} frames with a window; list the console showed in {len(shown)}: {shown[:12]}; outside the engine in "
          f"{len(outside)}: {outside[:12]}")
    for q in (int(v) for v in expected.split(",")):
        if q not in shown:
            print(f"FAILED: frame {q} should use the list the console showed")
            fails += 1
        elif rows[q]["m"] == 0:
            print(f"FAILED: frame {q} without redone masks")
            fails += 1
    for q in shown:
        # the normal neighboring frame, with the same windows: the sprite layer is the same (the enemy did not appear)
        v = rows[q]
        near = [rows[k] for k in (q - 1, q + 1) if k in rows and rows[k]["lista"] == 1 and rows[k]["boxes"] == v["boxes"]]
        if near:
            same += 1
            if v["spr"] not in [n["spr"] for n in near]:
                print(f"FAILED: frame {q}: layer {v['spr']} differs from the neighbors {[n['spr'] for n in near]}")
                fails += 1
    if outside:
        print(f"FAILED: {len(outside)} frames with a window outside the engine")
        fails += 1
    print(f"layer equal to a neighbor's checked in {same} frames")
    print("ring with the shown list:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
