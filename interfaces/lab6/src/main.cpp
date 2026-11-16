#include <QApplication>
#include <QCoreApplication>
#include <QDebug>

#include <cstdio>

#include "appconfig.h"
#include "logging.h"
#include "mainwindow.h"

int main(int argc, char** argv)
{
    AppConfig cfg;
    QString err;
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args.append(QString::fromLocal8Bit(argv[i]));

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

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("lab6"));
    QCoreApplication::setApplicationName(QStringLiteral("lab6ble"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setApplicationDisplayName(QStringLiteral("Lab6 BLE file transfer"));

    if (!cfg.logFile.isEmpty())
        Logger::instance().setLogFile(cfg.logFile);
    if (cfg.verbose)
        Logger::instance().info(QStringLiteral("запуск в режиме %1").arg(cfg.role));

    MainWindow window(cfg);
    if (!cfg.headless)
        window.show();
    window.autoStart();

    return app.exec();
}
