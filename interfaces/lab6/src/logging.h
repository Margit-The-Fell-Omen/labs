#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QTextStream>

class Logger : public QObject {
    Q_OBJECT
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger& instance();

    void setLogFile(const QString& path);
    void log(Level level, const QString& text);

    void debug(const QString& text) { log(Level::Debug, text); }
    void info(const QString& text) { log(Level::Info, text); }
    void warn(const QString& text) { log(Level::Warn, text); }
    void error(const QString& text) { log(Level::Error, text); }

signals:
    void message(int level, const QString& text);

private:
    Logger() = default;
    QFile file_;
    QTextStream stream_;
};
