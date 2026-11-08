"""Compare two C source trees ignoring comments and whitespace.

Usage: code_same.py OLD_DIR NEW_DIR [--strings]
Code tokens must be identical; string/char literals may differ (listed with --strings).
Exit 1 if any code token differs or a file is missing/added.

Used in Phase 9 to prove that a translation touched only comments and messages: copy the tree before
translating (e.g. runtime -> a temp folder), translate, then run this against the copy.
"""
import os
import re
import sys

TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|[A-Za-z_0-9]+|\S')


def strip(src):
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            out.append(" ")
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise ValueError("unterminated comment")
            i = j + 2
            out.append(" ")
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("\\\n", i):
            out.append(" ")
            i += 2
        else:
            out.append(c)
            i += 1
    return TOKEN.findall("".join(out))


def files(root):
    res = set()
    for d, _, fs in os.walk(root):
        for f in fs:
            res.add(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    return res


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--help" in sys.argv or len(args) != 2:
        print(__doc__)
        return 0 if "--help" in sys.argv else 2
    old, new = args
    show = "--strings" in sys.argv
    bad = 0
    fo, fn = files(old), files(new)
    for f in sorted(fo ^ fn):
        print(f"ONLY IN {'OLD' if f in fo else 'NEW'}: {f}")
        bad += 1
    nstr = 0
    for f in sorted(fo & fn):
        if not re.search(r"\.(c|h|inc)$", f):
            continue
        a = strip(open(os.path.join(old, f), encoding="utf-8").read())
        b = strip(open(os.path.join(new, f), encoding="utf-8").read())
        if len(a) != len(b):
            print(f"CODE DIFFERS (token count {len(a)} vs {len(b)}): {f}")
            bad += 1
            continue
        for x, y in zip(a, b):
            if x == y:
                continue
            if x[0] in "\"'" and y[0] == x[0]:
                nstr += 1
                if show:
                    print(f"{f}: {x}\n{' ' * len(f)}  {y}")
            else:
                print(f"CODE DIFFERS: {f}: {x!r} -> {y!r}")
                bad += 1
                break
    print(f"{'FAIL' if bad else 'OK'}: {len(fo & fn)} files, {bad} problems, {nstr} strings changed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
