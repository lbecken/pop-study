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
add_library(pop_core STATIC src/core/pop_seq.c src/core/pop_sound.c)
target_include_directories(pop_core PUBLIC src/core)

# Sound: effect data to PCM.  No I/O, no SDL (the SDL3 backend comes later).
add_library(pop_audio STATIC src/audio/pop_sfx.c)
target_include_directories(pop_audio PUBLIC src/audio)

# Loading generated data files.
add_library(pop_assets STATIC src/assets/pop_assets.c)
target_include_directories(pop_assets PUBLIC src/assets)
target_link_libraries(pop_assets PUBLIC pop_core pop_audio)

add_executable(pop_seqdump tools/pop_seqdump.c)
target_link_libraries(pop_seqdump PRIVATE pop_assets)

add_executable(pop_sfxdump tools/pop_sfxdump.c)
target_link_libraries(pop_sfxdump PRIVATE pop_assets)

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
                     "${POP_DATA}/sfx.bin" "${POP_DATA}/pop_sfxnames.h")
  add_custom_command(
    OUTPUT ${POP_DATA_FILES}
    COMMAND "${Python3_EXECUTABLE}" "${POP_TOOLS}/export_port_data.py"
            --source "${POP_SOURCE_DIR}" --out "${POP_DATA}"
    DEPENDS "${POP_TOOLS}/export_port_data.py" "${POP_TOOLS}/merlin_data.py"
            "${POP_TOOLS}/pop_animdata.py" "${POP_TOOLS}/mos6502.py"
            "${POP_AUDIO_TOOLS}/pop_sfx.py"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SEQTABLE.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SEQDATA.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SOUND.S"
            "${POP_SOURCE_DIR}/01 POP Source/Source/SOUNDNAMES.S"
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
else()
  message(STATUS "POP_SOURCE_DIR not set: original-data tests are not built")
endif()
