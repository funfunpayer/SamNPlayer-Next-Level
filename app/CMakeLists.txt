# The SamNPlayer 2 application: C++ logic (core + controllers) with a Qt Quick
# (QML) interface, animated like the 0.7 Flow look.
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Quick Qml Multimedia Svg Network WebSockets SerialPort Bluetooth)
qt_standard_project_setup()

# Controllers and painted items, shared with tests/app.
qt_add_library(samn_app STATIC)
# The Flow icon set (0.7 flow_icons.js), one SVG per icon.
file(GLOB SAMN_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/icons/*.svg)
qt_add_qml_module(samn_app
  URI SamN
  # Same resource path on Qt 6.4 and 6.5+ (6.5 changed the default).
  RESOURCE_PREFIX /qt/qml
  VERSION 1.0
  SOURCES
    src/theme.h
    src/curve_item.h src/curve_item.cpp
    src/heatmap_item.h src/heatmap_item.cpp
    src/wordmark_item.h src/wordmark_item.cpp
    src/audio_level.h src/audio_level.cpp
    src/ruler_item.h src/ruler_item.cpp
    src/mapping_settings.h src/mapping_settings.cpp
    src/playback_output.h src/playback_output.cpp
    src/player_controller.h src/player_controller.cpp
    src/script_clock.h src/script_clock.cpp
    src/library_controller.h src/library_controller.cpp
    src/icon_provider.h src/icon_provider.cpp
    src/device_controller.h src/device_controller.cpp
    src/training_controller.h src/training_controller.cpp
    src/training_curve_item.h src/training_curve_item.cpp
    src/frame_item.h src/frame_item.cpp
    src/frame_cache.h src/frame_cache.cpp
    src/frame_provider.h src/frame_provider.cpp
    src/generator_controller.h src/generator_controller.cpp
    src/editor_controller.h src/editor_controller.cpp
    src/app_log.h src/app_log.cpp
    src/settings_controller.h src/settings_controller.cpp
    src/update_checker.h src/update_checker.cpp
    src/device_io/waiter.h
    src/device_io/qt_transport.h
    src/device_io/socket_line_transport.h src/device_io/socket_line_transport.cpp
    src/device_io/serial_line_transport.h src/device_io/serial_line_transport.cpp
    src/device_io/websocket_transport.h src/device_io/websocket_transport.cpp
    src/device_io/ble_transport.h src/device_io/ble_transport.cpp
  QML_FILES
    qml/Main.qml
    qml/GlassCard.qml
    qml/TopBar.qml
    qml/PlayButton.qml
    qml/PlayerPage.qml
    qml/GeneratorPage.qml
    qml/TrainingPage.qml
    qml/TrainingRing.qml
    qml/TrainingEditor.qml
    qml/FlowToggle.qml
    qml/SettingRow.qml
    qml/NumField.qml
    qml/FlowButton.qml
    qml/IconButton.qml
    qml/FlowSlider.qml
    qml/OffsetField.qml
    qml/FlowCombo.qml
    qml/FlowTabs.qml
    qml/FlowTextField.qml
    qml/MarkList.qml
    qml/LaneName.qml
    qml/Icon.qml
    qml/FlowStage.qml
    qml/FlowFigure.qml
    qml/VideoTransport.qml
    qml/GeneratorProjectPanel.qml
    qml/SilkLayer.qml
    qml/PositionGauge.qml
    qml/LibraryPanel.qml
    qml/InspectorPanel.qml
    qml/ToolTipText.qml
    qml/SettingsPage.qml
  RESOURCES
    ${SAMN_ICONS}
    qml/assets/bg-top.svg
    qml/assets/bg-bottom.svg
    qml/assets/empty-video.svg
    qml/assets/empty-script.svg
    qml/assets/stage-ribbons.svg
    qml/assets/ambient-grid.svg
    qml/assets/logo.png
    qml/assets/flow-ribbons.svg
    qml/assets/flow-mark.svg
    qml/fonts/Sora.ttf
    qml/fonts/Figtree.ttf
)
# The licence texts for Settings → About (D-141).
qt_add_resources(samn_app "legal" PREFIX /legal BASE ${PROJECT_SOURCE_DIR}
  FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md)
target_include_directories(samn_app PUBLIC src src/device_io)
target_link_libraries(samn_app PUBLIC samn_core samn_engine Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Multimedia Qt6::Svg
  Qt6::Network Qt6::WebSockets Qt6::SerialPort Qt6::Bluetooth)

qt_add_executable(samnplayer WIN32 src/main.cpp)
target_link_libraries(samnplayer PRIVATE samn_app samn_appplugin)
option(SAMN_RELEASE "Release build: the version shows without -dev" OFF)
if(SAMN_RELEASE)
  target_compile_definitions(samnplayer PRIVATE SAMN_VERSION="${PROJECT_VERSION}")
else()
  target_compile_definitions(samnplayer PRIVATE SAMN_VERSION="${PROJECT_VERSION}-dev")
endif()
