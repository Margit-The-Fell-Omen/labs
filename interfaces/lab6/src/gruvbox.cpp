#include "gruvbox.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

void applyGruvbox(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, QColor(QStringLiteral("#282828")));
    p.setColor(QPalette::WindowText, QColor(QStringLiteral("#ebdbb2")));
    p.setColor(QPalette::Base, QColor(QStringLiteral("#1d2021")));
    p.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#32302f")));
    p.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#3c3836")));
    p.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#ebdbb2")));
    p.setColor(QPalette::Text, QColor(QStringLiteral("#ebdbb2")));
    p.setColor(QPalette::Button, QColor(QStringLiteral("#3c3836")));
    p.setColor(QPalette::ButtonText, QColor(QStringLiteral("#ebdbb2")));
    p.setColor(QPalette::BrightText, QColor(QStringLiteral("#fb4934")));
    p.setColor(QPalette::Link, QColor(QStringLiteral("#83a598")));
    p.setColor(QPalette::Highlight, QColor(QStringLiteral("#458588")));
    p.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#fbf1c7")));
    p.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#928374")));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#7c6f64")));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#7c6f64")));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#7c6f64")));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(QStringLiteral("#504945")));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(QStringLiteral("#928374")));
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background-color: #282828;
            color: #ebdbb2;
        }
        QLabel {
            color: #ebdbb2;
        }
        QLabel:disabled {
            color: #7c6f64;
        }
        QTabWidget::pane {
            border: 1px solid #504945;
            background: #282828;
        }
        QTabBar::tab {
            background: #3c3836;
            color: #ebdbb2;
            border: 1px solid #504945;
            border-bottom: none;
            padding: 6px 14px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #504945;
            color: #fabd2f;
        }
        QTabBar::tab:hover {
            background: #504945;
        }
        QPushButton {
            background-color: #3c3836;
            color: #ebdbb2;
            border: 1px solid #504945;
            border-radius: 4px;
            padding: 5px 12px;
        }
        QPushButton:hover {
            background-color: #504945;
            border-color: #fabd2f;
        }
        QPushButton:pressed {
            background-color: #282828;
            border-color: #fabd2f;
        }
        QPushButton:focus {
            border-color: #fabd2f;
        }
        QComboBox {
            background-color: #3c3836;
            color: #ebdbb2;
            border: 1px solid #504945;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QComboBox:hover, QComboBox:focus {
            border-color: #fabd2f;
        }
        QComboBox QAbstractItemView {
            background: #1d2021;
            color: #ebdbb2;
            selection-background-color: #458588;
            selection-color: #fbf1c7;
            border: 1px solid #504945;
        }
        QPushButton:disabled {
            background-color: #32302f;
            color: #7c6f64;
            border-color: #3c3836;
        }
        QLineEdit, QTextEdit, QPlainTextEdit {
            background-color: #1d2021;
            color: #ebdbb2;
            border: 1px solid #504945;
            border-radius: 3px;
            padding: 3px;
            selection-background-color: #458588;
            selection-color: #fbf1c7;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
            border-color: #fabd2f;
        }
        QTableWidget {
            background-color: #1d2021;
            alternate-background-color: #32302f;
            color: #ebdbb2;
            gridline-color: #504945;
            border: 1px solid #504945;
            selection-background-color: #458588;
            selection-color: #fbf1c7;
        }
        QTableWidget::item {
            padding: 3px;
        }
        QTableWidget::item:selected {
            background-color: #458588;
            color: #fbf1c7;
        }
        QHeaderView::section {
            background-color: #3c3836;
            color: #fabd2f;
            border: 1px solid #504945;
            padding: 5px;
        }
        QTableCornerButton::section {
            background-color: #3c3836;
            border: 1px solid #504945;
        }
        QProgressBar {
            background-color: #1d2021;
            color: #ebdbb2;
            border: 1px solid #504945;
            border-radius: 3px;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #b8bb26;
            border-radius: 2px;
        }
        QGroupBox {
            border: 1px solid #504945;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 6px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
            color: #fabd2f;
        }
        QCheckBox {
            color: #ebdbb2;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #504945;
            border-radius: 2px;
            background: #1d2021;
        }
        QCheckBox::indicator:checked {
            background: #b8bb26;
            border-color: #b8bb26;
        }
        QStatusBar {
            background: #3c3836;
            color: #928374;
        }
        QStatusBar::item {
            border: none;
        }
        QSplitter::handle {
            background: #504945;
        }
        QToolTip {
            background-color: #3c3836;
            color: #ebdbb2;
            border: 1px solid #fabd2f;
        }
        QScrollBar:vertical {
            background: #282828;
            width: 12px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #504945;
            min-height: 24px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical:hover {
            background: #665c54;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            height: 0;
            background: none;
        }
        QScrollBar:horizontal {
            background: #282828;
            height: 12px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #504945;
            min-width: 24px;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #665c54;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            width: 0;
            background: none;
        }
    )"));
}
