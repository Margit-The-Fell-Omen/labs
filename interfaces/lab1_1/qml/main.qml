import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BatteryDiagnostics 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 640
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: "Диагностика батареи"
    color: "#1d2021"

    // --- Gruvbox palette (dark) ---
    readonly property color bg0_h: "#1d2021"
    readonly property color bg0:   "#282828"
    readonly property color bg1:   "#3c3836"
    readonly property color bg2:   "#504945"
    readonly property color bg3:   "#665c54"
    readonly property color fg0:   "#fbf1c7"
    readonly property color fg1:   "#ebdbb2"
    readonly property color fg3:   "#bdae93"
    readonly property color fg4:   "#a89984"
    readonly property color red:   "#fb4934"
    readonly property color green: "#b8bb26"
    readonly property color yellow:"#fabd2f"
    readonly property color orange:"#fe8019"
    readonly property color aqua:  "#8ec07c"
    readonly property color blue:  "#83a598"

    function wearColor() {
        var w = batteryInfo.wearPercent
        if (w < 20) return window.green
        if (w < 40) return window.yellow
        return window.red
    }

    function chargeColor() {
        var c = batteryInfo.chargeLevel
        if (c < 20) return window.red
        if (c < 50) return window.yellow
        return window.green
    }

    component InfoCard: Rectangle {
        id: card
        property string label: ""
        property string value: ""
        property color valueColor: window.fg1

        color: window.bg1
        radius: 10
        border.width: 1
        border.color: window.bg2

        Layout.fillWidth: true
        Layout.preferredHeight: 82

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6

            Label {
                text: card.label
                color: window.fg4
                font.pixelSize: 11
                font.letterSpacing: 0.3
            }
            Label {
                text: card.value
                color: card.valueColor
                font.pixelSize: 17
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    Timer {
        id: pollTimer
        interval: 2000
        running: true
        repeat: true
        onTriggered: {
            batteryInfo.refresh()
            batteryGraph.addSample(batteryInfo.chargeLevel)
        }
    }

    Component.onCompleted: {
        batteryInfo.refresh()
        batteryGraph.addSample(batteryInfo.chargeLevel)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        // ==================== Header ====================
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                spacing: 2
                Label {
                    text: "Диагностика батареи"
                    color: window.fg0
                    font.pixelSize: 22
                    font.bold: true
                }
                Label {
                    text: "Мониторинг АКБ в реальном времени"
                    color: window.fg4
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                height: 30
                radius: 15
                color: window.bg1
                border.width: 1
                border.color: window.bg2

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    Rectangle {
                        width: 9
                        height: 9
                        radius: 5
                        color: batteryInfo.acOnline ? window.green : window.orange
                    }
                    Label {
                        text: batteryInfo.acOnline ? "От сети" : "От батареи"
                        color: window.fg1
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        // ==================== Tabs ====================
        TabBar {
            id: bar
            Layout.fillWidth: true
            background: Rectangle { color: window.bg0; radius: 8 }

            TabButton {
                text: "Диагностика"
                height: 36
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.bold: parent.checked
                    color: parent.checked ? window.green : window.fg3
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.checked ? window.bg1 : "transparent"
                    radius: 6
                }
            }
            TabButton {
                text: "График заряда"
                height: 36
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.bold: parent.checked
                    color: parent.checked ? window.green : window.fg3
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.checked ? window.bg1 : "transparent"
                    radius: 6
                }
            }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bar.currentIndex

            // ==================== Tab 1: Диагностика ====================
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14

                    Rectangle {
                        Layout.fillWidth: true
                        color: window.red
                        opacity: 0.12
                        radius: 8
                        visible: !batteryInfo.available
                        Label {
                            anchors.fill: parent
                            anchors.margins: 12
                            text: "Батарея не обнаружена — проверьте питание от АКБ"
                            color: window.red
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Wear card
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        color: window.bg1
                        radius: 10
                        border.width: 1
                        border.color: window.bg2

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "Степень износа батареи"
                                    color: window.fg3
                                    font.pixelSize: 13
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: batteryInfo.wearPercent.toFixed(2) + " %"
                                    color: window.wearColor()
                                    font.pixelSize: 26
                                    font.bold: true
                                }
                            }

                            ProgressBar {
                                id: wearBar
                                Layout.fillWidth: true
                                Layout.preferredHeight: 10
                                value: batteryInfo.wearPercent / 100.0

                                background: Rectangle {
                                    color: window.bg0
                                    radius: 5
                                }
                                contentItem: Item {
                                    Rectangle {
                                        width: wearBar.visualPosition * parent.width
                                        height: parent.height
                                        radius: 5
                                        color: window.wearColor()
                                    }
                                }
                            }
                        }
                    }

                    // Info cards grid
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 14

                        InfoCard { label: "Химический тип"; value: batteryInfo.chemistry }
                        InfoCard {
                            label: "Уровень заряда"
                            value: batteryInfo.chargeLevel.toFixed(1) + " %"
                            valueColor: window.chargeColor()
                        }
                        InfoCard { label: "Напряжение"; value: (batteryInfo.voltage / 1000.0).toFixed(2) + " В" }
                        InfoCard { label: "Температура"; value: batteryInfo.temperature + " °C" }
                        InfoCard { label: "Дизайнерская ёмкость"; value: batteryInfo.designCapacity + " мВт·ч" }
                        InfoCard { label: "Полная зарядная ёмкость"; value: batteryInfo.fullChargeCapacity + " мВт·ч" }
                        InfoCard { label: "Текущая ёмкость"; value: batteryInfo.currentCapacity + " мВт·ч" }
                        InfoCard {
                            label: "Циклы заряда"
                            value: batteryInfo.cycleCount > 0 ? String(batteryInfo.cycleCount) : "недоступно"
                        }
                    }
                }
            }

            // ==================== Tab 2: График заряда ====================
            Item {
                Rectangle {
                    anchors.fill: parent
                    color: window.bg1
                    radius: 10
                    border.width: 1
                    border.color: window.bg2

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "Изменение уровня заряда (%)"
                                color: window.fg1
                                font.pixelSize: 14
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "период опроса 2 с"
                                color: window.fg4
                                font.pixelSize: 12
                            }

                            Button {
                                text: "Сброс"
                                contentItem: Text {
                                    text: parent.text
                                    color: window.fg1
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.down ? window.bg3 : window.bg2
                                    radius: 6
                                }
                                onClicked: batteryGraph.clear()
                            }
                        }

                        BatteryGraph {
                            id: batteryGraph
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }
            }
        }
    }
}
