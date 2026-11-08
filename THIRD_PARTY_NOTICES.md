# Third-party notices

YsRecomp itself is released under the MIT License ([LICENSE](LICENSE)). The parts below come from elsewhere and keep
their own licenses.

## Code

| Component | License | How it is used |
|---|---|---|
| [PCERecomp](https://github.com/GITHUB_USER/PCERecomp) | MIT | The emulated console, the BIOS routines in C, the recompiler and the trace tools. Included as a Git submodule (`external/PCERecomp`) or a sibling folder; its own third-party notices are in its [THIRD_PARTY_NOTICES.md](https://github.com/GITHUB_USER/PCERecomp/blob/main/THIRD_PARTY_NOTICES.md). Its `tools/reference_trace/` (Geargrafx bridge, GPL-3.0) is a development tool and is not part of `YsRecomp.exe`. |
| [SDL 3](https://github.com/libsdl-org/SDL) 3.2.30 | zlib | Window, graphics, input (including XInput controllers) and audio. Not in this repository: CMake downloads the release archive at the first build and links it statically into `YsRecomp.exe`. |
| [stb_image](https://github.com/nothings/stb) 2.30 (bundled inside the SDL sources) | Public domain (or MIT, at your choice) | Decodes the launcher's JPG cover images (`runtime/app/launcher_image.c`). Used from the downloaded SDL sources, not copied here. |

## Images

| Path | What it is |
|---|---|
| `docs/YS I II.jpg`, `docs/Ys III.jpg`, `docs/Ys IV.jpg` | Cover images shown in the launcher. They are the author's own artwork, drawn after the official PC Engine CD covers of each game. The games, their names and their original cover art belong to their respective owners. |
| `docs/Ys Logo.jpg`, `docs/Icone_novo.png` | The launcher logo and the program icon: the author's own artwork. The "Ys" name and logo are trademarks of Nihon Falcom Corporation. |
| `docs/site/img/` | Screenshots of the project's GitHub Pages site, captured from YsRecomp running the game. |

## Not included

YsRecomp contains no ROM, disc image, BIOS or code generated from them. The C code translated from the game is
generated on the builder's machine from their own disc and is never committed. The options menu uses the game's 8x8
font and sound effects, which are read from the player's own disc at run time (`menu_font.bin`, `launcher_sfx.bin`
next to the settings file) and are not distributed.

Geargrafx (GPL-3.0) and Mednafen (GPL-2.0) were read as references for hardware behavior; no code was copied from
them.

*Ys* is a trademark of Nihon Falcom Corporation. PC Engine, TurboGrafx-16 and TurboGrafx-CD are trademarks of their
respective owners. YsRecomp is an unofficial fan project, not affiliated with or endorsed by any of them.
