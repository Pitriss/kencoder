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
    implicitHeight: Kirigami.Units.gridUnit * 47
    property bool draftModified: false
    property string transientStatus: ""
    Timer {
        id: messageTimer
        interval: 6000
        repeat: false
        onTriggered: root.transientStatus = ""
    }

    function syncFromHardware() {
        if (draftModified || !kcm.reportedAvailable)
            return
        brightness.value = kcm.reportedBrightness
        if (kcm.reportedMode === "off")
            return // Off is hardware state, not a selectable animation.
        for (let i = 0; i < animation.count; ++i) {
            if (animation.model[i].value === kcm.reportedMode) {
                animation.currentIndex = i
                break
            }
        }
        // Speed cannot be read. Never claim that the slider is hardware state.
    }

    Connections {
        target: kcm
        function onLightingStatusChanged() { root.syncFromHardware() }
        function onFeedbackChanged() {
            root.transientStatus = kcm.feedback
            messageTimer.restart()
        }
    }
    Component.onCompleted: syncFromHardware()

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
                onActivated: root.draftModified = true
                model: [
                    { label: "Statické", value: "static" },
                    { label: "Dýchání", value: "breathe" },
                    { label: "Vlna zleva", value: "left_wave" },
                    { label: "Vlna zprava", value: "right_wave" },
                    { label: "Reakce na stisk", value: "ripple" },
                    { label: "Vlna (směr nezjištěn)", value: "wave" }
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
                    value: 1 // Editor default until a hardware read succeeds.
                    onMoved: root.draftModified = true
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
                onActivated: root.draftModified = true
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
                onClicked: {
                    root.draftModified = false
                    kcm.applyLighting(animation.currentValue,
                                      Math.round(brightness.value), speed.currentValue)
                }
            }
            Controls.Button {
                text: "Vypnout"
                enabled: !kcm.busy
                onClicked: {
                    root.draftModified = false
                    kcm.turnOff()
                }
            }
            Controls.Button {
                text: "Nastavit Default"
                enabled: !kcm.busy
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Použije první krok uloženého presetu Default."
                onClicked: {
                    root.draftModified = false
                    kcm.applyDefault()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Načíst stav"
                enabled: !kcm.busy
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Čtení přes: " + (kcm.encoderPath.length ? kcm.encoderPath : "encoder není v PATH")
                onClicked: kcm.refreshLightingStatus()
            }
            Controls.Button {
                text: "Spustit KEncoder"
                onClicked: kcm.launchGui()
            }
        }

        Controls.GroupBox {
            id: automaticBehavior
            title: "Automatické chování"
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing * 2
            Layout.bottomMargin: Kirigami.Units.largeSpacing * 2
            // Preserve the native Plasma frame. Only replace its title item.
            // Place the text center on the theme-drawn top border.
            label: Controls.Label {
                text: automaticBehavior.title
                font.bold: true
                x: Kirigami.Units.gridUnit
                y: automaticBehavior.background
                    ? automaticBehavior.background.y - height / 2
                    : -height / 2
                z: 1
                leftPadding: Kirigami.Units.smallSpacing
                rightPadding: Kirigami.Units.smallSpacing
                background: Rectangle {
                    color: Kirigami.Theme.backgroundColor
                }
            }
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Controls.CheckBox {
                    id: offOnLock
                    text: "Zhasnout klávesnici při zamčení počítače"
                    checked: kcm.offOnLock
                    onClicked: kcm.saveSessionPolicy(checked, offOnScreenOff.checked,
                                                     notifyInactive.checked)
                }
                Controls.CheckBox {
                    id: offOnScreenOff
                    text: "Zhasnout klávesnici při spořiči obrazovky"
                    checked: kcm.offOnScreenOff
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.text: "V Plasma 6 se sleduje vypnutí monitorů (DPMS)."
                    onClicked: kcm.saveSessionPolicy(offOnLock.checked, checked,
                                                     notifyInactive.checked)
                }
                Controls.CheckBox {
                    id: notifyInactive
                    text: "Povolit KEncoder Notify při zamčené nebo zhasnuté obrazovce"
                    checked: kcm.notifyWhenInactive
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.text: "Při skutečném uspání počítače se notifikace nevykonávají."
                    onClicked: kcm.saveSessionPolicy(offOnLock.checked, offOnScreenOff.checked,
                                                     checked)
                }
            }
        }
        Controls.GroupBox {
            id: readinessBox
            title: "Připravenost"
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            label: Controls.Label {
                text: readinessBox.title
                font.bold: true
                x: Kirigami.Units.gridUnit
                y: readinessBox.background
                    ? readinessBox.background.y - height / 2 : -height / 2
                z: 1
                leftPadding: Kirigami.Units.smallSpacing
                rightPadding: Kirigami.Units.smallSpacing
                background: Rectangle { color: Kirigami.Theme.backgroundColor }
            }
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing * 2
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing * 2
                    Rectangle {
                        Layout.preferredWidth: 12; Layout.preferredHeight: 12
                        radius: 6
                        color: kcm.keyboardReadiness === 2 ? "#209c60" :
                               kcm.keyboardReadiness === 1 ? "#d68b16" : "#d63c43"
                        Accessible.name: kcm.keyboardReadinessText
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: "Klávesnice: " + kcm.keyboardReadinessText
                        wrapMode: Text.WordWrap
                    }
                }
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing * 2
                    Rectangle {
                        Layout.preferredWidth: 12; Layout.preferredHeight: 12
                        radius: 6
                        color: kcm.notifyReadiness === 2 ? "#209c60" :
                               kcm.notifyReadiness === 1 ? "#d68b16" : "#d63c43"
                        Accessible.name: kcm.notifyReadinessText
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: "KEncoder Notify: " + kcm.notifyReadinessText
                        wrapMode: Text.WordWrap
                    }
                }
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing * 2
                    Rectangle {
                        Layout.preferredWidth: 12; Layout.preferredHeight: 12
                        radius: 6
                        color: kcm.listenerReadiness === 2 ? "#209c60" :
                               kcm.listenerReadiness === 1 ? "#d68b16" : "#d63c43"
                        Accessible.name: kcm.listenerReadinessText
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: "Listener: " + kcm.listenerReadinessText
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
        // Same theme-native feedback frame as KEncoder Notify.
        // Temporary messages take priority over the idle readback state.
        Controls.Frame {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing * 2
            Layout.minimumHeight: Kirigami.Units.gridUnit * 3
            padding: Kirigami.Units.smallSpacing
            Controls.Label {
                anchors.fill: parent
                text: root.transientStatus.length > 0 ? root.transientStatus : kcm.lightingStatus
                font.bold: root.transientStatus.length === 0
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
