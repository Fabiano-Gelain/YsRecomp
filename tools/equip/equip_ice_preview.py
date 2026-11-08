"""Preview of the reflection on the ice: HTML page to pick a sword, shield and armor and watch Adol and his reflection
walking.

Usage: python tools\\equip\\equip_ice_preview.py PCE_TRACE CUE BIOS [OUTPUT.html]
  (default output: games\\ys\\generated\\equip_ice_preview.html, outside Git: it holds the game's frames)

Checks (and fails with exit code 1 if something does not match): with the game's own drawings, the rule of
equip_art.ice_compose gives exactly the ice frames on the disc, in the sword set and the sword-and-shield set, except
the retouches the game made by hand (ICE_RETOUCHES); the normal frames of the ice scene are those of the table; and
palette 14 is the rule's.
"""
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import equip_art as A  # noqa: E402
import equip_preview as P  # noqa: E402

# Reflection pixels the game retouched by hand: (frame set, frame, x, y)
ICE_RETOUCHES = {("sword", 2, 12, 21), ("sword", 6, 5, 24), ("sword", 6, 10, 24), ("sword", 9, 5, 24),
                 ("sword", 9, 10, 24), ("ss", 5, 12, 27), ("ss", 9, 7, 24)}


def shadow_of(normal, ice):
    """Outline pixels the game removes from the normal frame in the ice one (in Adol's rows)."""
    return frozenset((x, y) for y in range(24) for x in range(16) if normal[y][x] == A.OUT and ice[y][x] == 0)


def check_rule(name, normal, ice, pal):
    """The rule on the game's indices against the ice frame on the disc: list of differing (frame, x, y)."""
    bad = []
    for i in range(len(normal)):
        n, r = normal[i], ice[i]
        sh = shadow_of(n, r)
        for y in range(32):
            for x in range(16):
                if y >= 24:
                    src = A.ICE_ROWS.get(y)
                    want = 0
                    if src is not None and n[src][x] and (x, src) not in sh:
                        want = A.ICE_REFLECT[n[src][x]]
                elif (x, y) in sh or not n[y][x]:
                    want = 0
                else:
                    want = A.ICE_TOP.get(n[y][x], n[y][x])
                if want != r[y][x] and (y >= 24 or not _top_retouch(n[y][x], r[y][x])):
                    if (name, i, x, y) not in ICE_RETOUCHES:
                        bad.append((name, i, x, y, n[y][x], r[y][x], want))
    return bad


def _top_retouch(a, b):
    # blade in color 7 in a step with a shield (blade7, as in the normal frame) and a game pixel in color 8 -> 1; the
    # outline removed at the top (row 12 with a shield) already comes in shadow_of
    return (a == 7 and b == 13) or (a == 8 and b == 1)


def image_of(frame, pal):
    return [[pal[c] if c else None for c in r] for r in frame]


def encode(frames, colors):
    s = []
    for fr in frames:
        for row in fr:
            for c in row:
                if c is None:
                    s.append(".")
                else:
                    if c not in colors:
                        colors[c] = len(colors)
                    s.append(chr(0x100 + colors[c]))
    return "".join(s)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    pce_trace, cue, bios = (os.path.abspath(a) for a in sys.argv[1:4])
    out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(ROOT, "games", "ys", "generated", "equip_ice_preview.html")
    with tempfile.TemporaryDirectory() as tmp:
        sets, pal, masks = P.load_frames(pce_trace, cue, bios, tmp)
        ice = P.load_ice(pce_trace, cue, tmp)  # uses the state saved by load_frames
    errors = []
    if tuple(ice["pal14"]) != tuple(A.ICE_PAL):
        errors.append(f"palette 14 differs: {ice['pal14']}")
    if ice["ss_normal"] != sets["ss_vram"]:
        errors.append("normal shield frames of the ice scene differ from the table's")
    bad = check_rule("sword", sets["sword"], ice["sword"], pal) + check_rule("ss", sets["ss_vram"], ice["ss"], pal)
    errors += [f"rule differs from the disc: {b}" for b in bad[:20]]
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"equip_ice_preview: FAILED ({len(bad)} pixels)")
        return 1
    shadows = {"sword": [shadow_of(sets["sword"][i], ice["sword"][i]) for i in range(12)],
               "ss": [shadow_of(sets["ss_vram"][i], ice["ss"][i]) for i in range(12)]}
    items = A.ITEMS["ys1"]
    colors = {}
    combos = {}
    for s in [None] + items["sword"]:
        for h in [None] + items["shield"]:
            for a in [None] + items["armor"]:
                frames, plain = [], []
                for i, (d, k) in enumerate(P.FRAMES):
                    img = P.render(sets, masks, i, d, s, h, a, "ys1", pal)
                    # without a shield the shadow is the sword set's (the shield set only differs on the shield's pixels)
                    frames.append(A.ice_compose(img, shadows["ss" if h else "sword"][i], pal, P.VIEWS_FRAME(sets, s, h)[i],
                                                sets["sword"][i] if h else None))
                    plain.append(img)
                combos[f"{s}|{h}|{a}"] = encode(frames, colors)
                combos[f"n|{s}|{h}|{a}"] = encode(plain, colors)
    pal14 = A.ICE_PAL
    data = {"items": items, "combos": combos,
            "orig": {"sword": encode([image_of(f, pal14) for f in ice["sword"]], colors),
                     "ss": encode([image_of(f, pal14) for f in ice["ss"]], colors)}}
    data["colors"] = ["#%02x%02x%02x" % tuple(v * 255 // 7 for v in c) for c in colors]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(os.path.join(HERE, "equip_ice_preview.html"), encoding="utf-8") as f:
        page = f.read()
    page = page.replace("/*DATA*/null", json.dumps(data, separators=(",", ":"), ensure_ascii=False))
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"equip_ice_preview: OK (rule = disc frames, {len(ICE_RETOUCHES)} retouches; {len(combos) // 2} combinations) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
