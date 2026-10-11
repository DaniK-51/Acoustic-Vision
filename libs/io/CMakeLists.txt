include(FetchContent)

# tinyobjloader is header-only for our purposes; SOURCE_SUBDIR points at a
# path without CMakeLists.txt so MakeAvailable populates sources but does not
# add_subdirectory (and does not build their tester tools).
FetchContent_Declare(tinyobjloader
  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
  GIT_TAG v2.0.0rc13
  SOURCE_SUBDIR unused-no-cmake-here
)
FetchContent_MakeAvailable(tinyobjloader)

# tinygltf is header-only the same way (TINYGLTF_IMPLEMENTATION in one TU);
# skip their CMake and the stb image deps via TINYGLTF_NO_STB_*.
FetchContent_Declare(tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG v2.9.3
  SOURCE_SUBDIR unused-no-cmake-here
)
FetchContent_MakeAvailable(tinygltf)

add_library(av_io
  src/scenario_json.cpp
  src/source_catalog.cpp
  src/wav.cpp
  src/metrics_json.cpp
  src/obj_import.cpp
  src/gltf_import.cpp
  src/mesh_import.cpp
)

add_library(av::io ALIAS av_io)

target_include_directories(av_io
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(av_io PUBLIC cxx_std_20)

target_link_libraries(av_io
  PUBLIC
    av::core
    nlohmann_json::nlohmann_json
)

target_include_directories(av_io PRIVATE "${tinyobjloader_SOURCE_DIR}")
# SYSTEM keeps tinygltf warnings out of -Wall -Wextra on our TUs.
target_include_directories(av_io SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")

av_set_warnings(av_io)
