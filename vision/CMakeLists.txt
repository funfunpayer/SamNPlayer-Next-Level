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

# ONNX Runtime (MIT) for the detectors (D-155 step 4: NudeNet, later the
# student). OpenCV's dnn cannot read NudeNet on the OpenCV 4.6 of the Linux
# distributions, so the official prebuilt runtime comes from its NuGet
# package (Linux x64 and Windows x64), pinned by hash.
option(SAMN_WITH_ONNXRUNTIME "Run ONNX detectors with ONNX Runtime (downloaded at configure time)" ON)
set(SAMN_ONNXRUNTIME_VERSION 1.22.0)
if(SAMN_WITH_ONNXRUNTIME)
  if(WIN32)
    set(_ort_platform win-x64)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_ort_platform linux-x64)
  else()
    message(FATAL_ERROR "No prebuilt ONNX Runtime for this platform here; configure with -DSAMN_WITH_ONNXRUNTIME=OFF.")
  endif()
  FetchContent_Declare(
    onnxruntime
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${SAMN_ONNXRUNTIME_VERSION}/microsoft.ml.onnxruntime.${SAMN_ONNXRUNTIME_VERSION}.nupkg
    URL_HASH SHA256=d571e63a2329baacb713f441e65ad75284de354db6e1ac435fe4bebbb417986a
    DOWNLOAD_NAME onnxruntime-${SAMN_ONNXRUNTIME_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake-here
  )
  FetchContent_MakeAvailable(onnxruntime)
  set(_ort_native ${onnxruntime_SOURCE_DIR}/runtimes/${_ort_platform}/native)
  add_library(samn_onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(samn_onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/build/native/include)
  if(WIN32)
    set_target_properties(samn_onnxruntime PROPERTIES
      IMPORTED_LOCATION ${_ort_native}/onnxruntime.dll
      IMPORTED_IMPLIB ${_ort_native}/onnxruntime.lib)
  else()
    # The library's soname is libonnxruntime.so.1: a copy under that name is
    # what the loader looks for (the build's rpath points at its folder).
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/onnxruntime)
    file(COPY_FILE ${_ort_native}/libonnxruntime.so ${CMAKE_BINARY_DIR}/onnxruntime/libonnxruntime.so.1 ONLY_IF_DIFFERENT)
    set_target_properties(samn_onnxruntime PROPERTIES
      IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/onnxruntime/libonnxruntime.so.1
      IMPORTED_SONAME libonnxruntime.so.1)
  endif()
  target_link_libraries(samn_vision PUBLIC samn_onnxruntime)
  target_compile_definitions(samn_vision PRIVATE SAMN_HAVE_ONNXRUNTIME=1)
endif()

# Puts ONNX Runtime's DLL next to an executable on Windows, which has no
# rpath (vcpkg already copies OpenCV's).
function(samn_copy_runtime_dlls target)
  if(WIN32 AND SAMN_WITH_ONNXRUNTIME)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:samn_onnxruntime> $<TARGET_FILE_DIR:${target}>)
  endif()
endfunction()
