"""Builds the player package: FOLDER/YsRecomp/ with YsRecomp.exe + README.txt, and FOLDER/YsRecomp.zip.
If the user's launcher images are in docs/ (outside Git), they go into YsRecomp/launcher/.

Nothing from the disc, the BIOS, the configuration or the saves goes into the package (YsRecomp.ini and saves/
are created next to the .exe the first time the player runs it). Before building, checks that the .exe:
  - does not need the "Visual C++ Redistributable" (does not import VCRUNTIME140.dll or api-ms-win-crt-*);
  - has the icon (runtime/app/ysrecomp.ico) embedded.

Usage: make_package.py YsRecomp.exe OUTPUT_FOLDER
"""
import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
README = os.path.join(ROOT, "package", "README.txt")
ICON = os.path.join(ROOT, "runtime", "app", "ysrecomp.ico")
FILES = ("YsRecomp.exe", "README.txt")
KEEP = ("YsRecomp.ini", "saves", "sessions", "menu_font.bin", "launcher_sfx.bin")
GAME_DATA = ("menu_font.bin", "launcher_sfx.bin")
# Start menu images: pixel-art CD cases and the logo, made by the user (nothing from the disc)
LAUNCHER_IMAGES = ("Ys Logo.jpg", "YS I II.jpg", "Ys III.jpg", "Ys IV.jpg")


def launcher_images():
    """The launcher images that exist in docs/."""
    return [n for n in LAUNCHER_IMAGES if os.path.isfile(os.path.join(ROOT, "docs", n))]


def check_exe(exe):
    """List of problems with the .exe (empty = ready for the package)."""
    problems = []
    with open(exe, "rb") as f:
        data = f.read()
    low = data.lower()
    for dll in (b"vcruntime140", b"api-ms-win-crt-"):
        if dll in low:
            problems.append(f"the .exe depends on {dll.decode()}* (compile with /MT)")
    with open(ICON, "rb") as f:
        ico = f.read()
    png = ico.find(b"\x89PNG")  # the 256 px image, stored whole in the .exe resource
    if png < 0 or ico[png:] not in data:
        problems.append("the .exe does not have the icon embedded")
    return problems


def make(exe, out):
    problems = check_exe(exe)
    if problems:
        for p in problems:
            print(f"ERROR: {p}")
        return None
    folder = os.path.join(out, "YsRecomp")
    # What the player created by playing in this folder (configuration, saves, recorded sessions) stays
    os.makedirs(folder, exist_ok=True)
    for name in os.listdir(folder):
        if name in KEEP:
            continue
        path = os.path.join(folder, name)
        if os.path.isdir(path):
            shutil.rmtree(path)
        else:
            os.remove(path)
    shutil.copy2(exe, os.path.join(folder, "YsRecomp.exe"))
    # README with Windows line endings (old Notepad)
    with open(README, encoding="utf-8") as f:
        text = f.read()
    with open(os.path.join(folder, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)
    images = launcher_images()
    if images:
        os.makedirs(os.path.join(folder, "launcher"))
        for name in images:
            shutil.copy2(os.path.join(ROOT, "docs", name), os.path.join(folder, "launcher", name))
    zip_path = os.path.join(out, "YsRecomp.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in FILES:
            z.write(os.path.join(folder, name), f"YsRecomp/{name}")
        for name in images:
            z.write(os.path.join(folder, "launcher", name), f"YsRecomp/launcher/{name}")
        # The menu font and the launcher sound effects, made by the program from the disc on its first run here:
        # shipped so the player sees the menu font and hears the launcher right away (without them the program
        # makes them again from the player's disc).
        for name in GAME_DATA:
            if os.path.isfile(os.path.join(folder, name)):
                z.write(os.path.join(folder, name), f"YsRecomp/{name}")
            else:
                print(f"Note: {name} is not in {folder} (run the game once from there): not in the zip.")
    return folder, zip_path


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    r = make(sys.argv[1], sys.argv[2])
    if not r:
        return 1
    folder, zip_path = r
    print(f"Package: {folder}")
    print(f"Zip:     {zip_path} ({os.path.getsize(zip_path) // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
