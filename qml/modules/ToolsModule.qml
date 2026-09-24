import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Templates as Templates
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
    property string cleanupErrors: ""
    property string cleanupCurrent: ""

    function runNextCleanup() {
        if (utility.busy || MaintenanceBackend.running || cleanupQueue.length === 0)
            return
        var id = cleanupQueue.shift()
        cleanupCurrent = id
        var started = true
        if (id === "trash")
            started = MaintenanceBackend.cleanTrash("all")
        else if (id === "journal-vacuum")
            started = SystemBackend.vacuumJournal()
        else if (id === "dnf-clean")
            started = SystemBackend.cleanDnfCache()
        else
            started = utility.runBookmark(id)
        if (!started) {
            cleanupErrors += (cleanupErrors.length ? "\n" : "") + id + ": " + qsTr("impossibile avviare")
            Qt.callLater(root.cleanupStepFinished)
        }
    }

    function cleanupStepFinished() {
        if (cleanupTotal > 0 && cleanupDone < cleanupTotal)
            cleanupDone++
        if (cleanupQueue.length > 0) {
            Qt.callLater(root.runNextCleanup)
        } else {
            cleanupCurrent = ""
            cleanupTotal = 0
            cleanupDone = 0
        }
    }

    Connections {
        target: utility
        function onStateChanged() {
            if (!utility.busy && root.cleanupTotal > 0 && root.cleanupCurrent.length > 0
                    && root.cleanupCurrent !== "trash" && root.cleanupCurrent !== "journal-vacuum" && root.cleanupCurrent !== "dnf-clean") {
                if (utility.resultState !== "success")
                    root.cleanupErrors += (root.cleanupErrors.length ? "\n" : "") + root.cleanupCurrent + ": " + utility.output
                root.cleanupCurrent = ""
                root.cleanupStepFinished()
            }
        }
    }
    Connections {
        target: MaintenanceBackend
        function onFinished(success, output) {
            if (root.cleanupTotal > 0 && root.cleanupCurrent === "trash") {
                if (!success) root.cleanupErrors += (root.cleanupErrors.length ? "\n" : "") + "trash: " + output
                root.cleanupCurrent = ""
                root.cleanupStepFinished()
            }
        }
    }
    Connections {
        target: SystemBackend
        function onAdminMaintenanceFinished(operation, success, output) {
            if (root.cleanupTotal > 0 && root.cleanupCurrent === operation) {
                if (!success) root.cleanupErrors += (root.cleanupErrors.length ? "\n" : "") + operation + ": " + output
                root.cleanupCurrent = ""
                root.cleanupStepFinished()
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Controls.TabBar {
            id: toolTabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { text: qsTr("Riparazione & Pulizia"); font.bold: true }
            Controls.TabButton { text: qsTr("Diagnostica & Strumenti"); font.bold: true }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: toolTabs.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; text: qsTr("Riparatore Audio"); font.bold: true }
                        Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Riavvia PipeWire, PipeWire Pulse e WirePlumber nella sessione utente.") }
                        Controls.Button { text: qsTr("Ripristina stack audio"); icon.name: "audio-volume-high"; enabled: !repair.busy; onClicked: repair.restartAudio() }
                        Controls.Label { Layout.fillWidth: true; visible: repair.output.length > 0; wrapMode: Text.WordWrap; text: repair.output }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; text: qsTr("Pulizia disco unificata"); font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 700 ? 2 : 1
                            Controls.CheckBox { id: trash; text: qsTr("Cestini utente e volumi"); checked: true }
                            Controls.CheckBox { id: journal; text: qsTr("Journal archiviati oltre 16 MiB"); checked: true }
                            Controls.CheckBox { id: dnf; text: qsTr("Cache DNF5"); checked: true }
                            Controls.CheckBox { id: flatpak; text: qsTr("Runtime Flatpak inutilizzati"); checked: true; enabled: SystemBackend.programAvailable("flatpak") }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Button { text: qsTr("Stima spazio"); icon.name: "drive-harddisk"; enabled: !utility.busy && !MaintenanceBackend.running; onClicked: utility.runBookmark("cleanup-estimate") }
                            Controls.Button {
                                text: qsTr("Avvia pulizia selezionata")
                                icon.name: "edit-clear"
                                enabled: !utility.busy && !MaintenanceBackend.running && (trash.checked || journal.checked || dnf.checked || flatpak.checked)
                                onClicked: cleanupConfirmDialog.open()
                            }
                            Controls.Button { text: qsTr("Solo cestino home"); icon.name: "user-trash"; enabled: MaintenanceBackend.available && !MaintenanceBackend.running; onClicked: { trashScopeDialog.scope = "home"; trashScopeDialog.open() } }
                            Controls.Button { text: qsTr("Cestini altre partizioni"); icon.name: "drive-harddisk"; enabled: MaintenanceBackend.available && !MaintenanceBackend.running; onClicked: { trashScopeDialog.scope = "system"; trashScopeDialog.open() } }
                            Controls.Button { text: qsTr("RPM non necessari"); icon.name: "edit-find"; enabled: SystemBackend.programAvailable("dnf5") && !utility.busy; onClicked: utility.runBookmark("unneeded-rpms") }
                        }
                        Controls.ProgressBar { Layout.fillWidth: true; visible: root.cleanupTotal > 0; from: 0; to: Math.max(1, root.cleanupTotal); value: root.cleanupDone }
                        Kirigami.InlineMessage { Layout.fillWidth: true; visible: root.cleanupErrors.length > 0; type: Kirigami.MessageType.Warning; text: qsTr("Pulizia parziale:\n") + root.cleanupErrors }
                        Controls.Label { Layout.fillWidth: true; visible: utility.output.length > 0 || MaintenanceBackend.output.length > 0; wrapMode: Text.WordWrap; text: utility.output.length > 0 ? utility.output : MaintenanceBackend.output }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 760 ? 2 : 1
                    uniformCellWidths: true
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; text: qsTr("Diagnostica hardware rapida"); font.bold: true }
                            Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: UiMetrics.secondaryOpacity; text: qsTr("Controlli leggibili su GPU e stack grafico. VA-API è stato rimosso perché non affidabile su tutte le GPU/configurazioni.") }
                            Flow {
                                Layout.fillWidth: true
                                Controls.Button { text: qsTr("GPU / Mesa"); enabled: !utility.busy && SystemBackend.programAvailable("glxinfo"); onClicked: utility.runBookmark("gpu-driver") }
                                Controls.Button { text: qsTr("Vulkan"); enabled: !utility.busy && SystemBackend.programAvailable("vulkaninfo"); onClicked: utility.runBookmark("vulkan-info") }
                            }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; text: qsTr("Controlli di sistema"); font.bold: true }
                            Flow {
                                Layout.fillWidth: true
                                Controls.Button { text: qsTr("Sicurezza"); enabled: !utility.busy; onClicked: utility.runBookmark("security") }
                                Controls.Button { text: qsTr("Errori avvio"); enabled: !utility.busy; onClicked: utility.runBookmark("journal-errors") }
                                Controls.Button { text: qsTr("Warning kernel"); enabled: !utility.busy; onClicked: utility.runBookmark("kernel-errors") }
                                Controls.Button { text: qsTr("Spazio"); enabled: !utility.busy; onClicked: utility.runBookmark("disk-space") }
                                Controls.Button { text: qsTr("Inode"); enabled: !utility.busy; onClicked: utility.runBookmark("inodes") }
                                Controls.Button { text: qsTr("Tempo avvio"); enabled: !utility.busy; onClicked: utility.runBookmark("boot-time") }
                            }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; text: qsTr("Strumenti esterni"); font.bold: true }
                        Flow {
                            Layout.fillWidth: true
                            Controls.Button { text: qsTr("KSystemLog"); icon.name: "utilities-log-viewer"; enabled: SystemBackend.toolAvailable("ksystemlog"); onClicked: SystemBackend.launchTool("ksystemlog") }
                            Controls.Button { text: qsTr("Monitor di sistema"); icon.name: "utilities-system-monitor"; enabled: SystemBackend.toolAvailable("systemmonitor"); onClicked: SystemBackend.launchTool("systemmonitor") }
                            Controls.Button { text: qsTr("ISO Image Writer"); icon.name: "media-optical"; enabled: SystemBackend.toolAvailable("isoimagewriter"); onClicked: SystemBackend.launchTool("isoimagewriter") }
                            Controls.Button { text: qsTr("QDirStat"); icon.name: "folder-chart"; enabled: SystemBackend.toolAvailable("qdirstat"); onClicked: SystemBackend.launchTool("qdirstat") }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utility.output.length > 0
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading { Layout.fillWidth: true; level: 3; text: utility.title; font.bold: true }
                            Controls.Button { text: qsTr("Copia"); icon.name: "edit-copy"; onClicked: SystemBackend.copyToClipboard(utility.output) }
                        }
                        Controls.ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
                            contentWidth: availableWidth
                            Templates.TextArea {
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                font: Kirigami.Theme.fixedWidthFont
                                text: utility.output
                            }
                        }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: cleanupConfirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Avviare la pulizia selezionata?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        function selectedList() {
            var items = []
            if (trash.checked) items.push(qsTr("Cestini utente e volumi"))
            if (journal.checked) items.push(qsTr("Journal archiviati oltre 16 MiB"))
            if (dnf.checked) items.push(qsTr("Cache DNF5"))
            if (flatpak.checked) items.push(qsTr("Runtime Flatpak inutilizzati"))
            return items.join("\n• ")
        }
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Verranno eseguite le seguenti operazioni:\n• %1").arg(cleanupConfirmDialog.selectedList())
                  + (trash.checked ? qsTr("\n\nLa pulizia dei cestini è irreversibile.") : "")
        }
        onAccepted: {
            root.cleanupQueue = []
            root.cleanupDone = 0
            root.cleanupErrors = ""
            root.cleanupCurrent = ""
            if (trash.checked) root.cleanupQueue.push("trash")
            if (journal.checked) root.cleanupQueue.push("journal-vacuum")
            if (dnf.checked) root.cleanupQueue.push("dnf-clean")
            if (flatpak.checked) root.cleanupQueue.push("flatpak-unused")
            root.cleanupTotal = root.cleanupQueue.length
            root.runNextCleanup()
        }
    }

    Controls.Dialog {
        id: trashScopeDialog
        property string scope: "home"
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Svuotare i cestini selezionati?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { wrapMode: Text.WordWrap; text: qsTr("L'operazione elimina definitivamente gli elementi dal cestino selezionato.") }
        onAccepted: MaintenanceBackend.cleanTrash(scope)
    }
}
