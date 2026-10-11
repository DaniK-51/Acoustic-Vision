add_library(av_io
  src/scenario_json.cpp
  src/wav.cpp
  src/metrics_json.cpp
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

av_set_warnings(av_io)
