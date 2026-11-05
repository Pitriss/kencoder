// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root
    implicitWidth: Kirigami.Units.gridUnit * 34
    implicitHeight: Kirigami.Units.gridUnit * 28

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        Controls.Label {
            Layout.fillWidth: true
            text: "HP OMEN Encoder (03f0:3341)"
            font.bold: true
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Controls.ComboBox {
                id: animation
                Kirigami.FormData.label: "Animace:"
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: "Statické", value: "static" },
                    { label: "Dýchání", value: "breathe" },
                    { label: "Vlna zleva", value: "left_wave" },
                    { label: "Vlna zprava", value: "right_wave" },
                    { label: "Reakce na stisk", value: "ripple" }
                ]
            }

            RowLayout {
                Kirigami.FormData.label: "Jas (0 až 3):"
                Layout.fillWidth: true

                Controls.Slider {
                    id: brightness
                    Layout.fillWidth: true
                    from: 0
                    to: 3
                    stepSize: 1
                    snapMode: Controls.Slider.SnapAlways
                    value: 1 // UI default, NOT a readback from the keyboard.
                }
                Controls.Label {
                    text: brightness.value.toFixed(0)
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                }
            }

            Controls.ComboBox {
                id: speed
                Kirigami.FormData.label: "Rychlost:"
                Layout.fillWidth: true
                enabled: animation.currentValue !== "static"
                textRole: "label"
                valueRole: "value"
                currentIndex: 1
                model: [
                    { label: "Pomalá", value: "slow" },
                    { label: "Střední", value: "medium" },
                    { label: "Rychlá", value: "fast" }
                ]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Nastavit"
                enabled: !kcm.busy
                onClicked: kcm.applyLighting(animation.currentValue,
                                              Math.round(brightness.value), speed.currentValue)
            }
            Controls.Button {
                text: "Vypnout"
                enabled: !kcm.busy
                onClicked: kcm.turnOff()
            }
        }

        Controls.Label {
            Layout.fillWidth: true
            text: "Stav:"
            font.bold: true
        }
        Controls.Label {
            Layout.fillWidth: true
            text: kcm.feedback
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
    }
}
