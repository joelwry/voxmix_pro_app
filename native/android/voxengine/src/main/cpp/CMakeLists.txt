# libvoxengine.so: the platform-independent engine + effects (native/) + the Oboe device and JNI.
cmake_minimum_required(VERSION 3.22)
project(voxengine_android CXX)
set(CMAKE_CXX_STANDARD 17)

set(VOXMIX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
get_filename_component(VOXMIX_NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_subdirectory("${VOXMIX_NATIVE}" voxnative)

find_package(oboe REQUIRED CONFIG)

add_library(voxengine_jni SHARED OboeDevice.cpp VoxEngineJni.cpp)
set_target_properties(voxengine_jni PROPERTIES OUTPUT_NAME voxengine)
target_link_libraries(voxengine_jni PRIVATE voxengine oboe::oboe log)
target_compile_options(voxengine_jni PRIVATE -Wall -Wextra -Wno-unused-parameter)
# keep the binary small and the hot loops fast
target_link_options(voxengine_jni PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
