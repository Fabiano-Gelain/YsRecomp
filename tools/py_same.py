"""Compare two Python source trees ignoring comments, docstrings and string contents.

Usage: py_same.py OLD_DIR NEW_DIR [--names]
The syntax trees must be identical once every string constant is blanked and docstrings are removed.
Exit 1 if any file differs or is missing/added. --names lists, for each differing file, the
identifiers/attributes that changed (expected when a command-line option or a key was renamed).

The Python counterpart of code_same.py (Phase 9): copy the tree before translating, translate, then
run this against the copy.
"""
import ast
import os
import sys


def normalized(src):
    tree = ast.parse(src)
    for node in ast.walk(tree):
        body = getattr(node, "body", None)
        if isinstance(body, list) and body and isinstance(body[0], ast.Expr) \
                and isinstance(body[0].value, ast.Constant) and isinstance(body[0].value.value, str):
            node.body = body[1:] or [ast.Pass()]
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            node.value = ""
        elif isinstance(node, ast.JoinedStr):
            node.values = [v for v in node.values if not isinstance(v, ast.Constant)]
    return tree


def names(tree):
    out = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Name):
            out.add(node.id)
        elif isinstance(node, ast.Attribute):
            out.add("." + node.attr)
        elif isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            out.add(node.name)
        elif isinstance(node, ast.arg):
            out.add(node.arg)
        elif isinstance(node, ast.keyword) and node.arg:
            out.add(node.arg + "=")
    return out


def files(root):
    found = set()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for f in filenames:
            if f.endswith(".py"):
                found.add(os.path.relpath(os.path.join(dirpath, f), root))
    return found


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    old, new = sys.argv[1], sys.argv[2]
    show_names = "--names" in sys.argv[3:]
    a, b = files(old), files(new)
    problems = 0
    for rel in sorted(a ^ b):
        print(f"{'missing' if rel in a else 'added'}: {rel}")
        problems += 1
    for rel in sorted(a & b):
        with open(os.path.join(old, rel), encoding="utf-8") as f:
            t1 = normalized(f.read())
        with open(os.path.join(new, rel), encoding="utf-8") as f:
            t2 = normalized(f.read())
        if ast.dump(t1) != ast.dump(t2):
            problems += 1
            print(f"DIFFERENT: {rel}")
            if show_names:
                n1, n2 = names(t1), names(t2)
                print(f"  only old: {sorted(n1 - n2)}")
                print(f"  only new: {sorted(n2 - n1)}")
    print(f"{len(a & b)} files compared, {problems} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
