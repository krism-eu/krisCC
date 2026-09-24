import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Strumenti & Fix")
    UtilityBackend { id: utility }
    RepairBackend { id: repair }
    property var cleanupQueue: []
    property int cleanupTotal: 0
    property int cleanupDone: 0

    function runNextCleanup() {
        if (utility.busy || MaintenanceBackend.running || cleanupQueue.length === 0)
            return
        var id = cleanupQueue.shift()
        if (id === "trash")
            MaintenanceBackend.cleanTrash("all")
        else
            utility.runBookmark(id)
    }

    function cleanupStepFinished() {
        if (cleanupTotal > 0 && cleanupDone < cleanupTotal)
            cleanupDone++
        if (cleanupQueue.length > 0)
            Qt.callLater(root.runNextCleanup)
    }

    Connections {
        target: utility
        function onStateChanged() {
            if (!utility.busy && root.cleanupTotal > 0)
                root.cleanupStepFinished()
        }
    }
    Connections {
        target: MaintenanceBackend
        function onFinished(success, output) {
            if (root.cleanupTotal > 0)
                root.cleanupStepFinished()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        GridLayout {
            Layout.fillWidth: true
            columns: width > 760 ? 2 : 1
            uniformCellWidths: true
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; text: qsTr("Riparatore Audio"); font.bold: true }
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Riavvia PipeWire, PipeWire Pulse e WirePlumber nella sessione utente.") }
                    Controls.Button { text: qsTr("Ripristina stack audio"); icon.name: "audio-volume-high"; enabled: !repair.busy; onClicked: repair.restartAudio() }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; text: qsTr("Rete & DNS"); font.bold: true }
                    Controls.Label { text: qsTr("Interfaccia: %1").arg(SystemBackend.networkInterface || qsTr("non disponibile")) }
                    Controls.Button { text: qsTr("Svuota cache DNS"); enabled: !repair.busy; onClicked: repair.flushDns() }
                    Controls.Button { text: qsTr("Riapplica connessione attiva"); enabled: !repair.busy && SystemBackend.networkInterface.length > 0; onClicked: repair.reconnectNetwork(SystemBackend.networkInterface) }
                    Controls.Button { text: qsTr("Apri NetworkManager"); icon.name: "network-connect"; onClicked: SystemBackend.openNetworkSettings() }
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                Kirigami.Heading { level: 2; text: qsTr("Pulizia disco unificata"); font.bold: true }
                Controls.CheckBox { id: trash; text: qsTr("Cestini utente e volumi"); checked: true }
                Controls.CheckBox { id: journal; text: qsTr("Journal oltre 100 MiB"); checked: true }
                Controls.CheckBox { id: dnf; text: qsTr("Cache DNF5"); checked: true }
                Controls.CheckBox { id: flatpak; text: qsTr("Runtime Flatpak inutilizzati"); checked: true; enabled: SystemBackend.programAvailable("flatpak") }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Button {
                        text: qsTr("Stima spazio")
                        icon.name: "drive-harddisk"
                        enabled: !utility.busy && !MaintenanceBackend.running
                        onClicked: utility.runBookmark("cleanup-estimate")
                    }
                    Controls.Button {
                        text: qsTr("Avvia pulizia selezionata")
                        icon.name: "edit-clear"
                        enabled: !utility.busy && !MaintenanceBackend.running
                        onClicked: {
                            root.cleanupQueue = []
                            root.cleanupDone = 0
                            if (trash.checked) root.cleanupQueue.push("trash")
                            if (journal.checked) root.cleanupQueue.push("journal-vacuum")
                            if (dnf.checked) root.cleanupQueue.push("dnf-clean")
                            if (flatpak.checked) root.cleanupQueue.push("flatpak-unused")
                            root.cleanupTotal = root.cleanupQueue.length
                            root.runNextCleanup()
                        }
                    }
                }
                Controls.ProgressBar {
                    Layout.fillWidth: true
                    visible: root.cleanupTotal > 0
                    from: 0
                    to: Math.max(1, root.cleanupTotal)
                    value: root.cleanupDone
                }
                Controls.Label {
                    visible: root.cleanupTotal > 0
                    text: qsTr("%1 / %2 operazioni completate").arg(root.cleanupDone).arg(root.cleanupTotal)
                    opacity: UiMetrics.secondaryOpacity
                }
                Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: utility.output.length > 0 ? utility.output : MaintenanceBackend.output }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                Kirigami.Heading { level: 2; text: qsTr("Diagnostica hardware rapida"); font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    Controls.Button { text: qsTr("VA-API"); enabled: !utility.busy; onClicked: utility.runBookmark("vainfo") }
                    Controls.Button { text: qsTr("GPU / Mesa"); enabled: !utility.busy; onClicked: utility.runBookmark("gpu-driver") }
                }
                OutputCard { embedded: true; outputText: utility.output }
            }
        }
    }
}
