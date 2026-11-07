cmake_minimum_required(VERSION 3.16)

project(webcam-capture
    VERSION 1.0.0
    DESCRIPTION "Веб-камера — захват через DirectShow (frame capture: OpenCV)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(webcam-app WIN32
    src/main.cpp
    src/strutil.cpp
    src/reportlog.cpp
    src/bmpwrite.cpp
    src/snapframe.cpp
    src/dscamera.cpp
    src/appwindow.cpp
)

target_include_directories(webcam-app PRIVATE src)
target_compile_definitions(webcam-app PRIVATE UNICODE _UNICODE NOMINMAX)

if(WIN32)
    target_link_libraries(webcam-app PRIVATE
        ole32 oleaut32 uuid strmiids winmm comdlg32 shell32 comctl32
        opencv_imgcodecs4100 opencv_imgproc4100 opencv_core4100
    )
    target_sources(webcam-app PRIVATE res/app.rc)
    set_source_files_properties(res/app.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/res"
    )
endif()