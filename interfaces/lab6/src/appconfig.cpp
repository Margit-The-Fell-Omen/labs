#include "appconfig.h"

namespace {
bool takeValue(const QStringList& args, int& i, QString& out, QString& error)
{
    if (i + 1 >= args.size()) {
        error = QStringLiteral("для %1 нужно значение").arg(args.at(i));
        return false;
    }
    out = args.at(++i);
    return true;
}
}  // namespace

QString usageText(const QString& program)
{
    return QStringLiteral(
               "Usage: %1 [OPTIONS]\n"
               "  BLE file transfer over a custom GATT protocol (BlueZ D-Bus + Qt6).\n\n"
               "Options:\n"
               "  --role central|peripheral  роль приложения (по умолчанию central)\n"
               "  --adapter hciN             локальный адаптер (по умолчанию первый)\n"
               "  --send FILE                файл для отправки (central)\n"
               "  --out DIR                  каталог сохранения (peripheral)\n"
               "  --peer BD_ADDR             сразу подключиться к устройству (central)\n"
               "  --window BYTES             окно неподтверждённых байт (по умолчанию 65536)\n"
               "  --chunk BYTES              ограничить payload чанка\n"
               "  --corrupt-chunk N          отладка: испортить N-й чанк\n"
               "  --scan-timeout MS          таймаут поиска устройства\n"
               "  --log FILE                 писать журнал в файл\n"
               "  --headless                 без GUI (QT_QPA_PLATFORM=offscreen)\n"
               "  --auto                     сразу начать рекламу/поиск/передачу\n"
               "  --no-reconnect             не переподключаться автоматически\n"
               "  --verbose                  подробный журнал\n"
               "  -h, --help                 показать эту справку\n")
        .arg(program);
}

bool parseArgs(const QStringList& args, AppConfig& cfg, QString& error)
{
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);

        if (a == QStringLiteral("-h") || a == QStringLiteral("--help"))
            continue;
        if (a == QStringLiteral("--headless")) {
            cfg.headless = true;
        } else if (a == QStringLiteral("--auto")) {
            cfg.autoStart = true;
        } else if (a == QStringLiteral("--no-reconnect")) {
            cfg.noReconnect = true;
        } else if (a == QStringLiteral("--verbose")) {
            cfg.verbose = true;
        } else if (a == QStringLiteral("--role")) {
            if (!takeValue(args, i, cfg.role, error))
                return false;
            if (cfg.role != QStringLiteral("central") && cfg.role != QStringLiteral("peripheral")) {
                error = QStringLiteral("--role должен быть central или peripheral");
                return false;
            }
        } else if (a == QStringLiteral("--adapter")) {
            if (!takeValue(args, i, cfg.adapter, error))
                return false;
        } else if (a == QStringLiteral("--send")) {
            if (!takeValue(args, i, cfg.sendFile, error))
                return false;
        } else if (a == QStringLiteral("--out")) {
            if (!takeValue(args, i, cfg.outDir, error))
                return false;
        } else if (a == QStringLiteral("--peer")) {
            if (!takeValue(args, i, cfg.peer, error))
                return false;
        } else if (a == QStringLiteral("--log")) {
            if (!takeValue(args, i, cfg.logFile, error))
                return false;
        } else if (a == QStringLiteral("--window")) {
            QString v;
            if (!takeValue(args, i, v, error))
                return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 4096) {
                error = QStringLiteral("--window: нужно число >= 4096");
                return false;
            }
            cfg.window = n;
        } else if (a == QStringLiteral("--chunk")) {
            QString v;
            if (!takeValue(args, i, v, error))
                return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n <= 0 || n > 4096) {
                error = QStringLiteral("--chunk: нужно число 1..4096");
                return false;
            }
            cfg.chunk = n;
        } else if (a == QStringLiteral("--corrupt-chunk")) {
            QString v;
            if (!takeValue(args, i, v, error))
                return false;
            bool ok = false;
            cfg.corruptChunk = v.toInt(&ok);
            if (!ok || cfg.corruptChunk < 0) {
                error = QStringLiteral("--corrupt-chunk: нужно число >= 0");
                return false;
            }
        } else if (a == QStringLiteral("--scan-timeout")) {
            QString v;
            if (!takeValue(args, i, v, error))
                return false;
            bool ok = false;
            cfg.scanTimeoutMs = v.toInt(&ok);
            if (!ok || cfg.scanTimeoutMs < 1000) {
                error = QStringLiteral("--scan-timeout: нужно число >= 1000");
                return false;
            }
        } else {
            error = QStringLiteral("неизвестный аргумент: %1").arg(a);
            return false;
        }
    }
    return true;
}
