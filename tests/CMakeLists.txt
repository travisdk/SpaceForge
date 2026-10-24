# Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
# SPDX-License-Identifier: AGPL-3.0-or-later

# Unit tests, run with ctest. SpaceForge_Tests builds them all.
add_custom_target(SpaceForge_Tests)

# The two vendored engines on their own (no JUCE).
add_executable(test_engines test_engines.cpp)
target_link_libraries(test_engines PRIVATE cloudseed clouds juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME engines COMMAND test_engines)
add_dependencies(SpaceForge_Tests test_engines)

# The room engine: character targets, sample rates, block sizes, Mix, Freeze, no allocations.
add_executable(test_space test_space.cpp)
target_link_libraries(test_space PRIVATE spaceforge_analysis juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME space COMMAND test_space)
add_dependencies(SpaceForge_Tests test_space)

# The granular texture: sample-rate conversion, every host rate, extreme settings.
add_executable(test_texture test_texture.cpp)
target_link_libraries(test_texture PRIVATE spaceforge_analysis juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME texture COMMAND test_texture)
add_dependencies(SpaceForge_Tests test_texture)

# The macro contract: direction, audible effect, level, real-unit texts, no clicks.
add_executable(test_macros test_macros.cpp)
target_link_libraries(test_macros PRIVATE spaceforge_analysis juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME macros COMMAND test_macros)
add_dependencies(SpaceForge_Tests test_macros)

# The Modes: a medium room at Space 50 %, equal levels, no hiss, knobs in every Mode, clean switch.
add_executable(test_modes test_modes.cpp)
target_link_libraries(test_modes PRIVATE spaceforge_analysis juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME modes COMMAND test_modes)
add_dependencies(SpaceForge_Tests test_modes)

# The plug-in as a host drives it.
add_executable(test_plugin test_plugin.cpp)
target_include_directories(test_plugin PRIVATE ${PROJECT_SOURCE_DIR}/source $<TARGET_PROPERTY:SpaceForge2,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_plugin PRIVATE $<TARGET_PROPERTY:SpaceForge2,COMPILE_DEFINITIONS>)
target_link_libraries(test_plugin PRIVATE SpaceForge2 spaceforge_analysis)
add_test(NAME plugin COMMAND test_plugin)
add_dependencies(SpaceForge_Tests test_plugin)
