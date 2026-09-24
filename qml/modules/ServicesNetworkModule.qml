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
        { id: "bluetooth.service", title: qsTr("Bluetooth") },
        { id: "cups.service", title: qsTr("Stampa") },
        { id: "sshd.service", title: qsTr("SSH") },
        { id: "smb.service", title: qsTr("Samba") },
        { id: "firewalld.service", title: qsTr("Firewall") }
    ]
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
                        Layout.fillWidth: true
                        Controls.Label { Layout.fillWidth: true; text: modelData.title }
                        Controls.Label { Layout.preferredWidth: 120; text: SystemBackend.serviceStates[modelData.id] || qsTr("lettura…") }
                        Controls.Button {
                            text: qsTr("Avvia")
                            enabled: SystemBackend.serviceStates[modelData.id] !== "non disponibile"
                            onClicked: { root.pendingService = modelData.id; root.pendingServiceTitle = modelData.title; root.pendingAction = "start"; serviceConfirmDialog.open() }
                        }
                        Controls.Button {
                            text: qsTr("Ferma")
                            enabled: SystemBackend.serviceStates[modelData.id] !== "non disponibile"
                            onClicked: { root.pendingService = modelData.id; root.pendingServiceTitle = modelData.title; root.pendingAction = "stop"; serviceConfirmDialog.open() }
                        }
                        Controls.Button {
                            text: qsTr("Riavvia")
                            enabled: SystemBackend.serviceStates[modelData.id] !== "non disponibile"
                            onClicked: {
                                root.pendingService = modelData.id
                                root.pendingServiceTitle = modelData.title
                                root.pendingAction = "restart"
                                serviceConfirmDialog.open()
                            }
                        }
                        Controls.Button {
                            text: qsTr("Reset")
                            visible: SystemBackend.serviceStates[modelData.id] === "failed"
                            onClicked: {
                                root.pendingService = modelData.id
                                root.pendingServiceTitle = modelData.title
                                root.pendingAction = "reset"
                                serviceConfirmDialog.open()
                            }
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
                Controls.Label { text: qsTr("Interfaccia attiva: %1").arg(SystemBackend.networkInterface || qsTr("nessuna")) }
                Controls.Label { text: qsTr("Indirizzo: %1").arg(SystemBackend.networkAddress || qsTr("non disponibile")) }
                Controls.Button { text: qsTr("Configura IP / profili in NetworkManager"); icon.name: "network-connect"; onClicked: SystemBackend.openNetworkSettings() }
                Controls.Label { text: qsTr("DNS rapido per la connessione attiva"); font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    Controls.Button { text: qsTr("Automatico"); enabled: !repair.busy && SystemBackend.networkInterface.length > 0; onClicked: repair.applyDnsPreset(SystemBackend.networkInterface, "automatic") }
                    Controls.Button { text: qsTr("Cloudflare"); enabled: !repair.busy && SystemBackend.networkInterface.length > 0; onClicked: repair.applyDnsPreset(SystemBackend.networkInterface, "cloudflare") }
                    Controls.Button { text: qsTr("Quad9"); enabled: !repair.busy && SystemBackend.networkInterface.length > 0; onClicked: repair.applyDnsPreset(SystemBackend.networkInterface, "quad9") }
                    Controls.Button { text: qsTr("Google"); enabled: !repair.busy && SystemBackend.networkInterface.length > 0; onClicked: repair.applyDnsPreset(SystemBackend.networkInterface, "google") }
                }
                Controls.Button { text: qsTr("Svuota cache DNS"); enabled: !repair.busy; onClicked: repair.flushDns() }
                Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: repair.output }
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
            text: root.pendingService === "sshd.service"
                  && (root.pendingAction === "stop" || root.pendingAction === "restart")
                  ? qsTr("Attenzione: fermare o riavviare SSH può interrompere immediatamente una sessione remota attiva.")
                  : qsTr("Conferma l'operazione sul servizio selezionato.")
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
