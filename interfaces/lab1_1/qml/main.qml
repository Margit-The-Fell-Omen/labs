import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BatteryDiagnostics 1.0

ApplicationWindow {
    id: window
    width: 780
    height: 560
    visible: true
    title: "Диагностика батареи"
    color: "#121214"

    function wearColor() {
        var w = batteryInfo.wearPercent
        if (w < 20) return "#00be78"
        if (w < 40) return "#ffd166"
        return "#ff6b6b"
    }

    component FieldRow: RowLayout {
        property string label: ""
        property string value: ""
        property string valueColor: "#ffffff"
        Layout.fillWidth: true
        spacing: 12
        Label {
            Layout.preferredWidth: 250
            text: parent.label
            color: "#9a9aa0"
            font.pixelSize: 14
        }
        Label {
            text: parent.value
            color: parent.valueColor
            font.pixelSize: 14
            font.bold: true
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
        anchors.margins: 14
        spacing: 10

        TabBar {
            id: bar
            Layout.fillWidth: true
            background: Rectangle { color: "#1e1e23"; radius: 6 }

            TabButton {
                text: "Диагностика батареи"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 15
                    color: parent.checked ? "#00be78" : "#c8c8ce"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                text: "График заряда"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 15
                    color: parent.checked ? "#00be78" : "#c8c8ce"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
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
                Rectangle {
                    anchors.fill: parent
                    color: "#1e1e23"
                    radius: 8
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 10

                    Label {
                        text: "Батарея не обнаружена (проверьте питание от АКБ)"
                        color: "#ff6b6b"
                        visible: !batteryInfo.available
                        font.pixelSize: 14
                    }

                    ColumnLayout {
                        spacing: 4
                        Label { text: "Степень износа батареи"; color: "#9a9aa0"; font.pixelSize: 13 }
                        RowLayout {
                            spacing: 12
                            ProgressBar {
                                Layout.fillWidth: true
                                value: batteryInfo.wearPercent / 100.0
                            }
                            Label {
                                text: batteryInfo.wearPercent.toFixed(2) + " %"
                                color: window.wearColor()
                                font.bold: true
                                font.pixelSize: 22
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2c2c33" }

                    FieldRow { label: "Химический тип"; value: batteryInfo.chemistry }
                    FieldRow { label: "Дизайнерская ёмкость"; value: batteryInfo.designCapacity + " мВт·ч" }
                    FieldRow { label: "Полная зарядная ёмкость"; value: batteryInfo.fullChargeCapacity + " мВт·ч" }
                    FieldRow { label: "Текущая ёмкость"; value: batteryInfo.currentCapacity + " мВт·ч" }
                    FieldRow { label: "Напряжение"; value: (batteryInfo.voltage / 1000.0).toFixed(2) + " В" }
                    FieldRow { label: "Циклы заряда"; value: batteryInfo.cycleCount > 0 ? String(batteryInfo.cycleCount) : "недоступно" }
                    FieldRow { label: "Температура"; value: batteryInfo.temperature + " °C" }
                    FieldRow { label: "Уровень заряда"; value: batteryInfo.chargeLevel.toFixed(1) + " %" }
                    FieldRow { label: "Питание от сети"; value: batteryInfo.acOnline ? "Да" : "Нет" }

                    Item { Layout.fillHeight: true }
                }
            }

            // ==================== Tab 2: График заряда ====================
            Item {
                Rectangle {
                    anchors.fill: parent
                    color: "#1e1e23"
                    radius: 8
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Изменение уровня заряда (%), период опроса 2 с"
                            color: "#c8c8ce"
                            font.pixelSize: 14
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Сброс"
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
