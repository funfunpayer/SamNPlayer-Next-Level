# samn_core: file formats, mapping, filters, beat bar, script search, playback
# logic. Plain C++17, no Qt, so the app, the tests and the Python tools share
# it. Every src/*.cpp belongs to it (new modules need no CMake change).
file(GLOB SAMN_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(samn_core STATIC ${SAMN_CORE_SOURCES})
target_include_directories(samn_core PUBLIC include)
target_link_libraries(samn_core PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(samn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(samn_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(samn_core PRIVATE -Wall -Wextra)
endif()

if(SAMN_BUILD_PYTHON)
  find_package(pybind11 REQUIRED)
  pybind11_add_module(samn_native
    python/samn_native_py.cpp
    python/beatbar_py.cpp python/stroke_filter_py.cpp
    python/beat_strokes_py.cpp python/funscript_py.cpp)
  target_link_libraries(samn_native PRIVATE samn_core)
endif()
