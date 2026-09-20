import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Dashboard")
    signal openRequested(string pageId)

    property var historyEntries: []
    property int backupCount: 0

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

    function statusAccent(kind) {
        if (kind === "overlay" && RkBackend.statusValid && RkBackend.overlayState === "ready")
            return Kirigami.Theme.positiveTextColor
        if (kind === "selinux" && SystemBackend.selinuxState === "Enforcing")
            return Kirigami.Theme.positiveTextColor
        if (kind === "sync" && RkBackend.statusValid && !RkBackend.needsSync && !RkBackend.pendingRecovery)
            return Kirigami.Theme.highlightColor
        if (kind === "storage")
            return Kirigami.Theme.highlightColor
        return Kirigami.Theme.neutralTextColor
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

    function moduleAccent(id) {
        if (id === "flatpak") return Kirigami.Theme.positiveTextColor
        if (id === "podman") return Kirigami.Theme.neutralTextColor
        return Kirigami.Theme.highlightColor
    }

    function historyColor(state) {
        if (state === "success") return Kirigami.Theme.positiveTextColor
        if (state === "error") return Kirigami.Theme.negativeTextColor
        if (state === "warning" || state === "cancelled") return Kirigami.Theme.neutralTextColor
        return Kirigami.Theme.highlightColor
    }

    function suggestionTitle() {
        if (!RkBackend.statusValid) return qsTr("Controllo stato richiesto")
        if (RkBackend.pendingRecovery) return qsTr("Riavvio necessario")
        if (RkBackend.overlayState === "degraded") return qsTr("Overlay da controllare")
        if (RkBackend.needsSync) return qsTr("Sincronizzazione disponibile")
        return qsTr("Sistema allineato")
    }

    function suggestionText() {
        if (!RkBackend.statusValid) return qsTr("Aggiorna lo stato di rk per verificare il layer RPM.")
        if (RkBackend.pendingRecovery) return qsTr("Completa il recovery con un riavvio prima di altre operazioni rk.")
        if (RkBackend.overlayState === "degraded") return qsTr("Apri Recovery per i dettagli dell'overlay /usr.")
        if (RkBackend.needsSync) return qsTr("Il deployment corrente può essere riallineato con le richieste persistenti.")
        return qsTr("Overlay, richieste persistenti e deployment risultano coerenti.")
    }

    function suggestionColor() {
        if (RkBackend.pendingRecovery || RkBackend.overlayState === "degraded")
            return Kirigami.Theme.negativeTextColor
        if (RkBackend.needsSync || !RkBackend.statusValid)
            return Kirigami.Theme.neutralTextColor
        return Kirigami.Theme.positiveTextColor
    }

    function refreshDashboard() {
        root.historyEntries = SystemBackend.operationHistoryEntries()
        root.backupCount = SystemBackend.backups().length
    }

    Component.onCompleted: {
        refreshDashboard()
        RkBackend.refreshStatus()
        BootcBackend.refreshStatus()
        BootcBackend.refreshPackages()
    }

    Connections {
        target: RkBackend
        function onOperationFinished() { root.refreshDashboard() }
    }

    Connections {
        target: BootcBackend
        function onOperationFinished() { root.refreshDashboard() }
    }

    Connections {
        target: SystemBackend
        function onBackupStatusChanged() {
            if (!SystemBackend.backupBusy)
                root.refreshDashboard()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Kirigami.Heading {
                    level: 1
                    font.bold: true
                    text: qsTr("Dashboard")
                }

                Controls.Label {
                    text: qsTr("Panoramica rapida sullo stato di KrisOS")
                    opacity: 0.72
                }
            }

            Controls.Button {
                text: qsTr("Aggiorna stato")
                icon.name: "view-refresh"
                enabled: !RkBackend.busy && !BootcBackend.busy
                onClicked: {
                    RkBackend.refreshStatus()
                    BootcBackend.refreshStatus()
                    BootcBackend.refreshPackages()
                    root.refreshDashboard()
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !BootcBackend.bootcAvailable
            type: Kirigami.MessageType.Warning
            text: qsTr("bootc non è disponibile in questo ambiente. Le funzioni image-based sono disabilitate.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: RkBackend.errorText.length > 0
            type: Kirigami.MessageType.Warning
            text: qsTr("Stato del layer RPM non disponibile: %1").arg(RkBackend.errorText)
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 920 ? 4 : width > 520 ? 2 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: ["overlay", "selinux", "sync", "storage"]

                delegate: Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 122
                    radius: 12
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                                          Kirigami.Theme.textColor.g,
                                          Kirigami.Theme.textColor.b, 0.12)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        Rectangle {
                            Layout.preferredWidth: 52
                            Layout.preferredHeight: 52
                            radius: 12
                            color: Qt.rgba(root.statusAccent(modelData).r,
                                           root.statusAccent(modelData).g,
                                           root.statusAccent(modelData).b, 0.12)

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: 30
                                height: 30
                                source: root.statusIcon(modelData)
                                color: root.statusAccent(modelData)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Controls.Label {
                                text: root.statusTitle(modelData)
                                font.bold: true
                            }

                            Controls.Label {
                                Layout.fillWidth: true
                                text: root.statusValue(modelData)
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                                color: root.statusAccent(modelData)
                                elide: Text.ElideRight
                            }

                            Controls.Label {
                                Layout.fillWidth: true
                                text: root.statusDetail(modelData)
                                opacity: 0.68
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 700 ? 3 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Repeater {
                model: ["cpu", "ram", "temp"]

                delegate: Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    radius: 10
                    color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                   Kirigami.Theme.highlightColor.g,
                                   Kirigami.Theme.highlightColor.b, 0.045)
                    border.width: 1
                    border.color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                          Kirigami.Theme.highlightColor.g,
                                          Kirigami.Theme.highlightColor.b, 0.10)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            source: modelData === "cpu" ? "speedometer"
                                  : modelData === "ram" ? "media-flash-memory-stick"
                                  : "temperature"
                            color: Kirigami.Theme.highlightColor
                        }

                        Controls.Label {
                            text: modelData === "cpu" ? qsTr("CPU")
                                  : modelData === "ram" ? qsTr("RAM usata")
                                  : qsTr("Temperatura CPU")
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        ColumnLayout {
                            spacing: 0
                            Controls.Label {
                                Layout.alignment: Qt.AlignRight
                                font.bold: true
                                text: modelData === "cpu"
                                      ? (SystemBackend.cpuUsagePercent >= 0
                                         ? qsTr("%1%").arg(SystemBackend.cpuUsagePercent) : qsTr("n/d"))
                                      : modelData === "ram"
                                        ? (SystemBackend.memoryUsedMiB >= 0
                                           ? qsTr("%1 MiB").arg(SystemBackend.memoryUsedMiB) : qsTr("n/d"))
                                        : (SystemBackend.cpuTemperatureC >= 0
                                           ? qsTr("%1 °C").arg(SystemBackend.cpuTemperatureC.toFixed(0)) : qsTr("n/d"))
                            }
                            Controls.Label {
                                Layout.alignment: Qt.AlignRight
                                visible: modelData === "ram"
                                opacity: 0.58
                                text: SystemBackend.memoryTotalMiB >= 0
                                      ? qsTr("su %1 MiB · swap esclusa").arg(SystemBackend.memoryTotalMiB)
                                      : qsTr("swap esclusa")
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 920 ? 4 : width > 520 ? 2 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: [
                    { id: "software", title: qsTr("Software"), subtitle: qsTr("Gestisci i pacchetti RPM"), icon: "package-x-generic" },
                    { id: "flatpak", title: qsTr("Flatpak"), subtitle: qsTr("Gestisci le applicazioni utente"), icon: "applications-all" },
                    { id: "podman", title: qsTr("Container"), subtitle: qsTr("Gestisci i container Podman"), icon: "package" },
                    { id: "system", title: qsTr("Sistema"), subtitle: qsTr("Informazioni e diagnostica"), icon: "computer" }
                ]

                delegate: Controls.AbstractButton {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 158
                    hoverEnabled: true
                    onClicked: root.openRequested(modelData.id)

                    background: Rectangle {
                        radius: 12
                        color: parent.hovered
                               ? Qt.rgba(root.moduleAccent(modelData.id).r,
                                         root.moduleAccent(modelData.id).g,
                                         root.moduleAccent(modelData.id).b, 0.14)
                               : Qt.rgba(root.moduleAccent(modelData.id).r,
                                         root.moduleAccent(modelData.id).g,
                                         root.moduleAccent(modelData.id).b, 0.075)
                        border.width: 1
                        border.color: Qt.rgba(root.moduleAccent(modelData.id).r,
                                              root.moduleAccent(modelData.id).g,
                                              root.moduleAccent(modelData.id).b, 0.16)
                    }

                    contentItem: ColumnLayout {
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Icon {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                source: modelData.icon
                                color: root.moduleAccent(modelData.id)
                            }
                            Item { Layout.fillWidth: true }
                            Kirigami.Icon {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                source: "go-next"
                                color: root.moduleAccent(modelData.id)
                            }
                        }

                        Controls.Label {
                            text: modelData.title
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                        }

                        Controls.Label {
                            Layout.fillWidth: true
                            text: modelData.subtitle
                            opacity: 0.72
                            elide: Text.ElideRight
                        }

                        Item { Layout.fillHeight: true }

                        Controls.Label {
                            Layout.fillWidth: true
                            text: root.moduleValue(modelData.id)
                            font.bold: true
                            color: root.moduleAccent(modelData.id)
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 820 ? 2 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                radius: 12
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                                      Kirigami.Theme.textColor.g,
                                      Kirigami.Theme.textColor.b, 0.12)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            source: "view-history"
                            color: Kirigami.Theme.highlightColor
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: qsTr("Ultime operazioni")
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                        }
                        Controls.Button {
                            text: qsTr("Comandi")
                            flat: true
                            onClicked: root.openRequested("commands")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                       Kirigami.Theme.textColor.g,
                                       Kirigami.Theme.textColor.b, 0.10)
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.historyEntries.length === 0
                        text: qsTr("Nessuna operazione registrata. Le attività di krisCC compariranno qui.")
                        opacity: 0.65
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }

                    Repeater {
                        model: Math.min(root.historyEntries.length, 4)

                        delegate: RowLayout {
                            required property int index
                            property var entry: root.historyEntries[index]
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            spacing: Kirigami.Units.largeSpacing

                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 10
                                radius: 5
                                color: root.historyColor(entry.state)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: entry.category + " · " + entry.action
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    visible: entry.detail.length > 0
                                    text: entry.detail
                                    opacity: 0.62
                                    elide: Text.ElideRight
                                }
                            }

                            Controls.Label {
                                text: entry.time
                                opacity: 0.62
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                radius: 12
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                                      Kirigami.Theme.textColor.g,
                                      Kirigami.Theme.textColor.b, 0.12)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            source: "dialog-information"
                            color: Kirigami.Theme.highlightColor
                        }
                        Controls.Label {
                            text: qsTr("Suggerimenti")
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                        }
                        Item { Layout.fillWidth: true }
                        Controls.Button {
                            text: qsTr("Recovery")
                            flat: true
                            onClicked: root.openRequested("recovery")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: 10
                        color: Qt.rgba(root.suggestionColor().r,
                                       root.suggestionColor().g,
                                       root.suggestionColor().b, 0.09)
                        border.width: 1
                        border.color: Qt.rgba(root.suggestionColor().r,
                                              root.suggestionColor().g,
                                              root.suggestionColor().b, 0.14)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.largeSpacing
                            Kirigami.Icon {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                source: RkBackend.pendingRecovery ? "dialog-warning" : "security-high"
                                color: root.suggestionColor()
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Controls.Label {
                                    text: root.suggestionTitle()
                                    font.bold: true
                                    color: root.suggestionColor()
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: root.suggestionText()
                                    wrapMode: Text.WordWrap
                                    opacity: 0.78
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: 10
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                       Kirigami.Theme.highlightColor.g,
                                       Kirigami.Theme.highlightColor.b, 0.07)
                        border.width: 1
                        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                              Kirigami.Theme.highlightColor.g,
                                              Kirigami.Theme.highlightColor.b, 0.12)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.largeSpacing
                            Kirigami.Icon {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                source: "document-save-all"
                                color: Kirigami.Theme.highlightColor
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Controls.Label {
                                    text: qsTr("Backup locali")
                                    font.bold: true
                                    color: Kirigami.Theme.highlightColor
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: root.backupCount > 0
                                          ? qsTr("%1 backup disponibili. Verifica periodicamente gli archivi.").arg(root.backupCount)
                                          : qsTr("Nessun backup krisCC presente: puoi crearne uno dalle azioni rapide.")
                                    wrapMode: Text.WordWrap
                                    opacity: 0.78
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 142
            radius: 12
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                                  Kirigami.Theme.textColor.g,
                                  Kirigami.Theme.textColor.b, 0.12)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Controls.Label {
                    text: qsTr("Azioni rapide")
                    font.bold: true
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: width > 820 ? 4 : width > 420 ? 2 : 1
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: [
                            { id: "refresh", title: qsTr("Aggiorna stato"), note: qsTr("Ricarica le informazioni"), icon: "view-refresh" },
                            { id: "backup", title: qsTr("Crea backup"), note: qsTr("Configurazione utente"), icon: "document-save-all" },
                            { id: "recovery", title: qsTr("Apri Recovery"), note: qsTr("Layer RPM e ripristino"), icon: "edit-undo" },
                            { id: "terminal", title: qsTr("Apri terminale"), note: qsTr("Konsole"), icon: "utilities-terminal" }
                        ]

                        delegate: Controls.Button {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.title
                            icon.name: modelData.icon
                            enabled: modelData.id !== "backup" || !SystemBackend.backupBusy
                            onClicked: {
                                if (modelData.id === "refresh") {
                                    RkBackend.refreshStatus()
                                    BootcBackend.refreshStatus()
                                    BootcBackend.refreshPackages()
                                    root.refreshDashboard()
                                } else if (modelData.id === "backup") {
                                    SystemBackend.createSnapshot("config")
                                } else if (modelData.id === "recovery") {
                                    root.openRequested("recovery")
                                } else if (modelData.id === "terminal") {
                                    SystemBackend.launchTool("konsole")
                                }
                            }

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing
                                Kirigami.Icon {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    source: modelData.icon
                                    color: Kirigami.Theme.highlightColor
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Controls.Label {
                                        text: modelData.title
                                        font.bold: true
                                    }
                                    Controls.Label {
                                        text: modelData.note
                                        opacity: 0.65
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
