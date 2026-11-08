"""Preview of the equipment on Adol (hand-drawn swords and shields): HTML page to pick a sword, shield and armor and
watch Adol walking.

Usage: python tools\\equip\\equip_preview.py PCE_TRACE CUE BIOS [OUTPUT.html]
  (default output: games\\ys\\generated\\equip_preview.html, outside Git: it holds the game's frames)

Adol's frames come from the player's disc: pce_trace runs the session games\\ys\\sessions\\session_20261017_153505
(with the BIOS) up to frame 12259 (still without a sword) and up to 25000 (with the sword) and saves the state; the
12 frames of each set are in patterns $D0-$E5 of VRAM (docs/ys_notes.md, "Equipment on Adol"
Adol). The rules and the new drawings are in equip_art.py.

The equipment screen icons (for comparison) come from two sessions with the menu open (ICONS, without the BIOS).

Checks (and fails with exit code 1 if something does not match): the blade in every frame with a sword and in none
without; each shield showing in every frame; each piece changing the image; and the page, which builds Adol in
layers (body, sword, shield), equal to the direct drawing of equip_art.compose in every combination.
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pcerecomp_dir  # noqa: E402
sys.path.insert(0, pcerecomp_dir.TRACE_TOOLS)
import equip_art as A  # noqa: E402
import vdc_state as V  # noqa: E402

SESSION = os.path.join(ROOT, "games", "ys", "sessions", "session_20261017_153505.txt")
# frame set: (session frame, pattern added to those of DIRS); with a shield the game puts them in $180-$195
SOURCES = {"nosword": (12259, 0), "sword": (25000, 0), "ss": (43950, 0xB0)}
ICE_OFFSET = 0x18  # ice frames: 12 patterns after the normal ones, in the same steps
# with a shield VRAM only has the ice ones during the scene: a session in which Adol walks on it (without the BIOS)
ICE_SS_SESSION = os.path.join(ROOT, "games", "ys", "sessions", "session_20261023_000904")
ICE_SS_FRAME, ICE_SS_BASE = 12800, 0x18  # base = DIRS patterns + base at frame 12800 ($E8-$FD)
# patterns of each direction: standing, step 1, step 2 (the walk is standing, step 1, standing, step 2)
DIRS = [(0xD1, 0xD0, 0xD4), (0xD8, 0xD5, 0xD9), (0xDD, 0xDC, 0xE0), (0xE4, 0xE1, 0xE5)]
FRAMES = [(d, k) for d in range(4) for k in range(3)]
# what the page shows: nothing, sword only, sword and shield, shield only (the shield set, without the blade)
VIEWS = {"nosword": ("nosword", False), "sword": ("sword", True), "ss": ("ss", True), "sn": ("ss", False)}


def view_of(sword, shield):
    return ("ss" if sword else "sn") if shield else ("sword" if sword else "nosword")


def sprite16x32(vram, pat):
    img = []
    for cy in range(2):
        base = (pat + cy * 2) << 6
        for y in range(16):
            p = [vram[(base + y + 16 * k) & 0x7FFF] for k in range(4)]
            img.append([sum(((p[k] >> (15 - x)) & 1) << k for k in range(4)) for x in range(16)])
    return img


def load_frames(pce_trace, cue, bios, tmp):
    """The 3 frame sets from the disc and the shield pixels of each frame (those of the shield frame that differ
    from the sword-only one, minus skin and red clothes: the arm changes a bit)."""
    sets = {}
    for name, (q, add) in SOURCES.items():
        dump = os.path.join(tmp, name + ".bin")
        subprocess.run([pce_trace, "--frame", "--dump-state", dump, "--input", SESSION, "--bios", bios, cue,
                        str(q), os.path.join(tmp, name + ".ppm")], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        s = V.load(dump)
        sets[name] = [sprite16x32(s["vram"], DIRS[d][k] + add) for d, k in FRAMES]
        if name == "sword":
            pal = [V.rgb(s["col"][256 + 13 * 16 + c]) for c in range(16)]
    # in a front step of the shield set ($184) the game paints the blade in color 7: where the sword-only frame
    # has blade, color 7 of the shield frame is blade too (the frame as it is in VRAM stays in "ss_vram")
    sets["ss_vram"] = [[list(r) for r in f] for f in sets["ss"]]
    for i in range(len(FRAMES)):
        for y in range(32):
            for x in range(16):
                if sets["sword"][i][y][x] == A.BLADE and sets["ss"][i][y][x] == 7:
                    sets["ss"][i][y][x] = A.BLADE
    body = set(A.CLOTH) | {A.SKIN, 6, A.BLADE}  # the blade also changes a bit from frame to frame
    masks = [frozenset((x, y) for y in range(32) for x in range(16)
                       if sets["ss"][i][y][x] != sets["sword"][i][y][x] and sets["ss"][i][y][x] not in body
                       and sets["ss"][i][y][x] != 0)
             for i in range(len(FRAMES))]
    return sets, pal, masks


def load_ice(pce_trace, cue, tmp):
    """Ice frames from the disc: {"sword": 12 frames, "ss": 12 frames, "ss_normal": the normal ones of the same scene}.
    Runs after load_frames, with the same tmp: with a sword, the ice ones are already in the VRAM of the state it saved."""
    out = {}
    s = V.load(os.path.join(tmp, "sword.bin"))
    out["sword"] = [sprite16x32(s["vram"], DIRS[d][k] + ICE_OFFSET) for d, k in FRAMES]
    dump = os.path.join(tmp, "ice_ss.bin")
    subprocess.run([pce_trace, "--frame", "--dump-state", dump, "--bram", ICE_SS_SESSION + ".brm", "--input",
                    ICE_SS_SESSION + ".txt", cue, str(ICE_SS_FRAME), os.path.join(tmp, "ice_ss.ppm")], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    s = V.load(dump)
    out["ss"] = [sprite16x32(s["vram"], DIRS[d][k] + ICE_SS_BASE) for d, k in FRAMES]
    out["ss_normal"] = [sprite16x32(s["vram"], DIRS[d][k]) for d, k in FRAMES]
    out["pal14"] = [V.rgb(s["col"][256 + 14 * 16 + c]) for c in range(16)]
    return out


def VIEWS_FRAME(sets, sword, shield):
    """The game frames that the view of these pieces uses"""
    return sets[VIEWS[view_of(sword, shield)][0]]


def render(sets, masks, i, d, sword, shield, armor, game, pal):
    name, _ = VIEWS[view_of(sword, shield)]
    return A.compose(sets[name][i], pal, d, sword, shield, armor, game, masks[i] if name == "ss" else frozenset(),
                     sets["sword"][i], FRAMES[i][1])


# Equipment screen icons (for comparison on the page): with the menu open, icon n (16x16) is in tiles
# base+4n..base+4n+3 of VRAM, background palette 10 (docs/ys_notes.md); order: swords, armors, shields
PARTIDAS = os.path.join(ROOT, "games", "ys", "sessions")
ICONS = {
    "ys1": dict(session="session_20261021_214858", frame=22600, base=0x670, first=dict(sword=0, shield=5, armor=10)),
    "ys2": dict(session="session_20261023_230635", frame=7450, base=0x650, first=dict(sword=0, armor=6, shield=12)),
}


def load_icons(pce_trace, cue, tmp):
    icons = {}
    for game, src in ICONS.items():
        dump = os.path.join(tmp, game + "_menu.bin")
        args = [pce_trace, "--frame", "--dump-state", dump, "--input", os.path.join(PARTIDAS, src["session"] + ".txt")]
        brm = os.path.join(PARTIDAS, src["session"] + ".brm")
        if os.path.exists(brm):
            args += ["--bram", brm]
        subprocess.run(args + [cue, str(src["frame"]), os.path.join(tmp, game + "_menu.ppm")], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        s = V.load(dump)
        pal = [V.rgb(s["col"][10 * 16 + c]) for c in range(16)]
        icons[game] = {}
        for kind, first in src["first"].items():
            for n, item in enumerate(A.ITEMS[game][kind]):
                t = src["base"] + 4 * (first + n)
                img = [[None] * 16 for _ in range(16)]
                for q in range(4):
                    tp = V.tile_pixels(s["vram"], t + q)
                    for y in range(8):
                        for x in range(8):
                            c = tp[y][x]
                            img[y + 8 * (q >> 1)][x + 8 * (q & 1)] = pal[c] if c else None
                icons[game][item] = img
    return icons


def diff_layer(top, under):
    """Pixels of top that changed relative to under (the rest: None). An erased pixel becomes 'x'."""
    return [[(top[y][x] if top[y][x] is not None else "x") if top[y][x] != under[y][x] else None
             for x in range(16)] for y in range(32)]


def stack(*layers):
    out = [[None] * 16 for _ in range(32)]
    for L in layers:
        for y in range(32):
            for x in range(16):
                c = L[y][x]
                if c == "x":
                    out[y][x] = None
                elif c is not None:
                    out[y][x] = c
    return out


def check(sets, pal, masks):
    errors = []
    for i, (d, k) in enumerate(FRAMES):
        if not A.blade_pixels(sets["sword"][i]):
            errors.append(f"sword frame {i}: no blade")
        if A.blade_pixels(sets["nosword"][i]):
            errors.append(f"no-sword frame {i}: has a blade")
        if len(masks[i]) < 10:
            errors.append(f"shield frame {i}: only {len(masks[i])} shield pixels")
    for game, items in A.ITEMS.items():
        for kind in ("sword", "shield", "armor"):
            seen = {}
            for it in [None] + items[kind]:
                args = dict(sword=None, shield=None, armor=None)
                args[kind] = it
                img = json.dumps(render(sets, masks, 0, 0, args["sword"], args["shield"], args["armor"], game, pal))
                if img in seen:
                    errors.append(f"{game}: {it} same as {seen[img]}")
                seen[img] = it
    return errors


def build_layers(sets, pal, masks, game):
    """Page layers, by view (VIEWS): body (armor), sword (per armor) and shield (its pixels)."""
    items = A.ITEMS[game]
    L = {"body": {}, "sword": {}, "shield": {}}
    rng = list(enumerate(FRAMES))
    for a in [None] + items["armor"]:
        for v in VIEWS:
            sh = items["shield"][0] if v in ("ss", "sn") else None
            # body of a view with a shield: the shield pixels keep the game's colors
            L["body"][f"{a}|{v}"] = [render(sets, masks, i, d, None, None, a, game, pal) if not sh else
                                     A.compose(sets["ss"][i], pal, d, None, None, a, game, masks[i], sets["sword"][i], k)
                                     for i, (d, k) in rng]
    for s in items["sword"]:
        for a in [None] + items["armor"]:
            for v in ("sword", "ss"):
                name = VIEWS[v][0]
                mk = (lambda i: masks[i]) if v == "ss" else (lambda i: frozenset())
                L["sword"][f"{s}|{a}|{v}"] = [diff_layer(A.compose(sets[name][i], pal, d, s, None, a, game, mk(i),
                                                                   sets["sword"][i], k),
                                                         L["body"][f"{a}|{v}"][i]) for i, (d, k) in rng]
    # shield (the front drawing and the body under it depend on the armor): what changes on top of the body and of
    # a reference sword (the sword and the shield do not touch the same pixels: the check below proves it)
    for h in items["shield"]:
        for a in [None] + items["armor"]:
            for v in ("ss", "sn"):
                s0 = items["sword"][0] if v == "ss" else None
                frames = []
                for i, (d, k) in rng:
                    under = [L["body"][f"{a}|{v}"][i]] + ([L["sword"][f"{s0}|{a}|{v}"][i]] if s0 else [])
                    frames.append(diff_layer(render(sets, masks, i, d, s0, h, a, game, pal), stack(*under)))
                L["shield"][f"{h}|{a}|{v}"] = frames
    # the page stacks the layers: it must give the same as the direct drawing, in every combination
    errors = []
    for s in [None] + items["sword"]:
        for h in [None] + items["shield"]:
            v = view_of(s, h)
            for a in [None] + items["armor"]:
                for i, (d, k) in rng:
                    layers = [L["body"][f"{a}|{v}"][i]]
                    if s:
                        layers.append(L["sword"][f"{s}|{a}|{v}"][i])
                    if h:
                        layers.append(L["shield"][f"{h}|{a}|{v}"][i])
                    if stack(*layers) != render(sets, masks, i, d, s, h, a, game, pal):
                        errors.append(f"{game}: layers != drawing in {s}/{h}/{a} frame {i}")
                        break
    return L, errors


def encode(layers_by_key, colors):
    """Each layer: 12 frames x 512 pixels, one character per pixel ('.' = nothing, '-' = erase; color n = U+0100+n)."""
    out = {}
    for name, frames in layers_by_key.items():
        s = []
        for fr in frames:
            for row in fr:
                for c in row:
                    if c is None:
                        s.append(".")
                    elif c == "x":
                        s.append("-")
                    else:
                        if c not in colors:
                            colors[c] = len(colors)
                        s.append(chr(0x100 + colors[c]))
        out[name] = "".join(s)
    return out


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    pce_trace, cue, bios = (os.path.abspath(a) for a in sys.argv[1:4])
    out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(ROOT, "games", "ys", "generated", "equip_preview.html")
    if not (os.path.exists(cue) and os.path.exists(bios)):
        print("equip_preview: no disc or BIOS in roms\\ (skipped)")
        return 77
    with tempfile.TemporaryDirectory() as tmp:
        sets, pal, masks = load_frames(pce_trace, cue, bios, tmp)
        icons = load_icons(pce_trace, cue, tmp)
    errors = check(sets, pal, masks)
    colors = {}
    data = {"orig": encode({name: [[[pal[c] if c else None for c in r] for r in f] for f in sets[name]]
                            for name in SOURCES}, colors),
            "games": {}}
    combos = 0
    for game, items in A.ITEMS.items():
        L, errs = build_layers(sets, pal, masks, game)
        errors += errs
        combos += (len(items["sword"]) + 1) * (len(items["shield"]) + 1) * (len(items["armor"]) + 1)
        data["games"][game] = {"items": items, "body": encode(L["body"], colors),
                               "sword": encode(L["sword"], colors), "shield": encode(L["shield"], colors),
                               "icons": encode({k: [v] for k, v in icons.get(game, {}).items()}, colors)}
    data["colors"] = ["#%02x%02x%02x" % tuple(v * 255 // 7 for v in c) for c in colors]
    if errors:
        for e in errors[:20]:
            print("ERROR:", e)
        print(f"equip_preview: FAILED ({len(errors)} errors)")
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(os.path.join(HERE, "equip_preview.html"), encoding="utf-8") as f:
        page = f.read()
    page = page.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")))
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"equip_preview: OK ({combos} combinations x 12 frames, layers = direct drawing) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
