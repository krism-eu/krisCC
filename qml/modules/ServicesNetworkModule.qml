import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Servizi & Rete")
    RepairBackend { id: repair }
    UtilityBackend { id: diagnostic }
    property string pendingService: ""
    property string pendingAction: ""
    property string pendingServiceTitle: ""
    property var services: [
        { id: "NetworkManager.service", title: qsTr("Rete") },
        { id: "wifi", title: qsTr("Wi-Fi") },
        { id: "bluetooth.service", title: qsTr("Bluetooth") },
        { id: "cups.service", title: qsTr("Stampa") },
        { id: "firewalld.service", title: qsTr("Firewall") }
    ]

    function serviceId(item) {
        if (item.id !== "wifi")
            return item.id
        var iwd = SystemBackend.serviceStates["iwd.service"] || "missing"
        var wpa = SystemBackend.serviceStates["wpa_supplicant.service"] || "missing"
        if (iwd === "active" || iwd === "activating")
            return "iwd.service"
        if (wpa === "active" || wpa === "activating")
            return "wpa_supplicant.service"
        return iwd !== "missing" ? "iwd.service" : "wpa_supplicant.service"
    }

    function stateLabel(state) {
        if (state === "loading") return qsTr("lettura…")
        if (state === "missing") return qsTr("non disponibile")
        if (state === "unknown") return qsTr("sconosciuto")
        if (state === "active") return qsTr("attivo")
        if (state === "activating") return qsTr("avvio…")
        if (state === "inactive") return qsTr("inattivo")
        if (state === "failed") return qsTr("fallito")
        return state
    }

    function serviceState(item) {
        return SystemBackend.serviceStates[root.serviceId(item)] || "loading"
    }

    Component.onCompleted: SystemBackend.refreshServiceStates()
    onVisibleChanged: if (visible) SystemBackend.refreshServiceStates()

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Servizi comuni") }
                Repeater {
                    model: root.services
                    delegate: RowLayout {
                        required property var modelData
                        property string actualService: root.serviceId(modelData)
                        Layout.fillWidth: true
                        Controls.Label { Layout.fillWidth: true; text: modelData.title }
                        Controls.Label { Layout.preferredWidth: 120; text: root.stateLabel(root.serviceState(modelData)) }
                        Controls.Button {
                            text: qsTr("Avvia")
                            enabled: root.serviceState(modelData) !== "missing"
                            onClicked: { root.pendingService = actualService; root.pendingServiceTitle = modelData.title; root.pendingAction = "start"; serviceConfirmDialog.open() }
                        }
                        Controls.Button {
                            text: qsTr("Ferma")
                            enabled: root.serviceState(modelData) !== "missing"
                            onClicked: { root.pendingService = actualService; root.pendingServiceTitle = modelData.title; root.pendingAction = "stop"; serviceConfirmDialog.open() }
                        }
                        Controls.Button {
                            text: qsTr("Riavvia")
                            enabled: root.serviceState(modelData) !== "missing"
                            onClicked: { root.pendingService = actualService; root.pendingServiceTitle = modelData.title; root.pendingAction = "restart"; serviceConfirmDialog.open() }
                        }
                        Controls.Button {
                            text: qsTr("Reset")
                            visible: root.serviceState(modelData) === "failed"
                            onClicked: { root.pendingService = actualService; root.pendingServiceTitle = modelData.title; root.pendingAction = "reset"; serviceConfirmDialog.open() }
                        }
                    }
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("Unità fallite") }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        enabled: !diagnostic.busy
                        onClicked: diagnostic.runBookmark("failed-units")
                    }
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: diagnostic.output.length > 0 ? diagnostic.output : qsTr("Premi Aggiorna per controllare le unità systemd in stato failed.")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Connessione e DNS") }
                Controls.Label { text: qsTr("Interfaccia attiva: %1").arg(SystemBackend.networkDisplayName || SystemBackend.networkInterface || qsTr("nessuna")) }
                Controls.Label { text: qsTr("Indirizzo locale: %1").arg(SystemBackend.networkAddress || qsTr("non disponibile")) }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Controls.Button {
                        text: qsTr("Apri NetworkManager")
                        icon.name: "network-connect"
                        onClicked: SystemBackend.openNetworkSettings()
                    }
                    Controls.Button {
                        text: qsTr("Svuota cache DNS")
                        enabled: !repair.busy
                        onClicked: repair.flushDns()
                    }
                    Controls.Button {
                        text: SystemBackend.internetIdentityBusy ? qsTr("Verifica in corso…") : qsTr("IP Internet e DNS in uso")
                        icon.name: "network-server"
                        enabled: !SystemBackend.internetIdentityBusy
                        onClicked: SystemBackend.checkInternetIdentity()
                    }
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: SystemBackend.internetIdentity.length > 0
                    type: Kirigami.MessageType.Information
                    text: SystemBackend.internetIdentity
                }
                Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: repair.output.length > 0; text: repair.output }
            }
        }
    }

    Controls.Dialog {
        id: serviceConfirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
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
}
