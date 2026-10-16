# doctest 2.4.11 (MIT), single header in third_party/ (no download at build).
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${PROJECT_SOURCE_DIR}/third_party/doctest)

file(GLOB CORE_TESTS CONFIGURE_DEPENDS core/*_test.cpp)
add_executable(core_tests core/main.cpp ${CORE_TESTS})
target_link_libraries(core_tests PRIVATE samn_core doctest)
if(MSVC)
  # doctest specialises std::tuple (C5285 on new MSVC); third-party, not ours.
  target_compile_options(core_tests PRIVATE /wd5285)
endif()
add_test(NAME core_tests COMMAND core_tests)

if(SAMN_BUILD_APP)
  add_subdirectory(app)
endif()
