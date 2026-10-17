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
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(core)
if(SAMN_BUILD_VISION)
  add_subdirectory(vision)
endif()
if(SAMN_BUILD_APP)
  add_subdirectory(app)
endif()
if(SAMN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
