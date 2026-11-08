cmake_minimum_required(VERSION 3.21)
project(SamNPlayer2 VERSION 2.0.0 LANGUAGES CXX)

# SamNPlayer 2: C++ first (docs/PLAN.md). core/ is plain C++17 without Qt;
# app/ is the Qt 6 application; Python bindings of core/ serve the AI tools.
option(SAMN_BUILD_APP "Build the Qt application" ON)
option(SAMN_BUILD_PYTHON "Build the Python bindings of core (pybind11)" ON)
option(SAMN_BUILD_TESTS "Build the C++ tests" ON)
option(SAMN_BUILD_VISION "Build samn_vision (OpenCV: frames and tracking for the Generator)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# Prefer git over the Releases .tar.xz: GitHub Releases edges often 504 in CI
# (configure never starts). Same pin v3.11.3; shallow clone, no json tests.
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(core)
if(SAMN_BUILD_VISION)
  add_subdirectory(vision)
  add_subdirectory(engine)
endif()
if(SAMN_BUILD_APP)
  if(NOT SAMN_BUILD_VISION)
    message(FATAL_ERROR "The app needs the detection engine: turn SAMN_BUILD_VISION on (OpenCV with contrib).")
  endif()
  add_subdirectory(app)
endif()
if(SAMN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
