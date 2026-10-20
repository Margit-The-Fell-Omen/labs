cmake_minimum_required(VERSION 3.16)

project(hdssd-analyzer
    VERSION 1.0.0
    DESCRIPTION "HDD vs SSD comparative analyzer (native Win32, no Qt)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hdssd-analyzer WIN32
    src/main.cpp
    src/strutil.cpp
    src/storage.cpp
    src/winapi.cpp
    src/ataregs.cpp
    src/directregs.cpp
    src/nvme.cpp
    src/comparison.cpp
    src/report.cpp
    src/appwindow.cpp
)

target_include_directories(hdssd-analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(hdssd-analyzer PRIVATE UNICODE _UNICODE NOMINMAX)

if(WIN32)
    target_link_libraries(hdssd-analyzer PRIVATE
        shell32
        comctl32
        comdlg32
    )

    # Embed the elevated execution manifest so WMI reliability counters (SMART
    # temperature / power-on-hours / wear) are accessible.
    target_sources(hdssd-analyzer PRIVATE res/app.rc)
    set_source_files_properties(res/app.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/res"
    )
endif()