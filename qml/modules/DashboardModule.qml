import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Dashboard")
    signal openRequested(string pageId)

    function refreshDashboard() {
        SystemBackend.refreshDashboardState()
        RkBackend.refreshStatus()
        BootcBackend.refreshStatus()
        BootcBackend.refreshPackages()
    }

    onVisibleChanged: if (visible) root.refreshDashboard()
    Component.onCompleted: if (visible) root.refreshDashboard()

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

    function firewallLabel() {
        var state = SystemBackend.serviceStates["firewalld.service"] || ""
        if (state === "active") return qsTr("Attivo")
        if (state === "inactive" || state === "failed") return qsTr("Non attivo")
        return state.length > 0 ? state : qsTr("Verifica…")
    }

    function statusTitle(kind) {
        if (kind === "overlay") return qsTr("Overlay /usr")
        if (kind === "selinux") return qsTr("SELinux")
        if (kind === "sync") return qsTr("Sincronizzazione")
        if (kind === "firewall") return qsTr("Firewall")
        if (kind === "network") return qsTr("Rete")
        return qsTr("Spazio disco")
    }

    function statusValue(kind) {
        if (kind === "overlay") return root.overlayLabel()
        if (kind === "selinux") return SystemBackend.selinuxState
        if (kind === "sync") return root.syncLabel()
        if (kind === "firewall") return root.firewallLabel()
        if (kind === "network") {
            if (SystemBackend.networkState === "ipv4" || SystemBackend.networkState === "ipv6")
                return qsTr("Connessa")
            if (SystemBackend.networkState === "up")
                return qsTr("Interfaccia attiva")
            return qsTr("Non connessa")
        }
        return SystemBackend.storageSummary
    }

    function statusDetail(kind) {
        if (kind === "overlay")
            return RkBackend.statusValid && RkBackend.overlayState === "ready"
                   ? qsTr("Layer RPM operativo") : qsTr("Controlla Recovery")
        if (kind === "selinux") return qsTr("Protezione del sistema")
        if (kind === "sync") {
            if (!RkBackend.statusValid)
                return qsTr("Stato rk non disponibile")
            var syncCount = RkBackend.requests.length
            return syncCount === 1
                   ? qsTr("1 richiesta persistente")
                   : qsTr("%1 richieste persistenti").arg(syncCount)
        }
        if (kind === "firewall") return qsTr("firewalld")
        if (kind === "network") {
            if (SystemBackend.networkInterface.length === 0)
                return qsTr("Nessuna interfaccia attiva")
            if (SystemBackend.networkAddress.length === 0)
                return SystemBackend.networkInterface + qsTr(" · nessun IP")
            return SystemBackend.networkInterface + " · " + SystemBackend.networkAddress
        }
        return qsTr("Storage dati")
    }

    function statusIcon(kind) {
        if (kind === "overlay") return "drive-multidisk"
        if (kind === "selinux") return "security-high"
        if (kind === "sync") return "view-refresh"
        if (kind === "firewall") return "security-medium"
        if (kind === "network")
            return SystemBackend.networkKind === "wifi" ? "network-wireless" : "network-wired"
        return "drive-harddisk"
    }

    function moduleValue(id) {
        if (id === "software") {
            var rpmCount = BootcBackend.persistentPackageCount
            return rpmCount === 1
                   ? qsTr("1 RPM persistente")
                   : qsTr("%1 RPM persistenti").arg(rpmCount)
        }
        if (id === "flatpak")
            return SystemBackend.programAvailable("flatpak") ? qsTr("Flatpak disponibile") : qsTr("Non disponibile")
        if (id === "podman")
            return SystemBackend.programAvailable("podman") ? qsTr("Podman disponibile") : qsTr("Non disponibile")
        return SystemBackend.osName
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Controls.Button {
                text: qsTr("Aggiorna stato")
                icon.name: "view-refresh"
                enabled: !RkBackend.busy && !BootcBackend.busy
                onClicked: root.refreshDashboard()
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
            columns: width > 1200 ? 6 : width > 760 ? 3 : width > 520 ? 2 : 1
            uniformCellWidths: true
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: ["overlay", "selinux", "sync", "storage", "firewall", "network"]
                delegate: Kirigami.AbstractCard {
                    required property string modelData
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            Item {
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Layout.preferredWidth
                                Kirigami.Icon {
                                    anchors.fill: parent
                                    source: root.statusIcon(modelData)
                                }
                                Rectangle {
                                    visible: modelData === "network"
                                    width: Kirigami.Units.smallSpacing + 2
                                    height: width
                                    radius: width / 2
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    border.width: 1
                                    border.color: Kirigami.Theme.backgroundColor
                                    color: SystemBackend.networkState === "ipv4"
                                           ? "#2ecc71"
                                           : SystemBackend.networkState === "ipv6"
                                             ? "#3498db"
                                             : SystemBackend.networkState === "up"
                                               ? "#f39c12"
                                               : "#e74c3c"
                                }
                            }
                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 3
                                font.bold: true
                                text: root.statusTitle(modelData)
                                wrapMode: Text.WordWrap
                            }
                        }
                        Controls.Label { Layout.fillWidth: true; text: root.statusValue(modelData); font.bold: false; wrapMode: Text.WordWrap }
                        Controls.Label { Layout.fillWidth: true; text: root.statusDetail(modelData); opacity: UiMetrics.secondaryOpacity; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.horizontalStretchFactor: 2
                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Kirigami.Icon {
                            source: "cpu"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Layout.preferredWidth
                        }
                        Kirigami.Heading {
                            level: 3
                            font.bold: true
                            text: qsTr("CPU")
                        }
                        Item { Layout.fillWidth: true }
                        Controls.Label {
                            text: SystemBackend.cpuUsagePercent >= 0 ? qsTr("%1%").arg(SystemBackend.cpuUsagePercent) : qsTr("Campionamento…")
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        Kirigami.Icon {
                            source: "temperature-normal"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Layout.preferredWidth
                        }
                        Kirigami.Heading {
                            level: 3
                            font.bold: true
                            text: qsTr("Temperatura CPU")
                        }
                        Item { Layout.fillWidth: true }
                        Controls.Label {
                            text: SystemBackend.cpuTemperatureC >= 0 ? qsTr("%1 °C").arg(SystemBackend.cpuTemperatureC.toFixed(0)) : qsTr("Non disponibile")
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.horizontalStretchFactor: 2
                contentItem: ColumnLayout {
                    RowLayout {
                        Kirigami.Icon { source: "memory"; Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: Layout.preferredWidth }
                        Kirigami.Heading { level: 3; font.bold: true; text: qsTr("RAM usata") }
                    }
                    Controls.Label {
                        text: SystemBackend.memoryUsedMiB >= 0 ? qsTr("%1 MiB").arg(SystemBackend.memoryUsedMiB) : qsTr("Non disponibile")
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: SystemBackend.memoryTotalMiB >= 0 ? qsTr("su %1 MiB · swap esclusa").arg(SystemBackend.memoryTotalMiB) : qsTr("swap esclusa")
                        opacity: UiMetrics.secondaryOpacity
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    Controls.Label { text: qsTr("Processi con più RAM"); font.bold: true }
                    Repeater {
                        model: SystemBackend.topMemoryProcesses
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Controls.Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                elide: Text.ElideRight
                            }
                            Controls.Label {
                                text: qsTr("%1 MiB").arg(modelData.memoryMiB)
                                opacity: UiMetrics.secondaryOpacity
                            }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: SystemBackend.topMemoryProcesses.length === 0
                        text: qsTr("Dati processo non disponibili")
                        opacity: UiMetrics.secondaryOpacity
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
        contentItem: ColumnLayout {
            width: quickTrashDialog.availableWidth
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Elimina definitivamente gli elementi nei cestini del tuo utente, nella home e nelle partizioni montate supportate. L’operazione non può essere annullata.")
                wrapMode: Text.WordWrap
            }
        }
        onAccepted: MaintenanceBackend.cleanTrash("all")
    }
}
