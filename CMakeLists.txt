cmake_minimum_required(VERSION 3.24)
project(YsRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # .pdb in Release too, so the profiler (pce_trace --profile) can name the functions.
    # Does not change the generated code; /OPT:REF,ICF keeps the .exe the same size as before.
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    # C library inside the .exe (/MT), everywhere (SDL included): YsRecomp.exe runs on a Windows without the
    # "Visual C++ Redistributable" package installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    enable_language(RC)
endif()

# All .exe files in build\Release (PCERecomp's too), where the scripts look for them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# User's disc and BIOS (roms/, outside Git).
set(YS_CUE "${CMAKE_SOURCE_DIR}/roms/Ys Book I & II (USA).cue")
set(SYSCARD2 "${CMAKE_SOURCE_DIR}/roms/[BIOS] TurboGrafx CD System Card (USA) (v2.0).pce")

# PCERecomp (the emulated console and the tools): the working copy next to this folder (..\PCERecomp) when it
# exists (development of both projects side by side), otherwise the submodule in external/PCERecomp (a clone made
# with --recursive). PCERECOMP_DIR picks another one.
set(PCERECOMP_DIR "" CACHE PATH "PCERecomp folder")
if(NOT PCERECOMP_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../PCERecomp/CMakeLists.txt")
        get_filename_component(PCERECOMP_DIR "${CMAKE_SOURCE_DIR}/../PCERecomp" ABSOLUTE)
    else()
        set(PCERECOMP_DIR "${CMAKE_SOURCE_DIR}/external/PCERecomp")
    endif()
endif()
if(NOT EXISTS "${PCERECOMP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PCERecomp not found at ${PCERECOMP_DIR} (git submodule update --init, or -DPCERECOMP_DIR=...)")
endif()
set(PCE_TEST_CUE "${YS_CUE}")  # PCERecomp's cd_image test also checks the Ys disc
set(PCE_GAME_TRACE ON)          # pce_trace is built here (with the Ys extension)
add_subdirectory("${PCERECOMP_DIR}" pcerecomp)
set(GG_TRACE "${PCERECOMP_DIR}/tools/reference_trace/build/Release/gg_trace.exe")

# SDL3, downloaded and built as a static library (no DLL needed next to the .exe).
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.30.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)

# Cross-file optimization (LTCG) in Release, for our targets (SDL, created above, is left out):
# the hardware clock calls small functions from several files on every instruction.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# runtime/app (this project) next to runtime/{cpu,pce,cd,bios} (PCERecomp)
include_directories(runtime)

# PC version: portable configuration and identification of the disc chosen by the player.
add_library(pce_app STATIC
    runtime/app/config.c
    runtime/app/disc_id.c
    runtime/app/volume.c
    runtime/app/menu.c
    runtime/app/menu_font.c
    runtime/app/menu_chip.c
    runtime/app/input.c
    runtime/app/intro_skip.c
    runtime/app/video.c
    runtime/app/pacing.c
    runtime/app/vblank.c
    runtime/app/present.c
    runtime/app/redraw.c
    runtime/app/equip.c
    runtime/app/battle.c
    runtime/app/game_fixes.c
    runtime/app/motion.c runtime/app/ys_masks.c
    runtime/app/gpu_draw.c
    runtime/app/gpu_time.c
    runtime/app/ys_engine.c
    runtime/app/ys_windows.c runtime/app/ys_scroll.c runtime/app/ys_screen.c runtime/app/ys_music.c runtime/app/ys_dialog.c runtime/app/ys_intro.c runtime/app/ys_fade.c runtime/app/ys_panel.c runtime/app/ys_reward.c
    runtime/app/crop.c
    runtime/app/fade.c
    runtime/app/launcher.c
    runtime/app/launcher_sound.c
    runtime/app/launcher_image.c)
target_include_directories(pce_app PUBLIC runtime)
target_link_libraries(pce_app PUBLIC pce_core SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(pce_app PUBLIC dwmapi)
endif()
# Launcher: the user's JPG images, decoded with the stb_image bundled in the SDL sources
set_source_files_properties(runtime/app/launcher_image.c PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/src/video")

# Main program (window), with the interpreter.
add_executable(pce_runtime runtime/main.c)
target_link_libraries(pce_runtime PRIVATE pce_core pce_app pce_input trace_profiler SDL3::SDL3-static)

# PCERecomp's trace generator (its tools/trace/pce_trace.c; comparison with the reference emulator) with the
# Ys extension (tools/trace/ys_trace_ext.c: the PC version's options; pce_trace --help).
pce_trace_executable(pce_trace tools/trace/ys_trace_ext.c tools/trace/ys_actors.c tools/trace/hud_old.c runtime/app/ys_panel.c runtime/app/ys_fade.c runtime/app/menu_chip.c runtime/app/redraw.c runtime/app/game_fixes.c runtime/app/motion.c runtime/app/ys_masks.c
    runtime/app/equip.c runtime/app/equip_none.c
    runtime/app/gpu_draw.c
    runtime/app/ys_engine.c runtime/app/ys_windows.c runtime/app/ys_scroll.c runtime/app/ys_screen.c runtime/app/ys_music.c runtime/app/ys_dialog.c runtime/app/crop.c runtime/app/fade.c runtime/app/menu_font.c runtime/app/battle.c runtime/app/ys_reward.c)
target_link_libraries(pce_trace PRIVATE SDL3::SDL3-static)

# ---- Tests ----
enable_testing()


# Configuration (YsRecomp.ini) and identification of the disc in the chosen folder (error cases with
# altered copies of the user's disc in build/app_output; without the disc, the disc part is skipped).
add_executable(app_tests tests/app/app_tests.c)
target_link_libraries(app_tests PRIVATE pce_app)
add_test(NAME app_config COMMAND app_tests config "${CMAKE_BINARY_DIR}/app_output/config")
# Separate volumes (Mednafen balance) and the Geargrafx mix unchanged.
add_test(NAME app_volume COMMAND app_tests volume "${CMAKE_BINARY_DIR}/app_output/volume")
# Options menu (keys, volumes, exit confirmation, images by CRC).
add_test(NAME app_menu COMMAND app_tests menu "${CMAKE_BINARY_DIR}/app_output/menu")
add_test(NAME app_video COMMAND app_tests video "${CMAKE_BINARY_DIR}/app_output/video")
# Image options drawn with a software renderer (no window), checked pixel by pixel.
add_test(NAME app_image COMMAND app_tests image "${CMAKE_BINARY_DIR}/app_output/image")
# Border crop (recognition by shape, crops pixel by pixel, on screen by CRC).
add_test(NAME app_crop COMMAND app_tests crop "${CMAKE_BINARY_DIR}/app_output/crop")
# Smooth motion on a fake frame (redraw, sub-pixel positions, constant speed).
add_test(NAME app_redraw COMMAND app_tests redraw)
add_test(NAME app_fade COMMAND app_tests fade)
# Start menu (background, covers, selection, texts, sounds and the sound effects file).
add_test(NAME app_launcher COMMAND app_tests launcher "${CMAKE_BINARY_DIR}/app_output/launcher")
# Controls (keyboard and SDL virtual gamepad, rebinding through the menu, configuration).
add_test(NAME app_input COMMAND app_tests input "${CMAKE_BINARY_DIR}/app_output/input")
add_test(NAME app_disc_id COMMAND app_tests disc "${CMAKE_SOURCE_DIR}/roms/Ys Book I & II (USA).cue"
    "${CMAKE_BINARY_DIR}/app_output/disco")
set_tests_properties(app_disc_id PROPERTIES SKIP_RETURN_CODE 77)
# CD music with the options menu open (console paused) and then resuming where it stopped.
add_test(NAME app_cd_menu_music COMMAND app_tests cdmenu "${CMAKE_SOURCE_DIR}/roms/Ys Book I & II (USA).cue")
set_tests_properties(app_cd_menu_music PROPERTIES SKIP_RETURN_CODE 77)


find_program(PYTHON_EXE NAMES python python3)



# CD-ROM² with the real BIOS (System Card 2.0) and Ys: they need the user's files in roms/.
# Boot: 3 million instructions up to the "PUSH RUN BUTTON!" screen (frame 600).
# With RUN at frame 300 (tests/input/ys_boot.txt): the BIOS reads the disc through the SCSI controller, loads
# Ys into CD RAM and it shows the logo (frame 1200); trace of 8 million instructions starting at
# frame 299 (the disc load happens in that stretch).
set(YS_INPUT "${CMAKE_SOURCE_DIR}/tests/input/ys_boot.txt")
if(PYTHON_EXE)
    add_test(NAME trace_ys_boot COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_trace_test.py"
        "${GG_TRACE}"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" 3000000 "${CMAKE_BINARY_DIR}/ys_output"
        --bios "${SYSCARD2}" --name boot)
    add_test(NAME trace_ys_run COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_trace_test.py"
        "${GG_TRACE}"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" 8000000 "${CMAKE_BINARY_DIR}/ys_output"
        --bios "${SYSCARD2}" --input "${YS_INPUT}" --from-frame 299 --name run)
    # Whole sessions (tools/trace/run_session_test.py): Geargrafx and ours at the same time, the image of
    # every frame and all the sound (PSG + CD music + ADPCM) identical. The Geargrafx output is
    # reused while the game, the BIOS, the script and gg_trace do not change (tools/trace/gg_cache.py).
    # Boot: "PUSH RUN BUTTON!" screen, RUN, disc load and the Ys logo (frame 1200).
    add_test(NAME session_ys_boot COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_session_test.py"
        "${GG_TRACE}"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${CMAKE_BINARY_DIR}/ys_output/session_boot"
        1200 --bios "${SYSCARD2}" --input "${YS_INPUT}")
    # Into the game (tests/input/ys_game.txt): intro, copyright, new game, Esteria harbor (9000 frames).
    set(YS_JOGO_INPUT "${CMAKE_SOURCE_DIR}/tests/input/ys_game.txt")
    add_test(NAME session_ys_game COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_session_test.py"
        "${GG_TRACE}"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${CMAKE_BINARY_DIR}/ys_output/session_jogo"
        9000 --bios "${SYSCARD2}" --input "${YS_JOGO_INPUT}")
    # Save: starting with the save memory tests/input/ys_save.brm, "Continue" on the menu and the location
    # screen with "1: Minea Town".
    add_test(NAME session_ys_save COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_session_test.py"
        "${GG_TRACE}"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${CMAKE_BINARY_DIR}/ys_output/session_save"
        6600 --bios "${SYSCARD2}" --input "${CMAKE_SOURCE_DIR}/tests/input/ys_save.txt"
        --bram "${CMAKE_SOURCE_DIR}/tests/input/ys_save.brm")
    # The sound of the sessions above is compared with the Geargrafx fade (--gg-fader). The default rule is Mednafen's:
    # turning the fade on again does not bring the music back (tests/input/ys_goddess.txt; see runtime/cd/cdrom.h).
    add_test(NAME fade_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_fade_test.py"
        $<TARGET_FILE:pce_trace> "${SYSCARD2}" "${YS_CUE}" "${CMAKE_SOURCE_DIR}/tests/input/ys_goddess.txt"
        "${CMAKE_BINARY_DIR}/ys_output/fade")
    # Coverage v2: the code Ys loads from the disc into RAM, with the origin of each
    # instruction (position on the disc), checked against the CD image — same bytes, valid opcodes,
    # calls and jumps going to the target written in the disc's code.
    add_test(NAME coverage_ys COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_coverage_test.py"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${YS_JOGO_INPUT}" 9000
        "${CMAKE_BINARY_DIR}/ys_output/cobertura/roteiro.txt" --bios "${SYSCARD2}")
    # All of the Ys code found through the disc map, without sessions; what the coverage sessions
    # executed must be in the analysis (the limit = what is still missing: 10 instructions of program $20;
    # 2 from 1-byte loads outside the map; must not grow)
    add_test(NAME recomp_disc_ys COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/recomp/recomp.py" disc
        "${SYSCARD2}" --cd "${YS_CUE}" --map "${CMAKE_SOURCE_DIR}/games/ys/disc.toml"
        --check "${CMAKE_SOURCE_DIR}/games/ys/coverage" --limit 10 --limit-outside-map 2)
    # The game boots without the BIOS file (synthetic BIOS + BIOS routines in C)
    add_test(NAME nobios_ys COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_nobios_test.py"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${CMAKE_BINARY_DIR}/ys_output/sem_bios" 2500)
    # Without the BIOS, Gaban's cabin (ad_cplay: ADPCM straight from the CD) shows up and the voice plays
    add_test(NAME nobios_cabin_ys COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_nobios_test.py"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${CMAKE_BINARY_DIR}/ys_output/sem_bios_cabana" 3624
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261022_000602.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261022_000602.brm" --adpcm 2)
    # Every call to the C routines equal to the real BIOS, in the user's short session
    add_test(NAME bios_c_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_bios_check.py"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_161116.txt" --bios-check $<TARGET_FILE:bios_check>)
    # The C routines with the real BIOS timing — a session recorded with the BIOS, replayed with the C
    # routines, has the same image as with the real BIOS in every frame from 4249 on (includes the window switch
    # of the portrait conversation, at frame 8716)
    add_test(NAME hle_timing_ys COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/trace/run_hle_timing_test.py"
        $<TARGET_FILE:pce_trace> "${SYSCARD2}" "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.txt" 2881-4248 11195
        "${CMAKE_BINARY_DIR}/ys_output/hle_tempo")
    # Preview of the equipment on Adol (his 24 frames taken from the disc; blade, shield arm and each
    # piece checked; the page built in layers equal to the direct drawing in all 559 combinations)
    add_test(NAME equip_preview_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/equip/equip_preview.py"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${SYSCARD2}" "${CMAKE_BINARY_DIR}/ys_output/equip_preview.html")
    set_tests_properties(equip_preview_ys PROPERTIES PASS_REGULAR_EXPRESSION "equip_preview: OK")
    # The ice reflection rule equal to the disc's frames; the preview for choosing the pieces
    add_test(NAME equip_ice_preview_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/equip/equip_ice_preview.py"
        $<TARGET_FILE:pce_trace> "${YS_CUE}" "${SYSCARD2}" "${CMAKE_BINARY_DIR}/ys_output/equip_ice_preview.html")
    set_tests_properties(equip_ice_preview_ys PROPERTIES PASS_REGULAR_EXPRESSION "equip_ice_preview: OK")
    set_tests_properties(trace_ys_boot trace_ys_run session_ys_boot session_ys_game session_ys_save fade_ys
        coverage_ys nobios_ys nobios_cabin_ys bios_c_ys hle_timing_ys equip_preview_ys equip_ice_preview_ys
        PROPERTIES SKIP_RETURN_CODE 77)
endif()


# ---- Recompiled Ys (all of the code through the disc map) ----
# BIOS (System Card) + all the code Ys loads from the disc into RAM, found through the disc map
# (games/ys/disc.toml: file table, where each file goes, entry points), without depending on sessions (the
# coverage in games/ys/coverage only checks: test recomp_disc_ys). The C is generated in games/ys/generated/c
# (not versioned) and redone when the map or the generator changes. Functions that run from RAM check their
# bytes before executing (signature; huc6280_recomp.h). Without the BIOS and the CD, none of this exists.
if(PYTHON_EXE AND EXISTS "${YS_CUE}" AND EXISTS "${SYSCARD2}")
    set(YS_GEN_DIR "${CMAKE_SOURCE_DIR}/games/ys/generated/c")
    set(YS_GEN_FILES "${YS_GEN_DIR}/rc_table.c")
    foreach(n RANGE 0 23)  # PARTS in tools/recomp/pcerecomp/codegen.py
        if(n LESS 10)
            set(n "0${n}")
        endif()
        list(APPEND YS_GEN_FILES "${YS_GEN_DIR}/rc_part${n}.c")
    endforeach()
    file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${PCERECOMP_DIR}/tools/recomp/pcerecomp/*.py")
    add_custom_command(
        OUTPUT ${YS_GEN_FILES}
        COMMAND ${PYTHON_EXE} "${PCERECOMP_DIR}/tools/recomp/recomp.py" generate "${SYSCARD2}"
                --map "${CMAKE_SOURCE_DIR}/games/ys/disc.toml" --cd "${YS_CUE}" --out "${YS_GEN_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/games/ys/disc.toml" ${RECOMP_SOURCES} "${PCERECOMP_DIR}/tools/recomp/recomp.py"
        COMMENT "Generating the recompiled Ys C (all of the disc's code)")
    add_library(ys_recomp STATIC ${YS_GEN_FILES})
    target_link_libraries(ys_recomp PUBLIC pce_core)
    # No LTCG for the generated C (~40 MB): with it, every link of ys_trace/YsRecomp recompiled the whole game (~10 min
    # per .exe, even when touching only the runtime). The game's code is ~1% of the time; the hardware keeps LTCG.
    set_target_properties(ys_recomp PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    if(MSVC)
        target_compile_options(ys_recomp PRIVATE /W1 /bigobj /MP)  # /MP: the 24 parts in parallel
    endif()

    # Equipment on Adol (runtime/app/equip.h): Adol's 36 frames taken from the disc (session with the BIOS) by
    # pce_trace; generated C outside Git, like the game's. Only redone when the generator or the session changes (pce_trace
    # only needs to exist: redoing it on every runtime change would take ~2 min)
    set(EQUIP_FRAMES_C "${CMAKE_SOURCE_DIR}/games/ys/generated/equip/equip_frames.c")
    add_custom_command(
        OUTPUT "${EQUIP_FRAMES_C}"
        COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/equip/equip_frames.py" $<TARGET_FILE:pce_trace> "${YS_CUE}"
                "${SYSCARD2}" "${EQUIP_FRAMES_C}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/equip/equip_frames.py" "${CMAKE_SOURCE_DIR}/tools/equip/equip_preview.py"
                "${CMAKE_SOURCE_DIR}/tools/equip/equip_ice_preview.py" "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_000904.txt"
                "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_153505.txt"
        COMMENT "Extracting Adol's frames from the disc (equipment)")
    add_library(equip_frames STATIC "${EQUIP_FRAMES_C}")
    target_include_directories(equip_frames PRIVATE runtime)
    add_dependencies(equip_frames pce_trace)
    # The C drawing (equip.c) equal to the Python rules (equip_art.py) in every frame and piece
    add_executable(equip_check tools/equip/equip_check.c runtime/app/equip.c runtime/app/ys_fade.c)
    target_link_libraries(equip_check PRIVATE pce_core equip_frames)
    add_test(NAME equip_c_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/equip/equip_c_test.py"
        $<TARGET_FILE:equip_check> "${CMAKE_BINARY_DIR}/ys_output/equip_check.bin")
    set_tests_properties(equip_c_ys PROPERTIES PASS_REGULAR_EXPRESSION "equip_c_test: OK")
    pce_trace_executable(ys_trace tools/trace/ys_trace_ext.c tools/trace/ys_actors.c tools/trace/hud_old.c runtime/app/ys_panel.c runtime/app/ys_fade.c runtime/app/menu_chip.c runtime/app/redraw.c runtime/app/game_fixes.c runtime/app/motion.c runtime/app/ys_masks.c
    runtime/app/equip.c
    runtime/app/gpu_draw.c
    runtime/app/ys_engine.c runtime/app/ys_windows.c runtime/app/ys_scroll.c runtime/app/ys_screen.c runtime/app/ys_music.c runtime/app/ys_dialog.c runtime/app/crop.c runtime/app/fade.c runtime/app/menu_font.c runtime/app/battle.c runtime/app/ys_reward.c)
    target_compile_definitions(ys_trace PRIVATE PCE_RECOMP)
    target_link_libraries(ys_trace PRIVATE ys_recomp equip_frames SDL3::SDL3-static)

    # The game for the player. Without arguments: asks for the bin/cue folder the first time and keeps the
    # configuration and the save next to the .exe (YsRecomp.ini, saves/). Accepts pce_runtime's arguments.
    add_executable(YsRecomp runtime/main.c)
    if(WIN32)
        target_sources(YsRecomp PRIVATE runtime/app/ysrecomp.rc)
    endif()
    target_compile_definitions(YsRecomp PRIVATE PCE_RECOMP PCE_APP_YS)
    # Launcher: without launcher/ next to the .exe, the images in docs/
    target_compile_definitions(YsRecomp PRIVATE "PCE_DOCS_DIR=\"${CMAKE_SOURCE_DIR}/docs\"")
    target_link_libraries(YsRecomp PRIVATE ys_recomp pce_app pce_input equip_frames trace_profiler SDL3::SDL3-static)

    # Lockstep (recompiled vs interpreter, compared on every instruction and every frame: CPU, RAM, VRAM,
    # sprites, VDC, palette, image, sound, CD RAM, save and ADPCM RAM) in the session up to the harbor. Together with
    # session_ys_game (interpreter = Geargrafx), proves that the recompiled code is also equal to Geargrafx.
    add_test(NAME recomp_lockstep_ys COMMAND ys_trace --frame --lockstep --input "${YS_JOGO_INPUT}"
        --bios "${SYSCARD2}" "${YS_CUE}" 9000 "${CMAKE_BINARY_DIR}/recomp_ys/lockstep.ppm")
    # The same without the BIOS (C routines, as the player runs it), in a user session that starts from a save
    # and goes through the load menu
    set(YS_NOBIOS_SESSION "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261018_184410")
    add_test(NAME recomp_lockstep_ys_nobios COMMAND ys_trace --frame --lockstep --input "${YS_NOBIOS_SESSION}.txt"
        --bram "${YS_NOBIOS_SESSION}.brm" "${YS_CUE}" 4348 "${CMAKE_BINARY_DIR}/recomp_ys/lockstep_nobios.ppm")
    set_tests_properties(recomp_lockstep_ys_nobios PROPERTIES PASS_REGULAR_EXPRESSION "LOCKSTEP OK")
    # Instant dialog (app/ys_dialog.h). The user's session from the start of Ys I (Sara speaking, slow text,
    # frames 7401-8200): without the option, waits per letter; with "# dialog 0 1", none, and the lockstep equal with the
    # option in both; YsRecomp writes the configured option into the recorded session.
    add_test(NAME dialog_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_dialog_test.py"
        $<TARGET_FILE:ys_trace> $<TARGET_FILE:YsRecomp> "${YS_CUE}" "${SYSCARD2}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_161018.txt" 8300 "${CMAKE_BINARY_DIR}/recomp_ys/dialogo")
    set_tests_properties(dialog_ys PROPERTIES PASS_REGULAR_EXPRESSION "dialog_ys: OK")
    # How the player starts it: no game arguments, only the configuration (here a test one, --config,
    # pointing at the roms/ folder); the save goes to the configuration's folder.
    add_test(NAME app_start_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_app_start_test.py"
        $<TARGET_FILE:YsRecomp> "${CMAKE_SOURCE_DIR}/roms" "${CMAKE_BINARY_DIR}/app_output/inicio")
    set_tests_properties(app_start_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The start menu before the game (keys pressed automatically), the Ys sound effects recorded by the hidden
    # console the first time and read from launcher_sfx.bin the second time.
    add_test(NAME app_launcher_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_launcher_test.py"
        $<TARGET_FILE:YsRecomp> "${CMAKE_SOURCE_DIR}/roms" "${CMAKE_BINARY_DIR}/app_output/launcher_ys")
    set_tests_properties(app_launcher_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The player package (only YsRecomp.exe + README.txt, .exe with an icon and not needing the
    # Visual C++ Redistributable) extracted to a temporary folder outside the project reaches the game.
    add_test(NAME app_package_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/package/run_package_test.py"
        $<TARGET_FILE:YsRecomp> "${CMAKE_SOURCE_DIR}/roms" "${CMAKE_BINARY_DIR}/app_output/package")
    set_tests_properties(app_package_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The game runs on its own thread; with the window stuck for 2 s (as when dragging
    # or maximizing), the sound must not cut out and the game must not stop.
    add_test(NAME app_stall_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_stall_test.py"
        $<TARGET_FILE:YsRecomp> 600 2000 -- "${YS_CUE}" --save "${CMAKE_BINARY_DIR}/app_output/parada.brm")
    set_tests_properties(app_stall_ys PROPERTIES SKIP_RETURN_CODE 77)
    # F12 writes into the recorded session the frame on screen (a defect seen by the player)
    add_test(NAME app_mark_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_mark_test.py"
        $<TARGET_FILE:YsRecomp> "${CMAKE_BINARY_DIR}/app_output/marca" 420 300 -- "${YS_CUE}"
        --save "${CMAKE_BINARY_DIR}/app_output/marca.brm")
    # Window and audio device stalled together for 2 s (Bluetooth headset or controller connecting): the game
    # follows the clock instead of waiting for the audio device.
    add_test(NAME app_stall_device_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_stall_test.py"
        $<TARGET_FILE:YsRecomp> 600 2000 device -- "${YS_CUE}" --save "${CMAKE_BINARY_DIR}/app_output/parada_disp.brm")
    set_tests_properties(app_stall_device_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The audio device stopping for 60 ms every 5 s (as with the GPU fully loaded at 180 Hz): the sound queue
    # returns to its target without overflowing (before: a dropout every ~10 s in the lava cave)
    add_test(NAME app_hiccup_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_hiccup_test.py"
        $<TARGET_FILE:YsRecomp> 1800 60 -- "${YS_CUE}" --save "${CMAKE_BINARY_DIR}/app_output/soluco.brm")
    set_tests_properties(app_hiccup_ys PROPERTIES SKIP_RETURN_CODE 77)
    # Opening and closing the options menu mid-session does not change the game (same state at the end)
    # and the chosen volume goes into the configuration (--turbo: without waiting for the clock).
    add_test(NAME app_menu_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_menu_test.py"
        $<TARGET_FILE:YsRecomp> 1800 1200 "${CMAKE_BINARY_DIR}/app_output/menu_ys" --
        --bios "${SYSCARD2}" "${YS_CUE}" --input "${YS_JOGO_INPUT}" --turbo)
    # Under the options menu, the last image shown (with the equipment on Adol), not the console's raw frame.
    # The chip music keeps playing with the options menu open (without the BIOS; a sound effect at frame 2803)
    add_test(NAME app_menu_chip_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_menu_chip_test.py"
        $<TARGET_FILE:YsRecomp> 2830 2804 "${CMAKE_BINARY_DIR}/app_output/menu_chip" --
        "${YS_CUE}" --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261018_184410.txt"
        --save "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261018_184410.brm")
    add_test(NAME app_menu_image_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_menu_under_test.py"
        $<TARGET_FILE:YsRecomp> 5100 5000 "${CMAKE_BINARY_DIR}/app_output/menu_imagem" --
        --bios "${SYSCARD2}" "${YS_CUE}" --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_160656.txt"
        --save "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_160656.brm")
    # Skipping the intro (simulated Enter on 3 screens): stops at the first frame of each next screen and the
    # console state at the end is the same as watching it all.
    add_test(NAME app_intro_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_intro_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}" "${CMAKE_BINARY_DIR}/app_output/abertura")
    set_tests_properties(app_intro_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The options menu does not open during the intro; image and CRT brightness on the path without smooth motion
    add_test(NAME app_menu_intro_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_menu_opening_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}" "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.brm" "${CMAKE_BINARY_DIR}/app_output/menu_abertura")
    set_tests_properties(app_menu_intro_ys PROPERTIES SKIP_RETURN_CODE 77)
    # Performance: the start of the user's sessions equal, frame by frame, to the reference recorded
    # before the optimizations (build/perf_ref/hle, run_perf_check.py --save; without it, skipped).
    add_test(NAME perf_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/run_perf_check.py"
        --check "${CMAKE_BINARY_DIR}/perf_ref/hle" --exe $<TARGET_FILE:ys_trace> --cue "${YS_CUE}" --frames 4000)
    set_tests_properties(perf_ys PROPERTIES SKIP_RETURN_CODE 77)
    # Border crop off, "full" (with smooth motion) and "frame" (without): same console state at
    # the end and images really cropped, in a user session (without the BIOS; --turbo).
    add_test(NAME app_crop_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_crop_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.brm" 3400 "${CMAKE_BINARY_DIR}/app_output/recorte")
    set_tests_properties(app_crop_ys PROPERTIES SKIP_RETURN_CODE 77)
    # On the torn frame with the shrunk window, the map stays without the window's sprites (the pink equipment
    # frames came out large outside it); in real time, with the user's .ini at 960x540
    add_test(NAME app_torn_window_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_torn_ring_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.brm" "${CMAKE_BINARY_DIR}/app_output/janela_rasgada")
    set_tests_properties(app_torn_window_ys PROPERTIES SKIP_RETURN_CODE 77)
    # When loading a save and when changing area, the screen edge does not flash on the black screen (the border the game copies
    # for the next scroll, brought forward by the torn-frame fix); in real time, with fill at 16:9
    add_test(NAME app_load_no_border_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_load_frame_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261104_192718.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261104_192718.brm" "${CMAKE_BINARY_DIR}/app_output/carregar_moldura")
    set_tests_properties(app_load_no_border_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The layout switches of the same session, read from the game: there is no tear inside a window to fix there
    add_test(NAME layout_switches_ys COMMAND ys_trace --frame --game-fixes
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261104_192718.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261104_192718.brm"
        "${YS_CUE}" 4100 "${CMAKE_BINARY_DIR}/recomp_ys/trocas_layout.ppm")
    set_tests_properties(layout_switches_ys PROPERTIES
        PASS_REGULAR_EXPRESSION "torn inside the windows: 0, fixed 0, with a tear inside afterwards: 0:\nlayout switches: 4: 3093 3178 3844 4012\n")
    # Adol walks without stops in the Ys II sewer, through the window path: the game redraws the floor while the image is
    # being output at the step decision, and the garbage frame (since removed) froze the image there (19 stops in those frames)
    add_test(NAME app_rhythm_sewer_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_walk_rhythm_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_224821.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_224821.brm" 9031 10098 915 1.6
        "${CMAKE_BINARY_DIR}/app_output/ritmo_esgoto")
    set_tests_properties(app_rhythm_sewer_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The new frame prepared on its own thread (while the previous one's images are on screen) comes out equal to
    # the one prepared by the window (--prep-inline), frame by frame (--prep-hashes), with the menu opened and closed midway
    add_test(NAME app_prep_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_prep_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.brm" 3845 2000
        "${CMAKE_BINARY_DIR}/app_output/preparo")
    set_tests_properties(app_prep_ys PROPERTIES SKIP_RETURN_CODE 77)
    # The frame with a half-drawn window in conversations (window switch, 8716; window closing, 9471); in the session
    # recorded with the BIOS, replayed with the C routines. Their tear stays only inside the game's windows: they are
    # fixed (new window whole; the closing one, whole as it was), with no tear left inside them
    add_test(NAME torn_window_ys COMMAND ys_trace --frame --game-fixes
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.brm"
        "${YS_CUE}" 9500 "${CMAKE_BINARY_DIR}/recomp_ys/janela_rasgada.ppm")
    set_tests_properties(torn_window_ys PROPERTIES
        PASS_REGULAR_EXPRESSION "torn inside the windows: 48, fixed 37, with a tear inside afterwards: 0:[0-9 ]* 8716 [0-9 ]*9471")
    # In the dark cave of Ys II the game redraws the floor near Adol while the image is being output, on every step: the
    # garbage frame (since removed) caused a stutter. In the back-and-forth 4575-5280, no stops
    add_test(NAME rhythm_cave_ys COMMAND ys_trace --frame --smooth-frames 4545 735 - --walk-rhythm 4575 5280
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_190512.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_190512.brm"
        "${YS_CUE}" 5317 "${CMAKE_BINARY_DIR}/recomp_ys/ritmo_caverna.ppm")
    set_tests_properties(rhythm_cave_ys PROPERTIES PASS_REGULAR_EXPRESSION "Adol rhythm \\(frames 4575-5279\\): [0-9]+ walking, [0-9]+ at the rhythm of [0-9.]+, 0 stops")
    # On the save screen (20618-) the game puts the whole image in a single scroll, with the map elsewhere in the background
    # map: around the shrunk window, the map from before it with the earlier scroll must match the current one (before, 83
    # frames did not match, and a snapshot with frozen characters was shown)
    add_test(NAME save_window_ring_ys COMMAND ys_trace --frame --game-fixes --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_232204.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_232204.brm"
        "${YS_CUE}" 20700 "${CMAKE_BINARY_DIR}/recomp_ys/janela_save_anel.ppm")
    set_tests_properties(save_window_ring_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "map around the shrunken windows: redrawn in 2890 frames, 0 not matching the current one, 0 without a map")
    # When listing the saves (LOAD, without the BIOS), the C save memory routine runs past the end of frame 9096
    # and the VDC already draws lines of the next one: the line log must not lose them (before: image jumping 8
    # lines and sliding back in 9097-9119, with the "late" scroll switch fixed for nothing). With
    # smooth motion reading the game engine (replacing the old smooth_save_ys)
    add_test(NAME motion_save_ys COMMAND ys_trace --frame --smooth-check
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.brm"
        "${YS_CUE}" 9600 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_save.ppm")
    set_tests_properties(motion_save_ys PROPERTIES
        PASS_REGULAR_EXPRESSION "late scroll change fixed in 0 frames \\(1st: -1\\).*: 0 images with the border/panel different from the frame"
        FAIL_REGULAR_EXPRESSION "FAILED|at frame 9098 \\(image")
    # The old CD track is only muted in the game routine that plays a song with a fade on (GAME OVER: 62076;
    # 59944: fade just started); the volume coming back with the same song (22167) is not. (Merging with motion_light_ys, from
    # the same session, was 5x slower than the two separately)
    add_test(NAME cd_pop_ys COMMAND ys_trace --frame --game-fixes --volumes 100,100,100,100,100
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.brm"
        "${YS_CUE}" 62200 "${CMAKE_BINARY_DIR}/recomp_ys/cd_pop.ppm")
    set_tests_properties(cd_pop_ys PROPERTIES PASS_REGULAR_EXPRESSION "old CD track silenced: 2: 59944 62076\n")
    # At the end of Ys I (session without the BIOS), the whole screen brightens to white: the crop follows the 6
    # steps of the flash, the fill HUD comes from the panel with the flash undone (without ENEMY) and turns off at white.
    # The steps read from the game (--fades: the flash loop, app/ys_fade.c), at the same frames
    add_test(NAME crop_flash_ys COMMAND ys_trace --frame --crop-log --fades --crop-shot fill black
        "${CMAKE_BINARY_DIR}/recomp_ys/clarao_fill.ppm"
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_000904.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_000904.brm"
        "${YS_CUE}" 81350 "${CMAKE_BINARY_DIR}/recomp_ys/clarao.ppm")
    set_tests_properties(crop_flash_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "fades: frame 81331 level 7 brightening 1 .*frame 81331 flash 1 of 7\n.*fades: frame 81349 level 7 brightening 4 .*frame 81349 flash 4 of 7\n(.*\n)*--crop-shot: HUD, pan -?[0-9]+, flash 4\n")
    # In the tower (session without the BIOS), "Adol gets the Glasses" passes under the woman's face (sprites in
    # front of the background; that is the game): the window stays in front, and smooth motion redraws it the same
    add_test(NAME item_window_ys COMMAND ys_trace --frame --game-fixes --smooth-check --crop-shot fill black
        "${CMAKE_BINARY_DIR}/recomp_ys/janela_item_fill.ppm"
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_203049.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_203049.brm"
        "${YS_CUE}" 6400 "${CMAKE_BINARY_DIR}/recomp_ys/janela_item.ppm")
    # On the Ys II equipment screen (session without the BIOS), the frame of the chosen weapon/armor (whole sprites
    # on top of the window) stays in front; before, the item-window rule above hid it
    # (7793-7800). What remains is 7013, the small shop window opening under the seller's shoulder (the item-window case).
    # (Game windows, only their own sprites in front): 8 frames, from 4177: on the frame a window opens,
    # Adol under it (the game hides him with the mask on the next frame; 4177, 4566, 5012, 7372, 7717, 7790) and the
    # small shop window already at 7012
    # In the same run: the portrait closes (5686) and the game darkens the screen in steps: the scene only ends with the
    # image fully black (5708, darkened 7); before it ended at step 2 (5688) and the switch to the fill band looked
    # like a zoom
    add_test(NAME equipment_ys COMMAND ys_trace --frame --game-fixes --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_230635.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_230635.brm"
        "${YS_CUE}" 7801 "${CMAKE_BINARY_DIR}/recomp_ys/equipamento.ppm")
    set_tests_properties(equipment_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "frame 5686 game window closes \\(columns 129-286, rows 1-126 of the map\\), scene in fill.*crop: frame 5708 scene ends \\(darkened 7, 30 frames.*item window in front: 8 frames \\(1st: 4177\\)")
    # From the frame the window opens (6047; before, through the edge on the map, only once it was whole)
    set_tests_properties(item_window_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "item window in front: 354 frames \\(1st: 6047\\)\n.*6399 of 6400 frames redrawn equal")
    # The image assembled on the GPU with the window layers, equal to the CPU one (merging with
    # item_window_ys was slower: --smooth-check with 4 images per frame over the whole session)
    add_test(NAME gpu_window_ys COMMAND ys_trace --frame --game-fixes --gpu --smooth-frames 6000 400 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_203049.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_203049.brm"
        "${YS_CUE}" 6400 "${CMAKE_BINARY_DIR}/recomp_ys/placa_janela.ppm")
    set_tests_properties(gpu_window_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "item window in front: 354 frames.*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 44 of 44. 4x image equal to the CPU one in 1724 of 1724"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # (Research) who writes to the background map (pce_trace --bat-writers, Vdc.write_hook hook that only observes);
    # in the Ys II session (without the BIOS), the border of the game's window system ($81:607E, docs/ys_notes.md) is drawn at
    # frames 3863 (portrait scene), 3997-3998 and 4177 (conversation window, the first one saving the background)
    add_test(NAME game_windows_ys COMMAND pce_trace --frame
        --bat-writers "${CMAKE_BINARY_DIR}/janelas_jogo.txt" --bat-routine 81:607E
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_230635.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_230635.brm"
        "${YS_CUE}" 4180 "${CMAKE_BINARY_DIR}/janelas_jogo.ppm")
    set_tests_properties(game_windows_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "routine 81:607E wrote to the map in 4 frames \\(1st: 3863\\)")
    # Game windows through the open/close hooks (pce_trace --windows, app/ys_windows.c), checked
    # every frame against $3E and the game's stack; the windows' sprites (characters without bit 5: cursor, pink
    # equipment frames) inside them. Ys II without the BIOS (SELECT menu, inventory, equipment):
    # (in windows_crop_ys, in the same run as the crop). And Ys I with the BIOS: $3E comes dirty from the intro (1 too many until the game clears it in a scene), $605D calls the masks at
    # $80:4E2F (in Ys II, $4F78); scene figures without bit 5 (outside the windows)
    # (In the same run, --crop-log): the bordered screen by the game's program (app/ys_screen.c): load menu
    # ($01), start of Ys I ($28: the old way left 26 frames cropped like the load screen) and the game ($08); where the
    # image has the border's shape, the game says the same; the 4 without the shape are the load screen being erased; no
    # black frame with the HUD band lit
    add_test(NAME windows_ys1 COMMAND pce_trace --frame --windows - --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_161018.txt" --bios "${SYSCARD2}"
        "${YS_CUE}" 9564 "${CMAKE_BINARY_DIR}/windows_ys1.ppm")
    set_tests_properties(windows_ys1 PROPERTIES PASS_REGULAR_EXPRESSION
        "crop: frame 2709 on, load screen \\(program 01\\).*crop: frame 4170 off \\(program 28\\).*crop: frame 4992 on \\(program 08\\).*screen from the game: 4 program loads. 4573 frames in the game, 1461 on the load screen, 3530 without border. with the border: 5670 with the shape in the image, 360 black \\(0 not darkened, 1st: -1\\), 4 without the shape \\(1st: 4165\\). without border: 0 with the shape after the flash, 0 with the shape \\(1st: -1\\)\n.*2190 frames with a window .*, 19 openings, 19 closings. different from \\$3E and the game stack in 0 of 2190 frames \\(1st: -1\\)\nwindow sprites: 3920, inside a window 0, outside in a scene \\(window without saved background\\) 1000, outside 947")
    # The new HUD from the tiles the game writes to the panel (app/ys_panel.c) against the old way by colors
    # (tools/trace/hud_old.c), with the save screen ("Which to save?", save numbers in palette 5, frames 3850-4200):
    # equal in every lit frame except 1 (4249: the game moves the numbers back to palette 15 after the image); the
    # different ones are the entry into the game with the screen darkened (white stops being white: the old way missed the bars)
    add_test(NAME panel_ys COMMAND pce_trace --frame --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.brm" --bios "${SYSCARD2}"
        "${YS_CUE}" 11195 "${CMAKE_BINARY_DIR}/panel_ys.ppm")
    set_tests_properties(panel_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "panel from the game: 8168 frames in the game, 0 outside the cells. equal to the old one 7846 \\(0 without labels\\). the old one with nothing 122. different darkened 199, with the late change 0. lit and different: labels 0, bars 0, save 1 \\(1st: 4249\\)")
    # HUD on the save screen. When listing the saves the game passes them through the player's EXP ($2ABB) for a few frames: NEXT
    # comes from the EXP written in the panel and does not flicker (Ys I: 197, by RAM 912; Ys II: 2110, by RAM 52087). With the cursor on a
    # save, NEXT, the maximum and the bar are that save's ($2C4A, $2C48); the bar color read from the palette is the image's
    add_test(NAME hud_save_ys COMMAND ys_trace --frame --panel-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_221359.brm" --bios "${SYSCARD2}"
        "${YS_CUE}" 4060 "${CMAKE_BINARY_DIR}/recomp_ys/hud_save.ppm")
    set_tests_properties(hud_save_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "panel 3811: .*NEXT 197 \\(from RAM 912\\) color FFB600 image FFB600.*panel 4056:[^\n]*H\\.P 024/065 [^\n]*hud-save 1 [^\n]*NEXT 112 ")
    add_test(NAME hud_save_ys2 COMMAND ys_trace --frame --panel-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_213911.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_213911.brm"
        "${YS_CUE}" 25905 "${CMAKE_BINARY_DIR}/recomp_ys/hud_save2.ppm")
    set_tests_properties(hud_save_ys2 PROPERTIES PASS_REGULAR_EXPRESSION
        "panel 25833: .*NEXT 2110 \\(from RAM 52087\\).*panel 25841: [^\n]*H\\.P 030 [^\n]*hud-save 1 [^\n]*H\\.P 37017/145 [^\n]*NEXT 11298 .*panel 25900:[^\n]*H\\.P 215 [^\n]*NEXT 2934 ")
    # Mid-image scroll switches by the game (hooks in the line interrupt, app/ys_scroll.c) and the
    # fix for the late ones (fix_scroll). Centipede boss (Ys I, without the BIOS): the switch at the start of the map arrives 1-2
    # lines late; session of 10/25: the ring's return in the middle of the map is late (border lines in the middle of the map,
    # frame 4195; the earlier fix, by the shape of the lines, missed it). Apart from the late switches, the console always shows
    # what the game wanted (only not checked with the screen blanked)
    add_test(NAME scroll_ys COMMAND ys_trace --frame --scroll-log -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_205639.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_205639.brm"
        "${YS_CUE}" 6584 "${CMAKE_BINARY_DIR}/recomp_ys/rolagem.ppm")
    set_tests_properties(scroll_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "scroll: 3567 frames with the game's changes, 176 with a late change. 212 fixed \\(1st: 4686. 212 on the map, 0 at the bottom\\). not matching the game: 80 with the screen blanked, 0 with it lit")
    add_test(NAME scroll_back_ys COMMAND ys_trace --frame --scroll-log -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_154039.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_154039.brm"
        "${YS_CUE}" 4200 "${CMAKE_BINARY_DIR}/recomp_ys/rolagem_volta.ppm")
    set_tests_properties(scroll_back_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "scroll: 1166 frames with the game's changes, 1 with a late change. 1 fixed \\(1st: 4195. 1 on the map, 0 at the bottom\\). not matching the game: 103 with the screen blanked, 0 with it lit")
    # The window boxes from the game (ys_windows_boxes: map tile by the current camera, scroll of
    # each line; on the first image line or at the end of the frame, by the $81:6037/$6057 hooks and the image line of each
    # change) against those of the current crop (crop_ring_boxes, 1 pixel inward) and the windows' sprites against
    # ring_skip's own. Ys II without the BIOS up to frame 8500 (the camera jumps 80 pixels at 8200 with the SELECT menu open):
    # no different edge; 9 frames of count (open/close the crop sees 1 frame later); 14 sprites only
    # through the crop (whole characters under the window on the frame it opens, before the masks)
    # The crop uses the game's windows: the one with no saved background (3014-3150, with the black screen) is a scene and
    # ends at the darkening; SELECT and the inventory on top of it, by the game's tiles. In the same run, the
    # windows through the hooks equal to $3E and the game's stack in every frame, and their sprites inside.
    # The bordered screen by the game's program (load menu $01, Ys II game $30), equal to the shape in the image
    # (the 6 without the shape: the load screen being erased and the border being drawn on entry)
    add_test(NAME windows_crop_ys COMMAND pce_trace --frame --crop-log --windows -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261031_000404.brm"
        "${YS_CUE}" 8500 "${CMAKE_BINARY_DIR}/janelas_recorte.ppm")
    set_tests_properties(windows_crop_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "crop: frame 2522 on, load screen \\(program 01\\).*crop: frame 2834 on \\(program 30\\).*frame 3014 scene starts \\(darkened 7.*crop: frame 3150 scene ends \\(darkened 7, 0 frames.*crop: frame 3236 game window opens \\(columns 9-102, rows 9-70 of the map\\).*crop: frame 3603 game window opens \\(columns 9-246, rows 9-118 of the map\\).*screen from the game: 3 program loads. 5667 frames in the game, 312 on the load screen, 2521 without border. with the border: 5684 with the shape in the image, 289 black \\(0 not darkened, 1st: -1\\), 6 without the shape \\(1st: 2829\\). without border: 0 with the shape after the flash, 0 with the shape \\(1st: -1\\)\n.*3449 frames with a window .*, 25 openings, 25 closings. different from \\$3E and the game stack in 0 of 3449 frames.*window sprites: 2540, inside a window 2540, outside in a scene \\(window without saved background\\) 0, outside 0 in")
    # (in CMake regexes "." matches the newline: "(.*\n)*" hangs ctest for hours)
    # Ys II intro (session without the BIOS), the girl turning her face and the field with the column of light (figures
    # reassembled every frame): outside the engine only the scroll moves and the sprites stay still, no piece out of place
    # (the old way: 1 with more than 1000 odd pixels). In the 9 frames where the game changes VRAM while the image is being output, the
    # redraw does not reproduce them and they go as they came (582 of 591: below the 99% of the whole-session rule)
    add_test(NAME motion_intro2_ys COMMAND ys_trace --frame --smooth-frames 91540 560 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_000904.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261023_000904.brm"
        "${YS_CUE}" 92100 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_abertura2.ppm")
    set_tests_properties(motion_intro2_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "582 of 591 frames redrawn equal.*4x image equal in 59 of 59.*\n(.*\n)*.*: 0 images with the border.*\n.*: 0 images with light out of place.*\n.*. 0 with more than 1000")
    # Ys characters read from the game engine (tools/trace/ys_actors.h, docs/ys_notes.md "Engine memory map"):
    # in every gameplay frame, each sprite is at its character's position + the drawing's piece, the table
    # drawn is that of the list the VDC copied and only the 4 corners of the border have no owner. Counts the late
    # frames (the logic overran the frame: the screen repeats the previous list). Ys II (cave, without the BIOS) and Ys I
    add_test(NAME actors_ys COMMAND ys_trace --frame --actors -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.brm"
        "${YS_CUE}" 12904 "${CMAKE_BINARY_DIR}/recomp_ys/atores.ppm")
    set_tests_properties(actors_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "9495 with the engine list \\(0 with the table different from the copied list\\). 28784 character sprites, 72068 without owner. 0 out of the character position.*\n.*\n.*\nactors: 173 late frames")
    # (In the same run): the game's fade read through the hooks (--fades, app/ys_fade.c), with the real BIOS copying the
    # colors: the 7 steps of 4 frames from the first (the old way, by the image, missed the first ones)
    add_test(NAME actors_ys1 COMMAND ys_trace --frame --actors - --fades --bios "${SYSCARD2}"
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.brm"
        "${YS_CUE}" 5959 "${CMAKE_BINARY_DIR}/recomp_ys/atores1.ppm")
    # New smooth motion, reading the game engine (app/motion.h). Ys II cave on FAST (the old "2-0-4"
    # case): the late frames replaced by the list the console did not show and 0 stutters of the camera and the hero
    # (the old way: 70 and 45); 2 images with more than 1000 odd pixels (floor revealed by the game; the old way: 2).
    # (In the same run): the image assembled on the GPU, read back: at f = 1 equal to the frame; with 4
    # pixels per pixel, equal to the CPU one (the 42 different: the game rewriting the floor while the image is output and the
    # strip at the map edge with sprites)
    add_test(NAME motion_cave_fast_ys COMMAND ys_trace --frame --gpu --smooth-frames 5600 800 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.brm"
        "${YS_CUE}" 6400 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_caverna_fast.ppm")
    set_tests_properties(motion_cave_fast_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "831 of 831 frames redrawn equal. 830 with the engine list \\([0-9]+ interpolated\\). 55 late frames swapped.*4x image equal in 84 of 84\n.*\n.*: 0 images with the border.*\n.*\n.*[0-9]+ with more than 1000.*\n.*: 0 camera jerks while moving.*. 0 jerks of the hero.*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 84 of 84. 4x image equal to the CPU one in (328[2-9]|329[0-9]|33[01][0-9]|332[0-4]) of 3324"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # (The GPU gave 3282 of 3324 with 6 tests alongside and 3304 alone, with both the old and the current code: accepts
    # 3282 or more)
    # Map stopped at the edge with Adol walking on screen, FAST in the field and the Ys I mine with the screen
    # scrolling (the light stuck to Adol moves with B's map): 0 stutters, 0 light out of place
    add_test(NAME motion_cave_ys COMMAND ys_trace --frame --smooth-frames 61470 60 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_004620.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_004620.brm"
        "${YS_CUE}" 61530 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_caverna.ppm")
    add_test(NAME motion_fast_ys COMMAND ys_trace --frame --smooth-frames 11560 260 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_000647.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_000647.brm"
        "${YS_CUE}" 11820 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_fast.ppm")
    add_test(NAME motion_light_ys COMMAND ys_trace --frame --smooth-frames 43050 3 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261021_214858.brm"
        "${YS_CUE}" 43053 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_luz.ppm")
    set_tests_properties(motion_cave_ys motion_fast_ys motion_light_ys PROPERTIES PASS_REGULAR_EXPRESSION
        ": 0 images with the border.*\n.*: 0 images with light out of place.*\n.*. 0 with more than 1000.*\n.*: 0 camera jerks while moving.*. 0 jerks of the hero"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # A whole Ys I session (field, with the engine list in almost every gameplay frame) and the temple
    # (map scrolled to the end; the top border sits in the map at lines 256+): border and panel always those of the frame
    add_test(NAME motion_ys COMMAND ys_trace --frame --smooth-check
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_183502.brm"
        "${YS_CUE}" 5900 "${CMAKE_BINARY_DIR}/recomp_ys/movimento.ppm")
    add_test(NAME motion_temple_ys COMMAND ys_trace --frame --smooth-check
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_071633.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261020_071633.brm"
        "${YS_CUE}" 6217 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_templo.ppm")
    set_tests_properties(motion_ys motion_temple_ys PROPERTIES PASS_REGULAR_EXPRESSION
        ". [1-9][0-9]* late frames swapped.*\n.*\n.*: 0 images with the border.*\n.*: 0 images with light out of place"
        FAIL_REGULAR_EXPRESSION "FAILED")
    set_tests_properties(actors_ys1 PROPERTIES PASS_REGULAR_EXPRESSION
        "fades: frame 5354 level 6 brightening 0 .*fades: frame 5378 level 0 brightening 0 .*fades: frame 5474 level 7 brightening 0 .*fades: [0-9]+ calls of the fade routine \\([0-9]+ with unknown destination\\), 0 of the flash, 185 copies to the VCE\n.*2934 with the engine list \\(0 with the table different from the copied list\\). 7853 character sprites, 42148 without owner. 0 out of the character position.*\n.*\n.*\nactors: 12 late frames")
    # Adol under the bushes and roofs of the Ys II village; in the images between frames the masks
    # by the game's rule (app/ys_masks.h) at the step he is on; he never sticks out of them more than out of the game's (the GPU
    # equal to the CPU with them). The rejections are masks above the top edge of the screen
    add_test(NAME gpu_roof_ys COMMAND ys_trace --frame --gpu --smooth-frames 4150 80 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_003515.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_003515.brm"
        "${YS_CUE}" 4230 "${CMAKE_BINARY_DIR}/recomp_ys/placa_telhado.ppm")
    set_tests_properties(gpu_roof_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "rule masks \\(characters behind walls\\) in 110 frames \\([0-9]+ blocks: [0-9]+ with the game's points in B, [0-9]+ in A, 0 by the rule, used in 0 images. 507 hidden walkers, 0 blocks without a slot, 0 images without the block\\). changed 1844 4x points in 38 images. .*. 0 of [0-9]+ images going out more than the game's(.*\n)*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 12 of 12. 4x image equal to the CPU one in 444 of 444"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # Equipment on Adol (sword, shield and armor) in the frame and in the smooth-motion drawings, on the GPU and the
    # CPU: Adol recognized and replaced in every map frame; Ys I with Short Sword, Small Shield and Chain
    # Mail (session with the BIOS) and Ys II in the cave (Long Sword, Wood Shield, Chain Mail)
    add_test(NAME equip_ys1 COMMAND ys_trace --frame --gpu --equip --smooth-frames 43900 200 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261017_153505.txt" --bios "${SYSCARD2}"
        "${YS_CUE}" 44100 "${CMAKE_BINARY_DIR}/recomp_ys/equip_ys1.ppm")
    add_test(NAME equip_ys2 COMMAND ys_trace --frame --gpu --equip --smooth-frames 4900 200 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_160656.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_160656.brm" --bios "${SYSCARD2}"
        "${YS_CUE}" 5100 "${CMAKE_BINARY_DIR}/recomp_ys/equip_ys2.ppm")
    set_tests_properties(equip_ys1 equip_ys2 PROPERTIES PASS_REGULAR_EXPRESSION
        "4x image equal to the CPU one in 924 of 924(.*\n)*equipment: 230 map frames. 230 with Adol in the game, 230 swapped. Adol not recognized in 0"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # Adol by the character the game created as the hero and the colors by the game's fade routine.
    # Blue monster (the curse, Solomon temple, Ys II) and yellow (the staff): no sprite replaced, like the console.
    # Yellow back to normal form: the equipment comes back. List: the sprite table copy inside a long TIA
    # (before, the list went stale and Adol had no equipment for 4 frames). Fade with VBlank handled by the BIOS (loading
    # a save through the menu): the game's level at each step and the equipment darkening along.
    set(EQ_PARTIDAS "${CMAKE_SOURCE_DIR}/games/ys/sessions")
    macro(equip_case NOME PARTIDA DE QUADROS ESPERADO)
        math(EXPR _ate "${DE} + ${QUADROS}")
        add_test(NAME ${NOME} COMMAND ys_trace --frame --fades --equip --smooth-frames ${DE} ${QUADROS} -
            --input "${EQ_PARTIDAS}/session_${PARTIDA}.txt" --bram "${EQ_PARTIDAS}/session_${PARTIDA}.brm"
            "${YS_CUE}" ${_ate} "${CMAKE_BINARY_DIR}/recomp_ys/${NOME}.ppm")
        set_tests_properties(${NOME} PROPERTIES PASS_REGULAR_EXPRESSION "${ESPERADO}" FAIL_REGULAR_EXPRESSION "FAILED")
    endmacro()
    equip_case(equip_blue_monster_ys 20261031_195127 6000 60
        "equipment: 90 map frames. 90 with Adol in the game, 0 swapped. Adol not recognized in 90")
    equip_case(equip_yellow_monster_ys 20261103_232204 22900 60
        "equipment: 90 map frames. 90 with Adol in the game, 0 swapped. Adol not recognized in 90")
    equip_case(equip_monster_back_ys 20261103_232204 23380 60
        "equipment: 90 map frames. 90 with Adol in the game, 31 swapped. Adol not recognized in 54")
    equip_case(equip_list_ys 20261021_214858 9095 20
        "equipment: 50 map frames. 50 with Adol in the game, 24 swapped. Adol not recognized in 0")
    # On the Darm Tower ice, Adol and his reflection with the equipment, recognized by the drawing the engine requests (3 frames with the screen still black: no pixels changed)
    equip_case(equip_ice_ys 20261023_000904 14000 60
        "equipment: 90 map frames. 90 with Adol in the game, 87 swapped. Adol not recognized in 0")
    equip_case(equip_fade_bios_ys 20261027_182704 13320 30
        "fades: frame 13329 level 5 (.*\n)*equipment: 60 map frames. 60 with Adol in the game, 58 swapped. Adol not recognized in 2")
    # Kill rewards and health bars (runtime/app/battle.h). Ys II, cave (without the BIOS): the session's 5 kills,
    # each +45 EXP +45 GOLD; the save loaded at the start (EXP and GOLD rising by thousands) is not a reward; and nothing
    # drawn with the conversation window. The reward through the hooks of the hit routine (app/ys_reward.h), on the frame
    # of the kill (1 earlier than the old way, which saw EXP rise in the next list), at the same place; no event reward there.
    # (No ";" in the expressions: CMake splits them into several and one match is enough; at one point 23 tests passed that way.)
    add_test(NAME battle_ys2 COMMAND ys_trace --frame --battle --smooth-frames 3000 0 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261024_084643.brm"
        "${YS_CUE}" 8300 "${CMAKE_BINARY_DIR}/recomp_ys/battle_ys2.ppm")
    set_tests_properties(battle_ys2 PROPERTIES PASS_REGULAR_EXPRESSION
        "reward at frame 4161: \\+45 EXP \\+45 GOLD at 151,91(.*\n)*reward at frame 8236: \\+45 EXP \\+45 GOLD at 184,82(.*\n)*battle: 5 rewards. 1262 frames with a bar. 875 frames with a window(.*\n)*game rewards: 5 kills, 5 paid, 0 event sums, 0 lost. 0 without the enemy on screen\n"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # (Rewards and bars are counted per frame: 1 image per frame, PCE_SMOOTH_STEPS=1 at the end of the file,
    # gives the same numbers in 60 s instead of ~400 s)
    # Walking right on FAST, the strip at the left edge of the map comes from A's map (at f = 0 = A), on the GPU
    # and the CPU
    add_test(NAME motion_edge_ys COMMAND ys_trace --frame --gpu --smooth-frames 4104 300 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_080341.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_080341.brm"
        "${YS_CUE}" 4404 "${CMAKE_BINARY_DIR}/recomp_ys/movimento_beira.ppm")
    set_tests_properties(motion_edge_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "map edge at f = 0 different from A in 0 of 72 frames(.*\n)*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 33 of 33. 4x image equal to the CPU one in 1324 of 1324"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # Walking right behind the roof, between A and B (and at f = 1 with constant speed) Adol's back
    # edge (the shadow) stuck out of the game's masks; the rule's masks at the step he is on hide it, in every
    # image, on the GPU and the CPU (the same 9444 pixels as the original fix)
    add_test(NAME gpu_behind_roof_ys COMMAND ys_trace --frame --gpu --smooth-frames 3580 100 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_160756.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261025_160756.brm"
        "${YS_CUE}" 3680 "${CMAKE_BINARY_DIR}/recomp_ys/atras_telhado.ppm")
    set_tests_properties(gpu_behind_roof_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "rule masks \\(characters behind walls\\) in 130 frames \\([0-9]+ blocks: [0-9]+ with the game's points in B, [0-9]+ in A, 0 by the rule, used in 0 images. 809 hidden walkers, 0 blocks without a slot, 0 images without the block\\). changed 9444 4x points in 132 images. .*. 0 of [0-9]+ images going out more than the game's(.*\n)*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 14 of 14. 4x image equal to the CPU one in 524 of 524"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # Dark mine of Ys I (user session of 2026-11-02): the walkers that used to be rejected (the rule did not
    # reproduce the masks, built by the game with the previously lit floor) come in with the pixels the game drew; at
    # frame 4096 the mask of the step already decided in B covers the enemy's head (without it, the head showed in the dark):
    # the images match those with the game's masks
    add_test(NAME masks_mine_ys COMMAND ys_trace --frame --gpu --smooth-frames 4085 20 -
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.brm"
        "${YS_CUE}" 4105 "${CMAKE_BINARY_DIR}/recomp_ys/mascaras_mina.ppm")
    set_tests_properties(masks_mine_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "159 hidden walkers, 0 blocks without a slot, 0 images without the block\\). changed 0 4x points in 0 images. .*. 0 of [0-9]+ images going out more than the game's(.*\n)*GPU \\([a-z0-9]+\\): image at f = 1 equal to the frame in 5 of 5. 4x image equal to the CPU one in 204 of 204"
        FAIL_REGULAR_EXPRESSION "FAILED")
    # Conversation in the Ys I village in fill (same session): around the shrunk window, the masks the game
    # built with the window's filled tiles are redone by the rule with the map from before it (the villager behind the
    # roof, frame 8640, stays hidden as without the window)
    add_test(NAME masks_window_ys COMMAND ys_trace --frame --game-fixes --crop-log --crop-shot fill shadow
        "${CMAKE_BINARY_DIR}/recomp_ys/mascaras_janela_fill.ppm"
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261102_212804.brm"
        "${YS_CUE}" 8700 "${CMAKE_BINARY_DIR}/recomp_ys/mascaras_janela.ppm")
    set_tests_properties(masks_window_ys PROPERTIES PASS_REGULAR_EXPRESSION
        "around the shrunken windows: 779 game masks remade without the window in 199 frames \\(hiding 34656 character points\\). 0 frames with a window without the engine list")
    # Ys I mine in fill with the menu open (user session of 2026-11-03; the game is paused): around the
    # shrunk window, the image equals the one from before it. The game character that punches through the light mask in the window's tiles
    # (state $50) goes away with it; with it, 888 pixels of the circle became a square corner
    add_test(NAME light_window_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/trace/check_light_window.py"
        $<TARGET_FILE:ys_trace> "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_194815" "${YS_CUE}"
        "${CMAKE_BINARY_DIR}/recomp_ys/luz_janela" 10397:10420)
    # Ys I mine in fill with the menu opening (user session of 2026-11-03): on the late frame that could not
    # switch lists, the masks touching the window are redone with the list the console showed (without them, the
    # enemies flickered in the dark); in real time, with the real frame preparation
    # Zoom when leaving the save screen in fill (user session of 2026-11-03, two saves): the menu window stays in
    # its place in the 2 frames where the logic camera returns before the scroll, and the crop does not become a scene
    add_test(NAME zoom_save_ys COMMAND ys_trace --frame --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_213911.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_213911.brm"
        "${YS_CUE}" 26560 "${CMAKE_BINARY_DIR}/recomp_ys/zoom_save.ppm")
    set_tests_properties(zoom_save_ys PROPERTIES
        PASS_REGULAR_EXPRESSION "frame 26549 game window opens \\(columns 9-102, rows 9-70"
        FAIL_REGULAR_EXPRESSION "columns 9-287")
    # Zoom at GAME OVER when pressing RUN in fill (user session of 2026-11-03): the text stays lit with the map's level
    # at black; the scene only ends with a truly black image, at the end of the text's fade
    add_test(NAME zoom_gameover_ys COMMAND ys_trace --frame --crop-log
        --input "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_224821.txt"
        --bram "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_224821.brm"
        "${YS_CUE}" 7530 "${CMAKE_BINARY_DIR}/recomp_ys/zoom_gameover.ppm")
    set_tests_properties(zoom_gameover_ys PROPERTIES
        PASS_REGULAR_EXPRESSION "frame 7520 scene ends"
        FAIL_REGULAR_EXPRESSION "frame 4448 scene ends;frame 7489 scene ends")
    add_test(NAME app_ring_list_ys COMMAND ${PYTHON_EXE} "${CMAKE_SOURCE_DIR}/tools/app/run_ring_shown_list_test.py"
        $<TARGET_FILE:YsRecomp> "${YS_CUE}"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_203809.txt"
        "${CMAKE_SOURCE_DIR}/games/ys/sessions/session_20261103_203809.brm" "${CMAKE_BINARY_DIR}/app_output/anel_lista"
        4835 4762,4827)
    set_tests_properties(app_ring_list_ys PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
    set_tests_properties(light_window_ys PROPERTIES PASS_REGULAR_EXPRESSION "10397:10420: .*: 0 different points around it(.*\n)*mine light with window: ok"
        FAIL_REGULAR_EXPRESSION "FAILED")
endif()

# ---- Test groups (LABELS) ----
# quick:    ~20 s; run on every change (.\build.ps1 -Test)
# console:  simulated console (CPU, video, sound, CD, BIOS in C) against Geargrafx/the real BIOS
# recomp:   recompiled code against the interpreter (lockstep), performance and window options in the game
# package:  the player package (.\build.ps1 -Package)
# history:  old bugs checked in long sessions (smooth motion, crop, sound); outside
#           -TestAll, run with .\build.ps1 -TestHist when touching those parts and before large milestones
# The full suite (.\build.ps1 -TestAll, everything except history) runs in parallel before committing milestones that
# touch the console or the recompiler. Tests of the same session run together in a single run.
set(TESTS_QUICK app_config app_volume app_menu app_video app_image app_crop
    app_redraw app_fade app_input app_disc_id app_cd_menu_music app_start_ys nobios_ys app_launcher equip_c_ys
    app_mark_ys)
set(TESTS_CONSOLE trace_ys_boot trace_ys_run session_ys_boot session_ys_game session_ys_save fade_ys
    coverage_ys bios_c_ys hle_timing_ys nobios_cabin_ys equip_preview_ys equip_ice_preview_ys game_windows_ys windows_ys1 windows_crop_ys
    panel_ys)
set(TESTS_RECOMP recomp_disc_ys recomp_lockstep_ys recomp_lockstep_ys_nobios dialog_ys app_menu_ys app_intro_ys app_menu_intro_ys perf_ys torn_window_ys save_window_ring_ys hud_save_ys hud_save_ys2
    app_crop_ys app_torn_window_ys app_load_no_border_ys layout_switches_ys app_rhythm_sewer_ys rhythm_cave_ys equipment_ys actors_ys actors_ys1 motion_fast_ys
    motion_cave_fast_ys gpu_roof_ys motion_edge_ys gpu_behind_roof_ys masks_mine_ys masks_window_ys light_window_ys zoom_save_ys zoom_gameover_ys app_ring_list_ys
    equip_ys1 equip_ys2 battle_ys2 equip_blue_monster_ys equip_yellow_monster_ys equip_monster_back_ys equip_list_ys
    equip_fade_bios_ys equip_ice_ys
    app_menu_image_ys app_menu_chip_ys app_hiccup_ys app_stall_ys app_stall_device_ys scroll_ys scroll_back_ys
    app_prep_ys)
set(TESTS_HISTORY cd_pop_ys motion_save_ys crop_flash_ys item_window_ys motion_intro2_ys
    motion_cave_ys motion_light_ys motion_ys motion_temple_ys gpu_window_ys)
set(TESTS_PACKAGE app_package_ys app_launcher_ys)
# No test shows on screen or plays sound: the window opens hidden (PCE_HIDDEN_WINDOW, runtime/main.c: no
# fullscreen, no focus, no V-Sync) and the sound goes to SDL's dummy audio device (it consumes at the real pace:
# the sound queues of the real-time tests still count). Tests that measure real time or count what the window
# drew run alone (RUN_SERIAL): other heavy tests alongside would change the pacing.
set(TESTS_TEMPO_REAL app_stall_ys app_stall_device_ys app_hiccup_ys app_torn_window_ys app_load_no_border_ys app_rhythm_sewer_ys app_ring_list_ys
    app_menu_chip_ys app_mark_ys app_intro_ys app_menu_intro_ys app_crop_ys app_prep_ys)
foreach(group QUICK CONSOLE RECOMP HISTORY PACKAGE)
    string(TOLOWER ${group} label)
    foreach(t ${TESTS_${group}})
        if(TEST ${t})
            set_tests_properties(${t} PROPERTIES LABELS ${label})
        endif()
    endforeach()
endforeach()
get_property(ALL_TESTS DIRECTORY PROPERTY TESTS)
foreach(t ${ALL_TESTS})
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "PCE_HIDDEN_WINDOW=1;SDL_AUDIO_DRIVER=dummy")
endforeach()
if(TEST battle_ys2)
    set_property(TEST battle_ys2 APPEND PROPERTY ENVIRONMENT "PCE_SMOOTH_STEPS=1")
endif()
foreach(t ${TESTS_TEMPO_REAL})
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES RUN_SERIAL TRUE)
    endif()
endforeach()
