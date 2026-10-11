add_executable(av-cli main.cpp)

target_link_libraries(av-cli
  PRIVATE
    av::io
)

av_set_warnings(av-cli)
