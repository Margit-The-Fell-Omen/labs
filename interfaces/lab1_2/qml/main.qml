import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: window
    width: 940
    height: 640
    visible: true
    title: "Управление схемами электропитания"
    color: "#121214"

    function fmtTimeout(sec) {
        if (sec < 0) return "недоступно"
        if (sec === 0) return "Никогда"
        if (sec < 60) return sec + " с"
        if (sec % 3600 === 0) return (sec / 3600) + " ч"
        if (sec % 60 === 0) return (sec / 60) + " мин"
        var totalMin = Math.floor(sec / 60)
        if (totalMin >= 60)
            return Math.floor(totalMin / 60) + " ч " + (totalMin % 60) + " мин"
        return totalMin + " мин " + (sec % 60) + " с"
    }

    function fmtPercent(v) {
        if (v < 0) return "недоступно"
        return v + " %"
    }

    Component.onCompleted: {
        powerManager.refresh()
        battery.refresh()
    }

    Timer {
        id: batteryTimer
        interval: 5000
        running: true
        repeat: true
        onTriggered: battery.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            text: "Схемы электропитания"
            color: "#ffffff"
            font.pixelSize: 20
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: "#1e1e23"
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        text: "Доступные схемы"
                        color: "#9a9aa0"
                        font.pixelSize: 13
                    }

                    ListView {
                        id: listView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: powerManager.schemes
                        spacing: 6
                        clip: true

                        delegate: Rectangle {
                            width: listView.width
                            height: 46
                            radius: 6
                            color: index === powerManager.activeIndex ? "#10321f"
                                 : index === powerManager.selectedIndex ? "#24324d"
                                 : "#2a2a31"
                            border.width: index === powerManager.selectedIndex ? 1 : 0
                            border.color: "#3b7bd6"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8

                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: index === powerManager.activeIndex ? "#00be78" : "#3a3a42"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: index === powerManager.activeIndex ? "#00be78" : "#e8e8ee"
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: "активна"
                                    visible: index === powerManager.activeIndex
                                    color: "#00be78"
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: powerManager.select(index)
                                onDoubleClicked: powerManager.activate(index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1e1e23"
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8

                    Label {
                        text: "Параметры выбранной схемы"
                        color: "#ffffff"
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Label {
                        text: powerManager.selectedName
                        color: "#9a9aa0"
                        font.pixelSize: 14
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2c2c33" }

                    GridLayout {
                        columns: 3
                        columnSpacing: 24
                        rowSpacing: 10

                        Label { text: "Параметр"; color: "#9a9aa0"; font.pixelSize: 12; font.bold: true }
                        Label { text: "От сети (AC)"; color: "#9a9aa0"; font.pixelSize: 12; font.bold: true }
                        Label { text: "От батареи (DC)"; color: "#9a9aa0"; font.pixelSize: 12; font.bold: true }

                        Label { text: "Отключение дисплея"; color: "#c8c8ce"; font.pixelSize: 14 }
                        Label { text: window.fmtTimeout(powerManager.displayTimeoutAC); color: "#ffffff"; font.pixelSize: 14 }
                        Label { text: window.fmtTimeout(powerManager.displayTimeoutDC); color: "#ffffff"; font.pixelSize: 14 }

                        Label { text: "Переход в сон"; color: "#c8c8ce"; font.pixelSize: 14 }
                        Label { text: window.fmtTimeout(powerManager.sleepTimeoutAC); color: "#ffffff"; font.pixelSize: 14 }
                        Label { text: window.fmtTimeout(powerManager.sleepTimeoutDC); color: "#ffffff"; font.pixelSize: 14 }

                        Label { text: "Критический уровень заряда"; color: "#c8c8ce"; font.pixelSize: 14 }
                        Label { text: window.fmtPercent(powerManager.criticalBatteryAC); color: "#ffffff"; font.pixelSize: 14 }
                        Label { text: window.fmtPercent(powerManager.criticalBatteryDC); color: "#ffffff"; font.pixelSize: 14 }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2c2c33" }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "Двойной клик по схеме или кнопка — переключение активной схемы"
                            color: "#9a9aa0"
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        Button {
                            text: "Применить схему"
                            onClicked: powerManager.activate(powerManager.selectedIndex)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: "#1e1e23"
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 16

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: battery.available ? "#00be78" : "#ff6b6b"
                }

                Label {
                    text: battery.statusText
                    color: battery.available ? "#e8e8ee" : "#ff6b6b"
                    font.pixelSize: 13
                    Layout.fillWidth: true
                }

                Label {
                    text: battery.acOnline ? "Сеть подключена" : "Питание от батареи"
                    color: battery.acOnline ? "#00be78" : "#ffd166"
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }
}
