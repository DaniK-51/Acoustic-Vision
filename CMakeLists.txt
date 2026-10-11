cmake_minimum_required(VERSION 3.24)

project(AcousticVision
  VERSION 0.1.0
  DESCRIPTION "Room acoustics analysis: scene + engines -> IR, metrics, fields"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AV_BUILD_TESTS "Build unit tests" ON)
option(AV_BUILD_API "Build REST API server" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AvWarnings)

# find_package first (vcpkg/system), FetchContent fallback when missing.
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/io)
add_subdirectory(libs/sim)

add_subdirectory(apps/cli)

if(AV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
