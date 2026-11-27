#pragma once

#include <QString>
#include <QStringList>

struct AppConfig {
    QString role = QStringLiteral("central");  // central | peripheral
    QString adapter;                           // hci0, hci1, ...
    QString sendFile;
    QString outDir;
    QString peer;                              // BD_ADDR для auto-connect
    QString logFile;

    bool headless = false;
    bool roleExplicit = false;
    bool autoStart = false;
    bool noReconnect = false;
    bool verbose = false;

    int window = 64 * 1024;
    int chunk = 0;                             // 0 = автоматически по MTU
    int corruptChunk = -1;                     // отладка: испортить N-й чанк
    int scanTimeoutMs = 15000;
};

QString usageText(const QString& program);
bool parseArgs(const QStringList& args, AppConfig& cfg, QString& error);
