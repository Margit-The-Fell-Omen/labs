cmake_minimum_required(VERSION 3.16)

project(BatteryDiagnostics VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Quick Gui Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Quick Gui Core)

set(PROJECT_SOURCES
    main.cpp
    batteryinfo.h
    batteryinfo.cpp
    batterygraph.h
    batterygraph.cpp
    qml.qrc
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(battery_diagnostics ${PROJECT_SOURCES})
else()
    add_executable(battery_diagnostics ${PROJECT_SOURCES})
endif()

target_link_libraries(battery_diagnostics PRIVATE
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
)

if(WIN32)
    target_link_libraries(battery_diagnostics PRIVATE gdiplus setupapi uuid)
    set_target_properties(battery_diagnostics PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
