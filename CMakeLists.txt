# Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)
project(SpaceForge2 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: link the C++ runtime statically (/MT). A plug-in runs inside the host's process, so a
# dynamic MSVCP140.dll would have to be installed on the user's PC and could be an older version
# already loaded by the host. Set before any target is created. (From SpaceForge 1.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE defaults to ~/SDKs (Linux, macOS) or %USERPROFILE%\SDKs (Windows); override with -DJUCE_DIR=...
if (WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" SPACEFORGE_HOME)
else()
    set(SPACEFORGE_HOME "$ENV{HOME}")
endif()
set(JUCE_DIR "${SPACEFORGE_HOME}/SDKs/JUCE-8.0.15" CACHE PATH "JUCE source tree")
set(CLAP_JUCE_EXTENSIONS_DIR "${SPACEFORGE_HOME}/SDKs/clap-juce-extensions-main" CACHE PATH "clap-juce-extensions source tree")

# Per-user CLAP and VST3 folders from the specifications (no administrator rights needed)
if (WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" SPACEFORGE_LOCALAPPDATA)
    set(SPACEFORGE_DEFAULT_CLAP_DIR "${SPACEFORGE_LOCALAPPDATA}/Programs/Common/CLAP")
    set(SPACEFORGE_DEFAULT_VST3_DIR "${SPACEFORGE_LOCALAPPDATA}/Programs/Common/VST3")
elseif (APPLE)
    set(SPACEFORGE_DEFAULT_CLAP_DIR "${SPACEFORGE_HOME}/Library/Audio/Plug-Ins/CLAP")
    set(SPACEFORGE_DEFAULT_VST3_DIR "${SPACEFORGE_HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(SPACEFORGE_DEFAULT_CLAP_DIR "${SPACEFORGE_HOME}/.clap")
    set(SPACEFORGE_DEFAULT_VST3_DIR "${SPACEFORGE_HOME}/.vst3")
endif()
option(SPACEFORGE_INSTALL_AFTER_BUILD "Copy the CLAP and VST3 into the user's plug-in folders after each build" ON)
set(CLAP_INSTALL_DIR "${SPACEFORGE_DEFAULT_CLAP_DIR}" CACHE PATH "Where the built .clap is copied")
set(VST3_INSTALL_DIR "${SPACEFORGE_DEFAULT_VST3_DIR}" CACHE PATH "Where the built .vst3 is copied")

add_subdirectory(${JUCE_DIR} JUCE)
add_subdirectory(${CLAP_JUCE_EXTENSIONS_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------------
# The two engines, vendored unmodified in third_party/ (versions and licenses in THIRD_PARTY.md).
# They build with their authors' own warning level; their headers are SYSTEM includes so they
# never add warnings to our code.

# Cloud Seed (rooms and echoes). BUFFER_SIZE is only its internal chunk size: it splits longer
# host blocks itself. strcpy_s is MSVC-only, so other compilers get a bounded equivalent.
add_library(cloudseed STATIC
    third_party/cloudseedcore/Parameters.cpp
    third_party/cloudseedcore/DSP/Biquad.cpp
    third_party/cloudseedcore/DSP/RandomBuffer.cpp)
target_include_directories(cloudseed SYSTEM PUBLIC third_party/cloudseedcore)
target_compile_definitions(cloudseed PUBLIC BUFFER_SIZE=1024 MAX_STR_SIZE=32)
if (NOT MSVC)
    target_compile_options(cloudseed PUBLIC -include ${CMAKE_CURRENT_SOURCE_DIR}/source/compat/CloudSeedCompat.h)
endif()
set_target_properties(cloudseed PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Clouds (granular texture) with the parts of stmlib it needs. TEST selects the desktop build
# that its author uses for testing on a computer (no STM32 hardware drivers).
add_library(clouds STATIC
    third_party/eurorack/clouds/resources.cc
    third_party/eurorack/clouds/dsp/correlator.cc
    third_party/eurorack/clouds/dsp/granular_processor.cc
    third_party/eurorack/clouds/dsp/mu_law.cc
    third_party/eurorack/clouds/dsp/pvoc/frame_transformation.cc
    third_party/eurorack/clouds/dsp/pvoc/phase_vocoder.cc
    third_party/eurorack/clouds/dsp/pvoc/stft.cc
    third_party/eurorack/stmlib/dsp/atan.cc
    third_party/eurorack/stmlib/dsp/units.cc
    third_party/eurorack/stmlib/utils/random.cc)
target_include_directories(clouds SYSTEM PUBLIC third_party/eurorack)
target_compile_definitions(clouds PUBLIC TEST)
set_target_properties(clouds PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------------------------
juce_add_plugin(SpaceForge2
    COMPANY_NAME "Henrik Juhl"
    BUNDLE_ID dk.hjuhl.spaceforge2
    PLUGIN_MANUFACTURER_CODE Hjuh
    PLUGIN_CODE SpF2
    PRODUCT_NAME "SpaceForge2"
    PLUGIN_NAME "SpaceForge 2"
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx Reverb Delay
    COPY_PLUGIN_AFTER_BUILD ${SPACEFORGE_INSTALL_AFTER_BUILD}
    VST3_COPY_DIR "${VST3_INSTALL_DIR}"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(SpaceForge2 PRIVATE
    source/plugin/PluginProcessor.cpp)

# SPACEFORGE_VERSION_STRING in SpaceForgeVersion.h: "git describe --tags --dirty", refreshed at
# every build; the project VERSION when git or tags are missing. (From SpaceForge 1.)
find_package(Git QUIET)
set(SPACEFORGE_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/SpaceForgeVersion.h")
set(SPACEFORGE_VERSION_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DOUTPUT=${SPACEFORGE_VERSION_HEADER}
    -DFALLBACK=${PROJECT_VERSION} -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake)
execute_process(COMMAND ${SPACEFORGE_VERSION_COMMAND})
add_custom_target(SpaceForgeVersion COMMAND ${SPACEFORGE_VERSION_COMMAND} BYPRODUCTS ${SPACEFORGE_VERSION_HEADER}
    COMMENT "Checking the SpaceForge 2 version")
add_dependencies(SpaceForge2 SpaceForgeVersion)
target_include_directories(SpaceForge2 PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated source)

target_compile_definitions(SpaceForge2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# GCC: run the link-time optimisation in parallel (and without its "serial compilation" note)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(SpaceForge2 PUBLIC -flto=auto)
endif()

target_link_libraries(SpaceForge2
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# CLAP via clap-juce-extensions (as in SpaceForge 1), copied to the CLAP folder after each build
clap_juce_extensions_plugin(TARGET SpaceForge2
    CLAP_ID "dk.hjuhl.spaceforge2"
    CLAP_FEATURES audio-effect reverb delay stereo)
if (SPACEFORGE_INSTALL_AFTER_BUILD)
    add_custom_command(TARGET SpaceForge2_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CLAP_INSTALL_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SpaceForge2_CLAP> ${CLAP_INSTALL_DIR}/
        COMMENT "Installing SpaceForge2.clap into ${CLAP_INSTALL_DIR}")
endif()

# ---------------------------------------------------------------------------------------------
option(SPACEFORGE_BUILD_TESTS "Build the unit tests (run with ctest)" ON)
if (SPACEFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
