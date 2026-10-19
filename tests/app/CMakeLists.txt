find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
# One Qt Test executable per controller (QTEST_MAIN each).
foreach(t player_controller library_controller icon_provider device_transports device_controller playback_output training_controller settings_controller update_checker)
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
target_compile_definitions(training_qml_test PRIVATE SAMN_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME training_qml_test COMMAND training_qml_test)
set_tests_properties(training_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg")

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

# The shell (Main.qml) and the bundled vector assets.
add_executable(shell_qml_test shell_qml_test.cpp)
target_link_libraries(shell_qml_test PRIVATE samn_app samn_appplugin Qt6::Test)
target_compile_definitions(shell_qml_test PRIVATE SAMN_APP_QML_DIR="${CMAKE_SOURCE_DIR}/app/qml")
add_test(NAME shell_qml_test COMMAND shell_qml_test)
set_tests_properties(shell_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg")

# The Generator mode on generated test clips (tests/engine/synthetic_clips.h).
add_executable(generator_controller_test generator_controller_test.cpp)
target_link_libraries(generator_controller_test PRIVATE samn_app Qt6::Test)
target_include_directories(generator_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/engine)
add_test(NAME generator_controller_test COMMAND generator_controller_test)
set_tests_properties(generator_controller_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The AI Train label editor's controller (D-155).
add_executable(label_controller_test label_controller_test.cpp)
target_link_libraries(label_controller_test PRIVATE samn_app Qt6::Test)
target_include_directories(label_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/engine)
add_test(NAME label_controller_test COMMAND label_controller_test)
set_tests_properties(label_controller_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The Generator page's QML, loaded the way the app loads it.
add_executable(generator_qml_test generator_qml_test.cpp)
target_link_libraries(generator_qml_test PRIVATE samn_app samn_appplugin Qt6::Test)
target_include_directories(generator_qml_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/engine)
add_test(NAME generator_qml_test COMMAND generator_qml_test)
set_tests_properties(generator_qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg")

add_executable(script_clock_test script_clock_test.cpp)
target_link_libraries(script_clock_test PRIVATE samn_app Qt6::Test)
add_test(NAME script_clock_test COMMAND script_clock_test)

# The Editor inside the Generator mode (D-137, D-144).
add_executable(editor_controller_test editor_controller_test.cpp)
target_link_libraries(editor_controller_test PRIVATE samn_app Qt6::Test)
target_include_directories(editor_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/engine)
add_test(NAME editor_controller_test COMMAND editor_controller_test)
set_tests_properties(editor_controller_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Video frames for the Editor: cache with a hard limit, async provider (D-142).
add_executable(frame_cache_test frame_cache_test.cpp)
target_link_libraries(frame_cache_test PRIVATE samn_app Qt6::Test)
target_include_directories(frame_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/tests/engine)
add_test(NAME frame_cache_test COMMAND frame_cache_test)
set_tests_properties(frame_cache_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Every test above writes its output unbuffered (see unbuffered_stdout.cpp).
get_property(_samn_app_tests DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _samn_app_tests)
  get_target_property(_type ${_t} TYPE)
  if(_type STREQUAL "EXECUTABLE")
    target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unbuffered_stdout.cpp)
  endif()
endforeach()
