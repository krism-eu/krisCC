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
                        Controls.Button { text: qsTr("Avvia"); onClicked: SystemBackend.startService(modelData.id) }
                        Controls.Button { text: qsTr("Ferma"); onClicked: SystemBackend.stopService(modelData.id) }
                        Controls.Button { text: qsTr("Riavvia"); onClicked: SystemBackend.restartService(modelData.id) }
                        Controls.Button { text: qsTr("Reset"); visible: SystemBackend.serviceStates[modelData.id] === "failed"; onClicked: SystemBackend.resetFailedService(modelData.id) }
                    }
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
}
