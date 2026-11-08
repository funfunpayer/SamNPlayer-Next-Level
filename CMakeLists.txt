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

# nlohmann_json: a system or vcpkg copy first (no network at configure time),
# the pinned release archive as the fallback. A github.com outage (HTTP 504 on
# the release download) then no longer stops the configure step.
include(FetchContent) # also used by vision/ (ONNX Runtime)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  )
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
