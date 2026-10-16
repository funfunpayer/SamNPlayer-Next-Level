# The SamNPlayer 2 application: C++ logic (core + controllers) with a Qt Quick
# (QML) interface, animated like the 0.7 Flow look.
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Quick Qml Multimedia Svg)
qt_standard_project_setup()

# Controllers and painted items, shared with tests/app.
qt_add_library(samn_app STATIC)
qt_add_qml_module(samn_app
  URI SamN
  # Same resource path on Qt 6.4 and 6.5+ (6.5 changed the default).
  RESOURCE_PREFIX /qt/qml
  VERSION 1.0
  SOURCES
    src/theme.h
    src/curve_item.h src/curve_item.cpp
    src/heatmap_item.h src/heatmap_item.cpp
    src/audio_level.h src/audio_level.cpp
    src/ruler_item.h src/ruler_item.cpp
    src/player_controller.h src/player_controller.cpp
    src/library_controller.h src/library_controller.cpp
  QML_FILES
    qml/Main.qml
    qml/GlassCard.qml
    qml/TopBar.qml
    qml/ModeDeck.qml
    qml/PlayButton.qml
    qml/PlayerPage.qml
    qml/NotYetPage.qml
    qml/FlowButton.qml
    qml/IconButton.qml
    qml/FlowSlider.qml
    qml/OffsetField.qml
    qml/FlowCombo.qml
  RESOURCES
    qml/assets/bg-top.svg
    qml/assets/bg-bottom.svg
    qml/assets/empty-video.svg
    qml/assets/empty-script.svg
    qml/assets/stage-ribbons.svg
    qml/assets/ambient-grid.svg
    qml/assets/logo.png
    qml/fonts/Sora.ttf
    qml/fonts/Figtree.ttf
)
target_include_directories(samn_app PUBLIC src)
target_link_libraries(samn_app PUBLIC samn_core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Multimedia Qt6::Svg)

qt_add_executable(samnplayer WIN32 src/main.cpp)
target_link_libraries(samnplayer PRIVATE samn_app samn_appplugin)
