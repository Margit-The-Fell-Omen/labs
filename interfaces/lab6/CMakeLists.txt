cmake_minimum_required(VERSION 3.16)

project(lab6ble
    VERSION 1.0.0
    DESCRIPTION "BLE file transfer over a custom GATT protocol (BlueZ D-Bus + Qt6)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets DBus Test)

# ---------------------------------------------------------------------------
# lab6core: протокол и логика передачи, не зависящие от D-Bus/железа.
# Тестируется в loopback-режиме без Bluetooth-адаптера.
# ---------------------------------------------------------------------------
add_library(lab6core STATIC
    src/protocol/crc32.cpp
    src/protocol/frame.cpp
    src/transfer/ratemeter.cpp
    src/transfer/resumestore.cpp
    src/transfer/sender.cpp
    src/transfer/receiver.cpp
)

target_include_directories(lab6core PUBLIC src)
target_link_libraries(lab6core PUBLIC Qt6::Core)
target_compile_options(lab6core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Приложение: GUI + BlueZ D-Bus.
# ---------------------------------------------------------------------------
add_executable(lab6ble
    src/main.cpp
    src/appconfig.cpp
    src/logging.cpp
    src/gruvbox.cpp
    src/mainwindow.cpp
    src/ble/dbusutil.cpp
    src/ble/gattserver.cpp
    src/ble/central.cpp
)

target_link_libraries(lab6ble PRIVATE
    lab6core
    Qt6::Widgets
    Qt6::DBus
)

target_compile_options(lab6ble PRIVATE -Wall -Wextra -Wpedantic)

# qt.conf рядом с бинарём: Qt находит плагины даже при запуске через sudo,
# где QT_PLUGIN_PATH вырезан из окружения.
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

enable_testing()
add_subdirectory(tests)
