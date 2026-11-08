"""Generates the YsRecomp.exe icon (art replaced on 2026-10-25): the Adol drawn by the user in
docs/Icone_novo.png (2048x2048 JPEG, outside Git; a 16x18-pixel sprite on a slightly irregular grid, read
pixel by pixel from the color at the center of each cell, with similar colors merged).

Sizes (integer scaling, sharp pixels): 256 (sprite 14x), 48 (2x) and 24 (1x) with the whole body; 32
(2x) and 16 (1x) with only the face (the whole body is unreadable that small). The 256 one as PNG, the others as
BMP, as Windows expects. Output: runtime/app/ysrecomp.ico. With --preview FILE.png also writes a preview of each
size, side by side.

Usage: python tools/package/make_icon.py [--preview preview.png]
"""
import os
import struct
import sys
import zlib

ART = [
    "......KKKKK.....",
    "....KKRrrrrKK...",
    "...KRRrRrHHHRK..",
    "...KrrrKRHHHHrK.",
    ".K.KrrRKKRHHHrK.",
    "KWKKrRKSSKRRHrK.",
    "KWKKrRSkSSkSRrK.",
    "KWWKkKSkSSkSKK..",
    ".KW.BdKSSSSKr...",
    ".KWkRbdKKKKbRHHK",
    ".KWkRrbcCCcbKRHK",
    "KGKKkrBcCCbKKRHK",
    "KGSKcRBBbBBcBSRK",
    "KGKBcBBbCbBcbSK.",
    ".KKbBRRBCBRncKK.",
    "...dRrrRcRrRKK..",
    "...KBbbbBbbbB...",
    "....KBcoKBcB....",
]

COLORS = {
    "K": (52, 52, 52),     # outline
    "k": (37, 37, 37),     # dark outline (eyes)
    "r": (184, 1, 1),      # hair
    "R": (116, 1, 1),      # hair, shadow
    "H": (254, 2, 2),      # hair, highlight
    "S": (249, 185, 124),  # skin
    "W": (182, 240, 246),  # blade
    "G": (249, 133, 60),   # sword hilt
    "B": (4, 57, 253),     # armor
    "o": (1, 23, 253),     # armor, deep
    "d": (20, 46, 98),     # armor, shadow
    "n": (40, 97, 159),    # armor, half shadow
    "b": (50, 124, 252),   # armor, light
    "c": (2, 181, 240),    # armor, highlight
    "C": (102, 241, 236),  # armor, strong highlight
}

# Face: rows 0 to 12, columns 3 to 15 of the art (13x13, hair, face and shoulders), in a 16x16 box (and 2x in the 32 one).
FACE = [line[3:16] for line in ART[0:13]]


def pixels(art, scale, size):
    """Rows of (r, g, b, a): `art` scaled `scale` times, centered in a size x size box."""
    clear = (0, 0, 0, 0)
    w, h = len(art[0]) * scale, len(art) * scale
    left, top = (size - w) // 2, (size - h) // 2
    rows = [[clear] * size for _ in range(top)]
    for line in art:
        row = [clear] * left
        for ch in line:
            row.extend([clear if ch == "." else COLORS[ch] + (255,)] * scale)
        row.extend([clear] * (size - len(row)))
        rows.extend([list(row) for _ in range(scale)])
    rows.extend([[clear] * size for _ in range(size - len(rows))])
    return rows


# (size, art, scale)
SIZES = [(16, FACE, 1), (24, ART, 1), (32, FACE, 2), (48, ART, 2), (256, ART, 14)]


def png(rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    w, h = len(rows[0]), len(rows)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp(rows):
    """Icon image as BMP: header with doubled height, bottom-up BGRA and AND mask."""
    w, h = len(rows[0]), len(rows)
    head = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    color = b"".join(bytes((b, g, r, a)) for row in reversed(rows) for (r, g, b, a) in row)
    stride = ((w + 31) // 32) * 4
    mask = bytearray()
    for row in reversed(rows):
        bits = bytearray(stride)
        for x, px in enumerate(row):
            if px[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return head + color + bytes(mask)


def ico():
    images = [(size, (png if size == 256 else bmp)(pixels(art, scale, size))) for size, art, scale in SIZES]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in images)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "runtime", "app", "ysrecomp.ico")
    with open(path, "wb") as f:
        f.write(ico())
    print(f"Wrote {path}")
    if "--preview" in sys.argv:
        prev = sys.argv[sys.argv.index("--preview") + 1]
        # each size scaled 2x (the 256 one unscaled), side by side, gray background
        tiles = [pixels(art, scale, size) for size, art, scale in SIZES]
        tiles = [[[px for px in row for _ in range(1 if len(t) == 256 else 2)] for row in t
                  for _ in range(1 if len(t) == 256 else 2)] for t in tiles]
        gray = (200, 200, 200, 255)
        sheet = []
        for y in range(256):
            row = []
            for t in tiles:
                row += [gray] * 8 + [(px if px[3] else gray) for px in t[y]] if y < len(t) else \
                    [gray] * (8 + len(t[0]))
            sheet.append(row)
        with open(prev, "wb") as f:
            f.write(png(sheet))
        print(f"Preview in {prev}")


if __name__ == "__main__":
    main()
