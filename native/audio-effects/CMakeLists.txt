# voxfx: VoxMix effect library. Plain C++17, no platform or framework dependencies.
# Contains the vendored DSP from the VocalSweet and MoodShift FX plugin repositories
# (see vendor/PROVENANCE.md) plus the VoxMix effect wrappers.

file(GLOB MOODSHIFT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moodshift/Effects/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moodshift/Effects/Library/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moodshift/Effects/Pitch/*.cpp")
# The plugin's 40-effect EffectChain is not used on mobile (each slot creates one effect).
list(FILTER MOODSHIFT_SOURCES EXCLUDE REGEX ".*/Effects/EffectChain\\.cpp$")

add_library(voxfx STATIC
    src/Effect.cpp
    src/EffectChain.cpp
    src/CoreEffects.cpp
    src/VocalSweetEffect.cpp
    src/MoodShiftEffects.cpp
    src/PitchCorrection.cpp
    src/MasteringEffect.cpp
    ${MOODSHIFT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moodshift/Presets/FactoryPresets.cpp")
target_include_directories(voxfx PUBLIC include)
target_compile_features(voxfx PUBLIC cxx_std_17)
set_target_properties(voxfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(voxfx PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
