import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Controls.Basic as Basic
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Servizi & Rete")

    RepairBackend { id: repair }

    property string pendingService: ""
    property string pendingAction: ""
    property string pendingServiceTitle: ""

    property string pendingManagedUnit: ""
    property string pendingManagedAction: ""
    property bool pendingManagedUserScope: false

    property string journalUnit: ""
    property bool journalUserScope: false

    property var services: [
        { id: "NetworkManager.service", title: qsTr("Rete") },
        { id: "wifi", title: qsTr("Wi-Fi") },
        { id: "bluetooth.service", title: qsTr("Bluetooth") },
        { id: "cups.service", title: qsTr("Stampa") },
        { id: "firewalld.service", title: qsTr("Firewall") },
        { id: "cockpit.socket", title: qsTr("Cockpit") }
    ]

    function serviceId(item) {
        return item.id
    }

    function stateLabel(state) {
        if (state === "loading") return qsTr("lettura…")
        if (state === "missing") return qsTr("non disponibile")
        if (state === "unknown") return qsTr("sconosciuto")
        if (state === "active") return qsTr("attivo")
        if (state === "activating") return qsTr("avvio…")
        if (state === "inactive") return qsTr("inattivo")
        if (state === "failed") return qsTr("fallito")
        return state || qsTr("sconosciuto")
    }

    function serviceState(item) {
        return SystemBackend.serviceStates[root.serviceId(item)] || "loading"
    }

    function filteredServices() {
        var query = serviceFilter.text.trim().toLowerCase()
        if (query.length === 0)
            return ServiceManagerBackend.services
        var result = []
        for (var i = 0; i < ServiceManagerBackend.services.length; ++i) {
            var row = ServiceManagerBackend.services[i]
            var haystack = (row.unit + " " + row.description + " " + row.active
                            + " " + row.enabled).toLowerCase()
            if (haystack.indexOf(query) >= 0)
                result.push(row)
        }
        return result
    }

    function journalFilteredText() {
        var source = ServiceManagerBackend.journalText || ""
        var query = journalSearch.text.trim().toLowerCase()
        if (query.length === 0)
            return source
        var lines = source.split("\n")
        var result = []
        for (var i = 0; i < lines.length; ++i) {
            if (lines[i].toLowerCase().indexOf(query) >= 0)
                result.push(lines[i])
        }
        return result.join("\n")
    }

    function openJournal(unit, userScope) {
        root.journalUnit = unit
        root.journalUserScope = userScope
        journalUnitField.text = unit
        journalScope.currentIndex = userScope ? 1 : 0
        serviceTabs.currentIndex = 4
        ServiceManagerBackend.loadJournal(unit, userScope, journalPriority.currentValue)
    }

    function refreshCurrentTab() {
        if (serviceTabs.currentIndex === 0) {
            SystemBackend.refreshServiceStates()
        } else if (serviceTabs.currentIndex === 1) {
            ServiceManagerBackend.refreshServices(serviceScope.currentIndex === 1)
        } else if (serviceTabs.currentIndex === 2) {
            ServiceManagerBackend.refreshFailedUnits(failedScope.currentIndex === 1)
        }
    }

    Component.onCompleted: SystemBackend.refreshServiceStates()
    onVisibleChanged: if (visible) refreshCurrentTab()

    Connections {
        target: ServiceManagerBackend
        function onControlFinished(userScope, unit, action, success) {
            if (serviceTabs.currentIndex === 1)
                ServiceManagerBackend.refreshServices(serviceScope.currentIndex === 1)
            else if (serviceTabs.currentIndex === 2)
                ServiceManagerBackend.refreshFailedUnits(failedScope.currentIndex === 1)
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Controls.TabBar {
            id: serviceTabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { text: qsTr("Comuni"); font.bold: true }
            Controls.TabButton { text: qsTr("Tutti i servizi"); font.bold: true }
            Controls.TabButton { text: qsTr("Unità fallite"); font.bold: true }
            Controls.TabButton { text: qsTr("Rete"); font.bold: true }
            Controls.TabButton { text: qsTr("Log"); font.bold: true }
            onCurrentIndexChanged: root.refreshCurrentTab()
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: serviceTabs.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 2
                                font.bold: true
                                text: qsTr("Servizi comuni")
                            }
                            Controls.Button {
                                text: qsTr("Aggiorna")
                                icon.name: "view-refresh"
                                onClicked: SystemBackend.refreshServiceStates()
                            }
                        }

                        Repeater {
                            model: root.services
                            delegate: RowLayout {
                                required property var modelData
                                property string actualService: root.serviceId(modelData)
                                Layout.fillWidth: true

                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                }
                                Controls.Label {
                                    Layout.preferredWidth: 120
                                    text: root.stateLabel(root.serviceState(modelData))
                                }
                                Controls.Button {
                                    text: qsTr("Avvia")
                                    enabled: root.serviceState(modelData) === "inactive"
                                          || root.serviceState(modelData) === "failed"
                                    onClicked: {
                                        root.pendingService = actualService
                                        root.pendingServiceTitle = modelData.title
                                        root.pendingAction = "start"
                                        serviceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Ferma")
                                    enabled: root.serviceState(modelData) === "active"
                                          || root.serviceState(modelData) === "activating"
                                    onClicked: {
                                        root.pendingService = actualService
                                        root.pendingServiceTitle = modelData.title
                                        root.pendingAction = "stop"
                                        serviceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Riavvia")
                                    enabled: root.serviceState(modelData) === "active"
                                    onClicked: {
                                        root.pendingService = actualService
                                        root.pendingServiceTitle = modelData.title
                                        root.pendingAction = "restart"
                                        serviceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Reset")
                                    visible: root.serviceState(modelData) === "failed"
                                    onClicked: {
                                        root.pendingService = actualService
                                        root.pendingServiceTitle = modelData.title
                                        root.pendingAction = "reset"
                                        serviceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    flat: true
                                    icon.name: "utilities-log-viewer"
                                    text: qsTr("Log")
                                    display: Controls.AbstractButton.IconOnly
                                    opacity: actualService === "wifi" ? 0 : 1
                                    enabled: actualService !== "wifi"
                                    Controls.ToolTip.visible: hovered && enabled
                                    Controls.ToolTip.text: text
                                    onClicked: root.openJournal(actualService, false)
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Controls.ComboBox {
                        id: serviceScope
                        model: [qsTr("Sistema"), qsTr("Utente")]
                        onActivated: ServiceManagerBackend.refreshServices(currentIndex === 1)
                    }
                    Controls.TextField {
                        id: serviceFilter
                        Layout.fillWidth: true
                        placeholderText: qsTr("Cerca servizio o descrizione…")
                    }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        enabled: !ServiceManagerBackend.busy
                        onClicked: ServiceManagerBackend.refreshServices(serviceScope.currentIndex === 1)
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: ServiceManagerBackend.message.length > 0
                    type: ServiceManagerBackend.state === "error"
                          ? Kirigami.MessageType.Error
                          : Kirigami.MessageType.Information
                    text: ServiceManagerBackend.message
                }

                Controls.BusyIndicator {
                    visible: ServiceManagerBackend.busy
                    running: visible
                    Layout.alignment: Qt.AlignHCenter
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !ServiceManagerBackend.busy
                          && ServiceManagerBackend.services.length === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessun servizio caricato. Premi Aggiorna.")
                }

                Repeater {
                    model: root.filteredServices()
                    delegate: Kirigami.AbstractCard {
                        required property var modelData
                        Layout.fillWidth: true

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        font.bold: true
                                        text: modelData.unit
                                        elide: Text.ElideRight
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        visible: modelData.description.length > 0
                                        wrapMode: Text.WordWrap
                                        opacity: UiMetrics.secondaryOpacity
                                        text: modelData.description
                                    }
                                }
                                Controls.Label {
                                    text: root.stateLabel(modelData.active)
                                }
                                Controls.Label {
                                    Layout.preferredWidth: 100
                                    opacity: UiMetrics.secondaryOpacity
                                    text: modelData.enabled || qsTr("n/d")
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Controls.Button {
                                    text: qsTr("Avvia")
                                    enabled: !ServiceManagerBackend.busy
                                          && modelData.active !== "active"
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "start"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Ferma")
                                    enabled: !ServiceManagerBackend.busy
                                          && modelData.active === "active"
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "stop"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Riavvia")
                                    enabled: !ServiceManagerBackend.busy
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "restart"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: modelData.enabled === "enabled"
                                          ? qsTr("Disabilita")
                                          : qsTr("Abilita")
                                    enabled: !ServiceManagerBackend.busy
                                          && modelData.enabled !== "static"
                                          && modelData.enabled !== "masked"
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction =
                                            modelData.enabled === "enabled" ? "disable" : "enable"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    visible: modelData.active === "failed"
                                    text: qsTr("Reset failed")
                                    enabled: !ServiceManagerBackend.busy
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "reset-failed"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Log")
                                    icon.name: "utilities-log-viewer"
                                    onClicked: root.openJournal(
                                        modelData.unit, modelData.scope === "user")
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Controls.ComboBox {
                        id: failedScope
                        model: [qsTr("Sistema"), qsTr("Utente")]
                        onActivated: ServiceManagerBackend.refreshFailedUnits(currentIndex === 1)
                    }
                    Item { Layout.fillWidth: true }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        enabled: !ServiceManagerBackend.busy
                        onClicked: ServiceManagerBackend.refreshFailedUnits(failedScope.currentIndex === 1)
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !ServiceManagerBackend.busy
                          && ServiceManagerBackend.failedUnits.length === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessuna unità fallita nelle informazioni correnti.")
                }

                Repeater {
                    model: ServiceManagerBackend.failedUnits
                    delegate: Kirigami.AbstractCard {
                        required property var modelData
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Controls.Label {
                                        font.bold: true
                                        text: modelData.unit
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        opacity: UiMetrics.secondaryOpacity
                                        text: modelData.description
                                    }
                                }
                                Controls.Label {
                                    text: root.stateLabel(modelData.active)
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Controls.Button {
                                    text: qsTr("Log")
                                    icon.name: "utilities-log-viewer"
                                    onClicked: root.openJournal(
                                        modelData.unit, modelData.scope === "user")
                                }
                                Controls.Button {
                                    text: qsTr("Riprova")
                                    enabled: !ServiceManagerBackend.busy
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "restart"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Reset stato")
                                    enabled: !ServiceManagerBackend.busy
                                    onClicked: {
                                        root.pendingManagedUnit = modelData.unit
                                        root.pendingManagedUserScope = modelData.scope === "user"
                                        root.pendingManagedAction = "reset-failed"
                                        managedServiceConfirmDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading {
                            level: 2
                            font.bold: true
                            text: qsTr("Connessione e DNS")
                        }
                        Controls.Label {
                            text: qsTr("Interfaccia attiva: %1").arg(
                                      SystemBackend.networkDisplayName
                                      || SystemBackend.networkInterface
                                      || qsTr("nessuna"))
                        }
                        Controls.Label {
                            text: qsTr("Indirizzo locale: %1").arg(
                                      SystemBackend.networkAddress
                                      || qsTr("non disponibile"))
                        }
                        Controls.Label {
                            text: qsTr("Stato: %1").arg(SystemBackend.networkState)
                            opacity: UiMetrics.secondaryOpacity
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Controls.Button {
                                text: qsTr("Riconnetti interfaccia")
                                icon.name: "view-refresh"
                                enabled: !repair.busy
                                      && SystemBackend.networkInterface.length > 0
                                onClicked: repair.reconnectNetwork(
                                               SystemBackend.networkInterface)
                            }
                            Controls.Button {
                                text: qsTr("Svuota cache DNS")
                                enabled: !repair.busy
                                onClicked: repair.flushDns()
                            }
                            Controls.Button {
                                text: SystemBackend.internetIdentityBusy
                                      ? qsTr("Verifica in corso…")
                                      : qsTr("IP Internet e DNS in uso")
                                icon.name: "network-server"
                                enabled: !SystemBackend.internetIdentityBusy
                                onClicked: SystemBackend.checkInternetIdentity()
                            }
                            Controls.Button {
                                text: qsTr("Configurazione completa")
                                icon.name: "network-connect"
                                onClicked: SystemBackend.openNetworkSettings()
                            }
                        }

                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            visible: SystemBackend.internetIdentity.length > 0
                            type: Kirigami.MessageType.Information
                            text: SystemBackend.internetIdentity
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            visible: repair.output.length > 0
                            wrapMode: Text.WordWrap
                            text: repair.output
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Controls.ComboBox {
                        id: journalScope
                        model: [qsTr("Sistema"), qsTr("Utente")]
                    }
                    Controls.ComboBox {
                        id: journalPriority
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { label: qsTr("Info+"), value: "info" },
                            { label: qsTr("Warning+"), value: "warning" },
                            { label: qsTr("Errori"), value: "err" }
                        ]
                        currentIndex: 1
                    }
                    Controls.TextField {
                        id: journalUnitField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Unità, es. NetworkManager.service (opzionale)")
                    }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        enabled: !ServiceManagerBackend.busy
                        onClicked: {
                            root.journalUnit = journalUnitField.text.trim()
                            root.journalUserScope = journalScope.currentIndex === 1
                            ServiceManagerBackend.loadJournal(
                                root.journalUnit,
                                root.journalUserScope,
                                journalPriority.currentValue)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Controls.TextField {
                        id: journalSearch
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filtra il testo già caricato…")
                    }
                    Controls.Button {
                        text: qsTr("Copia")
                        icon.name: "edit-copy"
                        enabled: ServiceManagerBackend.journalText.length > 0
                        onClicked: SystemBackend.copyToClipboard(root.journalFilteredText())
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: ServiceManagerBackend.message.length > 0
                          && ServiceManagerBackend.journalText.length === 0
                    type: ServiceManagerBackend.state === "error"
                          ? Kirigami.MessageType.Error
                          : Kirigami.MessageType.Information
                    text: ServiceManagerBackend.message
                }

                Controls.ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 520
                    Basic.TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                        font.family: Kirigami.Theme.fixedWidthFont.family
                        text: root.journalFilteredText()
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: serviceConfirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30,
                        parent ? parent.width - Kirigami.Units.largeSpacing * 2
                               : Kirigami.Units.gridUnit * 30)
        title: root.pendingAction === "stop"
               ? qsTr("Fermare %1?").arg(root.pendingServiceTitle)
               : root.pendingAction === "restart"
                 ? qsTr("Riavviare %1?").arg(root.pendingServiceTitle)
                 : root.pendingAction === "reset"
                   ? qsTr("Reimpostare lo stato fallito di %1?").arg(root.pendingServiceTitle)
                   : qsTr("Avviare %1?").arg(root.pendingServiceTitle)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Conferma l'operazione sul servizio selezionato.")
        }
        onAccepted: {
            if (root.pendingAction === "stop")
                SystemBackend.stopService(root.pendingService)
            else if (root.pendingAction === "restart")
                SystemBackend.restartService(root.pendingService)
            else if (root.pendingAction === "reset")
                SystemBackend.resetFailedService(root.pendingService)
            else
                SystemBackend.startService(root.pendingService)
        }
    }

    Controls.Dialog {
        id: managedServiceConfirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 32,
                        parent ? parent.width - Kirigami.Units.largeSpacing * 2
                               : Kirigami.Units.gridUnit * 32)
        title: qsTr("Confermare l'operazione?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("%1 su %2 (%3).")
                  .arg(root.pendingManagedAction)
                  .arg(root.pendingManagedUnit)
                  .arg(root.pendingManagedUserScope ? qsTr("utente") : qsTr("sistema"))
        }
        onAccepted: ServiceManagerBackend.controlUnit(
                        root.pendingManagedUnit,
                        root.pendingManagedUserScope,
                        root.pendingManagedAction)
    }
}
