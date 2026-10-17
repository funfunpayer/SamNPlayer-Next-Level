# samn_vision: video frames and tracking for the Generator (P3, D-133).
# Plain C++17 on OpenCV (core, imgproc, videoio, video, tracking from
# contrib, dnn), no Qt. Every src/*.cpp belongs to it.
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc videoio video tracking dnn)
file(GLOB SAMN_VISION_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(samn_vision STATIC ${SAMN_VISION_SOURCES})
target_include_directories(samn_vision PUBLIC include)
target_link_libraries(samn_vision PUBLIC ${OpenCV_LIBS})
target_include_directories(samn_vision SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(samn_vision PRIVATE SAMN_OPENCV_VERSION="${OpenCV_VERSION}")
if(MSVC)
  target_compile_options(samn_vision PRIVATE /W4 /utf-8)
else()
  target_compile_options(samn_vision PRIVATE -Wall -Wextra)
endif()
