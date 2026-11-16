#include "logging.h"

#include <QDateTime>
#include <cstdio>

namespace {
const char* levelName(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Debug:
        return "DBG";
    case Logger::Level::Info:
        return "INF";
    case Logger::Level::Warn:
        return "WRN";
    case Logger::Level::Error:
        return "ERR";
    }
    return "???";
}
}  // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogFile(const QString& path)
{
    if (path.isEmpty())
        return;
    if (file_.isOpen())
        file_.close();
    file_.setFileName(path);
    if (file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        stream_.setDevice(&file_);
    } else {
        std::fprintf(stderr, "не удалось открыть журнал %s: %s\n",
                     qPrintable(path), qPrintable(file_.errorString()));
    }
}

void Logger::log(Level level, const QString& text)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] %2 %3")
                             .arg(stamp, QString::fromLatin1(levelName(level)), text);

    if (file_.isOpen()) {
        stream_ << line << '\n';
        stream_.flush();
    }

    if (level != Level::Debug) {
        std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
        std::fflush(stderr);
    }

    emit message(static_cast<int>(level), line);
}
