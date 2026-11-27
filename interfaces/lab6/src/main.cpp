#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QSocketNotifier>

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>

#include "appconfig.h"
#include "gruvbox.h"
#include "logging.h"
#include "mainwindow.h"

namespace {

int gSignalFds[2] = {-1, -1};

// Async-signal-safe: только write в self-pipe, без вызовов Qt.
void onUnixSignal(int)
{
    if (gSignalFds[1] >= 0) {
        const char byte = 1;
        const ssize_t ignored = ::write(gSignalFds[1], &byte, 1);
        (void)ignored;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    constexpr const char* kBuildTag = "lab6-2026-11-27-le-pref";

    AppConfig cfg;
    QString err;
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args.append(QString::fromLocal8Bit(argv[i]));

    if (args.contains(QStringLiteral("--version"))) {
        std::fputs(kBuildTag, stdout);
        std::fputc('\n', stdout);
        return 0;
    }

    if (args.contains(QStringLiteral("-h")) || args.contains(QStringLiteral("--help"))) {
        std::fputs(usageText(args.value(0)).toLocal8Bit().constData(), stdout);
        return 0;
    }

    if (!parseArgs(args, cfg, err)) {
        std::fprintf(stderr, "%s\n\n", err.toLocal8Bit().constData());
        std::fputs(usageText(args.value(0)).toLocal8Bit().constData(), stderr);
        return 2;
    }

    // Headless-режим: Qt GUI не нужен, но MainWindow используется как контроллер.
    if (cfg.headless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Один экземпляр: иначе два GATT/advertising-регистрации конфликтуют.
    const QString lockPath = QStringLiteral("/tmp/lab6ble-%1.lock").arg(::getuid());
    const int lockFd = ::open(lockPath.toLocal8Bit().constData(), O_CREAT | O_RDWR, 0600);
    if (lockFd >= 0 && ::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "lab6ble уже запущен (lock %s). Закройте старый экземпляр.\n",
                     lockPath.toLocal8Bit().constData());
        return 1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("lab6"));
    QCoreApplication::setApplicationName(QStringLiteral("lab6ble"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setApplicationDisplayName(QStringLiteral("Lab6 BLE file transfer"));

    applyGruvbox(app);

    if (!cfg.logFile.isEmpty())
        Logger::instance().setLogFile(cfg.logFile);
    Logger::instance().info(QStringLiteral("build: %1").arg(QString::fromLatin1(kBuildTag)));
    if (cfg.verbose)
        Logger::instance().info(QStringLiteral("запуск в режиме %1").arg(cfg.role));

    MainWindow window(cfg);
    if (!cfg.headless)
        window.show();

    // Ctrl+C/SIGTERM: аккуратно выходим из event loop и даём destructor'ам
    // закрыть GATT/advertising/соединение, вместо аварийного завершения.
    QSocketNotifier* signalNotifier = nullptr;
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, gSignalFds) == 0) {
        signalNotifier = new QSocketNotifier(gSignalFds[0], QSocketNotifier::Read);
        QObject::connect(signalNotifier, &QSocketNotifier::activated, &app, [&window] {
            char byte = 0;
            const ssize_t ignored = ::read(gSignalFds[0], &byte, 1);
            (void)ignored;
            Logger::instance().info(
                QStringLiteral("получен сигнал завершения, закрываем приложение"));
            window.close();
            QCoreApplication::quit();
        });

        std::signal(SIGINT, onUnixSignal);
        std::signal(SIGTERM, onUnixSignal);
        std::signal(SIGHUP, onUnixSignal);
    }
    std::signal(SIGPIPE, SIG_IGN);

    // autoStart() может делать синхронные D-Bus-вызовы; сигнал, пришедший
    // во время них, уже будет записан в self-pipe и обработан сразу после.
    window.autoStart();
    const int rc = app.exec();

    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGHUP, SIG_DFL);
    gSignalFds[1] = -1;

    if (signalNotifier) {
        signalNotifier->setEnabled(false);
        delete signalNotifier;
    }
    if (gSignalFds[0] >= 0)
        ::close(gSignalFds[0]);
    if (gSignalFds[1] >= 0)
        ::close(gSignalFds[1]);

    return rc;
}
