import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Dashboard")
    signal openRequested(string pageId)

    function overlayLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        return RkBackend.overlayState === "ready" ? qsTr("Pronto") : qsTr("Degradato")
    }

    function syncLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        if (RkBackend.pendingRecovery) return qsTr("Riavvio richiesto")
        return RkBackend.needsSync ? qsTr("Da sincronizzare") : qsTr("Allineato")
    }

    function statusTitle(kind) {
        if (kind === "overlay") return qsTr("Overlay /usr")
        if (kind === "selinux") return qsTr("SELinux")
        if (kind === "sync") return qsTr("Sincronizzazione")
        return qsTr("Spazio disco")
    }

    function statusValue(kind) {
        if (kind === "overlay") return root.overlayLabel()
        if (kind === "selinux") return SystemBackend.selinuxState
        if (kind === "sync") return root.syncLabel()
        return SystemBackend.storageSummary
    }

    function statusDetail(kind) {
        if (kind === "overlay")
            return RkBackend.statusValid && RkBackend.overlayState === "ready"
                   ? qsTr("Layer RPM operativo") : qsTr("Controlla Recovery")
        if (kind === "selinux") return qsTr("Protezione del sistema")
        if (kind === "sync")
            return RkBackend.statusValid
                   ? qsTr("%1 richieste persistenti").arg(RkBackend.requests.length)
                   : qsTr("Stato rk non disponibile")
        return qsTr("Storage dati")
    }

    function statusIcon(kind) {
        if (kind === "overlay") return "drive-multidisk"
        if (kind === "selinux") return "security-high"
        if (kind === "sync") return "view-refresh"
        return "drive-harddisk"
    }

    function moduleValue(id) {
        if (id === "software")
            return qsTr("%1 RPM persistenti").arg(BootcBackend.persistentPackageCount)
        if (id === "flatpak")
            return SystemBackend.programAvailable("flatpak") ? qsTr("Flatpak disponibile") : qsTr("Non disponibile")
        if (id === "podman")
            return SystemBackend.programAvailable("podman") ? qsTr("Podman disponibile") : qsTr("Non disponibile")
        return SystemBackend.osName
    }

    Component.onCompleted: {
        RkBackend.refreshStatus()
        BootcBackend.refreshStatus()
        BootcBackend.refreshPackages()
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing
        RowLayout {
            Layout.fillWidth: true
            PageIntro { title: qsTr("Dashboard"); subtitle: qsTr("Panoramica rapida sullo stato di KrisOS") }
            Controls.Button {
                text: qsTr("Aggiorna stato"); icon.name: "view-refresh"
                enabled: !RkBackend.busy && !BootcBackend.busy
                onClicked: { RkBackend.refreshStatus(); BootcBackend.refreshStatus(); BootcBackend.refreshPackages() }
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !BootcBackend.bootcAvailable || RkBackend.errorText.length > 0
            type: Kirigami.MessageType.Warning
            text: !BootcBackend.bootcAvailable ? qsTr("bootc non è disponibile in questo ambiente.") : RkBackend.errorText
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width > 960 ? 4 : width > 520 ? 2 : 1
            uniformCellWidths: true
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: ["overlay", "selinux", "sync", "storage"]
                delegate: Kirigami.AbstractCard {
                    required property string modelData
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            Kirigami.Icon { source: root.statusIcon(modelData); Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: Layout.preferredWidth }
                            Kirigami.Heading { Layout.fillWidth: true; level: 3; font.bold: true; text: root.statusTitle(modelData); wrapMode: Text.WordWrap }
                        }
                        Controls.Label { Layout.fillWidth: true; text: root.statusValue(modelData); font.bold: false; wrapMode: Text.WordWrap }
                        Controls.Label { Layout.fillWidth: true; text: root.statusDetail(modelData); opacity: UiMetrics.secondaryOpacity; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width > 700 ? 3 : 1
            uniformCellWidths: true
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: ["cpu", "ram", "temp"]
                delegate: Kirigami.AbstractCard {
                    required property string modelData
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            level: 3; font.bold: true
                            text: modelData === "cpu" ? qsTr("CPU") : modelData === "ram" ? qsTr("RAM usata") : qsTr("Temperatura CPU")
                        }
                        Controls.Label {
                            font.bold: false
                            text: modelData === "cpu" ? (SystemBackend.cpuUsagePercent >= 0 ? qsTr("%1%").arg(SystemBackend.cpuUsagePercent) : qsTr("Campionamento…"))
                                : modelData === "ram" ? (SystemBackend.memoryUsedMiB >= 0 ? qsTr("%1 MiB").arg(SystemBackend.memoryUsedMiB) : qsTr("Non disponibile"))
                                : (SystemBackend.cpuTemperatureC >= 0 ? qsTr("%1 °C").arg(SystemBackend.cpuTemperatureC.toFixed(0)) : qsTr("Non disponibile"))
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            visible: modelData === "ram"
                            text: SystemBackend.memoryTotalMiB >= 0 ? qsTr("su %1 MiB · swap esclusa").arg(SystemBackend.memoryTotalMiB) : qsTr("swap esclusa")
                            opacity: UiMetrics.secondaryOpacity; wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width > 960 ? 4 : width > 520 ? 2 : 1
            uniformCellWidths: true
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: [
                    { id: "software", title: qsTr("Software"), icon: "package-x-generic" },
                    { id: "flatpak", title: qsTr("Flatpak"), icon: "applications-all" },
                    { id: "podman", title: qsTr("Container"), icon: "package" },
                    { id: "system", title: qsTr("Sistema"), icon: "computer" }
                ]
                delegate: Kirigami.AbstractCard {
                    required property var modelData
                    Layout.fillWidth: true
                    onClicked: root.openRequested(modelData.id)
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.medium; Layout.preferredHeight: Layout.preferredWidth }
                        Kirigami.Heading { level: 2; font.bold: true; text: modelData.title }
                        Controls.Label { Layout.fillWidth: true; text: root.moduleValue(modelData.id); font.bold: false; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Azioni rapide") }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Controls.Button { text: qsTr("Svuota cestini"); icon.name: "user-trash"; enabled: MaintenanceBackend.available && !MaintenanceBackend.running; onClicked: quickTrashDialog.open() }
                    Controls.Button { text: qsTr("Backup e Recovery"); icon.name: "document-save-all"; onClicked: root.openRequested("recovery") }
                    Controls.Button { text: qsTr("Terminale"); icon.name: "utilities-terminal"; onClicked: SystemBackend.launchTool("konsole") }
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: MaintenanceBackend.resultState !== "idle"
                    type: MaintenanceBackend.resultState === "error" ? Kirigami.MessageType.Error : MaintenanceBackend.resultState === "success" ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information
                    text: MaintenanceBackend.running ? qsTr("Pulizia in corso…") : MaintenanceBackend.output
                }
            }
        }
    }
    Controls.Dialog {
        id: quickTrashDialog
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        modal: true
        title: qsTr("Svuotare i cestini?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { text: qsTr("Elimina definitivamente gli elementi nei cestini del tuo utente, nella home e nelle partizioni montate supportate. L’operazione non può essere annullata."); wrapMode: Text.WordWrap }
        onAccepted: MaintenanceBackend.cleanTrash("all")
    }
}
