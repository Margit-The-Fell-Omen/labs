#pragma once
// Главное окно: таблица USB-устройств + журнал событий.

#include <QMainWindow>

#include "devtable.h"

class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class QLabel;
class QThread;
class UsbMonitor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(int intervalMs, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSnapshot(const devtable::Snapshot& snap, const devtable::Diff& diff);
    void onFailed(const QString& message);

private:
    void setupUi();
    void rebuildTable(const devtable::Snapshot& snap);
    void applyGruvboxCell(QTableWidgetItem* item, int col);
    void logEvent(const QString& line, const QString& color = QString());

    QTableWidget* table_ = nullptr;
    QTextEdit* log_ = nullptr;
    QLabel* status_ = nullptr;
    int intervalMs_ = 1000;

    QThread* workerThread_ = nullptr;
    UsbMonitor* monitor_ = nullptr;
    QString lastError_;
};