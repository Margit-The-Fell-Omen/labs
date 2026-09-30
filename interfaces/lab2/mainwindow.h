#pragma once

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QVector>

#include "pcibackend.h"
#include "registerinfo.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class HexDumpView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void rebuildTree();
    void onSelectionChanged();
    void onByteHovered(int offset);

private:
    void buildUi();
    void populateBus(QTreeWidgetItem *busItem, quint32 bus);
    void showFunction(int index);
    void clearInfoPanel();
    void populateFields();
    void populateBits(QTableWidget *table, const QVector<BitField> &bits, int offset);

    QHash<quint32, QString> m_descriptions;
    QVector<PciFunction> m_functions;
    QSet<quint32> m_visitedBuses;
    PciDeviceHandle m_device = nullptr;
    int m_currentIndex = -1;
    QString m_error;

    QPushButton *m_rescanButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTreeWidget *m_tree = nullptr;

    QLabel *m_summaryLabel = nullptr;
    HexDumpView *m_hexDump = nullptr;
    QTableWidget *m_fieldsTable = nullptr;
    QTableWidget *m_commandBits = nullptr;
    QTableWidget *m_statusBits = nullptr;
};