add_library(av_core
  src/materials/material.cpp
  src/scenario/scenario.cpp
  src/scene/scene_model.cpp
  src/scene/scene_mesher.cpp
  src/metrics/metrics.cpp
  src/geometry/mesh.cpp
  src/geometry/mesh_builder.cpp
  src/geometry/bvh.cpp
  src/acoustics/engine.cpp
  src/acoustics/builtin_engines.cpp
  src/acoustics/rt_core.cpp
  src/acoustics/sabine_eyring_engine.cpp
  src/acoustics/image_source_engine.cpp
  src/acoustics/ray_tracing_engine.cpp
  src/acoustics/hybrid_engine.cpp
)

add_library(av::core ALIAS av_core)

target_include_directories(av_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(av_core PUBLIC cxx_std_20)

target_link_libraries(av_core
  PUBLIC
    nlohmann_json::nlohmann_json
)

av_set_warnings(av_core)
