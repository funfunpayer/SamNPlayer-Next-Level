# samn_engine: the detection engine, a built-in plugin (D-140, D-141).
# Plain C++17 on samn_vision (OpenCV), no Qt. Every src/*.cpp belongs to it.
file(GLOB SAMN_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(samn_engine STATIC ${SAMN_ENGINE_SOURCES})
target_include_directories(samn_engine PUBLIC include PRIVATE src)
target_link_libraries(samn_engine PUBLIC samn_vision)
if(MSVC)
  target_compile_options(samn_engine PRIVATE /W4 /utf-8)
else()
  target_compile_options(samn_engine PRIVATE -Wall -Wextra)
endif()
