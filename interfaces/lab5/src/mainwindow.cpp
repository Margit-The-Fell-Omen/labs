#include "mainwindow.h"

#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTime>
#include <QVBoxLayout>

#include <iterator>

#include "usbmonitor.h"

namespace {
// Заголовки колонок таблицы.
const char* kColumns[] = {
    "Bus:Addr", "VID", "PID", "Класс", "Протокол",
    "Endpoint'ы", "HID", "Производитель", "Продукт", "Серийный №",
};

// Палитра Gruvbox (dark). Пиксельные цвета приведены к #RRGGBB.
const char* kGruvboxStyle = R"QSS(
* {
    background-color: #282828;   /* bg0 */
    color: #ebdbb2;              /* fg */
    font-family: "DejaVu Sans Mono", monospace;
}
QMainWindow, QWidget#central, QSplitter {
    background-color: #282828;
}
QLabel#status {
    color: #a89984;              /* fg4 */
    padding: 4px;
}
QTableWidget {
    background-color: #1d2021;   /* bg0_h */
    alternate-background-color: #282828;
    gridline-color: #3c3836;     /* bg3 */
    border: 1px solid #504945;   /* bg4 */
    selection-background-color: #d65d0e; /* orange */
    selection-color: #282828;
}
QHeaderView::section {
    background-color: #32302f;   /* bg1 */
    color: #ebdbb2;
    border: 1px solid #504945;
    padding: 4px;
    font-weight: bold;
}
QTextEdit {
    background-color: #1d2021;
    color: #a89984;
    border: 1px solid #504945;
}
QMessageBox, QMessageBox QLabel {
    background-color: #282828;
    color: #ebdbb2;
}
)QSS";
}  // namespace

MainWindow::MainWindow(int intervalMs, QWidget* parent)
    : QMainWindow(parent), intervalMs_(intervalMs) {
    setupUi();
    setWindowTitle("USB HID — пробник на libusb + sysfs (Qt)");
    resize(1000, 500);

    // Создаём фоновый монитор и переносим его в отдельный поток.
    monitor_ = new UsbMonitor(intervalMs_);
    workerThread_ = new QThread(this);

    // Соединения между рабочим потоком и GUI (queued — автоматически).
    connect(workerThread_, &QThread::started, monitor_, &UsbMonitor::start);
    connect(monitor_, &UsbMonitor::snapshotReady, this, &MainWindow::onSnapshot);
    connect(monitor_, &UsbMonitor::failed, this, &MainWindow::onFailed);
    // После остановки потока удаляем монитор в его потоке.
    connect(workerThread_, &QThread::finished, monitor_, &QObject::deleteLater);

    monitor_->moveToThread(workerThread_);
    workerThread_->setParent(this);
    workerThread_->start();

    statusBar()->showMessage(QString("Опрос каждые %1 мс. Права на usbfs: "
                                     "sudo или 50-hid.rules.").arg(intervalMs_));
}

MainWindow::~MainWindow() {
    // Запрашиваем остановку рабочего потока и ждём его завершения.
    if (workerThread_ && workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(2000);
    }
}

void MainWindow::setupUi() {
    table_ = new QTableWidget(this);
    table_->setColumnCount(static_cast<int>(std::size(kColumns)));
    table_->setHorizontalHeaderLabels([] {
        QStringList l;
        for (const char* c : kColumns) l << QString::fromUtf8(c);
        return l;
    }());
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    log_ = new QTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumHeight(140);

    status_ = new QLabel(this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(table_);
    splitter->addWidget(log_);
    splitter->setStretchFactor(0, 1);

    auto* central = new QWidget(this);
    central->setObjectName("central");
    auto* lay = new QVBoxLayout(central);
    lay->addWidget(splitter, /*stretch=*/1);
    setCentralWidget(central);

    status_->setObjectName("status");

    // Применяем палитру Gruvbox (dark) ко всему окну.
    setStyleSheet(QString::fromUtf8(kGruvboxStyle));
    table_->setAlternatingRowColors(true);
}

void MainWindow::applyGruvboxCell(QTableWidgetItem* item, int col) {
    // Подкраска отдельных элементов в стиле Gruvbox.
    const QColor fg("#ebdbb2");   // основной текст (fg)
    const QColor dim("#a89984");  // приглушённый (fg4)
    const QColor green("#b8bb26"); // яркий зелёный (HID)
    const QColor red("#fb4934");   // яркий красный

    switch (col) {
    case 6:  // колонка HID
        item->setForeground(item->text() == QLatin1String("HID") ? QBrush(green)
                                                                 : QBrush(dim));
        break;
    case 0:  // Bus:Addr
        item->setForeground(QBrush(red));
        break;
    default:
        item->setForeground(QBrush(fg));
        break;
    }
}

void MainWindow::rebuildTable(const devtable::Snapshot& snap) {
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(snap.size()));

    int row = 0;
    for (const auto& kv : snap) {
        const devtable::DevInfo& d = kv.second;

        auto cell = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            item->setToolTip(text);
            applyGruvboxCell(item, col);
            table_->setItem(row, col, item);
        };

        cell(0, QString("%1:%2").arg(d.key.bus).arg(d.key.addr));
        cell(1, QString("0x%1").arg(d.vid, 4, 16, QLatin1Char('0')));
        cell(2, QString("0x%1").arg(d.pid, 4, 16, QLatin1Char('0')));
        cell(3, QString("0x%1").arg(d.devClass, 2, 16, QLatin1Char('0')));
        cell(4, QString::number(d.devProtocol));
        cell(5, QString::number(d.endpointCount));
        cell(6, d.isHid ? QString("HID") : QString("-"));
        cell(7, QString::fromUtf8(d.manufacturer));
        cell(8, QString::fromUtf8(d.product));
        cell(9, QString::fromUtf8(d.serial));
        ++row;
    }
}

void MainWindow::logEvent(const QString& line, const QString& color) {
    // Журнал событий горячего подключения (Gruvbox green/red для + / -).
    const QString ts = QTime::currentTime().toString("hh:mm:ss");
    if (color.isEmpty()) {
        log_->append(QString("[%1] %2").arg(ts, line));
    } else {
        log_->append(QString("<span style=\"color:#a89984\">[%1]</span>"
                             " <span style=\"color:%2\">%3</span>")
                         .arg(ts, color, line));
    }
}

void MainWindow::onSnapshot(const devtable::Snapshot& snap,
                            const devtable::Diff& diff) {
    // Обновляем таблицу и выдаём события подключения/отключения.
    rebuildTable(snap);

    for (const auto& add : diff.added)
        logEvent(QString("(+) Подключено: %1").arg(
                     QString::fromUtf8(add.summaryLine().c_str())),
                 "#b8bb26");  // Gruvbox ярко-зелёный
    for (const auto& rm : diff.removed)
        logEvent(QString("(-) Отключено: %1").arg(
                     QString::fromUtf8(rm.summaryLine().c_str())),
                 "#fb4934");  // Gruvbox ярко-красный

    status_->setText(QString("Устройств на шине: %1").arg(snap.size()));
}

void MainWindow::onFailed(const QString& message) {
    status_->setText(message);
    QMessageBox::warning(this, tr("USB"), message);
}