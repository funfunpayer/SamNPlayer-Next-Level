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

# nlohmann_json: a system or vcpkg copy first (no network at configure time).
# Otherwise a shallow git clone of the pinned tag: GitHub's Releases edge often
# answers 504 in CI, which stopped the configure step before it began.
include(FetchContent) # also used by vision/ (ONNX Runtime)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
  )
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  set(JSON_Install OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()

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
