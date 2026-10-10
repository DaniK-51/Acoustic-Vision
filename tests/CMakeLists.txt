include(FetchContent)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(av_unit_tests
  unit/test_materials.cpp
  unit/test_engine_registry.cpp
  unit/test_metrics.cpp
  unit/test_engines.cpp
)

target_link_libraries(av_unit_tests
  PRIVATE
    av::core
    GTest::gtest_main
)

av_set_warnings(av_unit_tests)

gtest_discover_tests(av_unit_tests)
