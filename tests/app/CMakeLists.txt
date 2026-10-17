find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
# One Qt Test executable per controller (QTEST_MAIN each).
foreach(t player_controller library_controller icon_provider device_transports device_controller playback_output training_controller)
  add_executable(${t}_test ${t}_test.cpp)
  target_link_libraries(${t}_test PRIVATE samn_app Qt6::Test)
  # fake_servers.h reuses the Buttplug message shapes of the core tests.
  target_include_directories(${t}_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/core)
  add_test(NAME ${t}_test COMMAND ${t}_test)
  set_tests_properties(${t}_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The Training page's QML, loaded the way the app loads it.
add_executable(training_qml_test training_qml_test.cpp)
target_link_libraries(training_qml_test PRIVATE samn_app samn_appplugin Qt6::Test)
target_include_directories(training_qml_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/core)
add_test(NAME training_qml_test COMMAND training_qml_test)
set_tests_properties(training_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The Inspector's QML, loaded the way the app loads it (static module plugin).
add_executable(inspector_qml_test inspector_qml_test.cpp)
target_link_libraries(inspector_qml_test PRIVATE samn_app samn_appplugin Qt6::Test)
target_include_directories(inspector_qml_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/core)
add_test(NAME inspector_qml_test COMMAND inspector_qml_test)
set_tests_properties(inspector_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(player_page_qml_test player_page_qml_test.cpp)
target_link_libraries(player_page_qml_test PRIVATE samn_app samn_appplugin Qt6::Test)
target_compile_definitions(player_page_qml_test PRIVATE SAMN_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME player_page_qml_test COMMAND player_page_qml_test)
set_tests_properties(player_page_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg")
