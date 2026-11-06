// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root
    implicitWidth: Kirigami.Units.gridUnit * 39
    implicitHeight: Kirigami.Units.gridUnit * 40
    property string selectedPresetId: ""
    property string selectedRuleId: ""

    function presetById(id) {
        const data = kcm.presets
        for (let i = 0; i < data.length; ++i)
            if (data[i].id === id) return data[i]
        return null
    }
    function ruleById(id) {
        const data = kcm.rules
        for (let i = 0; i < data.length; ++i)
            if (data[i].id === id) return data[i]
        return null
    }
    function stepDescription(s) {
        const names = {
            "off": "Vypnout", "static": "Statické", "breathe": "Dýchání",
            "ripple": "Reakce na stisk", "left_wave": "Vlna zleva →",
            "right_wave": "Vlna zprava ←", "wait": "Čekat"
        }
        let extra = ""
        if (s.type !== "wait" && s.type !== "off") extra += " · jas " + s.brightness
        if (s.type !== "wait" && s.type !== "off" && s.type !== "static")
            extra += " · rychlost " + s.speed
        return (names[s.type] || s.type) + extra + " · " + s.duration_ms + " ms"
    }
    function stepIsAnimated(type) {
        return type !== "static" && type !== "off" && type !== "wait"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            text: "KEncoder Notify"
            font.bold: true
            Layout.fillWidth: true
        }
        Controls.CheckBox {
            id: enabledCheck
            text: "Aktivovat naslouchání notifikacím"
            checked: kcm.listeningEnabled
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Controls.Label { text: "Backend:" }
            Controls.ComboBox {
                id: backendCombo
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: "KEncoder listener (KDE watcher)", value: "kde-watcher" },
                    { label: "KNotify scanner / user daemon (fáze 2)", value: "knotify-scanner" }
                ]
                currentIndex: kcm.backend === "knotify-scanner" ? 1 : 0
                delegate: Controls.ItemDelegate {
                    width: backendCombo.width
                    text: modelData.label
                    enabled: index === 0
                    highlighted: backendCombo.highlightedIndex === index
                    onClicked: {
                        backendCombo.currentIndex = index
                        backendCombo.popup.close()
                    }
                }
            }
        }
        Controls.Button {
            text: "Použít nastavení naslouchání"
            Layout.alignment: Qt.AlignLeft
            onClicked: kcm.applyEssentials(enabledCheck.checked, backendCombo.currentValue)
        }
        Controls.Label {
            Layout.fillWidth: true
            text: "Listener je volitelný. Výchozí stav: vypnuto. Druhý backend zatím není implementován."
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
        }

        Kirigami.Separator { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Controls.Label {
                text: "Presety světelných sekvencí"
                font.bold: true
                Layout.fillWidth: true
            }
            Controls.Button {
                text: "Nový preset"
                onClicked: presetDialog.edit("")
            }
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 5
            background: Controls.Frame {}
            ListView {
                id: presetsView
                clip: true
                model: kcm.presets
                boundsBehavior: Flickable.StopAtBounds
                delegate: Controls.ItemDelegate {
                    width: presetsView.width
                    text: modelData.name + " (" + modelData.steps.length + " kroků)"
                    highlighted: root.selectedPresetId === modelData.id
                    onClicked: root.selectedPresetId = modelData.id
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Přehrát"
                enabled: root.selectedPresetId !== "" && !kcm.busy
                onClicked: kcm.playPreset(root.selectedPresetId)
            }
            Controls.Button {
                text: "Upravit"
                enabled: root.selectedPresetId !== ""
                onClicked: presetDialog.edit(root.selectedPresetId)
            }
            Controls.Button {
                text: "Smazat"
                enabled: root.selectedPresetId !== ""
                onClicked: {
                    confirmDialog.isPreset = true
                    confirmDialog.deleteId = root.selectedPresetId
                    confirmDialog.open()
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Controls.Label {
                text: "Pravidla reakcí na notifikace"
                font.bold: true
                Layout.fillWidth: true
            }
            Controls.Button {
                text: "Nové pravidlo"
                enabled: kcm.presets.length > 0
                onClicked: ruleDialog.edit("")
            }
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 5
            background: Controls.Frame {}
            ListView {
                id: rulesView
                clip: true
                model: kcm.rules
                boundsBehavior: Flickable.StopAtBounds
                delegate: Controls.ItemDelegate {
                    width: rulesView.width
                    text: (modelData.enabled ? "● " : "○ ") + modelData.name
                    highlighted: root.selectedRuleId === modelData.id
                    onClicked: root.selectedRuleId = modelData.id
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Upravit"
                enabled: root.selectedRuleId !== ""
                onClicked: ruleDialog.edit(root.selectedRuleId)
            }
            Controls.Button {
                text: "Smazat"
                enabled: root.selectedRuleId !== ""
                onClicked: {
                    confirmDialog.isPreset = false
                    confirmDialog.deleteId = root.selectedRuleId
                    confirmDialog.open()
                }
            }
        }
        Controls.Label {
            Layout.fillWidth: true
            text: kcm.feedback
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.textColor
        }
    }

    Controls.Dialog {
        id: presetDialog
        modal: true
        parent: Controls.Overlay.overlay
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        width: Math.min(root.width * 0.9, Kirigami.Units.gridUnit * 35)
        height: Math.min(root.height * 0.88, Kirigami.Units.gridUnit * 34)
        title: editingId === "" ? "Nový preset" : "Upravit preset"
        property string editingId: ""
        property var draftSteps: []
        function edit(id) {
            editingId = id
            const preset = root.presetById(id)
            presetName.text = preset ? preset.name : ""
            draftSteps = preset ? preset.steps.slice() : []
            open()
        }
        function moveStep(i, offset) {
            const target = i + offset
            if (target < 0 || target >= draftSteps.length) return
            const entries = draftSteps.slice()
            const tmp = entries[i]
            entries[i] = entries[target]
            entries[target] = tmp
            draftSteps = entries
        }
        function removeStep(i) {
            const entries = draftSteps.slice()
            entries.splice(i, 1)
            draftSteps = entries
        }
        function addStep() {
            const type = stepType.currentValue
            const step = { "type": type, "duration_ms": durationSpin.value }
            if (type !== "wait" && type !== "off") step.brightness = brightnessSpin.value
            if (root.stepIsAnimated(type)) step.speed = speedSpin.value
            draftSteps = draftSteps.concat([step])
        }
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.TextField {
                id: presetName
                Layout.fillWidth: true
                placeholderText: "Název presetu"
            }
            Controls.Label { text: "Pořadí kroků (opakování je možné přidáním stejného kroku):" }
            Controls.ScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumHeight: Kirigami.Units.gridUnit * 7
                ListView {
                    id: stepsView
                    clip: true
                    model: presetDialog.draftSteps
                    delegate: RowLayout {
                        width: stepsView.width
                        Controls.Label {
                            Layout.fillWidth: true
                            text: (index + 1) + ". " + root.stepDescription(modelData)
                            elide: Text.ElideRight
                        }
                        Controls.Button {
                            text: "↑"
                            enabled: index > 0
                            onClicked: presetDialog.moveStep(index, -1)
                        }
                        Controls.Button {
                            text: "↓"
                            enabled: index < presetDialog.draftSteps.length - 1
                            onClicked: presetDialog.moveStep(index, 1)
                        }
                        Controls.Button {
                            text: "×"
                            onClicked: presetDialog.removeStep(index)
                        }
                    }
                }
            }
            Controls.Label { text: "Přidat krok:"; font.bold: true }
            Controls.ComboBox {
                id: stepType
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    {label: "Statické", value: "static"},
                    {label: "Vypnout", value: "off"},
                    {label: "Vlna zleva →", value: "left_wave"},
                    {label: "Vlna zprava ←", value: "right_wave"},
                    {label: "Dýchání", value: "breathe"},
                    {label: "Reakce na stisk", value: "ripple"},
                    {label: "Čekat bez příkazu", value: "wait"}
                ]
            }
            RowLayout {
                Layout.fillWidth: true
                Controls.Label { text: "Jas:" }
                Controls.SpinBox {
                    id: brightnessSpin
                    from: 0; to: 3; value: 1
                    enabled: stepType.currentValue !== "wait" && stepType.currentValue !== "off"
                }
                Controls.Label { text: "Rychlost:" }
                Controls.SpinBox {
                    id: speedSpin
                    from: 1; to: 3; value: 2
                    enabled: root.stepIsAnimated(stepType.currentValue)
                }
            }
            RowLayout {
                Controls.Label { text: "Trvání (ms):" }
                Controls.SpinBox {
                    id: durationSpin
                    from: 50; to: 60000; value: 200; stepSize: 50
                }
                Controls.Button { text: "Přidat krok"; onClicked: presetDialog.addStep() }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Controls.Button { text: "Zrušit"; onClicked: presetDialog.close() }
                Controls.Button {
                    text: "Uložit preset"
                    enabled: presetName.text.trim().length > 0 && presetDialog.draftSteps.length > 0
                    onClicked: {
                        if (kcm.savePreset(presetDialog.editingId, presetName.text,
                                           presetDialog.draftSteps)) {
                            root.selectedPresetId = ""
                            presetDialog.close()
                        }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: ruleDialog
        modal: true
        parent: Controls.Overlay.overlay
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        width: Math.min(root.width * 0.9, Kirigami.Units.gridUnit * 31)
        height: Math.min(root.height * 0.83, Kirigami.Units.gridUnit * 28)
        title: editingId === "" ? "Nové pravidlo" : "Upravit pravidlo"
        property string editingId: ""
        function edit(id) {
            editingId = id
            const rule = root.ruleById(id)
            ruleName.text = rule ? rule.name : ""
            ruleEnabled.checked = rule ? rule.enabled : true
            matchApp.text = rule ? rule.match.app_contains : ""
            matchTitle.text = rule ? rule.match.title_contains : ""
            matchBody.text = rule ? rule.match.text_contains : ""
            cooldown.value = rule ? rule.cooldown_ms : 5000
            rulePreset.currentIndex = 0
            if (rule) {
                for (let i = 0; i < rulePreset.count; ++i)
                    if (rulePreset.valueAt(i) === rule.preset_id) rulePreset.currentIndex = i
            }
            open()
        }
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.TextField {
                id: ruleName
                placeholderText: "Název pravidla"
                Layout.fillWidth: true
            }
            Controls.CheckBox { id: ruleEnabled; text: "Pravidlo je aktivní"; checked: true }
            Controls.Label { text: "Podmínky (nerozlišují velikost písmen):" }
            Controls.TextField {
                id: matchApp
                Layout.fillWidth: true
                placeholderText: "Aplikace obsahuje: Teams"
            }
            Controls.TextField {
                id: matchTitle
                Layout.fillWidth: true
                placeholderText: "Titulek obsahuje (nepovinné)"
            }
            Controls.TextField {
                id: matchBody
                Layout.fillWidth: true
                placeholderText: "Zpráva obsahuje: XYZ"
            }
            Controls.Label { text: "Spustit preset:" }
            Controls.ComboBox {
                id: rulePreset
                Layout.fillWidth: true
                model: kcm.presets
                textRole: "name"
                valueRole: "id"
            }
            RowLayout {
                Controls.Label { text: "Ochranná prodleva (ms):" }
                Controls.SpinBox {
                    id: cooldown
                    from: 0; to: 3600000; stepSize: 1000; value: 5000
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Controls.Button { text: "Zrušit"; onClicked: ruleDialog.close() }
                Controls.Button {
                    text: "Uložit pravidlo"
                    enabled: ruleName.text.trim().length > 0 && rulePreset.currentIndex >= 0 &&
                             (matchApp.text.length > 0 || matchTitle.text.length > 0 || matchBody.text.length > 0)
                    onClicked: {
                        if (kcm.saveRule(ruleDialog.editingId, ruleName.text, ruleEnabled.checked,
                                         matchApp.text, matchTitle.text, matchBody.text,
                                         rulePreset.currentValue, cooldown.value)) {
                            root.selectedRuleId = ""
                            ruleDialog.close()
                        }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: confirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        title: "Opravdu smazat?"
        property bool isPreset: true
        property string deleteId: ""
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: {
            if (isPreset) {
                if (kcm.deletePreset(deleteId)) root.selectedPresetId = ""
            } else {
                if (kcm.deleteRule(deleteId)) root.selectedRuleId = ""
            }
        }
        Controls.Label {
            text: confirmDialog.isPreset ? "Odstranit vybraný preset?" : "Odstranit vybrané pravidlo?"
        }
    }
}
