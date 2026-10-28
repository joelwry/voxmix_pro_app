# voxengine: platform-independent real-time audio engine (no React Native, no Android code).
add_library(voxengine STATIC
    src/Analysis.cpp
    src/AudioSource.cpp
    src/Engine.cpp
    src/GraphBuilder.cpp
    src/Metering.cpp
    src/MixGraph.cpp
    src/OfflineRenderer.cpp
    src/Presets.cpp
    src/Project.cpp
    src/ProjectSerializer.cpp
    src/RecordingManager.cpp
    src/WavWriter.cpp)
target_include_directories(voxengine PUBLIC include ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
target_link_libraries(voxengine PUBLIC voxfx)
find_package(Threads REQUIRED)
target_link_libraries(voxengine PUBLIC Threads::Threads)
set_target_properties(voxengine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(voxengine PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
