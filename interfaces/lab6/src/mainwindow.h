#pragma once

#include <QMainWindow>
#include <QTimer>

#include "appconfig.h"
#include "ble/central.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
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
    void onSend();
    void onCancel();
    void onBrowseFile();
    void onBrowseOut();
    void onToggleAdvertising();
    void onDevicesChanged(const QList<BleDeviceInfo>& devices);
    void onTransportReady(quint16 mtu);
    void onCentralDisconnected();
    void onSenderFinished(bool ok, const QString& message);
    void onReceiverFinished(bool ok, const QString& message);
    void onTick();
    void appendLog(int level, const QString& line);

private:
    void setupUi();
    void wireSignals();
    void logInfo(const QString& text);
    void logError(const QString& text);
    void pumpSender();
    void updateSenderStats();
    void updateReceiverStats();
    void updateButtons();
    void startAdvertising();
    void stopAdvertising();
    QString selectedDevicePath() const;

    AppConfig cfg_;
    Sender* sender_ = nullptr;
    Receiver* receiver_ = nullptr;
    BleCentral* central_ = nullptr;
    GattServer* gattServer_ = nullptr;

    QTabWidget* tabs_ = nullptr;

    QPushButton* scanBtn_ = nullptr;
    QTableWidget* deviceTable_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QCheckBox* autoReconnectBox_ = nullptr;

    QLineEdit* fileEdit_ = nullptr;
    QPushButton* browseFileBtn_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QProgressBar* txProgress_ = nullptr;
    QLabel* txState_ = nullptr;
    QLabel* txStats_ = nullptr;

    QPushButton* advBtn_ = nullptr;
    QLineEdit* outDirEdit_ = nullptr;
    QPushButton* browseOutBtn_ = nullptr;
    QProgressBar* rxProgress_ = nullptr;
    QLabel* rxState_ = nullptr;
    QLabel* rxStats_ = nullptr;

    QPlainTextEdit* log_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer tick_;
    QTimer reconnectTimer_;

    QString peerPath_;
    QString pendingPeerAddress_;
    bool pendingAutoConnect_ = false;
    bool pendingSend_ = false;
    bool autoReconnect_ = true;
};
