add_executable(voxmix_tests TestMain.cpp EffectTests.cpp EngineTests.cpp)
target_link_libraries(voxmix_tests PRIVATE voxengine)
target_include_directories(voxmix_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME voxmix_tests COMMAND voxmix_tests)
