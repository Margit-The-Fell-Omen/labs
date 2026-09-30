cmake_minimum_required(VERSION 3.16)

project(PciRegisters VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    main.cpp
    pcibackend.h
    pcibackend.cpp
    pcidb.h
    pcidb.cpp
    registerinfo.h
    registerinfo.cpp
    hexdumpview.h
    hexdumpview.cpp
    mainwindow.h
    mainwindow.cpp
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(pci_registers ${PROJECT_SOURCES})
else()
    add_executable(pci_registers ${PROJECT_SOURCES})
endif()

target_link_libraries(pci_registers PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)

if(WIN32)
    target_link_libraries(pci_registers PRIVATE setupapi)
    set_target_properties(pci_registers PROPERTIES WIN32_EXECUTABLE TRUE)
endif()