cmake_minimum_required(VERSION 3.16)

project(PowerSchemes VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Quick Gui Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Quick Gui Core)

set(PROJECT_SOURCES
    main.cpp
    powerschememanager.h
    powerschememanager.cpp
    batterymonitor.h
    batterymonitor.cpp
    qml.qrc
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(power_schemes ${PROJECT_SOURCES})
else()
    add_executable(power_schemes ${PROJECT_SOURCES})
endif()

target_link_libraries(power_schemes PRIVATE
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
)

if(WIN32)
    target_link_libraries(power_schemes PRIVATE powrprof setupapi uuid)
    set_target_properties(power_schemes PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
