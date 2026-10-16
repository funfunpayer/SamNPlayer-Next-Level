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
  foreach(mod beatbar stroke_filter beat_strokes funscript)
    pybind11_add_module(${mod}_native python/${mod}_py.cpp)
    target_link_libraries(${mod}_native PRIVATE samn_core)
  endforeach()
endif()
