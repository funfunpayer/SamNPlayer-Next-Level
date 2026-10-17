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

# No personal name, old project name or model identifier in the sources.
add_test(NAME identifiers COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_identifiers.cmake)

if(SAMN_BUILD_VISION)
  file(GLOB VISION_TESTS CONFIGURE_DEPENDS vision/*_test.cpp)
  add_executable(vision_tests core/main.cpp ${VISION_TESTS})
  target_link_libraries(vision_tests PRIVATE samn_vision doctest)
  add_test(NAME vision_tests COMMAND vision_tests)

  file(GLOB ENGINE_TESTS CONFIGURE_DEPENDS engine/*_test.cpp)
  add_executable(engine_tests core/main.cpp ${ENGINE_TESTS})
  target_link_libraries(engine_tests PRIVATE samn_engine doctest)
  target_include_directories(engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
  add_test(NAME engine_tests COMMAND engine_tests)
  # Parity tool against 0.7 (run by hand, docs/benchmarks/P3_1_TRACK_PARITY.md).
  add_executable(engine_parity_dump EXCLUDE_FROM_ALL engine/parity_dump.cpp)
  target_link_libraries(engine_parity_dump PRIVATE samn_engine)
endif()

if(SAMN_BUILD_APP)
  add_subdirectory(app)
endif()
