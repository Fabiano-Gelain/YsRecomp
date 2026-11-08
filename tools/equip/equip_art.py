"""Visible equipment on Adol: drawing rules applied on top of the game's own frames.

The game has two sets of 12 frames of small Adol (16x32, sprite palette 13; docs/ys_notes.md, "Equipment
on Adol"): without a sword (start of Ys I) and with a generic sword (blade in color 11). Shield
and armor never show. Nothing here comes from the disc: only the rules and the new drawings (shields, swords,
armors, colors of each piece), hand-made by looking at the equipment screen icons. The game frame comes in as 32
rows x 16 color indices (0 = transparent) plus its palette (r, g, b from 0 to 7); it goes out as 32 x 16 colors
(r, g, b from 0 to 7) or None.

Directions: 0 = facing front (down), 1 = back, 2 = side facing right, 3 = facing left.
"""

OUT = 14            # outline and shadow
BLADE = 11          # blade of the game's sword
SKIN = 10           # skin (face and hands)
ARMOR = (1, 8, 5, 7, 6)  # the game's blue armor, dark to light (6 = highlight)
CLOTH = (9, 4, 2)   # red clothes (shoulders, sleeves), dark to light
SHOULDER_ROWS = (13, 14)

ITEMS = {
    "ys1": {
        "sword": ["Short Sword", "Long Sword", "Talwar", "Silver Sword", "Flame Sword"],
        "shield": ["Small Shield", "Middle Shield", "Large Shield", "Silver Shield", "Battle Shield"],
        "armor": ["Chain Mail", "Plate Mail", "Reflex", "Silver Armor", "Battle Armor"],
    },
    "ys2": {
        "sword": ["Short Sword", "Long Sword", "Talwar", "Hyper Cutter", "Battle Sword", "Clelia Sword"],
        "shield": ["Wood Shield", "Small Shield", "Large Shield", "Iron Shield", "Battle Shield", "Clelia Shield"],
        "armor": ["Chain Mail", "Breast Plate", "Plate Mail", "Reflex", "Battle Armor", "Clelia Armor"],
    },
}

# ---- Colors (r, g, b from 0 to 7), many from the equipment screen icon palette ----
WHITE = (7, 7, 7)
BLUE = ((0, 0, 5), (0, 3, 7), (0, 5, 7), (1, 7, 7), WHITE)       # icon blue, dark -> highlight
GREEN = ((0, 2, 2), (1, 3, 0), (2, 5, 0), (4, 7, 2), WHITE)
FIRE = ((2, 0, 0), (7, 0, 0), (7, 2, 0), (6, 5, 0), (7, 7, 4))   # red, orange, yellow
SILVER = ((2, 2, 3), (4, 4, 5), (5, 5, 6), (6, 6, 7), WHITE)
GOLD = (6, 5, 0)
RED = ((3, 0, 0), (4, 0, 0), (6, 0, 0), (7, 2, 1), (7, 4, 3))    # red clothes
# gems (dark, highlight), as in the icons
RUBY = ((5, 0, 0), (7, 3, 3))
SAPPHIRE = ((0, 3, 7), (1, 7, 7))
AMBER = ((6, 2, 0), (7, 6, 2))
PEARL = ((5, 5, 7), WHITE)

# Armors: by body part. ARMOR_ROLES says, by hand, which part each pixel of each of the 12 frames belongs to, in rows
# 13-23: S shoulder pad, C chest, M middle of the chest, A arm, B belt, K skirt, L leg, F foot; "." = left as is
# (outline, skin, blade). An armor or clothing pixel outside the map (frames with a shield differ a bit) goes by its row.
ARMOR_PARTS = "SCMABKLF"
ARMOR_ROLES_TOP = 13
ARMOR_ROLES = {
    (0, 0): [".....S......S...", ".....SS....SSS..", ".....SCCMMCC.A..", "......CCMMC..A..", ".....ACCCCCAA...",
             "....AABBBBBAA...", "....AKKKKK..A...", ".....KKKKKKK....", "....LLLLLLLLL...", ".....FFF.FFF....",
             "................"],
    (0, 1): ["....SS......SS..", ".....SS....S.S..", ".....SCCMCCC....", "......CCMCC.....", "......CCMCCA....",
             "......BBBBBAA...", "....AKKKKKK.A...", ".....KKKKKKK....", "......LL.LLL....", "........FFFFF...",
             ".........FFF...."],
    (0, 2): ["....SS......SS..", "...SSSS....SSS..", ".....SCCMMCC....", "......CCMMC.....", ".....A.CMC.A....",
             "....AA.BBB.AA...", "....A.KKKKK.A...", ".....KKKKKKK....", ".....LLL.LL.....", "....FFFFF.......",
             ".....FFF........"],
    (1, 0): ["...SSS...SSS....", "..SSSCC.CCSSS...", "..A.CCMCMCC.A...", "..A..CCMCC..A...", "....A.CMC.AA....",
             "...AA.BBB.AA....", "...AKKKKKK.A....", "....KKKKKKK.....", "....LLLLLLL.....", "................",
             "................"],
    (1, 1): ["..SSSS...SSSS...", "..SSSCC.CCS.A...", "....CCMCMCC.A...", ".....CCMCC......", "....A.CMC.A.....",
             "...AA.BBB.AA....", "...AKKKKKK.A....", "....KKKKKKK.....", ".....LL.LLL.....", ".......FFFFF....",
             "........FFF....."],
    (1, 2): ["..SSSS...SSSS...", "..SS.CC.CCSSS...", "..A.CCMCMCC.A...", ".....CCMCC......", "....A.CMC.A.....",
             "...AA.BBB.AA....", "...A.KKKKK.A....", "....KKKKKKK.....", "....LLL.LL......", "...FFFFF........",
             "....FFF........."],
    (2, 0): [".....S..........", "....S..CCC......", "....SAACMC......", "......ACC.......", "....AAA.........",
             ".....B..........", ".....KK.........", "......L.LL......", ".....LLLL.......", ".....FFFF.......",
             ".....FFFFF......"],
    (2, 1): [".....S..........", "....S..CCC......", ".....A..MC......", "......ACCM......", ".....AAA........",
             "......B.........", ".....K..........", "...LL..KKLLL....", "...LLL..LLLL....", "....FFF.FFF.....",
             ".........F......"],
    (2, 2): [".....S..........", "....S..CCC......", "....SAA.M.......", ".....A.A........", "......A.........",
             "....BB..........", ".....K.KK.......", "...LLKK.LL......", "..LLLLL.LLL.....", "...FFF.FFF......",
             "....FF.........."],
    (3, 0): ["..........S.....", "......CCC..S....", "......CMAAAS....", "......CCAA......", ".....CCC.AAA....",
             "......B..BB.....", "......K.........", "......L..L......", ".......LLL......", ".......FFFF.....",
             "......FFFFF....."],
    (3, 1): ["..........S.....", "......CCC..S....", "......CM.A......", "......CMAAAA....", "......CCAAAA....",
             ".........BB.....", "......KK.KK.....", ".....LLLK..LL...", "....LLLL..LLL...", ".....FFF.FFF....",
             "......F........."],
    (3, 2): ["..........S.....", "......CCC..S....", "......MC.AAS....", ".......AA.A.....", "......AAAA......",
             ".......BB.K.....", "........K.K.....", "......L.L.KL....", ".....LLL.LLLL...", "......FF..FFF...",
             "..........FF...."],
}
# Game shade -> part shade: dark (0), mid (1), light (2). Index 1 alone stays dark: it is what separates the legs
# (without it, from the front, they became one block)
ARMOR_TONE = {1: 0, 8: 1, 5: 1, 7: 2, 6: 2, 9: 0, 4: 1, 2: 2}
_W, _CY, _LI, _BL, _DK = WHITE, (1, 7, 7), (0, 5, 7), (0, 3, 7), (0, 0, 5)
_RD, _MR, _FI, _OR, _YE, _PA, _GO = (4, 0, 0), (2, 0, 0), (7, 0, 0), (7, 3, 0), (7, 6, 0), (7, 7, 4), (6, 5, 0)
_GD, _GM, _GL = (1, 3, 0), (2, 5, 0), (4, 7, 2)
_RED1, _RED2 = RED[1], RED[2]
_LEATHER = ((2, 1, 0), (3, 2, 1), (4, 3, 1))
# Each part: 3 colors (dark, mid, light). mesh = parts in mail (alternating shades, except the dark ones); y = the game's
# light chest "Y" marked (chest shade 5 comes out light; 1-pixel gem); pad = shoulder pad 1 pixel wider; horns = height and
# color of the spikes; gem = gem (dark, highlight) at the top of the middle of the chest.
# No armor: red cloth clothes, leather boots and belt
NONE = dict(S=(_RED1, _RED2, _RED2), C=(_RED1, _RED2, _RED2), M=(_RED1, _RED2, _RED2), A=(_RED1, _RED2, _RED2),
            B=(_LEATHER[1],) * 3, K=(_RED1, _RED1, _RED2), L=_LEATHER, F=_LEATHER)
ARMORS_YS1 = {
    # icon: blue mail, red belt, gold tips on the shoulders; blue shield with red and yellow
    "Chain Mail": dict(S=(_BL, _LI, _YE), C=(_DK, _BL, _LI), M=(_DK, _BL, _LI), A=(_DK, _BL, _LI), B=(_FI, _FI, _FI),
                       K=(_DK, _BL, _LI), L=(_DK, _BL, _LI), F=(_DK, _BL, _LI), mesh="CMK"),
    # icon: smooth blue plates, white highlight, high red gem; blue shield with a white stripe
    "Plate Mail": dict(S=(_BL, _CY, _W), C=(_DK, _BL, _LI), M=(_BL, _LI, _CY), A=(_DK, _BL, _LI), B=(_DK, _BL, _BL),
                       K=(_DK, _BL, _LI), L=(_DK, _BL, _CY), F=(_DK, _BL, _LI), pad=True, y=True, gem=RUBY),
    # icon: gold collar and shoulders, blue body, dark red gem; shield with a gold rim
    "Reflex": dict(S=(_GO, _YE, _PA), C=(_DK, _BL, _LI), M=(_BL, _LI, _CY), A=(_DK, _BL, _LI), B=(_GO, _YE, _YE),
                   K=(_DK, _BL, _GO), L=(_DK, _BL, _LI), F=(_GO, _GO, _YE), y=True, gem=RUBY),
    # icon: light blue and white, small red gem; light blue, red and white shield
    "Silver Armor": dict(S=(_LI, _CY, _W), C=(_DK, _BL, _CY), M=(_LI, _CY, _W), A=(_BL, _LI, _W), B=(_CY, _CY, _CY),
                         K=(_DK, _BL, _CY), L=(_DK, _LI, _W), F=(_DK, _BL, _LI), pad=True, y=True, gem=RUBY),
    # icon: red and orange, gold shoulders, blue gem; gold and red shield with a blue gem
    "Battle Armor": dict(S=(_OR, _YE, _PA), C=(_RD, _FI, _OR), M=(_FI, _OR, _YE), A=(_RD, _FI, _OR), B=(_GO, _YE, _YE),
                         K=(_MR, _RD, _OR), L=(_RD, _FI, _OR), F=(_MR, _RD, _FI), pad=True, horns=(1, _YE), gem=SAPPHIRE),
}
ARMORS_YS2 = {
    # icon: big red shoulder pads, light blue mail, red belt; dark blue shield with red stripes
    "Chain Mail": dict(S=(_RD, _FI, _OR), C=(_DK, _BL, _CY), M=(_DK, _BL, _CY), A=(_DK, _BL, _LI), B=(_FI, _FI, _FI),
                       K=(_DK, _BL, _CY), L=(_DK, _BL, _LI), F=(_DK, _BL, _LI), pad=True, mesh="CMK"),
    # icon: one-piece blue plate, white highlight, no gem; blue shield with a white highlight
    "Breast Plate": dict(S=(_BL, _LI, _CY), C=(_DK, _BL, _LI), M=(_BL, _LI, _CY), A=(_DK, _BL, _LI), B=(_DK, _BL, _BL),
                         K=(_DK, _BL, _LI), L=(_DK, _BL, _LI), F=(_DK, _BL, _BL), y=True),
    # icon: green, white on the chest, orange gem; the matching shield is blue in the game itself (user's decision: faithful)
    "Plate Mail": dict(S=(_GD, _GM, _GL), C=(_GD, _GM, _GL), M=(_GM, _GL, _W), A=(_GD, _GM, _GM), B=(_OR, _OR, _OR),
                       K=((0, 2, 0), _GD, _GM), L=(_GD, _GM, _GL), F=(_GD, _GM, _GM), pad=True, gem=AMBER),
    # icon: light blue crystal, spikes, white; blue shield with a light rim
    "Reflex": dict(S=(_BL, _CY, _W), C=(_DK, _BL, _CY), M=(_BL, _CY, _W), A=(_DK, _BL, _LI), B=(_DK, _BL, _BL),
                   K=(_DK, _BL, _LI), L=(_DK, _BL, _CY), F=(_DK, _BL, _LI), horns=(1, _CY), y=True, gem=PEARL),
    # icon: red, gold horned shoulders, blue gem; red and gold shield
    "Battle Armor": dict(S=(_GO, _YE, _PA), C=(_RD, _FI, _OR), M=(_FI, _OR, _YE), A=(_RD, _FI, _OR), B=(_GO, _YE, _YE),
                         K=(_MR, _RD, _OR), L=(_RD, _FI, _OR), F=(_MR, _RD, _FI), pad=True, horns=(2, _YE), gem=SAPPHIRE),
    # icon: bulky blue, gold sash and boots; blue shield with gold and red eyes
    "Clelia Armor": dict(S=(_LI, _CY, _W), C=(_DK, _BL, _LI), M=(_BL, _LI, _CY), A=(_DK, _BL, _LI), B=(_GO, _YE, _YE),
                         K=(_DK, _BL, _GO), L=(_DK, _BL, _LI), F=(_GO, _GO, _YE), pad=True, y=True, gem=RUBY),
}
ARMORS = {"ys1": ARMORS_YS1, "ys2": ARMORS_YS2}

# Swords: hand drawn, pixel by pixel, along the game's blade (from the front it has 10 pixels; from the back and the
# side fewer, and the drawing shrinks with it). Each piece, inspired by its icon:
#   blade: blade colors, from the hand to the tip (how many of the 10 pixels it has: the short sword has 5)
#   side / side2: pixels beside the blade, one on each side (same positions as blade; "." = none), in place of the
#         outline: wide blade, edge, flames
#   guard: guard color (across the base of the blade; guard2 = one more pixel on each side)
#   hilt: hilt color (the color-3 pixel next to the base); curve: the tip bends outward
# The letters in blade/side are colors from SW_COLORS.
SW_COLORS = {
    "W": WHITE, "c": (5, 7, 7), "C": (1, 7, 7), "l": (2, 5, 7), "b": (0, 3, 7), "d": (0, 0, 5),
    "y": (7, 6, 0), "Y": (7, 7, 4), "o": (7, 3, 0), "r": (7, 0, 0), "m": (4, 0, 0),
}
SWORDS_YS1 = {
    # icon: short and thin, white with a blue edge, small gold guard, red hilt
    "Short Sword": dict(blade="lcWcW", guard=(6, 5, 0), hilt=(5, 0, 0)),
    # icon: long, blue with a white highlight, wide gold guard, orange hilt
    "Long Sword": dict(blade="blclcWcWcW", guard=(7, 6, 0), guard2=True, hilt=(7, 3, 0)),
    # icon: thin and curved, white and blue, dark red hilt
    "Talwar": dict(blade="bWcWcWcW", guard=(4, 0, 0), hilt=(5, 0, 0), curve=True),
    # icon: light blue with gold dots along it, gold guard
    "Silver Sword": dict(blade="CYCWCYCWCW", guard=(7, 6, 0), guard2=True, hilt=(0, 3, 7)),
    # icon: fire blade (red, orange, yellow, white), light blue guard
    "Flame Sword": dict(blade="oyoyYyYWYW", side="r.o.r.o.y.", side2=".r.o.r.o.y", guard=(1, 7, 7),
                        hilt=(0, 3, 7)),
}
SWORDS_YS2 = {
    # icon: short, blue and light blue, red guard
    "Short Sword": dict(blade="bcCcW", guard=(7, 0, 0), hilt=(4, 0, 0)),
    # icon: long, blue with a light blue highlight, orange guard
    "Long Sword": dict(blade="blblcClcCW", guard=(7, 3, 0), guard2=True, hilt=(4, 0, 0)),
    # icon: curved, light blue and white, red guard
    "Talwar": dict(blade="lCcWcWcW", guard=(7, 0, 0), hilt=(4, 0, 0), curve=True),
    # icon: wide, dark blue with a light blue and white edge
    "Hyper Cutter": dict(blade="dbdbdbdbbW", side="lClClCWCW.", guard=(7, 3, 0), hilt=(4, 0, 0)),
    # icon: wide, blue and light blue, big orange guard
    "Battle Sword": dict(blade="CcCcCcCcWW", side="bbbbbbbbl.", guard=(7, 3, 0), guard2=True, hilt=(4, 0, 0)),
    # icon: blue with gold dots, red and gold guard
    "Clelia Sword": dict(blade="bybCbybCcW", side=".l.l.l.l..", guard=(7, 6, 0), guard2=True, hilt=(7, 0, 0)),
}
SWORDS = {"ys1": SWORDS_YS1, "ys2": SWORDS_YS2}
BLADE_FULL = 10     # blade pixels from the front (each sword's drawing is measured against it)

# Shields: the game has a third set of frames, with sword and shield (docs/ys_notes.md): a shield from the front
# (5x8 pixels in the hand), and from the back and the side only a strip 1-2 pixels wide. The game shield's pixels in
# each frame (mask) come from outside: those that differ from the sword-only frame; those in the shield's columns
# (SHIELD_COLS) are the shield, the rest is the arm that changed.
#   Front: hand drawing of each piece (front: rows 12 to 23, columns 8 to 15 of the standing front frame; "." =
#   whatever is underneath, "o" = outline, letters = colors from colors). Under the game's shield goes the body of the
#   sword-only frame (under), for a shield smaller than the game's.
#   Back and side: the game's strip, each color in its role painted with the piece's colors (face dark/mid/light,
#   rim, highlight, emblem: SHIELD_ROLE), minus trim rows at the top and bottom (smaller shield).
SHIELD_ROLE = {1: ("face", 0), 8: ("face", 1), 5: ("face", 2), 7: ("rim", 0), 11: ("hl", 0),
               13: ("emblem", 0), 3: ("emblem", 1), 12: ("emblem", 2)}
SHIELD_COLS = {0: (9, 15), 1: (0, 2), 2: (11, 15), 3: (0, 4)}   # columns of the game's shield in each direction
FRONT_X0, FRONT_Y0 = 8, 12      # corner of the front drawing
FRONT_FACE = (10, 14)           # corner of the game shield's face in the standing front frame
SH_COLORS = {
    "W": WHITE, "C": (1, 7, 7), "L": (0, 5, 7), "B": (0, 3, 7), "D": (0, 0, 5), "K": (0, 0, 3),
    "R": (4, 0, 0), "M": (2, 0, 0), "r": (7, 0, 0), "O": (7, 3, 0), "Y": (7, 6, 0), "y": (7, 7, 4),
    "G": (6, 5, 0), "g": (2, 5, 0), "e": (4, 7, 2), "S": (5, 5, 6), "s": (6, 6, 7), "t": (3, 3, 4),
}
SHIELDS_YS1 = {
    # icon: small and round, blue rim, dark red half, orange and yellow band
    "Small Shield": dict(front=[
        "........",
        "........",
        "...ooo..",
        "..oCCCo.",
        ".oCRROWo",
        ".oCRROYo",
        ".oBRROYo",
        "..oBDBo.",
        "...oBo..",
        "....o...",
        "........",
        "........"],
        trim=(1, 2),
        face=BLUE[0:3], rim=(BLUE[3],), hl=(WHITE,), emblem=((5, 0, 0), (7, 0, 0), (7, 3, 3))),
    # icon: blue, tall, rounded corners, with vertical white stripes
    "Middle Shield": dict(front=[
        "........",
        "........",
        "...ooo..",
        "..oLLLo.",
        ".oLBWBLo",
        ".oDBWBLo",
        ".oDBWBCo",
        ".oDBWBLo",
        ".oDBWBLo",
        "..oDDDo.",
        "...ooo..",
        "........"],
        trim=(0, 1),
        face=BLUE[0:3], rim=(BLUE[3],), hl=(WHITE,), emblem=(BLUE[2], BLUE[3], WHITE)),
    # icon: large knight's shield, gold rim, blue with a diagonal white band
    "Large Shield": dict(front=[
        "........",
        "...ooo..",
        "..oGGGo.",
        ".oGLBWGo",
        ".oGBWDGo",
        ".oGWDDGo",
        ".oGDDDGo",
        "..oGDGo.",
        "...oGo..",
        "....o...",
        "........",
        "........"],
        face=BLUE[0:3], rim=(GOLD,), hl=(WHITE,), emblem=(BLUE[3], WHITE, WHITE)),
    # icon: round, light blue frame, four dark red fields and a white cross
    "Silver Shield": dict(front=[
        "........",
        "...ooo..",
        "..oCCCo.",
        ".oLRWRLo",
        ".oLWWWLo",
        ".oLRWRLo",
        ".oLRWRLo",
        "..oLLLo.",
        "...ooo..",
        "........",
        "........",
        "........"],
        face=BLUE[0:3], rim=(BLUE[3],), hl=(WHITE,), emblem=((5, 0, 0), (7, 0, 0), GOLD)),
    # icon: red, orange and gold, blue gem in the middle
    "Battle Shield": dict(front=[
        "........",
        "...ooo..",
        "..oYYYo.",
        ".oYOOOYo",
        ".oYRBRYo",
        ".oYRCRYo",
        ".oYRRRYo",
        "..oYRYo.",
        "...oYo..",
        "....o...",
        "........",
        "........"],
        face=FIRE[1:4], rim=(GOLD,), hl=((7, 7, 4),), emblem=((4, 0, 0), (7, 0, 0), (7, 7, 4))),
}
SHIELDS_YS2 = {
    # icon: oval, blue rim, dark red middle with a red and light blue band
    "Wood Shield": dict(front=[
        "........",
        "...ooo..",
        "..oLLLo.",
        ".oLDDDLo",
        ".oLrrrLo",
        ".oLDDDLo",
        ".oLrrrLo",
        "..oDDDo.",
        "...ooo..",
        "........",
        "........",
        "........"],
        trim=(0, 1),
        face=BLUE[0:3], rim=(BLUE[3],), hl=(BLUE[3],), emblem=((6, 2, 0), GOLD, (7, 7, 4))),
    # icon: small and round, blue with a white highlight on top
    "Small Shield": dict(front=[
        "........",
        "........",
        "...ooo..",
        "..oLCLo.",
        ".oBLWCLo",
        ".oDBLCBo",
        ".oDDBBBo",
        "..oCDCo.",
        "..oDBDo.",
        "...oDo..",
        "....o...",
        "........"],
        trim=(1, 2),
        face=BLUE[0:3], rim=(BLUE[3],), hl=(WHITE,), emblem=(BLUE[2], BLUE[3], WHITE)),
    # icon: knight's shield (point at the bottom), dark blue rim, blue with a light blue and white highlight
    "Large Shield": dict(front=[
        "........",
        "...ooo..",
        "..oLLLo.",
        ".oLDDDLo",
        ".oLDWDLo",
        ".oLDCDLo",
        ".oLDDDLo",
        "..oLDLo.",
        "...oLo..",
        "....o...",
        "........",
        "........"],
        face=BLUE[1:4], rim=(BLUE[3],), hl=(WHITE,), emblem=(BLUE[3], WHITE, WHITE)),
    # icon: square, blue with light blue and orange rivets
    "Iron Shield": dict(front=[
        "........",
        "....o...",
        "...oCo..",
        "..oCDCo.",
        ".oDODODo",
        ".oDDDDDo",
        ".oDODODo",
        "..oDDDo.",
        "...oDo..",
        "....o...",
        "........",
        "........"],
        face=BLUE[0:3], rim=((4, 4, 5),), hl=(WHITE,), emblem=((5, 1, 0), (7, 2, 0), (7, 5, 2))),
    # icon: battlements on top, dark red with orange, yellow and green
    "Battle Shield": dict(front=[
        "........",
        "........",
        "..o.o.o.",
        ".oYoYoYo",
        ".oYYYYYo",
        ".oOMgMOo",
        ".oOMMMOo",
        ".oOMgMOo",
        "..oOMOo.",
        "...oOo..",
        "....o...",
        "........"],
        face=FIRE[1:4], rim=(GOLD,), hl=((7, 7, 4),), emblem=GREEN[1:4]),
    # icon: square with rounded corners, blue with a green and gold checker
    "Clelia Shield": dict(front=[
        "........",
        "...ooo..",
        "..oBBBo.",
        ".oDrDrDo",
        ".oDDGDDo",
        ".oDgDgDo",
        ".oDDgDDo",
        "..oDDDo.",
        "...ooo..",
        "........",
        "........",
        "........"],
        face=BLUE[0:3], rim=(BLUE[3],), hl=(WHITE,), emblem=GREEN[1:4]),
}
SHIELDS = {"ys1": SHIELDS_YS1, "ys2": SHIELDS_YS2}

def _copy(frame):
    return [list(r) for r in frame]


def blade_pixels(frame):
    """Blade pixels, from the hand (lowest) to the tip (highest)."""
    px = [(x, y) for y in range(32) for x in range(16) if frame[y][x] == BLADE]
    return sorted(px, key=lambda p: (-p[1], p[0]))


def figure(frame, x, y):
    return 0 <= x < 16 and 0 <= y < 32 and frame[y][x] not in (0, OUT)


def _put_outline(out, x, y, pal):
    if 0 <= x < 16 and 0 <= y < 32 and out[y][x] is None:
        out[y][x] = pal[OUT]


def near_blade(f, x, y):
    """Some neighbor is blade: this outline stays, so the sword does not merge into the armor."""
    return any(0 <= x + dx < 16 and 0 <= y + dy < 32 and f[y + dy][x + dx] == BLADE
               for dx in (-1, 0, 1) for dy in (-1, 0, 1))


def armor_role(direction, pose, x, y):
    """The body part of pixel (x, y) of the frame"""
    r = ARMOR_ROLES[(direction, pose)][y - ARMOR_ROLES_TOP][x]
    if r != ".":
        return r
    return "S" if y < 15 else "C" if y < 18 else "B" if y == 18 else "K" if y < 21 else "F"


def _armor(out, f, direction, spec, pal, pose):
    roles = ARMOR_ROLES[(direction, pose)]
    for y in range(ARMOR_ROLES_TOP, 24):
        for x in range(16):
            c = f[y][x]
            if c not in ARMOR_TONE:
                continue
            r = armor_role(direction, pose, x, y)
            t = ARMOR_TONE[c]
            if spec.get("y") and r == "C" and c == 5:
                t = 2
            if r in spec.get("mesh", "") and t > 0:
                t = 2 if (x + y) % 2 else 1
            out[y][x] = spec[r][t]
    # gem: the 1st pixel of the middle of the chest (none from the back); the one below gets the dark color, unless it marks the "Y"
    gem = spec.get("gem")
    if gem and direction != 1:
        done = False
        for y in range(15, 18):
            for x in range(16):
                if not done and roles[y - ARMOR_ROLES_TOP][x] == "M" and f[y][x] in ARMOR_TONE:
                    out[y][x] = gem[1]
                    if not spec.get("y") and roles[y + 1 - ARMOR_ROLES_TOP][x] == "M" and f[y + 1][x] in ARMOR_TONE:
                        out[y + 1][x] = gem[0]
                    done = True
    if direction not in (0, 1):
        return
    # wider shoulder pad: the outermost shoulder pixel on each side gains one pixel, and the outline moves with it (never
    # the outline beside the blade)
    if spec.get("pad"):
        for y in SHOULDER_ROWS:
            xs = [x for x in range(16) if roles[y - ARMOR_ROLES_TOP][x] == "S" and f[y][x] in ARMOR_TONE]
            if not xs:
                continue
            for x, step in ((min(xs), -1), (max(xs), 1)):
                X = x + step
                if 0 <= X < 16 and f[y][X] == OUT and not near_blade(f, X, y):
                    out[y][X] = spec["S"][1]
                    if 0 <= X + step < 16 and not figure(f, X + step, y):
                        out[y][X + step] = pal[OUT]
    # spikes coming up out of the shoulders: both or none (a single one looks like a stray pixel)
    if spec.get("horns"):
        h, col = spec["horns"]
        y0 = SHOULDER_ROWS[0]
        xs = [x for x in range(16) if roles[0][x] == "S" and f[y0][x] in ARMOR_TONE]
        sides = (min(xs) - 1, max(xs) + 1) if xs else ()
        if xs and all(0 <= x < 16 and not any(near_blade(f, x, y0 - j) for j in range(h + 1)) for x in sides):
            for x in sides:
                for j in range(1, h + 1):
                    Y = y0 - j
                    if figure(f, x, Y):
                        break
                    out[Y][x] = col
                    _put_outline(out, x - 1, Y, pal)
                    _put_outline(out, x + 1, Y, pal)
                _put_outline(out, x, y0 - h - 1, pal)


def head_guard(base):
    """The skin of the head (rows 5-13) and the outline around it: eyes and chin, which no equipment touches."""
    g = set()
    for y in range(5, 14):
        for x in range(16):
            if base[y][x] != SKIN:
                continue
            g.add((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if 0 <= x + dx < 16 and base[y + dy][x + dx] == OUT:
                        g.add((x + dx, y + dy))
    return g


def _sign(v):
    return (v > 0) - (v < 0)


def shield_area(direction, mask):
    """Pixels of the game's shield in the frame (those of mask in its columns; the rest of mask is the arm)."""
    lo, hi = SHIELD_COLS[direction]
    return frozenset(p for p in mask if lo <= p[0] <= hi)


def _sword(out, f, mask, sp, pal):
    """Sword: the piece's drawing along the game's blade; with no sword (sp None), removes the blade."""
    blade = [p for p in blade_pixels(f) if p not in mask]
    if not blade:
        return
    keep = 0
    if sp:
        K = len(sp["blade"])
        keep = min(len(blade), max(2, round(len(blade) * K / BLADE_FULL)))
    gone = set(blade[keep:])
    for (x, y) in gone:
        out[y][x] = None
    # outline that only served the removed part goes too
    for y in range(32):
        for x in range(16):
            if f[y][x] != OUT or y >= 22:
                continue
            near = [(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if (dx or dy)]
            if any(p in gone for p in near) and not any(figure(f, *p) and p not in gone for p in near):
                out[y][x] = None
    if not sp:
        return
    K = len(sp["blade"])
    kept = blade[:keep]
    col = [SW_COLORS[sp["blade"][K - 1 if i == keep - 1 else i * K // keep]] for i in range(keep)]
    for i, (x, y) in enumerate(kept):
        out[y][x] = col[i]
    bx, by = kept[0]
    tx, ty = kept[-1]
    # pixels beside the blade (in place of the outline), and the outline outside them
    for side, sgn in (("side", 1), ("side2", -1)):
        s = sp.get(side)
        if not s:
            continue
        for i, (x, y) in enumerate(kept):
            ch = s[K - 1 if i == keep - 1 else i * K // keep]
            if ch == ".":
                continue
            # grows away from the body: the outline between the blade and the arm stays
            for d in (sgn, -sgn):
                X, far = x + d, x + 2 * d
                if not 0 <= X < 16 or f[y][X] != OUT or (X, y) in mask:
                    continue
                if 0 <= far < 16 and f[y][far] not in (0, OUT, BLADE):
                    continue
                out[y][X] = SW_COLORS[ch]
                _put_outline(out, far, y, pal)
                break
    # new tip: outline around it
    for dx, dy in ((0, -1), (-1, 0), (1, 0), (-1, -1), (1, -1)):
        _put_outline(out, tx + dx, ty + dy, pal)
    if sp.get("curve") and keep >= 3:
        # curved blade: the tip bends one pixel away from the body
        side = -1 if tx <= bx else 1
        X = tx + side
        if 0 <= X < 16 and not figure(f, X, ty):
            out[ty][X] = col[-1]
            out[ty][tx] = col[0]
    # guard: across the base of the blade, in place of the outline (guard2: one more pixel on each side)
    dx, dy = _sign(tx - bx), _sign(ty - by)
    qx, qy = -dy, dx
    for sgn in (1, -1):
        for k in ((1, 2) if sp.get("guard2") else (1,)):
            X, Y = bx + sgn * k * qx, by + sgn * k * qy
            if not (0 <= X < 16 and 0 <= Y < 32) or (X, Y) in mask:
                break
            if f[Y][X] != OUT and not (k == 2 and f[Y][X] == 0):
                break
            out[Y][X] = sp["guard"]
            _put_outline(out, X + sgn * qx, Y + sgn * qy, pal)
    # hilt: the color-3 pixel next to the base of the blade
    for dx, dy in ((0, 1), (-1, 1), (1, 1), (0, 2), (-1, 0), (1, 0)):
        X, Y = bx + dx, by + dy
        if 0 <= X < 16 and 0 <= Y < 32 and f[Y][X] == 3:
            out[Y][X] = sp["hilt"]
            break


# ---- Reflection on the ice (docs/ys_notes.md, "Ice reflection") ----
# On the ice floors of Darm Tower the game swaps Adol's patterns 00-0F for 10-1F (same direction and step), in palette
# 14 instead of 13. The new frame is the normal one without the shadow, with the blade in color 13 and color 13 in 1
# and, in rows 24-30, the reflection: seven rows of Adol himself, bottom to top, with each index swapped for a lighter
# one. Checked against the frames on the disc (24 frames, 7 retouches by the game: ICE_RETOUCHES in equip_ice_preview.py).
ICE_ROWS = {24: 22, 25: 19, 26: 17, 27: 14, 28: 11, 29: 9, 30: 6}   # reflection row: Adol row
ICE_TOP = {BLADE: 13, 13: 1}                                        # index on Adol: index in the ice frame
ICE_REFLECT = {1: 8, 2: 11, 3: 12, 4: 2, 5: 7, 6: 6, 7: 7, 8: 5, 9: 4, 10: 10, 11: 13, 12: 6, 13: 1, 14: 1}
ICE_PAL = ((3, 3, 3), (0, 2, 7), (6, 1, 0), (7, 3, 1), (5, 1, 0), (0, 6, 7), (7, 7, 6), (2, 7, 7),   # the game's palette 14
           (1, 4, 7), (4, 0, 0), (7, 5, 4), (7, 2, 0), (7, 4, 0), (7, 7, 7), (1, 0, 3), (0, 0, 0))


def _cap(v):
    return min(v, 7)


def ice_top_color(c):
    """Color of a piece on Adol standing on ice: the game raises its green one step (blues and reds)."""
    return (c[0], _cap(c[1] + 1), c[2])


def ice_reflect_color(c):
    """Color of a piece in the reflection: as the game does with its own colors. Blue: green goes up 3 (and red 1),
    toward light blue; red and yellow: red goes up 2 and green 1 (light yellow almost white); green: all three go
    up."""
    r, g, b = c
    top = max(c)
    if b == top:
        return (_cap(r + 1), _cap(g + 3), _cap(b + 2))
    if r == top:
        return (_cap(r + 2), _cap(g + 1), _cap(b + 3) if g >= 5 else b)
    return (_cap(r + 2), _cap(g + 2), _cap(b + 1))


def ice_color_maps(pal):
    """Game colors (reference palette 13) on Adol and in the ice reflection, by the game's own indices."""
    top = {pal[i]: ICE_PAL[ICE_TOP.get(i, i)] for i in range(1, 16)}
    refl = {pal[i]: ICE_PAL[ICE_REFLECT[i]] for i in ICE_REFLECT}
    return top, refl


def ice_compose(out, shadow, pal, frame, under=None):
    """Already composed frame (compose: 32 x 16 colors or None) -> the same on the ice. shadow = pixels the game removes
    from the normal frame (the shadow); they only go if they are still the game's outline. frame and under = the game
    frame: the game's color swap only applies to pixels that are still its own; a piece with a color equal to one in its
    palette goes by the piece rule (the pieces' yellow, (7, 6, 0), used to turn orange on the ice)."""
    top, refl = ice_color_maps(pal)
    outline = pal[OUT]
    res = [[None] * 16 for _ in range(32)]
    clean = [list(r[:]) for r in out[:24]]

    def own(x, y, c):
        return c == outline or c == pal[frame[y][x]] or (under is not None and c == pal[under[y][x]])

    for (x, y) in shadow:
        if clean[y][x] == outline:
            clean[y][x] = None
    for y in range(24):
        for x in range(16):
            c = clean[y][x]
            if c is not None:
                res[y][x] = top[c] if own(x, y, c) and c in top else ice_top_color(c)
    for y, src in ICE_ROWS.items():
        for x in range(16):
            c = clean[src][x]
            if c is not None:
                res[y][x] = refl[c] if own(x, src, c) and c in refl else ice_reflect_color(c)
    return res


def compose(frame, pal, direction, sword=None, shield=None, armor=None, game="ys1", mask=frozenset(), under=None, pose=0):
    """Game frame -> frame with the equipment (colors 0-7 or None). The frame comes from the right frame set
    (no sword, with sword, or with sword and shield; with a shield and no sword, the sword-and-shield one, which
    loses its blade here); mask = the game shield's pixels in that frame (empty without a shield); under = the
    sword-only frame in the same pose (the body under the front shield); pose = 0 standing, 1 step 1, 2 step 2."""
    out = _compose(frame, pal, direction, sword, shield, armor, game, mask, under, pose)
    # the face as in the game: eyes and chin touch nothing
    base = under if mask else frame
    for (x, y) in head_guard(base):
        out[y][x] = pal[base[y][x]]
    return out


def _compose(frame, pal, direction, sword, shield, armor, game, mask, under, pose):
    f = _copy(frame)
    sh = SHIELDS[game][shield] if shield else None
    area = shield_area(direction, mask) if sh else frozenset()
    front = direction == 0 and bool(area)
    if front:
        # corner of the game's shield in this frame (the drawing moves with it) and the body under it
        face = [p for p in area if f[p[1]][p[0]] in SHIELD_ROLE]
        ox = min(p[0] for p in face) - FRONT_FACE[0]
        oy = min(p[1] for p in face) - FRONT_FACE[1]
        for (x, y) in area:
            f[y][x] = under[y][x]
    out = [[None if c == 0 else pal[c] for c in row] for row in f]
    _armor(out, f, direction, ARMORS[game][armor] if armor else NONE, pal, pose)
    _sword(out, f, mask, SWORDS[game][sword] if sword else None, pal)

    if not sh:
        # no shield in a frame with a shield (preview page only): its pixels as in the game
        for (x, y) in mask:
            out[y][x] = pal[f[y][x]] if f[y][x] else None
        return out
    if front:
        for j, row in enumerate(sh["front"]):
            for i, ch in enumerate(row):
                X, Y = FRONT_X0 + ox + i, FRONT_Y0 + oy + j
                if ch != "." and 0 <= X < 16 and 0 <= Y < 32:
                    out[Y][X] = pal[OUT] if ch == "o" else SH_COLORS[ch]
        return out
    _shield_edge(out, f, area, sh, pal)
    return out


def _shield_edge(out, f, area, sh, pal):
    """Back and side: the strip of the game's shield with the piece's colors, shorter for smaller shields."""
    for (x, y) in area:
        out[y][x] = pal[f[y][x]] if f[y][x] else None
        role = SHIELD_ROLE.get(f[y][x])
        if role:
            out[y][x] = sh[role[0]][role[1]]
    # trim applies to the 8-row strip (back and facing left); on a shorter strip, proportionally
    rows = sorted({y for (x, y) in area if f[y][x] in SHIELD_ROLE})
    top, bottom = ((t * len(rows) + 4) // 8 for t in sh.get("trim", (0, 0)))
    cut = set(rows[:top]) | set(rows[len(rows) - bottom:] if bottom else [])
    gone = {(x, y) for (x, y) in area if f[y][x] in SHIELD_ROLE and y in cut}
    if not gone:
        return
    for (x, y) in gone:
        out[y][x] = None
    for (x, y) in sorted(area):
        if f[y][x] != OUT:
            continue
        near = [(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if (dx or dy)]
        if not any(figure(f, *p) and p not in gone for p in near):
            out[y][x] = None
    for (x, y) in sorted(area):
        if f[y][x] in SHIELD_ROLE and (x, y) not in gone:
            for dy in (-1, 1):
                if (x, y + dy) in gone:
                    out[y + dy][x] = pal[OUT]
