# VoxMix Pro native code: effect library (voxfx) + audio engine (voxengine) + host tests.
# The same sources are built for Android by native/android/voxengine (Gradle + CMake).
cmake_minimum_required(VERSION 3.22)
project(VoxMixNative VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VOXMIX_BUILD_TESTS "Build the host test suite" ON)
option(VOXMIX_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

if(VOXMIX_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(audio-effects)
add_subdirectory(audio-engine)

if(VOXMIX_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(audio-engine/tests)
endif()
