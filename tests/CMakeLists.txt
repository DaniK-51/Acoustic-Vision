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
  unit/test_ray_tracing.cpp
  unit/test_io.cpp
  unit/test_mesh.cpp
  unit/test_obj_import.cpp
  unit/test_gltf_import.cpp
  unit/test_sim.cpp
  unit/test_runs_store.cpp
  # Crow-free api helper under test (P2e runs history).
  ${CMAKE_SOURCE_DIR}/apps/api/runs_store.cpp
)

target_include_directories(av_unit_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/apps/api
)

target_compile_definitions(av_unit_tests
  PRIVATE AV_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(av_unit_tests
  PRIVATE
    av::core
    av::io
    av::sim
    GTest::gtest_main
)

av_set_warnings(av_unit_tests)

gtest_discover_tests(av_unit_tests)

# API smoke: spawn av-api on an ephemeral port (only when the server is built).
if(AV_BUILD_API AND TARGET av-api)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME api_smoke
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/integration/api_smoke.py"
              "$<TARGET_FILE:av-api>"
              "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/demo-scenario.json"
    )
    set_tests_properties(api_smoke PROPERTIES TIMEOUT 120)
  else()
    message(STATUS "Python3 not found; api_smoke integration test skipped")
  endif()
endif()

# CLI field smoke: av-cli run writes field.json when params.fieldGrid is set.
if(TARGET av-cli)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME cli_field_smoke
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_field_smoke.py"
              "$<TARGET_FILE:av-cli>"
              "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/box-scenario.json"
    )
    set_tests_properties(cli_field_smoke PROPERTIES TIMEOUT 60)
  else()
    message(STATUS "Python3 not found; cli_field_smoke integration test skipped")
  endif()
endif()
