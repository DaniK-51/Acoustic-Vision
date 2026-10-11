add_library(av_sim
  src/job_manager.cpp
)

add_library(av::sim ALIAS av_sim)

target_include_directories(av_sim
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(av_sim PUBLIC cxx_std_20)

target_link_libraries(av_sim
  PUBLIC
    av::core
)

av_set_warnings(av_sim)
