#include "mainwindow.h"

#include "hexdumpview.h"
#include "pcidb.h"

#include <QAbstractItemView>
#include <QColor>
#include <QCursor>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

quint16 le16(const QByteArray &d, int off)
{
    if (off + 1 >= d.size())
        return 0;
    return static_cast<quint16>(static_cast<uchar>(d[off]))
        | (static_cast<quint16>(static_cast<uchar>(d[off + 1])) << 8);
}

quint32 le24(const QByteArray &d, int off)
{
    if (off + 2 >= d.size())
        return 0;
    return static_cast<quint32>(static_cast<uchar>(d[off]))
        | (static_cast<quint32>(static_cast<uchar>(d[off + 1])) << 8)
        | (static_cast<quint32>(static_cast<uchar>(d[off + 2])) << 16);
}

quint32 le32(const QByteArray &d, int off)
{
    if (off + 3 >= d.size())
        return 0;
    return static_cast<quint32>(static_cast<uchar>(d[off]))
        | (static_cast<quint32>(static_cast<uchar>(d[off + 1])) << 8)
        | (static_cast<quint32>(static_cast<uchar>(d[off + 2])) << 16)
        | (static_cast<quint32>(static_cast<uchar>(d[off + 3])) << 24);
}

const RegisterField *findField(int offset)
{
    const QVector<RegisterField> &fields = headerFields();
    for (const RegisterField &f : fields) {
        if (offset >= f.offset && offset < f.offset + f.size)
            return &f;
    }
    return nullptr;
}

void setupTable(QTableWidget *table)
{
    table->setHorizontalHeaderLabels({QStringLiteral("Бит"), QStringLiteral("Описание"),
                                      QStringLiteral("Значение")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
}

QString formatValue(const QByteArray &d, const RegisterField &f)
{
    QString hex;
    quint64 dec = 0;
    if (f.size == 1) {
        const quint8 v = static_cast<uchar>(d[f.offset]);
        hex = QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0'));
        dec = v;
    } else if (f.size == 2) {
        const quint16 v = le16(d, f.offset);
        hex = QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0'));
        dec = v;
    } else if (f.size == 3) {
        const quint32 v = le24(d, f.offset);
        hex = QStringLiteral("%1").arg(v, 6, 16, QLatin1Char('0'));
        dec = v;
    } else {
        const quint32 v = le32(d, f.offset);
        hex = QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0'));
        dec = v;
    }

    if (f.size <= 2)
        return QStringLiteral("%1h (%2)").arg(hex).arg(dec);
    return QStringLiteral("%1h").arg(hex);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    rebuildTree();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Древовидное представление топологии PCI"));

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *rootLayout = new QVBoxLayout(central);

    auto *topBar = new QHBoxLayout();
    m_rescanButton = new QPushButton(QStringLiteral("Сканировать шины"));
    topBar->addWidget(m_rescanButton);
    m_statusLabel = new QLabel();
    m_statusLabel->setWordWrap(true);
    topBar->addWidget(m_statusLabel, 1);
    rootLayout->addLayout(topBar);

    auto *splitter = new QSplitter(Qt::Horizontal);

    m_tree = new QTreeWidget();
    m_tree->setHeaderLabel(QStringLiteral("Шины / устройства / функции"));
    m_tree->setMinimumWidth(320);

    auto *rightPane = new QWidget();
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *infoGroup = new QGroupBox(QStringLiteral("Информация о функции"));
    auto *infoLayout = new QVBoxLayout(infoGroup);
    m_summaryLabel = new QLabel();
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_summaryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_summaryLabel->setWordWrap(true);
    infoLayout->addWidget(m_summaryLabel);
    rightLayout->addWidget(infoGroup);

    auto *hexGroup = new QGroupBox(QStringLiteral("Конфигурационное пространство (hex)"));
    auto *hexLayout = new QVBoxLayout(hexGroup);
    m_hexDump = new HexDumpView();
    m_hexDump->setMinimumHeight(140);
    hexLayout->addWidget(m_hexDump);
    rightLayout->addWidget(hexGroup);

    auto *fieldsGroup = new QGroupBox(QStringLiteral("Регистры конфигурационного заголовка"));
    auto *fieldsLayout = new QVBoxLayout(fieldsGroup);
    m_fieldsTable = new QTableWidget(0, 3);
    m_fieldsTable->setHorizontalHeaderLabels({QStringLiteral("Поле"), QStringLiteral("Смещение"),
                                              QStringLiteral("Значение")});
    m_fieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fieldsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fieldsTable->verticalHeader()->setVisible(false);
    m_fieldsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fieldsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_fieldsTable->setAlternatingRowColors(true);
    fieldsLayout->addWidget(m_fieldsTable);
    rightLayout->addWidget(fieldsGroup);

    auto *commandGroup = new QGroupBox(QStringLiteral("Command (смещение 04h)"));
    auto *commandLayout = new QVBoxLayout(commandGroup);
    m_commandBits = new QTableWidget(0, 3);
    setupTable(m_commandBits);
    commandLayout->addWidget(m_commandBits);
    rightLayout->addWidget(commandGroup);

    auto *statusGroup = new QGroupBox(QStringLiteral("Status (смещение 06h)"));
    auto *statusLayout = new QVBoxLayout(statusGroup);
    m_statusBits = new QTableWidget(0, 3);
    setupTable(m_statusBits);
    statusLayout->addWidget(m_statusBits);
    rightLayout->addWidget(statusGroup);

    rightLayout->addStretch(1);

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(rightPane);
    scroll->setFrameShape(QFrame::NoFrame);

    splitter->addWidget(m_tree);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({360, 900});

    rootLayout->addWidget(splitter, 1);

    connect(m_rescanButton, &QPushButton::clicked, this, &MainWindow::rebuildTree);
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *, QTreeWidgetItem *) { onSelectionChanged(); });
    connect(m_hexDump, &HexDumpView::byteHovered, this, &MainWindow::onByteHovered);

    const QSize avail = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(avail.width() - 40, avail.height() - 40);
}

void MainWindow::rebuildTree()
{
    m_tree->clear();
    m_functions.clear();
    m_visitedBuses.clear();
    m_currentIndex = -1;
    m_error.clear();

    if (m_device) {
        closePciDevice(m_device);
        m_device = nullptr;
    }

    QString err;
    m_device = openPciDevice(err);
    if (!m_device) {
        m_statusLabel->setText(err);
        clearInfoPanel();
        return;
    }

    m_descriptions = pciDeviceDescriptions();

    auto *bus0 = new QTreeWidgetItem(m_tree);
    bus0->setText(0, QStringLiteral("Шина 0"));
    bus0->setData(0, Qt::UserRole, -1);
    populateBus(bus0, 0);

    m_tree->expandAll();

    if (!m_error.isEmpty())
        m_statusLabel->setText(QStringLiteral("Найдено функций: %1. %2").arg(m_functions.size()).arg(m_error));
    else
        m_statusLabel->setText(QStringLiteral("Найдено функций: %1").arg(m_functions.size()));
}

void MainWindow::populateBus(QTreeWidgetItem *busItem, quint32 bus)
{
    if (bus > 0xFF || m_visitedBuses.contains(bus))
        return;
    m_visitedBuses.insert(bus);

    QString err;
    const QVector<PciFunction> funcs = scanPciBus(m_device, bus, m_descriptions, err);
    if (!err.isEmpty())
        m_error = err;

    QMap<quint32, QVector<PciFunction>> byDevice;
    for (const PciFunction &fn : funcs)
        byDevice[fn.device].append(fn);

    for (auto it = byDevice.constBegin(); it != byDevice.constEnd(); ++it) {
        auto *deviceItem = new QTreeWidgetItem(busItem);
        deviceItem->setText(0, QStringLiteral("Устройство %1").arg(it.key()));
        deviceItem->setData(0, Qt::UserRole, -1);

        const QVector<PciFunction> &devFunctions = it.value();
        for (const PciFunction &fn : devFunctions) {
            const QString vname = vendorName(fn.vendorId);
            const QString dname = className(fn.baseClass, fn.subClass, fn.progIf);

            auto *funcItem = new QTreeWidgetItem(deviceItem);
            const int idx = m_functions.size();
            m_functions.append(fn);
            funcItem->setText(0, QStringLiteral("Функция %1 (%2, %3)")
                .arg(fn.function).arg(vname).arg(dname));
            funcItem->setData(0, Qt::UserRole, idx);

            if (fn.isBridge) {
                const quint32 secondary = fn.secondaryBus;
                if (secondary != 0 && secondary != bus && !m_visitedBuses.contains(secondary)) {
                    auto *sub = new QTreeWidgetItem(funcItem);
                    sub->setText(0, QStringLiteral("Шина %1").arg(secondary));
                    sub->setData(0, Qt::UserRole, -1);
                    populateBus(sub, secondary);
                }
            }
        }
    }
}

void MainWindow::onSelectionChanged()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) {
        clearInfoPanel();
        return;
    }

    const int index = item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_functions.size()) {
        clearInfoPanel();
        return;
    }

    showFunction(index);
}

void MainWindow::showFunction(int index)
{
    m_currentIndex = index;
    const PciFunction &fn = m_functions[index];

    const QString classHex = QStringLiteral("%1%2%3h")
        .arg(fn.baseClass, 2, 16, QLatin1Char('0'))
        .arg(fn.subClass, 2, 16, QLatin1Char('0'))
        .arg(fn.progIf, 2, 16, QLatin1Char('0'));

    QString text;
    text += QStringLiteral("<b>Функция:</b> шина %1, устройство %2, функция %3<br>")
        .arg(fn.bus).arg(fn.device).arg(fn.function);
    text += QStringLiteral("<b>Vendor:</b> %1 (%2h)<br>")
        .arg(vendorName(fn.vendorId)).arg(fn.vendorId, 4, 16, QLatin1Char('0'));
    text += QStringLiteral("<b>Device:</b> %1 (%2h)<br>")
        .arg(className(fn.baseClass, fn.subClass, fn.progIf))
        .arg(fn.deviceId, 4, 16, QLatin1Char('0'));
    text += QStringLiteral("<b>Class:</b> %1 (%2)<br>")
        .arg(classHex).arg(className(fn.baseClass, fn.subClass, fn.progIf));
    text += QStringLiteral("<b>Revision:</b> %1h<br>").arg(fn.revision, 2, 16, QLatin1Char('0'));
    text += QStringLiteral("<b>Header type:</b> %1h<br>").arg(fn.headerType, 2, 16, QLatin1Char('0'));
    if (fn.isBridge)
        text += QStringLiteral("<b>Вторичная шина (secondary bus):</b> %1h<br>")
            .arg(fn.secondaryBus, 2, 16, QLatin1Char('0'));
    if (!fn.deviceDescription.isEmpty())
        text += QStringLiteral("<b>Описание:</b> %1<br>").arg(fn.deviceDescription.toHtmlEscaped());

    m_summaryLabel->setText(text);

    m_hexDump->setData(fn.config);
    m_hexDump->setChangedOffsets(QSet<int>());

    populateFields();
    populateBits(m_commandBits, commandBits(), 0x04);
    populateBits(m_statusBits, statusBits(), 0x06);
}

void MainWindow::clearInfoPanel()
{
    m_currentIndex = -1;
    m_summaryLabel->setText(QStringLiteral("Выберите функцию в дереве."));
    m_hexDump->setData(QByteArray());
    m_fieldsTable->setRowCount(0);
    m_commandBits->setRowCount(0);
    m_statusBits->setRowCount(0);
}

void MainWindow::populateFields()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_functions.size())
        return;

    const QByteArray &cfg = m_functions[m_currentIndex].config;
    const QVector<RegisterField> fields = headerFields();
    m_fieldsTable->setRowCount(fields.size());

    for (int row = 0; row < fields.size(); ++row) {
        const RegisterField &f = fields[row];

        auto *nameItem = new QTableWidgetItem(f.name);
        auto *offsetItem = new QTableWidgetItem(QStringLiteral("%1h")
            .arg(f.offset, 2, 16, QLatin1Char('0')));
        auto *valueItem = new QTableWidgetItem(formatValue(cfg, f));

        QString raw;
        for (int i = 0; i < f.size; ++i)
            raw += QStringLiteral("%1h ")
                .arg(static_cast<uchar>(cfg[f.offset + i]), 2, 16, QLatin1Char('0'));
        valueItem->setToolTip(QStringLiteral("Байты: %1").arg(raw.trimmed()));

        m_fieldsTable->setItem(row, 0, nameItem);
        m_fieldsTable->setItem(row, 1, offsetItem);
        m_fieldsTable->setItem(row, 2, valueItem);
    }
}

void MainWindow::populateBits(QTableWidget *table, const QVector<BitField> &bits, int offset)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_functions.size())
        return;

    const QByteArray &cfg = m_functions[m_currentIndex].config;
    const quint16 value = le16(cfg, offset);

    table->setRowCount(bits.size());
    for (int row = 0; row < bits.size(); ++row) {
        const BitField &b = bits[row];
        const bool set = (value & (1u << b.bit)) != 0;

        auto *bitItem = new QTableWidgetItem(QString::number(b.bit));
        bitItem->setTextAlignment(Qt::AlignCenter);
        auto *nameItem = new QTableWidgetItem(b.name);
        auto *valueItem = new QTableWidgetItem(set ? QStringLiteral("1") : QStringLiteral("0"));
        valueItem->setTextAlignment(Qt::AlignCenter);

        table->setItem(row, 0, bitItem);
        table->setItem(row, 1, nameItem);
        table->setItem(row, 2, valueItem);
    }
}

void MainWindow::onByteHovered(int offset)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_functions.size())
        return;

    const QByteArray &cfg = m_functions[m_currentIndex].config;
    if (offset < 0 || offset >= cfg.size())
        return;

    QString tooltip;
    if (offset == 0x04 || offset == 0x05) {
        const quint16 v = le16(cfg, 0x04);
        tooltip = QStringLiteral("<b>Command (04h) = %1h</b>").arg(v, 4, 16, QLatin1Char('0'));
        const QVector<BitField> &bits = commandBits();
        for (const BitField &b : bits) {
            if (b.bit > 10)
                continue;
            tooltip += QStringLiteral("<br>Бит %1: %2 — %3")
                .arg(b.bit).arg((v >> b.bit) & 1).arg(b.name.toHtmlEscaped());
        }
    } else if (offset == 0x06 || offset == 0x07) {
        const quint16 v = le16(cfg, 0x06);
        tooltip = QStringLiteral("<b>Status (06h) = %1h</b>").arg(v, 4, 16, QLatin1Char('0'));
        const QVector<BitField> &bits = statusBits();
        for (const BitField &b : bits) {
            tooltip += QStringLiteral("<br>Бит %1: %2 — %3")
                .arg(b.bit).arg((v >> b.bit) & 1).arg(b.name.toHtmlEscaped());
        }
    } else {
        const uchar byte = static_cast<uchar>(cfg[offset]);
        tooltip = QStringLiteral("<b>Смещение %1h</b><br>%2h (%3)")
            .arg(offset, 2, 16, QLatin1Char('0'))
            .arg(byte, 2, 16, QLatin1Char('0'))
            .arg(byte);
        const RegisterField *field = findField(offset);
        if (field)
            tooltip += QStringLiteral("<br>Поле: %1").arg(field->name.toHtmlEscaped());
    }

    QToolTip::showText(QCursor::pos(), tooltip, m_hexDump);
}