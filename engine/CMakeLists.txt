# samn_engine: the detection engine, a built-in plugin (D-140, D-141).
# Plain C++17 on samn_vision (OpenCV) and samn_core, no Qt. Every src/*.cpp belongs to it.
file(GLOB SAMN_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(samn_engine STATIC ${SAMN_ENGINE_SOURCES})
target_include_directories(samn_engine PUBLIC include PRIVATE src)
target_link_libraries(samn_engine PUBLIC samn_vision samn_core)
if(MSVC)
  target_compile_options(samn_engine PRIVATE /W4 /utf-8)
else()
  target_compile_options(samn_engine PRIVATE -Wall -Wextra)
endif()

# The comparison controller on the command line (P3.8).
add_executable(samn_compare tools/samn_compare.cpp)
target_link_libraries(samn_compare PRIVATE samn_engine)

# The self-checking engine on the command line (docs/P3_SELF_CHECK_ENGINE.md).
add_executable(samn_selfcheck tools/samn_selfcheck.cpp)
target_link_libraries(samn_selfcheck PRIVATE samn_engine)
