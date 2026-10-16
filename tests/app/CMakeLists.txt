find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
# One Qt Test executable per controller (QTEST_MAIN each).
foreach(t player_controller library_controller icon_provider device_transports device_controller)
  add_executable(${t}_test ${t}_test.cpp)
  target_link_libraries(${t}_test PRIVATE samn_app Qt6::Test)
  # fake_servers.h reuses the Buttplug message shapes of the core tests.
  target_include_directories(${t}_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/core)
  add_test(NAME ${t}_test COMMAND ${t}_test)
  set_tests_properties(${t}_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
