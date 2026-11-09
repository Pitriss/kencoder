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
    readonly property string defaultPresetId: "default"
    // Confirmations and errors temporarily replace the listener state in the status bar.
    property string uiStatus: ""
    function showStatus(message) {
        uiStatus = message
        statusTimeout.restart()
    }
    Timer {
        id: statusTimeout
        interval: 6000
        repeat: false
        onTriggered: root.uiStatus = ""
    }
    Connections {
        target: kcm
        function onFeedbackChanged() { root.showStatus(kcm.feedback) }
    }
    function showListenerHint() {
        root.showStatus(backendCombo.currentValue === "kde-watcher"
            ? qsTr("Vybrán listener KDE watcher. Dostupnost se ověří při spuštění; změny potvrďte tlačítkem Použít.")
            : qsTr("Listener KNotify scanner je plánován pro fázi 2 a nyní jej nelze aktivovat."))
    }

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
    function stepDescription(s, isDefault) {
        const names = {
            "off": qsTr("Vypnout"), "static": qsTr("Statické"), "breathe": qsTr("Dýchání"),
            "ripple": qsTr("Reakce na stisk"), "left_wave": qsTr("Vlna zleva →"),
            "right_wave": qsTr("Vlna zprava ←"), "wait": qsTr("Čekat")
        }
        let extra = ""
        if (s.type !== "wait" && s.type !== "off") extra += qsTr(" · jas ") + s.brightness
        if (s.type !== "wait" && s.type !== "off" && s.type !== "static")
            extra += qsTr(" · rychlost ") + s.speed
        return (names[s.type] || s.type) + extra + (isDefault ? "" : " · " + s.duration_ms + " ms")
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
            text: qsTr("Aktivovat naslouchání notifikacím")
            checked: kcm.listeningEnabled
            Layout.fillWidth: true
            onClicked: root.showStatus(qsTr("Změna listeneru čeká na potvrzení tlačítkem Použít nastavení listeneru."))
        }
        Controls.ComboBox {
                id: backendCombo
                Layout.fillWidth: true
                Accessible.name: qsTr("Režim listeneru")
                onActivated: root.showListenerHint()
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: qsTr("Listener KDE watcher"), value: "kde-watcher" },
                    { label: qsTr("Listener KNotify scanner (fáze 2)"), value: "knotify-scanner" }
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
                        root.showListenerHint()
                    }
                }
        }
        Controls.Button {
            text: qsTr("Použít nastavení listeneru")
            Layout.alignment: Qt.AlignLeft
            onClicked: kcm.applyEssentials(enabledCheck.checked, backendCombo.currentValue)
        }

        // Drag the horizontal splitter to resize PRESETS and RULES up/down only.
        Controls.SplitView {
            id: sectionsSplit
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Keep extra space above/below the hairline without altering pane sizing.
            // The vertical splitter still resizes panels only up/down.
            handle: Item {
                implicitWidth: 1
                implicitHeight: Kirigami.Units.smallSpacing * 5
                HoverHandler {
                    id: splitterHover
                    cursorShape: Qt.SplitVCursor
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: splitterGrip.left
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: Kirigami.Theme.textColor
                    opacity: splitterHover.hovered ? 0.8 : 0.45
                }
                Row {
                    id: splitterGrip
                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing / 2
                    Repeater {
                        model: 3
                        Rectangle {
                            width: 2
                            height: Kirigami.Units.smallSpacing * 2
                            radius: 1
                            color: Kirigami.Theme.textColor
                            opacity: splitterHover.hovered ? 0.9 : 0.55
                        }
                    }
                }
                Rectangle {
                    anchors.left: splitterGrip.right
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: Kirigami.Theme.textColor
                    opacity: splitterHover.hovered ? 0.8 : 0.45
                }
            }
            ColumnLayout {
                Controls.SplitView.minimumHeight: Kirigami.Units.gridUnit * 11
                Controls.SplitView.preferredHeight: sectionsSplit.height / 2
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: qsTr("Presety světelných sekvencí")
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Controls.Button {
                        text: qsTr("Nový preset")
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
                            text: modelData.id === root.defaultPresetId
                                ? qsTr("Default (návratový stav)")
                                : modelData.name + " (" + modelData.steps.length + qsTr(" kroků)")
                            highlighted: root.selectedPresetId === modelData.id
                            onClicked: root.selectedPresetId = modelData.id
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Button {
                        text: qsTr("Přehrát")
                        enabled: root.selectedPresetId !== "" && !kcm.busy
                        onClicked: kcm.playPreset(root.selectedPresetId)
                    }
                    Controls.Button {
                        text: qsTr("Upravit")
                        enabled: root.selectedPresetId !== ""
                        onClicked: presetDialog.edit(root.selectedPresetId)
                    }
                    Controls.Button {
                        text: qsTr("Smazat")
                        enabled: root.selectedPresetId !== "" && root.selectedPresetId !== root.defaultPresetId
                        onClicked: {
                            confirmDialog.isPreset = true
                            confirmDialog.deleteId = root.selectedPresetId
                            confirmDialog.open()
                        }
                    }
                }

            }
            ColumnLayout {
                Controls.SplitView.minimumHeight: Kirigami.Units.gridUnit * 11
                Controls.SplitView.fillHeight: true
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: qsTr("Pravidla reakcí na notifikace")
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Controls.Button {
                        text: qsTr("Nové pravidlo")
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
                        // Keep the source delegate alive while auto-scrolling past many rows.
                        cacheBuffer: 4096
                        property string draggedRuleId: ""
                        property int dragTargetIndex: -1
                        property real dragPointerY: 0
                        property int autoScrollDirection: 0
                        function updateDragTarget(y) {
                            if (draggedRuleId === "") return
                            dragPointerY = y
                            autoScrollDirection = y < 28 ? -1 : (y > height - 28 ? 1 : 0)
                            const insideY = Math.max(1, Math.min(height - 2, y))
                            const indexHere = indexAt(1, contentY + insideY)
                            if (indexHere >= 0) dragTargetIndex = indexHere
                        }
                        function clearDrag() {
                            draggedRuleId = ""
                            dragTargetIndex = -1
                            autoScrollDirection = 0
                        }
                        Timer {
                            interval: 45
                            repeat: true
                            running: rulesView.draggedRuleId !== "" && rulesView.autoScrollDirection !== 0
                            onTriggered: {
                                const maxY = Math.max(0, rulesView.contentHeight - rulesView.height)
                                rulesView.contentY = Math.max(0, Math.min(maxY,
                                    rulesView.contentY + 15 * rulesView.autoScrollDirection))
                                rulesView.updateDragTarget(rulesView.dragPointerY)
                            }
                        }
                        delegate: Controls.ItemDelegate {
                            width: rulesView.width
                            highlighted: root.selectedRuleId === modelData.id
                            // The target is highlighted while the user drags the grip.
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: Kirigami.Theme.highlightColor
                                border.width: rulesView.draggedRuleId !== "" &&
                                              rulesView.dragTargetIndex === index ? 2 : 0
                                radius: Kirigami.Units.smallSpacing
                                z: 2
                            }
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                Controls.ToolButton {
                                    id: dragGrip
                                    text: "⠿"
                                    Accessible.name: qsTr("Přetáhnout pravidlo myší")
                                    Controls.ToolTip.visible: dragMouse.containsMouse && !dragMouse.pressed
                                    Controls.ToolTip.text: qsTr("Přetáhnout pro změnu priority")
                                    Controls.ToolTip.delay: 700
                                    MouseArea {
                                        id: dragMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        preventStealing: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        onPressed: (mouse) => {
                                            root.selectedRuleId = modelData.id
                                            rulesView.draggedRuleId = modelData.id
                                            rulesView.dragTargetIndex = index
                                            rulesView.updateDragTarget(mapToItem(rulesView, mouse.x, mouse.y).y)
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (pressed)
                                                rulesView.updateDragTarget(mapToItem(rulesView, mouse.x, mouse.y).y)
                                        }
                                        onReleased: (mouse) => {
                                            const id = rulesView.draggedRuleId
                                            rulesView.updateDragTarget(mapToItem(rulesView, mouse.x, mouse.y).y)
                                            const target = rulesView.dragTargetIndex
                                            rulesView.clearDrag()
                                            if (id !== "" && target >= 0 && kcm.moveRuleTo(id, target))
                                                root.selectedRuleId = id
                                        }
                                        onCanceled: rulesView.clearDrag()
                                    }
                                }
                                Controls.CheckBox {
                                    checked: modelData.enabled
                                    onClicked: {
                                        if (!kcm.setRuleEnabled(modelData.id, checked))
                                            checked = modelData.enabled
                                    }
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    opacity: modelData.enabled ? 1.0 : 0.55
                                    elide: Text.ElideRight
                                }
                                Controls.ToolButton {
                                    text: "↑"
                                    enabled: index > 0
                                    Accessible.name: qsTr("Přesunout pravidlo nahoru")
                                    onClicked: {
                                        if (kcm.moveRule(modelData.id, -1))
                                            root.selectedRuleId = modelData.id
                                    }
                                }
                                Controls.ToolButton {
                                    text: "↓"
                                    enabled: index < rulesView.count - 1
                                    Accessible.name: qsTr("Přesunout pravidlo dolů")
                                    onClicked: {
                                        if (kcm.moveRule(modelData.id, 1))
                                            root.selectedRuleId = modelData.id
                                    }
                                }
                            }
                            onClicked: root.selectedRuleId = modelData.id
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Button {
                        text: qsTr("Upravit")
                        enabled: root.selectedRuleId !== ""
                        onClicked: ruleDialog.edit(root.selectedRuleId)
                    }
                    Controls.Button {
                        text: qsTr("Smazat")
                        enabled: root.selectedRuleId !== ""
                        onClicked: {
                            confirmDialog.isPreset = false
                            confirmDialog.deleteId = root.selectedRuleId
                            confirmDialog.open()
                        }
                    }
                }
            }
        }
        // Idle: listener state; confirmations/errors: only the message, then back to idle.
        Controls.Frame {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing * 2
            Layout.minimumHeight: Kirigami.Units.gridUnit * 3
            padding: Kirigami.Units.smallSpacing
            Controls.Label {
                anchors.fill: parent
                text: root.uiStatus.length > 0 ? root.uiStatus
                     : (kcm.listeningEnabled ? qsTr("Listener: povolen") : qsTr("Listener: vypnut"))
                font.bold: root.uiStatus.length === 0
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Controls.Dialog {
        id: presetDialog
        modal: true
        parent: Controls.Overlay.overlay
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        width: Math.min(root.width * 0.9, Kirigami.Units.gridUnit * 35)
        height: Math.min(root.height * 0.9, Kirigami.Units.gridUnit * 36)
        title: editingId === "" ? qsTr("Nový preset") : qsTr("Upravit preset")
        property string editingId: ""
        readonly property bool isDefault: editingId === root.defaultPresetId
        property int editingStepIndex: -1
        property var draftSteps: []
        readonly property real fieldLabelWidth: Kirigami.Units.gridUnit * 5
        // A resize handle may enlarge the dialog, never shrink its opening size.
        property real minimumResizeWidth: 0
        property real minimumResizeHeight: 0
        function edit(id) {
            editingId = id
            const preset = root.presetById(id)
            presetName.text = isDefault ? "Default" : (preset ? preset.name : "")
            editingStepIndex = -1
            draftSteps = preset ? (isDefault
                ? [Object.assign({}, preset.steps[0])]
                : preset.steps.map(s => Object.assign({}, s))) : []
            if (isDefault && draftSteps.length > 0)
                editStep(0)
            width = Math.min(root.width * 0.9, Kirigami.Units.gridUnit * 35)
            height = Math.min(root.height * 0.9, Kirigami.Units.gridUnit * 36)
            minimumResizeWidth = width
            minimumResizeHeight = height
            x = Math.max(0, (parent.width - width) / 2)
            y = Math.max(0, (parent.height - height) / 2)
            open()
        }
        function editStep(i) {
            if (i < 0 || i >= draftSteps.length) return
            editingStepIndex = i
            const step = draftSteps[i]
            for (let n = 0; n < stepType.count; ++n) {
                if (stepType.valueAt(n) === step.type) {
                    stepType.currentIndex = n
                    break
                }
            }
            if (step.brightness !== undefined) brightnessCombo.currentIndex = step.brightness
            if (step.speed !== undefined) speedCombo.currentIndex = step.speed - 1
            if (step.duration_ms !== undefined) durationSpin.value = step.duration_ms
        }
        function moveStep(i, offset) {
            const target = i + offset
            if (target < 0 || target >= draftSteps.length) return
            const entries = draftSteps.slice()
            const tmp = entries[i]
            entries[i] = entries[target]
            entries[target] = tmp
            draftSteps = entries
            editingStepIndex = -1
        }
        function removeStep(i) {
            const entries = draftSteps.slice()
            entries.splice(i, 1)
            draftSteps = entries
            editingStepIndex = -1
        }
        function addStep() {
            const type = stepType.currentValue
            if (isDefault && type === "wait") return
            if (isDefault && editingStepIndex < 0 && draftSteps.length > 0) return
            const step = { "type": type, "duration_ms": isDefault ? 50 : durationSpin.value }
            if (type !== "wait" && type !== "off") step.brightness = brightnessCombo.currentValue
            if (root.stepIsAnimated(type)) step.speed = speedCombo.currentValue
            const entries = draftSteps.slice()
            if (editingStepIndex >= 0) entries[editingStepIndex] = step
            else entries.push(step)
            draftSteps = isDefault ? entries.slice(0, 1) : entries
            editingStepIndex = -1
        }
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.TextField {
                id: presetName
                Layout.fillWidth: true
                placeholderText: qsTr("Název presetu")
                readOnly: presetDialog.isDefault
            }
            Controls.Label {
                text: presetDialog.isDefault
                    ? qsTr("Default: použije se pouze první krok, bez čekání na jeho trvání.")
                    : qsTr("Pořadí kroků (opakování je možné přidáním stejného kroku):")
                wrapMode: Text.WordWrap
            }
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
                            text: (index + 1) + ". " + root.stepDescription(modelData, presetDialog.isDefault)
                            elide: Text.ElideRight
                        }
                        Controls.Button {
                            text: qsTr("Upravit")
                            onClicked: presetDialog.editStep(index)
                        }
                        Controls.Button {
                            text: "↑"
                            visible: !presetDialog.isDefault
                            enabled: index > 0
                            onClicked: presetDialog.moveStep(index, -1)
                        }
                        Controls.Button {
                            text: "↓"
                            visible: !presetDialog.isDefault
                            enabled: index < presetDialog.draftSteps.length - 1
                            onClicked: presetDialog.moveStep(index, 1)
                        }
                        Controls.Button {
                            text: "×"
                            visible: !presetDialog.isDefault
                            onClicked: presetDialog.removeStep(index)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Controls.Label {
                    text: qsTr("Efekt:")
                    Layout.preferredWidth: presetDialog.fieldLabelWidth
                }
                Controls.ComboBox {
                    id: stepType
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    {label: qsTr("Statické"), value: "static"},
                    {label: qsTr("Vypnout"), value: "off"},
                    {label: qsTr("Vlna zleva →"), value: "left_wave"},
                    {label: qsTr("Vlna zprava ←"), value: "right_wave"},
                    {label: qsTr("Dýchání"), value: "breathe"},
                    {label: qsTr("Reakce na stisk"), value: "ripple"},
                    {label: qsTr("Čekat bez příkazu"), value: "wait"}
                ]
                delegate: Controls.ItemDelegate {
                    width: stepType.width
                    text: modelData.label
                    enabled: !presetDialog.isDefault || modelData.value !== "wait"
                    highlighted: stepType.highlightedIndex === index
                    onClicked: {
                        stepType.currentIndex = index
                        stepType.popup.close()
                    }
                }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: qsTr("Jas:")
                        Layout.preferredWidth: presetDialog.fieldLabelWidth
                    }
                    Controls.ComboBox {
                        id: brightnessCombo
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: 1
                        enabled: stepType.currentValue !== "wait" && stepType.currentValue !== "off"
                        model: [
                            {label: "0", value: 0}, {label: "1", value: 1},
                            {label: "2", value: 2}, {label: "3", value: 3}
                        ]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: qsTr("Rychlost:")
                        Layout.preferredWidth: presetDialog.fieldLabelWidth
                    }
                    Controls.ComboBox {
                        id: speedCombo
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: 1
                        enabled: root.stepIsAnimated(stepType.currentValue)
                        model: [
                            {label: qsTr("Pomalá"), value: 1},
                            {label: qsTr("Střední"), value: 2},
                            {label: qsTr("Rychlá"), value: 3}
                        ]
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Controls.Label {
                    text: qsTr("Trvání:")
                    Layout.preferredWidth: presetDialog.fieldLabelWidth
                    visible: !presetDialog.isDefault
                }
                Controls.SpinBox {
                    id: durationSpin
                    from: 50; to: 60000; value: 200; stepSize: 50
                    visible: !presetDialog.isDefault
                }
                Controls.Button {
                    text: presetDialog.editingStepIndex >= 0 ? qsTr("Uložit krok") : qsTr("Přidat krok")
                    enabled: !presetDialog.isDefault || presetDialog.editingStepIndex >= 0 ||
                             presetDialog.draftSteps.length === 0
                    onClicked: presetDialog.addStep()
                }
                Controls.Button {
                    text: qsTr("Zrušit úpravu")
                    visible: presetDialog.editingStepIndex >= 0 && !presetDialog.isDefault
                    onClicked: presetDialog.editingStepIndex = -1
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Controls.Button { text: qsTr("Zrušit"); onClicked: presetDialog.close() }
                Controls.Button {
                    text: qsTr("Uložit preset")
                    enabled: presetName.text.trim().length > 0 && presetDialog.draftSteps.length > 0
                    onClicked: {
                        if (presetDialog.editingStepIndex >= 0)
                            presetDialog.addStep()
                        if (kcm.savePreset(presetDialog.editingId, presetName.text,
                                           presetDialog.draftSteps)) {
                            root.selectedPresetId = ""
                            presetDialog.close()
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Controls.Label { text: qsTr("Změnit velikost"); color: Kirigami.Theme.disabledTextColor }
                Item {
                    Layout.preferredWidth: Kirigami.Units.gridUnit
                    Layout.preferredHeight: Kirigami.Units.gridUnit
                    Controls.Label { anchors.centerIn: parent; text: "◢" }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeFDiagCursor
                        property real startX: 0
                        property real startY: 0
                        property real startWidth: 0
                        property real startHeight: 0
                        onPressed: (mouse) => {
                            const point = mapToItem(presetDialog.parent, mouse.x, mouse.y)
                            startX = point.x
                            startY = point.y
                            startWidth = presetDialog.width
                            startHeight = presetDialog.height
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return
                            const point = mapToItem(presetDialog.parent, mouse.x, mouse.y)
                            presetDialog.width = Math.max(presetDialog.minimumResizeWidth,
                                Math.min(presetDialog.parent.width - presetDialog.x,
                                         startWidth + point.x - startX))
                            presetDialog.height = Math.max(presetDialog.minimumResizeHeight,
                                Math.min(presetDialog.parent.height - presetDialog.y,
                                         startHeight + point.y - startY))
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
        title: editingId === "" ? qsTr("Nové pravidlo") : qsTr("Upravit pravidlo")
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
                placeholderText: qsTr("Název pravidla")
                Layout.fillWidth: true
            }
            Controls.CheckBox { id: ruleEnabled; text: qsTr("Pravidlo je aktivní"); checked: true }
            Controls.Label { text: qsTr("Podmínky (nerozlišují velikost písmen):") }
            Controls.TextField {
                id: matchApp
                Layout.fillWidth: true
                placeholderText: qsTr("Aplikace obsahuje: Teams")
            }
            Controls.TextField {
                id: matchTitle
                Layout.fillWidth: true
                placeholderText: qsTr("Titulek obsahuje (nepovinné)")
            }
            Controls.TextField {
                id: matchBody
                Layout.fillWidth: true
                placeholderText: qsTr("Zpráva obsahuje: XYZ")
            }
            Controls.Label { text: qsTr("Spustit preset:") }
            Controls.ComboBox {
                id: rulePreset
                Layout.fillWidth: true
                model: kcm.presets
                textRole: "name"
                valueRole: "id"
            }
            RowLayout {
                Controls.Label { text: qsTr("Ochranná prodleva (ms):") }
                Controls.SpinBox {
                    id: cooldown
                    from: 0; to: 3600000; stepSize: 1000; value: 5000
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Controls.Button { text: qsTr("Zrušit"); onClicked: ruleDialog.close() }
                Controls.Button {
                    text: qsTr("Uložit pravidlo")
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
        title: qsTr("Opravdu smazat?")
        property bool isPreset: true
        property string deleteId: ""
        // Explicit implicit width avoids a cycle in the KDE desktop dialog style.
        implicitWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: {
            if (isPreset) {
                if (kcm.deletePreset(deleteId)) root.selectedPresetId = ""
            } else {
                if (kcm.deleteRule(deleteId)) root.selectedRuleId = ""
            }
        }
        Controls.Label {
            text: confirmDialog.isPreset ? qsTr("Odstranit vybraný preset?") : qsTr("Odstranit vybrané pravidlo?")
        }
    }
}
