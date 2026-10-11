# av-api: REST + WebSocket server (Crow + standalone Asio).
# Stack is fixed in ARCHITECTURE.md section 5 — Crow, no Boost.

include(FetchContent)

# --- Asio (standalone) ------------------------------------------------------
# Crow with CROW_USE_BOOST=OFF runs find_package(asio REQUIRED) through its
# bundled Findasio.cmake, which only needs ASIO_INCLUDE_DIR with asio.hpp.
# Provide that path from vcpkg/system (asio CONFIG) or a source checkout.
find_package(asio CONFIG QUIET)
if(asio_FOUND)
  # vcpkg's asio config exports asio::asio; leave discovery to Findasio.cmake.
else()
  FetchContent_Declare(asio_src
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
  )
  FetchContent_MakeAvailable(asio_src)
  # Release layout is <src>/asio/include/asio.hpp; some refs also mirror it
  # under <src>/include. Pick whichever exists so Findasio.cmake finds it.
  if(EXISTS "${asio_src_SOURCE_DIR}/asio/include/asio.hpp")
    set(_av_asio_include "${asio_src_SOURCE_DIR}/asio/include")
  else()
    set(_av_asio_include "${asio_src_SOURCE_DIR}/include")
  endif()
  set(ASIO_INCLUDE_DIR "${_av_asio_include}"
      CACHE PATH "standalone asio headers" FORCE)
endif()

# --- Crow -------------------------------------------------------------------
find_package(Crow CONFIG QUIET)
if(NOT Crow_FOUND)
  set(CROW_USE_BOOST OFF CACHE BOOL "Crow on standalone Asio" FORCE)
  set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CROW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
  )
  FetchContent_MakeAvailable(Crow)
endif()

add_executable(av-api
  main.cpp
  api_routes.cpp
  api_support.cpp
  job_watcher.cpp
  routes_events.cpp
  routes_health_engines.cpp
  routes_jobs_runs.cpp
  routes_scenes.cpp
  routes_simulations.cpp
  runs_store.cpp
  scene_store.cpp
)

target_include_directories(av-api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(av-api PRIVATE AV_VERSION="${PROJECT_VERSION}")

target_link_libraries(av-api
  PRIVATE
    av::sim
    av::io
    Crow::Crow
)

av_set_warnings(av-api)

# Built-in source catalog is looked up as source_catalog.json beside the
# executable (AV_SOURCE_CATALOG overrides the path).
add_custom_command(TARGET av-api POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/data/source_catalog.json"
          "$<TARGET_FILE_DIR:av-api>/source_catalog.json"
  COMMENT "Copy source_catalog.json beside av-api"
)

if(WIN32)
  target_link_libraries(av-api PRIVATE ws2_32 mswsock)
endif()
