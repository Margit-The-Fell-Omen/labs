#include "mainwindow.h"

#include "ble/gattserver.h"
#include "logging.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QColor>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString formatBytes(quint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024ull * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024ull * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatSpeed(double bytesPerSec)
{
    if (bytesPerSec < 1.0)
        return QStringLiteral("—");
    if (bytesPerSec < 1024.0)
        return QStringLiteral("%1 B/s").arg(bytesPerSec, 0, 'f', 0);
    if (bytesPerSec < 1024.0 * 1024.0)
        return QStringLiteral("%1 KiB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 2);
}

QString formatEta(qint64 seconds)
{
    if (seconds < 0)
        return QStringLiteral("—");
    if (seconds < 60)
        return QStringLiteral("%1 с").arg(seconds);
    if (seconds < 3600) {
        const qint64 m = seconds / 60;
        const qint64 s = seconds % 60;
        return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    }
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

void setProgressValue(QProgressBar* bar, quint64 done, quint64 total)
{
    if (total == 0) {
        bar->setRange(0, 1000);
        bar->setValue(0);
        return;
    }
    bar->setRange(0, 1000);
    const double ratio = static_cast<double>(done) / static_cast<double>(total);
    bar->setValue(static_cast<int>(qBound(0.0, ratio, 1.0) * 1000.0));
}

}  // namespace

MainWindow::MainWindow(const AppConfig& cfg, QWidget* parent)
    : QMainWindow(parent), cfg_(cfg)
{
    Logger::instance().setLogFile(cfg_.logFile);

    sender_ = new Sender(this);
    receiver_ = new Receiver(this);
    central_ = new BleCentral(this);
    gattServer_ = new GattServer(receiver_, this);

    central_->setAdapter(cfg_.adapter);
    if (!cfg_.outDir.isEmpty())
        receiver_->setOutDir(cfg_.outDir);
    sender_->setWindow(cfg_.window);
    if (cfg_.chunk > 0)
        sender_->setChunkOverride(static_cast<quint16>(cfg_.chunk));
    if (cfg_.corruptChunk >= 0)
        sender_->setCorruptChunk(cfg_.corruptChunk);

    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(2000);

    setupUi();
    wireSignals();
    tick_.setInterval(500);
    tick_.start();
    updateButtons();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Lab6 — BLE file transfer over GATT (BlueZ D-Bus)"));
    resize(1040, 760);

    // ---------------------------------------------------------------- central
    auto* centralPage = new QWidget;
    auto* centralLayout = new QVBoxLayout(centralPage);

    auto* centralTop = new QHBoxLayout;
    scanBtn_ = new QPushButton(QStringLiteral("Сканировать"));
    autoReconnectBox_ = new QCheckBox(QStringLiteral("Авто-переподключение"));
    autoReconnectBox_->setChecked(!cfg_.noReconnect);
    centralTop->addWidget(scanBtn_);
    centralTop->addWidget(autoReconnectBox_);
    centralTop->addStretch();
    centralLayout->addLayout(centralTop);

    deviceTable_ = new QTableWidget(0, 4);
    deviceTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Имя"), QStringLiteral("Адрес"), QStringLiteral("RSSI"),
         QStringLiteral("Наш сервис")});
    deviceTable_->horizontalHeader()->setStretchLastSection(true);
    deviceTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    deviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    deviceTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    deviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    centralLayout->addWidget(deviceTable_, 1);

    connectBtn_ = new QPushButton(QStringLiteral("Подключиться к выбранному"));
    centralLayout->addWidget(connectBtn_);

    auto* fileRow = new QHBoxLayout;
    fileEdit_ = new QLineEdit;
    fileEdit_->setPlaceholderText(QStringLiteral("файл для отправки"));
    browseFileBtn_ = new QPushButton(QStringLiteral("Выбрать…"));
    fileRow->addWidget(new QLabel(QStringLiteral("Файл:")));
    fileRow->addWidget(fileEdit_, 1);
    fileRow->addWidget(browseFileBtn_);
    centralLayout->addLayout(fileRow);

    auto* sendRow = new QHBoxLayout;
    sendBtn_ = new QPushButton(QStringLiteral("Отправить"));
    cancelBtn_ = new QPushButton(QStringLiteral("Отменить"));
    cancelBtn_->setEnabled(false);
    sendRow->addWidget(sendBtn_);
    sendRow->addWidget(cancelBtn_);
    sendRow->addStretch();
    centralLayout->addLayout(sendRow);

    txProgress_ = new QProgressBar;
    txProgress_->setRange(0, 1000);
    txProgress_->setTextVisible(true);
    centralLayout->addWidget(txProgress_);
    txState_ = new QLabel(QStringLiteral("ожидание"));
    txStats_ = new QLabel(QStringLiteral("—"));
    txStats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    centralLayout->addWidget(txState_);
    centralLayout->addWidget(txStats_);

    // ------------------------------------------------------------- peripheral
    auto* peripheralPage = new QWidget;
    auto* peripheralLayout = new QVBoxLayout(peripheralPage);

    auto* advRow = new QHBoxLayout;
    advBtn_ = new QPushButton(QStringLiteral("Начать рекламу"));
    advRow->addWidget(advBtn_);
    advRow->addStretch();
    peripheralLayout->addLayout(advRow);

    auto* outRow = new QHBoxLayout;
    outDirEdit_ = new QLineEdit(receiver_->outDir());
    browseOutBtn_ = new QPushButton(QStringLiteral("Выбрать…"));
    outRow->addWidget(new QLabel(QStringLiteral("Сохранять в:")));
    outRow->addWidget(outDirEdit_, 1);
    outRow->addWidget(browseOutBtn_);
    peripheralLayout->addLayout(outRow);

    rxProgress_ = new QProgressBar;
    rxProgress_->setRange(0, 1000);
    peripheralLayout->addWidget(rxProgress_);
    rxState_ = new QLabel(QStringLiteral("ожидание"));
    rxStats_ = new QLabel(QStringLiteral("—"));
    rxStats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    peripheralLayout->addWidget(rxState_);
    peripheralLayout->addWidget(rxStats_);
    peripheralLayout->addStretch();

    tabs_ = new QTabWidget;
    tabs_->addTab(centralPage, QStringLiteral("Central — отправитель"));
    tabs_->addTab(peripheralPage, QStringLiteral("Peripheral — приёмник"));
    tabs_->setCurrentIndex(cfg_.role == QStringLiteral("peripheral") ? 1 : 0);

    // ------------------------------------------------------------------ log
    auto* logBox = new QGroupBox(QStringLiteral("Журнал событий"));
    auto* logLayout = new QVBoxLayout(logBox);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(10000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logLayout->addWidget(log_);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(tabs_);
    splitter->addWidget(logBox);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    statusLabel_ = new QLabel(QStringLiteral("готов"));
    statusBar()->addWidget(statusLabel_, 1);

    // Настройки каталогов.
    QSettings settings;
    if (fileEdit_->text().isEmpty())
        fileEdit_->setText(settings.value(QStringLiteral("lastFile")).toString());
    if (outDirEdit_->text().isEmpty() || !QDir(outDirEdit_->text()).exists()) {
        const QString saved = settings.value(QStringLiteral("lastOutDir")).toString();
        if (!saved.isEmpty() && QDir(saved).exists())
            outDirEdit_->setText(saved);
    }
}

void MainWindow::wireSignals()
{
    connect(&Logger::instance(), &Logger::message, this, &MainWindow::appendLog);

    connect(scanBtn_, &QPushButton::clicked, this, &MainWindow::onScan);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(deviceTable_, &QTableWidget::doubleClicked, this, &MainWindow::onConnect);
    connect(deviceTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (central_->isScanning())
            central_->stopScan();
        updateButtons();
    });
    connect(browseFileBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseFile);
    connect(browseOutBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseOut);
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(advBtn_, &QPushButton::clicked, this, &MainWindow::onToggleAdvertising);
    connect(autoReconnectBox_, &QCheckBox::toggled, this, [this](bool on) { autoReconnect_ = on; });
    connect(&tick_, &QTimer::timeout, this, &MainWindow::onTick);

    connect(central_, &BleCentral::devicesChanged, this, &MainWindow::onDevicesChanged);
    connect(central_, &BleCentral::stateChanged, this, [this](const QString& text) {
        statusLabel_->setText(QStringLiteral("central: %1").arg(text));
    });
    connect(central_, &BleCentral::logMessage, this, &MainWindow::logInfo);
    connect(central_, &BleCentral::errorOccurred, this, &MainWindow::logError);
    connect(central_, &BleCentral::transportReady, this, &MainWindow::onTransportReady);
    connect(central_, &BleCentral::disconnected, this, &MainWindow::onCentralDisconnected);
    connect(central_, &BleCentral::controlReceived, this, [this](const QByteArray& value) {
        sender_->onControl(value);
        // ACK открывает окно; без этого передача могла бы стоять после
        // заполнения окна, пока не придёт следующий chunkWritable.
        pumpSender();
    });
    connect(central_, &BleCentral::chunkWritable, this, &MainWindow::pumpSender);

    connect(sender_, &Sender::controlToSend, central_, &BleCentral::sendControl);
    connect(sender_, &Sender::progress, this, [this](quint64, quint64) { updateSenderStats(); });
    connect(sender_, &Sender::stateChanged, this, [this](const QString& text) {
        txState_->setText(text);
    });
    connect(sender_, &Sender::logMessage, this, &MainWindow::logInfo);
    connect(sender_, &Sender::finished, this, &MainWindow::onSenderFinished);

    connect(receiver_, &Receiver::progress, this, [this](quint64, quint64) { updateReceiverStats(); });
    connect(receiver_, &Receiver::stateChanged, this, [this](const QString& text) {
        rxState_->setText(text);
    });
    connect(receiver_, &Receiver::logMessage, this, &MainWindow::logInfo);
    connect(receiver_, &Receiver::finished, this, &MainWindow::onReceiverFinished);

    connect(gattServer_, &GattServer::logMessage, this, &MainWindow::logInfo);
    connect(gattServer_, &GattServer::advertisingChanged, this, [this](bool) { updateButtons(); });
    connect(gattServer_, &GattServer::clientSeen, this, [this] {
        rxState_->setText(QStringLiteral("central подключился"));
    });
    connect(gattServer_, &GattServer::clientGone, this, [this] {
        rxState_->setText(QStringLiteral("central отключился"));
    });

    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (autoReconnect_ && !peerPath_.isEmpty() && !central_->isConnected())
            central_->connectToDevice(peerPath_);
    });
}

void MainWindow::autoStart()
{
    if (cfg_.role == QStringLiteral("peripheral")) {
        tabs_->setCurrentIndex(1);
        if (cfg_.autoStart)
            startAdvertising();
        return;
    }

    tabs_->setCurrentIndex(0);
    pendingAutoConnect_ = cfg_.autoStart;
    pendingPeerAddress_ = cfg_.peer;
    pendingSend_ = cfg_.autoStart && !cfg_.sendFile.isEmpty();

    if (!cfg_.autoStart)
        return;

    if (!cfg_.peer.isEmpty() && cfg_.peer.startsWith(QLatin1Char('/'))) {
        peerPath_ = cfg_.peer;
        pendingAutoConnect_ = false;
        central_->connectToDevice(peerPath_);
    } else {
        QTimer::singleShot(cfg_.scanTimeoutMs, this, [this] {
            if (pendingAutoConnect_) {
                pendingAutoConnect_ = false;
                logError(QStringLiteral("устройство не найдено за %1 мс").arg(cfg_.scanTimeoutMs));
            }
        });
        central_->startScan();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    tick_.stop();
    reconnectTimer_.stop();
    sender_->abort(QStringLiteral("приложение закрывается"));
    gattServer_->stop();
    central_->disconnectFromDevice();

    QSettings settings;
    settings.setValue(QStringLiteral("lastFile"), fileEdit_->text());
    settings.setValue(QStringLiteral("lastOutDir"), outDirEdit_->text());
    event->accept();
}

QString MainWindow::selectedDevicePath() const
{
    const int row = deviceTable_->currentRow();
    if (row < 0)
        return {};
    QTableWidgetItem* item = deviceTable_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void MainWindow::onDevicesChanged(const QList<BleDeviceInfo>& devices)
{
    deviceTable_->setRowCount(devices.size());
    for (int row = 0; row < devices.size(); ++row) {
        const BleDeviceInfo& d = devices.at(row);
        auto* name = new QTableWidgetItem(d.name);
        name->setData(Qt::UserRole, d.path);
        auto* addr = new QTableWidgetItem(d.address);
        auto* rssi = new QTableWidgetItem(QString::number(d.rssi));
        auto* service = new QTableWidgetItem(d.hasService ? QStringLiteral("да") : QStringLiteral("нет"));

        if (d.hasService) {
            const QColor bg(210, 245, 210);
            name->setBackground(bg);
            addr->setBackground(bg);
            rssi->setBackground(bg);
            service->setBackground(bg);
        }

        deviceTable_->setItem(row, 0, name);
        deviceTable_->setItem(row, 1, addr);
        deviceTable_->setItem(row, 2, rssi);
        deviceTable_->setItem(row, 3, service);
    }

    if (pendingAutoConnect_) {
        const BleDeviceInfo* chosen = nullptr;
        for (const BleDeviceInfo& d : devices) {
            if (!pendingPeerAddress_.isEmpty()
                && (d.address.compare(pendingPeerAddress_, Qt::CaseInsensitive) == 0
                    || d.path == pendingPeerAddress_)) {
                chosen = &d;
                break;
            }
            if (pendingPeerAddress_.isEmpty()
                && (d.hasService || d.name.contains(QStringLiteral("lab6"), Qt::CaseInsensitive))) {
                chosen = &d;
                break;
            }
        }
        if (chosen) {
            pendingAutoConnect_ = false;
            peerPath_ = chosen->path;
            central_->stopScan();
            central_->connectToDevice(peerPath_);
            logInfo(QStringLiteral("авто-подключение к %1 (%2)").arg(chosen->name, chosen->address));
        }
    }

    updateButtons();
}

void MainWindow::onScan()
{
    pendingAutoConnect_ = false;
    reconnectTimer_.stop();
    if (gattServer_->isAdvertising())
        stopAdvertising();
    central_->startScan();
    updateButtons();
}

void MainWindow::onConnect()
{
    pendingAutoConnect_ = false;
    const QString path = selectedDevicePath();
    if (path.isEmpty()) {
        logError(QStringLiteral("выберите устройство в таблице"));
        return;
    }
    peerPath_ = path;
    central_->connectToDevice(path);
    updateButtons();
}

void MainWindow::onBrowseFile()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Файл для отправки"),
                                                       QFileInfo(fileEdit_->text()).absolutePath());
    if (!file.isEmpty())
        fileEdit_->setText(file);
}

void MainWindow::onBrowseOut()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Каталог сохранения"),
                                                           outDirEdit_->text());
    if (!dir.isEmpty()) {
        outDirEdit_->setText(dir);
        receiver_->setOutDir(dir);
    }
}

void MainWindow::onSend()
{
    const QString file = fileEdit_->text();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        logError(QStringLiteral("файл не выбран или не существует"));
        return;
    }

    if (!central_->isReady()) {
        pendingSend_ = true;
        logInfo(QStringLiteral("ждём готовности BLE, файл: %1").arg(file));
        return;
    }

    pendingSend_ = false;
    sender_->setWindow(static_cast<quint64>(cfg_.window));
    if (cfg_.chunk > 0)
        sender_->setChunkOverride(static_cast<quint16>(cfg_.chunk));
    if (cfg_.corruptChunk >= 0)
        sender_->setCorruptChunk(cfg_.corruptChunk);

    QString err;
    if (!sender_->start(file, &err)) {
        logError(err);
        return;
    }
    sender_->onTransportReady(central_->mtu());
    pumpSender();
    updateButtons();
}

void MainWindow::onCancel()
{
    pendingSend_ = false;
    sender_->abort(QStringLiteral("отменено пользователем"));
    updateButtons();
}

void MainWindow::onTransportReady(quint16 mtu)
{
    reconnectTimer_.stop();
    logInfo(QStringLiteral("BLE-транспорт готов, MTU=%1").arg(mtu));

    if (pendingSend_ && !sender_->isActive()) {
        pendingSend_ = false;
        onSend();
        return;
    }

    if (sender_->isActive()) {
        sender_->onTransportReady(mtu);
        pumpSender();
    }
    updateButtons();
}

void MainWindow::onCentralDisconnected()
{
    logInfo(QStringLiteral("BLE-соединение потеряно"));
    sender_->onTransportLost();
    updateButtons();

    if (autoReconnect_ && !cfg_.noReconnect && !peerPath_.isEmpty() && sender_->isActive())
        reconnectTimer_.start();
}

void MainWindow::startAdvertising()
{
    central_->stopScan();
    receiver_->setOutDir(outDirEdit_->text());
    gattServer_->setAdapter(cfg_.adapter);

    QString err;
    if (!gattServer_->start(&err)) {
        logError(QStringLiteral("не удалось запустить рекламу: %1").arg(err));
    } else {
        logInfo(QStringLiteral("запуск рекламы, ждём central"));
    }
    updateButtons();
}

void MainWindow::stopAdvertising()
{
    gattServer_->stop();
    logInfo(QStringLiteral("реклама остановлена"));
    updateButtons();
}

void MainWindow::onToggleAdvertising()
{
    if (gattServer_->isAdvertising())
        stopAdvertising();
    else
        startAdvertising();
}

void MainWindow::onSenderFinished(bool ok, const QString& message)
{
    if (ok)
        logInfo(QStringLiteral("отправитель: %1").arg(message));
    else
        logError(QStringLiteral("отправитель: %1").arg(message));
    statusLabel_->setText(ok ? QStringLiteral("передача завершена")
                             : QStringLiteral("передача завершена с ошибкой"));
    updateButtons();
}

void MainWindow::onReceiverFinished(bool ok, const QString& message)
{
    if (ok)
        logInfo(QStringLiteral("приёмник: %1").arg(message));
    else
        logError(QStringLiteral("приёмник: %1").arg(message));
    statusLabel_->setText(ok ? QStringLiteral("файл принят")
                             : QStringLiteral("приём завершён с ошибкой"));
    updateButtons();
}

void MainWindow::pumpSender()
{
    if (!central_->isReady() || !sender_->isActive())
        return;

    while (sender_->canSendMore() && central_->canAcceptChunk()) {
        const QByteArray frame = sender_->takeChunk();
        if (frame.isEmpty())
            break;
        if (!central_->sendChunk(frame))
            break;
    }
    sender_->pump();
    updateSenderStats();
}

void MainWindow::updateSenderStats()
{
    const quint64 total = sender_->totalSize();
    const quint64 done = sender_->ackedOffset();
    setProgressValue(txProgress_, done, total);

    const RateMeter& rate = sender_->rateMeter();
    txStats_->setText(QStringLiteral("%1 / %2  •  %3  •  ETA %4  •  chunk %5 B")
                          .arg(formatBytes(done), formatBytes(total),
                               formatSpeed(rate.bytesPerSecond()),
                               formatEta(rate.etaSeconds(total, done)))
                          .arg(sender_->chunkSize()));
    txState_->setText(sender_->stateText());
}

void MainWindow::updateReceiverStats()
{
    const quint64 total = receiver_->totalSize();
    const quint64 done = receiver_->received();
    setProgressValue(rxProgress_, done, total);

    const RateMeter& rate = receiver_->rateMeter();
    rxStats_->setText(QStringLiteral("%1 / %2  •  %3  •  ETA %4")
                          .arg(formatBytes(done), formatBytes(total),
                               formatSpeed(rate.bytesPerSecond()),
                               formatEta(rate.etaSeconds(total, done))));
}

void MainWindow::updateButtons()
{
    const bool hasSelection = deviceTable_->currentRow() >= 0;
    connectBtn_->setEnabled(hasSelection && !central_->isConnected());
    scanBtn_->setEnabled(!central_->isConnected());
    sendBtn_->setEnabled(central_->isReady() && !sender_->isActive());
    cancelBtn_->setEnabled(sender_->isActive());
    advBtn_->setText(gattServer_->isAdvertising() ? QStringLiteral("Остановить рекламу")
                                                  : QStringLiteral("Начать рекламу"));
    txState_->setText(sender_->stateText());
}

void MainWindow::onTick()
{
    updateSenderStats();
    updateReceiverStats();
    updateButtons();

    if (sender_->checkStall()) {
        logError(QStringLiteral("передача зависла: переподключение"));
        if (autoReconnect_ && !peerPath_.isEmpty()) {
            if (central_->isConnected())
                central_->disconnectFromDevice();
            else
                central_->connectToDevice(peerPath_);
        }
    }
}

void MainWindow::appendLog(int level, const QString& line)
{
    Q_UNUSED(level);
    log_->appendPlainText(line);
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void MainWindow::logInfo(const QString& text)
{
    Logger::instance().info(text);
}

void MainWindow::logError(const QString& text)
{
    Logger::instance().error(text);
}
