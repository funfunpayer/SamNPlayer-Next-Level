cmake_minimum_required(VERSION 3.16)
project(samn_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# pybind11
find_package(pybind11 REQUIRED)

# nlohmann/json for funscript parse/write (header-only)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Beat bar module
pybind11_add_module(beatbar_native src/beatbar.cpp bindings/beatbar_py.cpp)
target_include_directories(beatbar_native PRIVATE include)

# Stroke filter module
pybind11_add_module(stroke_filter_native src/stroke_filter.cpp bindings/stroke_filter_py.cpp)
target_include_directories(stroke_filter_native PRIVATE include)

# Beat strokes module
pybind11_add_module(beat_strokes_native src/beat_strokes.cpp bindings/beat_strokes_py.cpp)
target_include_directories(beat_strokes_native PRIVATE include)

# Funscript core parse/write + ShiftActions
pybind11_add_module(funscript_native src/funscript.cpp bindings/funscript_py.cpp)
target_include_directories(funscript_native PRIVATE include)
target_link_libraries(funscript_native PRIVATE nlohmann_json::nlohmann_json)

# Install
install(TARGETS beatbar_native stroke_filter_native beat_strokes_native funscript_native DESTINATION .)
