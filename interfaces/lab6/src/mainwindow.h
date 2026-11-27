#pragma once

#include <QMainWindow>
#include <QTimer>

#include "appconfig.h"
#include "ble/central.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;
class BleCentral;
class GattServer;
class Receiver;
class Sender;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AppConfig& cfg, QWidget* parent = nullptr);
    ~MainWindow() override;

    void autoStart();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onScan();
    void onConnect();
    void onForget();
    void onSend();
    void onCancel();
    void onBrowseFile();
    void onBrowseOut();
    void onCentralBrowseOut();
    void onPeripheralBrowseFile();
    void onPeripheralSend();
    void onPeripheralCancel();
    void onToggleAdvertising();
    void onRoleChanged(int index);
    void onDevicesChanged(const QList<BleDeviceInfo>& devices);
    void onTransportReady(quint16 mtu);
    void onCentralDisconnected();
    void onSenderFinished(bool ok, const QString& message);
    void onReceiverFinished(bool ok, const QString& message);
    void onCentralReceiverFinished(bool ok, const QString& message);
    void onPeripheralSenderFinished(bool ok, const QString& message);
    void onTick();
    void appendLog(int level, const QString& line);

private:
    void setupUi();
    void applyRole(const QString& role);
    void wireSignals();
    void logInfo(const QString& text);
    void logError(const QString& text);
    void pumpSender();
    void pumpPeripheralSender();
    void maybeStartPeripheralSend();
    void updateSenderStats();
    void updateReceiverStats();
    void updateCentralReceiverStats();
    void updatePeripheralSenderStats();
    void updateButtons();
    void startAdvertising();
    void stopAdvertising();
    QString selectedDevicePath() const;

    AppConfig cfg_;
    Sender* sender_ = nullptr;              // central -> peripheral
    Receiver* receiver_ = nullptr;          // peripheral <- central
    Receiver* centralReceiver_ = nullptr;   // central <- peripheral
    Sender* peripheralSender_ = nullptr;    // peripheral -> central
    BleCentral* central_ = nullptr;
    GattServer* gattServer_ = nullptr;

    QTabWidget* tabs_ = nullptr;
    QComboBox* roleCombo_ = nullptr;

    QPushButton* scanBtn_ = nullptr;
    QTableWidget* deviceTable_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QPushButton* forgetBtn_ = nullptr;
    QCheckBox* autoReconnectBox_ = nullptr;

    // Central: отправка файла на peripheral.
    QLineEdit* fileEdit_ = nullptr;
    QPushButton* browseFileBtn_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QProgressBar* txProgress_ = nullptr;
    QLabel* txState_ = nullptr;
    QLabel* txStats_ = nullptr;

    // Central: приём файла от peripheral.
    QLineEdit* centralOutDirEdit_ = nullptr;
    QPushButton* centralBrowseOutBtn_ = nullptr;
    QProgressBar* centralRxProgress_ = nullptr;
    QLabel* centralRxState_ = nullptr;
    QLabel* centralRxStats_ = nullptr;

    // Peripheral: приём файла от central.
    QPushButton* advBtn_ = nullptr;
    QLineEdit* outDirEdit_ = nullptr;
    QPushButton* browseOutBtn_ = nullptr;
    QProgressBar* rxProgress_ = nullptr;
    QLabel* rxState_ = nullptr;
    QLabel* rxStats_ = nullptr;

    // Peripheral: отправка файла на central.
    QLineEdit* pFileEdit_ = nullptr;
    QPushButton* pBrowseFileBtn_ = nullptr;
    QPushButton* pSendBtn_ = nullptr;
    QPushButton* pCancelBtn_ = nullptr;
    QProgressBar* pTxProgress_ = nullptr;
    QLabel* pTxState_ = nullptr;
    QLabel* pTxStats_ = nullptr;

    QTextEdit* log_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer tick_;
    QTimer reconnectTimer_;

    QString peerPath_;
    QString pendingPeerAddress_;
    bool pendingAutoConnect_ = false;
    bool pendingSend_ = false;
    bool pendingPeripheralSend_ = false;
    bool peripheralWaitLogged_ = false;
    bool autoReconnect_ = true;
};
