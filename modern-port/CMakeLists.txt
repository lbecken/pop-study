# pop-study modern port.  New code; see ../README.md.
#
#   cmake -S modern-port -B build/port -DPOP_SOURCE_DIR=/path/to/original
#   cmake --build build/port
#   ctest --test-dir build/port --output-on-failure
#
# Without POP_SOURCE_DIR only the tests that use synthetic data are built.
# With it, the data the port needs is generated from that checkout into the
# build directory (nothing original is stored in this repository).

cmake_minimum_required(VERSION 3.20)
project(pop_modern_port VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(POP_WERROR "Treat compiler warnings as errors" OFF)
option(POP_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
set(POP_SOURCE_DIR "" CACHE PATH
    "Checkout of jmechner/Prince-of-Persia-Apple-II (enables original-data tests)")

if(MSVC)
  add_compile_options(/W4)
  if(POP_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow
                      -Wstrict-prototypes -Wmissing-prototypes)
  if(POP_WERROR)
    add_compile_options(-Werror)
  endif()
  if(POP_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# Game rules: no I/O, no SDL.
add_library(pop_core STATIC src/core/pop_seq.c src/core/pop_sound.c src/core/pop_world.c
            src/core/pop_control.c src/core/pop_collide.c
            src/core/pop_mover.c src/core/pop_auto.c src/core/pop_top.c src/core/pop_loop.c)
target_include_directories(pop_core PUBLIC src/core)

# Video: the blitter, the display lists, the redraw of a screen.  No I/O, no SDL.
add_library(pop_video STATIC src/video/pop_hires.c src/video/pop_lists.c
            src/video/pop_frameadv.c)
target_include_directories(pop_video PUBLIC src/video)
target_link_libraries(pop_video PUBLIC pop_core)

# Sound: effect data to PCM.  No I/O, no SDL (the SDL3 backend comes later).
add_library(pop_audio STATIC src/audio/pop_sfx.c)
target_include_directories(pop_audio PUBLIC src/audio)

# Loading generated data files.
add_library(pop_assets STATIC src/assets/pop_assets.c src/assets/pop_gfxfiles.c)
target_include_directories(pop_assets PUBLIC src/assets)
target_link_libraries(pop_assets PUBLIC pop_core pop_audio pop_video)

add_executable(pop_seqdump tools/pop_seqdump.c)
target_link_libraries(pop_seqdump PRIVATE pop_assets)

add_executable(pop_sfxdump tools/pop_sfxdump.c)
target_link_libraries(pop_sfxdump PRIVATE pop_assets)

add_executable(pop_worldrun tools/pop_worldrun.c)
target_link_libraries(pop_worldrun PRIVATE pop_assets)

add_executable(pop_hiresrun tools/pop_hiresrun.c)
target_link_libraries(pop_hiresrun PRIVATE pop_video)

add_executable(pop_gfxrun tools/pop_gfxrun.c)
target_link_libraries(pop_gfxrun PRIVATE pop_assets)

enable_testing()

add_executable(test_seq tests/test_seq.c)
target_link_libraries(test_seq PRIVATE pop_core)
add_test(NAME seq_unit COMMAND test_seq)

add_executable(test_assets tests/test_assets.c)
target_link_libraries(test_assets PRIVATE pop_assets)
add_test(NAME assets_unit COMMAND test_assets)

add_executable(test_sound tests/test_sound.c)
target_link_libraries(test_sound PRIVATE pop_assets)
if(NOT MSVC)
  target_link_libraries(test_sound PRIVATE m)
endif()
add_test(NAME sound_unit COMMAND test_sound)

add_executable(test_world tests/test_world.c)
target_link_libraries(test_world PRIVATE pop_core)
add_test(NAME world_unit COMMAND test_world)

add_executable(test_control tests/test_control.c)
target_link_libraries(test_control PRIVATE pop_core)
add_test(NAME control_unit COMMAND test_control)

add_executable(test_collide tests/test_collide.c)
target_link_libraries(test_collide PRIVATE pop_core)
add_test(NAME collide_unit COMMAND test_collide)

add_executable(test_mover tests/test_mover.c)
target_link_libraries(test_mover PRIVATE pop_core)
add_test(NAME mover_unit COMMAND test_mover)

add_executable(test_auto tests/test_auto.c)
target_link_libraries(test_auto PRIVATE pop_core)
add_test(NAME auto_unit COMMAND test_auto)

add_executable(test_top tests/test_top.c)
target_link_libraries(test_top PRIVATE pop_core)
add_test(NAME top_unit COMMAND test_top)

add_executable(test_loop tests/test_loop.c)
target_link_libraries(test_loop PRIVATE pop_core)
add_test(NAME loop_unit COMMAND test_loop)

add_executable(test_hires tests/test_hires.c)
target_link_libraries(test_hires PRIVATE pop_video)
add_test(NAME hires_unit COMMAND test_hires)

add_executable(test_gfx tests/test_gfx.c)
target_link_libraries(test_gfx PRIVATE pop_video)
add_test(NAME gfx_unit COMMAND test_gfx)

# --- tests on the original data (optional) -----------------------------------
if(POP_SOURCE_DIR)
  if(NOT EXISTS "${POP_SOURCE_DIR}/01 POP Source/Source/SEQTABLE.S")
    message(FATAL_ERROR "POP_SOURCE_DIR does not look like the original repository: "
                        "${POP_SOURCE_DIR}")
  endif()
  find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

  set(POP_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/extract_assets")
  set(POP_AUDIO_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/extract_audio")
  set(POP_DATA "${CMAKE_CURRENT_BINARY_DIR}/data")
  set(POP_DATA_FILES "${POP_DATA}/seqtable.bin" "${POP_DATA}/pop_seqnames.h"
                     "${POP_DATA}/sfx.bin" "${POP_DATA}/pop_sfxnames.h"
                     "${POP_DATA}/framedef.bin" "${POP_DATA}/imgdims.bin"
                     "${POP_DATA}/autodata.bin" "${POP_DATA}/bgdata.bin")
  add_custom_command(
    OUTPUT ${POP_DATA_FILES}
    COMMAND "${Python3_EXECUTABLE}" "${POP_TOOLS}/export_port_data.py"
            --source "${POP_SOURCE_DIR}" --out "${POP_DATA}"
    DEPENDS "${POP_TOOLS}/export_port_data.py" "${POP_TOOLS}/merlin_data.py"
            "${POP_TOOLS}/pop_animdata.py" "${POP_TOOLS}/mos6502.py"
            "${POP_TOOLS}/pop_imgtable.py" "${POP_TOOLS}/pop_room.py"
            "${POP_AUDIO_TOOLS}/pop_sfx.py"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SEQTABLE.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SEQDATA.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SOUND.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SOUNDNAMES.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/FRAMEDEF.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/AUTO.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SUBS.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/BGDATA.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/GAMEBG.S"
    COMMENT "Generating port data from the original source"
    VERBATIM)
  add_custom_target(pop_data DEPENDS ${POP_DATA_FILES})

  add_executable(test_seq_original tests/test_seq_original.c)
  target_include_directories(test_seq_original PRIVATE "${POP_DATA}" tests)
  target_link_libraries(test_seq_original PRIVATE pop_assets)
  add_dependencies(test_seq_original pop_data)
  add_dependencies(pop_seqdump pop_data)

  add_executable(test_sfx_original tests/test_sfx_original.c)
  target_include_directories(test_sfx_original PRIVATE "${POP_DATA}" tests)
  target_link_libraries(test_sfx_original PRIVATE pop_assets)
  add_dependencies(test_sfx_original pop_data)
  add_dependencies(pop_sfxdump pop_data)
  add_dependencies(pop_worldrun pop_data)
  add_dependencies(pop_gfxrun pop_data)

  add_test(NAME seq_original COMMAND test_seq_original "${POP_DATA}/seqtable.bin")
  add_test(NAME seq_c_vs_python
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_seq_traces.py"
                   "$<TARGET_FILE:pop_seqdump>" "${POP_DATA}/seqtable.bin"
                   "${POP_SOURCE_DIR}" 200)
  add_test(NAME sfx_original COMMAND test_sfx_original "${POP_DATA}/sfx.bin")
  add_test(NAME sfx_c_vs_python
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_sfx.py"
                   "$<TARGET_FILE:pop_sfxdump>" "${POP_DATA}/sfx.bin"
                   "${POP_SOURCE_DIR}" 44100)
  add_test(NAME world_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_world.py"
                   "$<TARGET_FILE:pop_worldrun>" "${POP_DATA}/framedef.bin"
                   "${POP_SOURCE_DIR}" 400 1989)
  add_executable(test_control_original tests/test_control_original.c)
  target_include_directories(test_control_original PRIVATE "${POP_DATA}" tests)
  target_link_libraries(test_control_original PRIVATE pop_assets)
  add_dependencies(test_control_original pop_data)
  add_test(NAME control_original
           COMMAND test_control_original "${POP_DATA}"
                   "${POP_SOURCE_DIR}/01 POP Source/Levels/LEVEL1")
  add_test(NAME control_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_control.py"
                   "$<TARGET_FILE:pop_worldrun>" "${POP_DATA}" "${POP_SOURCE_DIR}" 400 1989)
  add_test(NAME hires_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_hires.py"
                   "$<TARGET_FILE:pop_hiresrun>" "${POP_SOURCE_DIR}" 1200 1989)
  add_test(NAME rooms_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_rooms.py"
                   "$<TARGET_FILE:pop_gfxrun>" "${POP_DATA}" "${POP_SOURCE_DIR}" 15 1989)
  add_test(NAME demo_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_demo.py"
                   "$<TARGET_FILE:pop_worldrun>" "${POP_DATA}" "${POP_SOURCE_DIR}")
  add_test(NAME kidstep_c_vs_original
           COMMAND "${Python3_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/trace_kidstep.py"
                   "$<TARGET_FILE:pop_worldrun>" "${POP_DATA}" "${POP_SOURCE_DIR}" 60 1989)
else()
  message(STATUS "POP_SOURCE_DIR not set: original-data tests are not built")
endif()
