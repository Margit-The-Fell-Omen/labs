#include "mainwindow.h"

#include "ble/gattserver.h"
#include "logging.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

#include <QCheckBox>
#include <QSignalBlocker>
#include <QComboBox>
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
#include <QTextEdit>
#include <QTextDocument>
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
    centralReceiver_ = new Receiver(this);
    peripheralSender_ = new Sender(this);
    central_ = new BleCentral(this);
    gattServer_ = new GattServer(receiver_, peripheralSender_, this);

    central_->setAdapter(cfg_.adapter);
    if (!cfg_.outDir.isEmpty()) {
        receiver_->setOutDir(cfg_.outDir);
        centralReceiver_->setOutDir(cfg_.outDir);
    } else {
        centralReceiver_->setOutDir(receiver_->outDir());
    }
    for (Sender* s : {sender_, peripheralSender_}) {
        s->setWindow(cfg_.window);
        if (cfg_.chunk > 0)
            s->setChunkOverride(static_cast<quint16>(cfg_.chunk));
        if (cfg_.corruptChunk >= 0)
            s->setCorruptChunk(cfg_.corruptChunk);
    }

    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(2000);

    setupUi();
    wireSignals();
    tick_.setInterval(500);
    tick_.start();
    applyRole(cfg_.role);
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
    deviceTable_->setAlternatingRowColors(true);
    centralLayout->addWidget(deviceTable_, 1);

    connectBtn_ = new QPushButton(QStringLiteral("Подключиться к выбранному"));
    forgetBtn_ = new QPushButton(QStringLiteral("Забыть устройство"));
    auto* connectRow = new QHBoxLayout;
    connectRow->addWidget(connectBtn_);
    connectRow->addWidget(forgetBtn_);
    connectRow->addStretch();
    centralLayout->addLayout(connectRow);

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

    auto* centralRxBox = new QGroupBox(QStringLiteral("Принять от peripheral"));
    auto* centralRxLayout = new QVBoxLayout(centralRxBox);
    auto* centralOutRow = new QHBoxLayout;
    centralOutDirEdit_ = new QLineEdit(centralReceiver_->outDir());
    centralBrowseOutBtn_ = new QPushButton(QStringLiteral("Выбрать…"));
    centralOutRow->addWidget(new QLabel(QStringLiteral("Сохранять в:")));
    centralOutRow->addWidget(centralOutDirEdit_, 1);
    centralOutRow->addWidget(centralBrowseOutBtn_);
    centralRxLayout->addLayout(centralOutRow);
    centralRxProgress_ = new QProgressBar;
    centralRxProgress_->setRange(0, 1000);
    centralRxLayout->addWidget(centralRxProgress_);
    centralRxState_ = new QLabel(QStringLiteral("ожидание"));
    centralRxStats_ = new QLabel(QStringLiteral("—"));
    centralRxStats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    centralRxLayout->addWidget(centralRxState_);
    centralRxLayout->addWidget(centralRxStats_);
    centralLayout->addWidget(centralRxBox);

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

    auto* pTxBox = new QGroupBox(QStringLiteral("Отправить на central"));
    auto* pTxLayout = new QVBoxLayout(pTxBox);
    auto* pFileRow = new QHBoxLayout;
    pFileEdit_ = new QLineEdit;
    pFileEdit_->setPlaceholderText(QStringLiteral("файл для отправки"));
    pBrowseFileBtn_ = new QPushButton(QStringLiteral("Выбрать…"));
    pFileRow->addWidget(new QLabel(QStringLiteral("Файл:")));
    pFileRow->addWidget(pFileEdit_, 1);
    pFileRow->addWidget(pBrowseFileBtn_);
    pTxLayout->addLayout(pFileRow);
    auto* pSendRow = new QHBoxLayout;
    pSendBtn_ = new QPushButton(QStringLiteral("Отправить"));
    pCancelBtn_ = new QPushButton(QStringLiteral("Отменить"));
    pCancelBtn_->setEnabled(false);
    pSendRow->addWidget(pSendBtn_);
    pSendRow->addWidget(pCancelBtn_);
    pSendRow->addStretch();
    pTxLayout->addLayout(pSendRow);
    pTxProgress_ = new QProgressBar;
    pTxProgress_->setRange(0, 1000);
    pTxLayout->addWidget(pTxProgress_);
    pTxState_ = new QLabel(QStringLiteral("ожидание"));
    pTxStats_ = new QLabel(QStringLiteral("—"));
    pTxStats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pTxLayout->addWidget(pTxState_);
    pTxLayout->addWidget(pTxStats_);
    peripheralLayout->addWidget(pTxBox);
    peripheralLayout->addStretch();

    tabs_ = new QTabWidget;
    tabs_->addTab(centralPage, QStringLiteral("Central — отправка/приём"));
    tabs_->addTab(peripheralPage, QStringLiteral("Peripheral — приём/отправка"));

    roleCombo_ = new QComboBox;
    roleCombo_->addItem(QStringLiteral("Central"), QStringLiteral("central"));
    roleCombo_->addItem(QStringLiteral("Peripheral"), QStringLiteral("peripheral"));
    roleCombo_->setCurrentIndex(cfg_.role == QStringLiteral("peripheral") ? 1 : 0);

    auto* roleRow = new QHBoxLayout;
    roleRow->addWidget(new QLabel(QStringLiteral("Роль этого ноутбука:")));
    roleRow->addWidget(roleCombo_);
    advStatus_ = new QLabel(QStringLiteral("advertising: off"));
    advStatus_->setStyleSheet(QStringLiteral("color:#928374;"));
    roleRow->addWidget(advStatus_);
    roleRow->addWidget(new QLabel(QStringLiteral("(Central подключается; Peripheral рекламирует GATT)")));
    roleRow->addStretch();

    auto* tabsContainer = new QWidget;
    auto* tabsLayout = new QVBoxLayout(tabsContainer);
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsLayout->addLayout(roleRow);
    tabsLayout->addWidget(tabs_);

    // ------------------------------------------------------------------ log
    auto* logBox = new QGroupBox(QStringLiteral("Журнал событий"));
    auto* logLayout = new QVBoxLayout(logBox);
    log_ = new QTextEdit;
    log_->setReadOnly(true);
    log_->document()->setMaximumBlockCount(10000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logLayout->addWidget(log_);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(tabsContainer);
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
    if (pFileEdit_->text().isEmpty())
        pFileEdit_->setText(settings.value(QStringLiteral("lastPeripheralFile")).toString());
}

void MainWindow::wireSignals()
{
    connect(&Logger::instance(), &Logger::message, this, &MainWindow::appendLog);

    connect(scanBtn_, &QPushButton::clicked, this, &MainWindow::onScan);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(forgetBtn_, &QPushButton::clicked, this, &MainWindow::onForget);
    connect(deviceTable_, &QTableWidget::doubleClicked, this, &MainWindow::onConnect);
    connect(deviceTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (central_->isScanning())
            central_->stopScan();
        updateButtons();
    });
    connect(browseFileBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseFile);
    connect(browseOutBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseOut);
    connect(centralBrowseOutBtn_, &QPushButton::clicked, this, &MainWindow::onCentralBrowseOut);
    connect(pBrowseFileBtn_, &QPushButton::clicked, this, &MainWindow::onPeripheralBrowseFile);
    connect(pSendBtn_, &QPushButton::clicked, this, &MainWindow::onPeripheralSend);
    connect(pCancelBtn_, &QPushButton::clicked, this, &MainWindow::onPeripheralCancel);
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(advBtn_, &QPushButton::clicked, this, &MainWindow::onToggleAdvertising);
    connect(roleCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onRoleChanged);
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
        centralReceiver_->handleControl(value);
        pumpSender();
    });
    connect(central_, &BleCentral::dataReceived, this, [this](const QByteArray& value) {
        centralReceiver_->handleChunk(value);
    });
    connect(central_, &BleCentral::chunkWritable, this, &MainWindow::pumpSender);
    connect(centralReceiver_, &Receiver::controlToSend, central_, &BleCentral::sendControl);
    connect(centralReceiver_, &Receiver::progress, this, [this](quint64, quint64) { updateCentralReceiverStats(); });
    connect(centralReceiver_, &Receiver::stateChanged, this, [this](const QString& text) {
        centralRxState_->setText(text);
    });
    connect(centralReceiver_, &Receiver::logMessage, this, &MainWindow::logInfo);
    connect(centralReceiver_, &Receiver::finished, this, &MainWindow::onCentralReceiverFinished);

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

    connect(peripheralSender_, &Sender::progress, this, [this](quint64, quint64) { updatePeripheralSenderStats(); });
    connect(peripheralSender_, &Sender::stateChanged, this, [this](const QString& text) {
        pTxState_->setText(text);
    });
    connect(peripheralSender_, &Sender::logMessage, this, &MainWindow::logInfo);
    connect(peripheralSender_, &Sender::finished, this, &MainWindow::onPeripheralSenderFinished);

    connect(gattServer_, &GattServer::logMessage, this, &MainWindow::logInfo);
    connect(gattServer_, &GattServer::advertisingChanged, this, [this](bool on) {
        advStatus_->setText(on ? QStringLiteral("advertising: ON")
                               : QStringLiteral("advertising: off"));
        advStatus_->setStyleSheet(on ? QStringLiteral("color:#b8bb26;")
                                     : QStringLiteral("color:#928374;"));
        updateButtons();
    });
    connect(gattServer_, &GattServer::clientSeen, this, [this] {
        rxState_->setText(QStringLiteral("central подключился"));
    });
    connect(gattServer_, &GattServer::clientGone, this, [this] {
        rxState_->setText(QStringLiteral("central отключился"));
    });
    connect(gattServer_, &GattServer::mtuChanged, this, [this](quint16 mtu) {
        logInfo(QStringLiteral("peripheral: MTU central = %1").arg(mtu));
        maybeStartPeripheralSend();
        updateButtons();
    });
    connect(gattServer_, &GattServer::dataNotifyChanged, this, [this](bool) {
        maybeStartPeripheralSend();
        updateButtons();
    });
    connect(gattServer_, &GattServer::controlActivity, this, &MainWindow::pumpPeripheralSender);

    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (autoReconnect_ && !peerPath_.isEmpty() && !central_->isConnected())
            central_->connectToDevice(peerPath_);
    });
}

void MainWindow::onRoleChanged(int index)
{
    Q_UNUSED(index);
    const QString role = roleCombo_->currentData().toString();
    cfg_.role = role;
    applyRole(role);
}

void MainWindow::applyRole(const QString& role)
{
    const bool central = role == QStringLiteral("central");

    if (roleCombo_ && roleCombo_->currentData().toString() != role) {
        const QSignalBlocker blocker(roleCombo_);
        roleCombo_->setCurrentIndex(central ? 0 : 1);
    }

    // Роль — это только выбор активной вкладки. Не останавливаем
    // advertising/соединение при переключении: иначе вторая сторона
    // теряет GATT и получает «сервис не найден».
    tabs_->setTabEnabled(0, true);
    tabs_->setTabEnabled(1, true);
    tabs_->setCurrentIndex(central ? 0 : 1);
    statusLabel_->setText(central ? QStringLiteral("роль: Central")
                                  : QStringLiteral("роль: Peripheral"));
    logInfo(central ? QStringLiteral("роль этого ноутбука: Central (подключение к peripheral)")
                    : QStringLiteral("роль этого ноутбука: Peripheral (GATT advertising)"));
    updateButtons();
}

void MainWindow::autoStart()
{
    // Каждый экземпляр поднимает GATT/advertising: тогда Central может
    // подключиться к любому ноутбуку, а не только к «правильной» роли.
    startAdvertising();

    if (cfg_.role == QStringLiteral("peripheral")) {
        tabs_->setCurrentIndex(1);
        if (!cfg_.sendFile.isEmpty())
            pFileEdit_->setText(cfg_.sendFile);
        pendingPeripheralSend_ = cfg_.autoStart && !cfg_.sendFile.isEmpty();
        maybeStartPeripheralSend();
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
    peripheralSender_->abort(QStringLiteral("приложение закрывается"));
    gattServer_->stop();
    central_->disconnectFromDevice();

    QSettings settings;
    settings.setValue(QStringLiteral("lastFile"), fileEdit_->text());
    settings.setValue(QStringLiteral("lastOutDir"), outDirEdit_->text());
    settings.setValue(QStringLiteral("lastPeripheralFile"), pFileEdit_->text());
    settings.setValue(QStringLiteral("lastCentralOutDir"), centralOutDirEdit_->text());
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
            const QColor bg(50, 60, 40);
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
    reconnectTimer_.stop();

    if (central_->isScanning()) {
        pendingAutoConnect_ = false;
        central_->stopScan();
        logInfo(QStringLiteral("поиск остановлен"));
        updateButtons();
        return;
    }

    pendingAutoConnect_ = false;
    // Advertising не трогаем: сканирование и реклама могут работать
    // одновременно, а остановка рекламы ломает GATT для второй стороны.
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

void MainWindow::onForget()
{
    const QString path = selectedDevicePath();
    if (path.isEmpty()) {
        logError(QStringLiteral("выберите устройство для удаления из BlueZ"));
        return;
    }
    central_->stopScan();
    QString err;
    if (!central_->forgetDevice(path, &err)) {
        logError(QStringLiteral("RemoveDevice: %1").arg(err));
        return;
    }
    const int row = deviceTable_->currentRow();
    if (row >= 0)
        deviceTable_->removeRow(row);
    logInfo(QStringLiteral("устройство забыто; теперь снова нажмите «Сканировать»"));
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

void MainWindow::onCentralBrowseOut()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Каталог сохранения для central"), centralOutDirEdit_->text());
    if (!dir.isEmpty()) {
        centralOutDirEdit_->setText(dir);
        centralReceiver_->setOutDir(dir);
    }
}

void MainWindow::onPeripheralBrowseFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Файл для отправки central"),
        QFileInfo(pFileEdit_->text()).absolutePath());
    if (!file.isEmpty()) {
        pFileEdit_->setText(file);
        updateButtons();
    }
}

void MainWindow::onPeripheralSend()
{
    const QString file = pFileEdit_->text();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        logError(QStringLiteral("файл для отправки central не выбран"));
        return;
    }
    pendingPeripheralSend_ = true;
    peripheralWaitLogged_ = false;
    maybeStartPeripheralSend();
    updateButtons();
}

void MainWindow::onPeripheralCancel()
{
    pendingPeripheralSend_ = false;
    peripheralWaitLogged_ = false;
    peripheralSender_->abort(QStringLiteral("отменено пользователем"));
    updateButtons();
}

void MainWindow::maybeStartPeripheralSend()
{
    if (peripheralSender_->isActive()) {
        if (peripheralSender_->state() == Sender::State::WaitingStartAck
            && gattServer_->hasPeerMtu()) {
            peripheralSender_->onTransportReady(gattServer_->peerMtu());
            pumpPeripheralSender();
        }
        return;
    }
    if (!pendingPeripheralSend_)
        return;
    if (receiver_->isActive()) {
        if (!peripheralWaitLogged_)
            logInfo(QStringLiteral("peripheral sender: сначала завершится приём от central"));
        peripheralWaitLogged_ = true;
        return;
    }
    if (!gattServer_->hasPeerMtu()) {
        if (!peripheralWaitLogged_)
            logInfo(QStringLiteral("peripheral sender: MTU ещё неизвестен"));
        peripheralWaitLogged_ = true;
        return;
    }

    const QString file = pFileEdit_->text();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        pendingPeripheralSend_ = false;
        logError(QStringLiteral("файл для peripheral sender не найден"));
        return;
    }

    pendingPeripheralSend_ = false;
    peripheralWaitLogged_ = false;
    QString err;
    if (!peripheralSender_->start(file, &err)) {
        logError(err);
        return;
    }
    peripheralSender_->onTransportReady(gattServer_->peerMtu());
    pumpPeripheralSender();
    updateButtons();
}

void MainWindow::onSend()
{
    const QString file = fileEdit_->text();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        logError(QStringLiteral("файл не выбран или не существует"));
        return;
    }

    if (centralReceiver_->isActive()) {
        logError(QStringLiteral("сначала дождитесь окончания приёма от peripheral"));
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
    centralReceiver_->setMtu(mtu);
    centralReceiver_->setNotifying(true);
    if (!centralOutDirEdit_->text().isEmpty())
        centralReceiver_->setOutDir(centralOutDirEdit_->text());

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
    const bool hadTransfer = sender_->isActive() || centralReceiver_->isActive();
    sender_->onTransportLost();
    centralReceiver_->onTransportLost();
    updateButtons();

    if (cfg_.role == QStringLiteral("central") && autoReconnect_
        && !cfg_.noReconnect && !peerPath_.isEmpty() && hadTransfer) {
        reconnectTimer_.start();
    }
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

void MainWindow::onCentralReceiverFinished(bool ok, const QString& message)
{
    if (ok)
        logInfo(QStringLiteral("central принял: %1").arg(message));
    else
        logError(QStringLiteral("central приём: %1").arg(message));
    statusLabel_->setText(ok ? QStringLiteral("central: файл принят")
                             : QStringLiteral("central: приём с ошибкой"));
    updateButtons();
}

void MainWindow::onPeripheralSenderFinished(bool ok, const QString& message)
{
    if (ok)
        logInfo(QStringLiteral("peripheral отправил: %1").arg(message));
    else
        logError(QStringLiteral("peripheral отправка: %1").arg(message));
    statusLabel_->setText(ok ? QStringLiteral("peripheral: файл отправлен")
                             : QStringLiteral("peripheral: отправка с ошибкой"));
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

void MainWindow::pumpPeripheralSender()
{
    if (!peripheralSender_->isActive())
        return;

    while (peripheralSender_->canSendMore()) {
        const QByteArray frame = peripheralSender_->takeChunk();
        if (frame.isEmpty())
            break;
        gattServer_->sendDataNotification(frame);
    }
    peripheralSender_->pump();
    updatePeripheralSenderStats();
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
    pTxState_->setText(peripheralSender_->stateText());
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

void MainWindow::updateCentralReceiverStats()
{
    const quint64 total = centralReceiver_->totalSize();
    const quint64 done = centralReceiver_->received();
    setProgressValue(centralRxProgress_, done, total);

    const RateMeter& rate = centralReceiver_->rateMeter();
    centralRxStats_->setText(QStringLiteral("%1 / %2  •  %3  •  ETA %4")
                                 .arg(formatBytes(done), formatBytes(total),
                                      formatSpeed(rate.bytesPerSecond()),
                                      formatEta(rate.etaSeconds(total, done))));
}

void MainWindow::updatePeripheralSenderStats()
{
    const quint64 total = peripheralSender_->totalSize();
    const quint64 done = peripheralSender_->ackedOffset();
    setProgressValue(pTxProgress_, done, total);

    const RateMeter& rate = peripheralSender_->rateMeter();
    pTxStats_->setText(QStringLiteral("%1 / %2  •  %3  •  ETA %4  •  chunk %5 B")
                           .arg(formatBytes(done), formatBytes(total),
                                formatSpeed(rate.bytesPerSecond()),
                                formatEta(rate.etaSeconds(total, done)))
                           .arg(peripheralSender_->chunkSize()));
    pTxState_->setText(peripheralSender_->stateText());
}

void MainWindow::updateButtons()
{
    const bool hasSelection = deviceTable_->currentRow() >= 0;
    connectBtn_->setEnabled(hasSelection && !central_->isConnected());
    forgetBtn_->setEnabled(hasSelection);
    scanBtn_->setEnabled(!central_->isConnected());
    scanBtn_->setText(central_->isScanning() ? QStringLiteral("Остановить поиск")
                                             : QStringLiteral("Сканировать"));
    sendBtn_->setEnabled(central_->isReady() && !sender_->isActive());
    cancelBtn_->setEnabled(sender_->isActive());
    pSendBtn_->setEnabled(!pFileEdit_->text().isEmpty() && !peripheralSender_->isActive()
                          && !receiver_->isActive());
    pCancelBtn_->setEnabled(peripheralSender_->isActive());
    advBtn_->setText(gattServer_->isAdvertising() ? QStringLiteral("Остановить рекламу")
                                                  : QStringLiteral("Начать рекламу"));
    advStatus_->setText(gattServer_->isAdvertising() ? QStringLiteral("advertising: ON")
                                                     : QStringLiteral("advertising: off"));
    advStatus_->setStyleSheet(gattServer_->isAdvertising()
                                  ? QStringLiteral("color:#b8bb26;")
                                  : QStringLiteral("color:#928374;"));
    txState_->setText(sender_->stateText());
}

void MainWindow::onTick()
{
    updateSenderStats();
    updateReceiverStats();
    updateCentralReceiverStats();
    updatePeripheralSenderStats();
    pumpPeripheralSender();
    maybeStartPeripheralSend();
    updateButtons();

    if (peripheralSender_->checkStall()) {
        logError(QStringLiteral("peripheral sender: таймаут, отмена"));
        peripheralSender_->abort(QStringLiteral("таймаут ACK"));
    }

    if (sender_->checkStall()) {
        logError(QStringLiteral("передача зависла: state=%1 acked=%2/%3, переподключение")
                     .arg(sender_->stateText())
                     .arg(sender_->ackedOffset())
                     .arg(sender_->totalSize()));
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
    QString color = QStringLiteral("#ebdbb2");
    switch (static_cast<Logger::Level>(level)) {
    case Logger::Level::Debug:
        color = QStringLiteral("#928374");
        break;
    case Logger::Level::Warn:
        color = QStringLiteral("#fabd2f");
        break;
    case Logger::Level::Error:
        color = QStringLiteral("#fb4934");
        break;
    case Logger::Level::Info:
        break;
    }

    log_->append(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                     .arg(color, line.toHtmlEscaped()));
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
