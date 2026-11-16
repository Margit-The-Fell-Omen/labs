cmake_minimum_required(VERSION 3.16)

project(usbhidprobe
    VERSION 1.0.0
    DESCRIPTION "USB HID устройств — GUI-листинг и мониторинг (libusb + sysfs + Qt6)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# libusb через pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# libudev (мониторинг горячего подключения через netlink-события ядра).
pkg_check_modules(UDEV REQUIRED libudev)

# Qt6 Widgets (GUI).
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(usbhidprobe
    src/main.cpp
    src/mainwindow.cpp
    src/usbmonitor.cpp
    src/usbprobe.cpp
    src/sysfs.cpp
    src/devtable.cpp
)

target_include_directories(usbhidprobe PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
    src
)

target_link_directories(usbhidprobe PRIVATE
    ${LIBUSB_LIBRARY_DIRS}
    ${UDEV_LIBRARY_DIRS}
)

target_link_libraries(usbhidprobe PRIVATE
    Qt6::Widgets
    ${LIBUSB_LIBRARIES}
    ${UDEV_LIBRARIES}
)

target_compile_options(usbhidprobe PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Создаём qt.conf рядом с исполняемым файлом, чтобы приложение находило
# плагины Qt (xcb, стили) вне зависимости от переменной QT_PLUGIN_PATH.
# Это критично при запуске через sudo/env -i, где переменные окружения
# обрезаются; иначе Qt падает с abort() ("could not load the Qt platform
# plugin 'xcb'").
# ---------------------------------------------------------------------------
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
if(NOT _qmake)
    get_target_property(_qmake Qt6::qmake LOCATION)
endif()
execute_process(COMMAND "${_qmake}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE _qt_plugins_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_qt_plugins_dir)
    set(_qt_conf "[Paths]\nPlugins=${_qt_plugins_dir}\n")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" CONTENT "${_qt_conf}")
    message(STATUS "qt.conf: Plugins=${_qt_plugins_dir}")
else()
    message(WARNING "Не удалось определить QT_INSTALL_PLUGINS: qt.conf не создан")
endif()