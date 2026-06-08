cmake_minimum_required(VERSION 3.10)
project(rs_sender)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0)
link_directories(${GST_LIBRARY_DIRS})

# RTSP SERVER BASED ON CAMERA + SAVE 3D INFO + SEND IMU DATA + SEND TIMESTAMP
add_executable(
    rs_sender
    src/main.cpp
    src/comms_common.cpp
    src/app_config.cpp
    src/app_support.cpp
    src/client_transport.cpp
    src/video_streamer.cpp
    src/realsense_capture.cpp
    src/orange_cube_controller.cpp
)
target_include_directories(rs_sender PRIVATE ${GST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
target_include_directories(rs_sender PRIVATE ${CMAKE_SOURCE_DIR}/third_party/mavlink)
target_compile_options(rs_sender PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(rs_sender PRIVATE ${GST_LIBRARIES} realsense2 Threads::Threads)

target_compile_definitions(rs_sender PRIVATE USE_JETSON_HW)
