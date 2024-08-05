cmake_minimum_required(VERSION 3.25)
project(ScreenRecorder)

set(CMAKE_CXX_STANDARD 14)

# Set the path to your FFmpeg installation
set(FFMPEG_DIR "C:/Users/Jared/Desktop/ffmpeg-master-latest-win64-gpl-shared")

# Include FFmpeg headers
include_directories(${FFMPEG_DIR}/include)

# Link FFmpeg libraries
link_directories(${FFMPEG_DIR}/lib)

# Find FFmpeg using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
)

add_executable(ScreenRecorder main.cpp recorder.cpp)

# Link against FFmpeg libraries
target_link_libraries(ScreenRecorder
        ${FFMPEG_LIBRARIES}
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

# Include FFmpeg directories
target_include_directories(ScreenRecorder PRIVATE ${FFMPEG_INCLUDE_DIRS})

# Add FFmpeg compile definitions
target_compile_definitions(ScreenRecorder PRIVATE ${FFMPEG_CFLAGS_OTHER})

# Add manifest file
if(MSVC)
    set(APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
    if(EXISTS ${APP_MANIFEST})
        target_sources(ScreenRecorder PRIVATE ${APP_MANIFEST})
        set_property(TARGET ScreenRecorder PROPERTY LINK_FLAGS "/MANIFEST:NO")
        add_custom_command(
                TARGET ScreenRecorder
                POST_BUILD
                COMMAND mt.exe -manifest \"${APP_MANIFEST}\" -outputresource:\"$<TARGET_FILE:ScreenRecorder>\"\;\#1
                COMMENT "Adding manifest..."
        )
    else()
        message(WARNING "Manifest file not found: ${APP_MANIFEST}")
    endif()
endif()