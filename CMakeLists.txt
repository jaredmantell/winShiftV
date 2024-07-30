cmake_minimum_required(VERSION 3.25)
project(ScreenRecorder)

set(CMAKE_CXX_STANDARD 14)

# Set the path to your FFmpeg installation
set(FFMPEG_DIR "C:/Users/Jared/Desktop/ffmpeg-master-latest-win64-gpl-shared")

# Include FFmpeg headers
include_directories(${FFMPEG_DIR}/include)

# Link FFmpeg libraries
link_directories(${FFMPEG_DIR}/lib)

add_executable(ScreenRecorder main.cpp)

# Link against FFmpeg libraries
target_link_libraries(ScreenRecorder
        avcodec
        avformat
        avutil
        swscale
)

# Link against Windows libraries
target_link_libraries(ScreenRecorder
        gdi32
        user32
)

# Copy DLLs to output directory
file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
file(COPY ${FFMPEG_DLLS} DESTINATION ${CMAKE_BINARY_DIR})