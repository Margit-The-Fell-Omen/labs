// Точка входа Qt GUI.

#include <QApplication>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mainwindow.h"

namespace {
void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [OPTIONS]\n"
                 "  Linux USB HID probe GUI (libusb + sysfs + Qt6 Widgets).\n\n"
                 "Options:\n"
                 "  -h, --help         показать эту справку\n"
                 "  -i, --interval MS  период опроса в мс (по умолчанию 1000)\n\n"
                 "Требуются права на usbfs: запустите с sudo или примените 50-hid.rules.\n",
                 prog);
}
}  // namespace

namespace {
// Есть ли окружение графической сессии (X11/Wayland)?
bool hasDisplayEnv() {
    if (std::getenv("DISPLAY") != nullptr && std::getenv("DISPLAY")[0] != '\0')
        return true;
    if (std::getenv("WAYLAND_DISPLAY") != nullptr &&
        std::getenv("WAYLAND_DISPLAY")[0] != '\0')
        return true;
    if (std::getenv("QT_QPA_PLATFORM") != nullptr)
        return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    int intervalMs = 1000;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a == "-i" || a == "--interval") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s: отсутствует значение для %s\n",
                             argv[0], a.c_str());
                return 2;
            }
            intervalMs = std::atoi(argv[++i]);
            if (intervalMs < 50)
                intervalMs = 50;
        } else {
            std::fprintf(stderr, "%s: неизвестный аргумент: %s\n", argv[0], a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (!hasDisplayEnv()) {
        std::fprintf(stderr,
                     "Графическая сессия не обнаружена (DISPLAY/WAYLAND_DISPLAY "
                     "пусты).\n"
                     "Похоже, запуск выполнен с обрезанным окружением (например, "
                     "через sudo).\n"
                     "Для Wayland/Hyprland важно сохранить XDG_RUNTIME_DIR "
                     "(там сокет wayland-N).\n"
                     "Правильный запуск от root:\n"
                     "  ./run-root.sh\n"
                     "или вручную:\n"
                     "  sudo --preserve-env=DISPLAY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,"
                     "XAUTHORITY ./build/usbhidprobe\n"
                     "или укажите платформу, напр. QT_QPA_PLATFORM=offscreen.\n");
        return 0;
    }

    QApplication app(argc, argv);
    app.setApplicationName("usbhidprobe");
    app.setApplicationDisplayName("USB HID probe (libusb + sysfs)");

    MainWindow w(intervalMs);
    w.show();
    return app.exec();
}
