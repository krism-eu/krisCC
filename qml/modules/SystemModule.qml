import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Sistema")

    UtilityBackend { id: utilityBackend }

    property string pendingService: ""
    property string pendingServiceTitle: ""
    property var historyEntries: []
    property var services: [
        { id: "NetworkManager.service", title: qsTr("NetworkManager") },
        { id: "cups.service", title: qsTr("Stampa (CUPS)") },
        { id: "bluetooth.service", title: qsTr("Bluetooth") }
    ]

    function bootedDeployment() {
        var entries = BootcBackend.deployments
        for (var i = 0; i < entries.length; ++i) {
            if (entries[i].role === "Booted")
                return entries[i]
        }
        return entries.length > 0 ? entries[0] : {}
    }

    function stagedDeployment() {
        var entries = BootcBackend.deployments
        for (var i = 0; i < entries.length; ++i) {
            if (entries[i].role === "Staged")
                return entries[i]
        }
        return {}
    }

    function hasStagedDeployment() {
        return !!root.stagedDeployment().image
    }

    function shortDigest(value) {
        if (!value) return ""
        return value.length > 28 ? value.substring(0, 28) + "…" : value
    }

    Component.onCompleted: {
        root.historyEntries = SystemBackend.operationHistoryEntries()
        BootcBackend.refreshStatus()
        if (SystemBackend.uefiBootAvailable)
            SystemBackend.refreshUefiEntries()
        if (SystemBackend.grubEntriesAvailable)
            SystemBackend.refreshGrubEntries()
        SystemBackend.refreshServiceStates()
    }

    Connections {
        target: BootcBackend
        function onOperationFinished(success, output) {
            root.historyEntries = SystemBackend.operationHistoryEntries()
        }
    }

    Connections {
        target: SystemBackend
        function onRebootFinished(success, message) {
            if (!success && message.length > 0)
                SystemBackend.notify(qsTr("Riavvio non riuscito"), message)
        }
        function onBootSelectionFinished(kind, success, output) {
            root.historyEntries = SystemBackend.operationHistoryEntries()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        PageIntro { title: root.title; subtitle: qsTr("Aggiornamenti, salute, avvio e strumenti essenziali. Le normali preferenze desktop restano nelle Impostazioni di sistema Plasma.") }

        Controls.TabBar {
            id: sections
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Aggiornamenti") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Salute") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Avvio e dischi") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Strumenti") }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: sections.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 900 ? 2 : 1
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.width * 0.64
                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            RowLayout {
                                Layout.fillWidth: true
                                Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("BootC") }
                                Controls.BusyIndicator { visible: BootcBackend.busy || BootcBackend.operationRunning; running: visible }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: BootcBackend.bootcAvailable
                                      ? qsTr("Immagine di sistema gestita da BootC.")
                                      : qsTr("BootC non disponibile.")
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                font.bold: false
                                elide: Text.ElideMiddle
                                text: root.bootedDeployment().image || qsTr("Immagine non disponibile")
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: UiMetrics.secondaryOpacity
                                text: {
                                    var d = root.bootedDeployment()
                                    return [d.version || "", root.shortDigest(d.digest || "")].filter(function(x) { return !!x }).join(" · ")
                                }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: UiMetrics.secondaryOpacity
                                text: qsTr("%1 pacchetti RPM persistenti richiesti").arg(BootcBackend.persistentPackageCount)
                            }

                            Kirigami.InlineMessage {
                                Layout.fillWidth: true
                                visible: root.hasStagedDeployment()
                                type: Kirigami.MessageType.Positive
                                text: {
                                    var d = root.stagedDeployment()
                                    var state = d.downloadOnly === true
                                        ? qsTr("Aggiornamento scaricato, in attesa di applicazione")
                                        : qsTr("Aggiornamento predisposto per il prossimo avvio")
                                    return qsTr("%1: %2 · %3")
                                        .arg(state)
                                        .arg(d.version || qsTr("versione non indicata"))
                                        .arg(root.shortDigest(d.digest || d.checksum || ""))
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Controls.Button {
                                    text: qsTr("Aggiorna stato")
                                    icon.name: "view-refresh"
                                    enabled: !BootcBackend.busy && !BootcBackend.operationRunning
                                    onClicked: BootcBackend.refreshStatus()
                                }
                                Controls.Button {
                                    text: qsTr("Controlla immagine")
                                    icon.name: "system-search"
                                    enabled: BootcBackend.canOperate
                                    onClicked: BootcBackend.checkUpgrade()
                                }
                                Controls.Button {
                                    text: qsTr("Scarica")
                                    icon.name: "download"
                                    enabled: BootcBackend.canOperate
                                    onClicked: BootcBackend.downloadUpgrade()
                                }
                                Controls.Button {
                                    text: qsTr("Prepara aggiornamento")
                                    icon.name: "system-software-update"
                                    enabled: BootcBackend.canOperate
                                    onClicked: BootcBackend.prepareUpgrade()
                                }
                                Controls.Button {
                                    text: root.stagedDeployment().downloadOnly === true
                                          ? qsTr("Applica e riavvia") : qsTr("Riavvia ora")
                                    icon.name: "system-reboot"
                                    enabled: BootcBackend.bootcAvailable && root.hasStagedDeployment() && !BootcBackend.operationRunning
                                    onClicked: {
                                        applyDialog.downloadOnly = root.stagedDeployment().downloadOnly === true
                                        applyDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Risincronizza RPM")
                                    icon.name: "view-refresh"
                                    enabled: RkBackend.canSync && !RkBackend.operationRunning
                                    onClicked: syncDialog.open()
                                }
                            }
                            Kirigami.AbstractCard {
                                Layout.fillWidth: true
                                visible: BootcBackend.operationLines.length > 0
                                contentItem: ColumnLayout {
                                    Controls.Label { font.bold: false; text: qsTr("Operazione BootC") }
                                    Repeater {
                                        model: BootcBackend.operationLines
                                        delegate: Controls.Label {
                                            required property string modelData
                                            Layout.fillWidth: true
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            font.family: Kirigami.Theme.fixedWidthFont.family
                                            text: modelData
                                        }
                                    }
                                }
                            }

                            Controls.CheckBox {
                                id: bootTechnicalDetails
                                text: qsTr("Dettagli tecnici")
                            }
                            OutputCard {
                    visible: bootTechnicalDetails.checked
                    embedded: true
                    text: BootcBackend.statusText
                }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.width * 0.34
                        contentItem: ColumnLayout {
                            RowLayout {
                                Layout.fillWidth: true
                                Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("Flatpak") }
                                Controls.BusyIndicator { visible: utilityBackend.busy; running: visible }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: UiMetrics.secondaryOpacity
                                text: qsTr("Controllo e aggiornamento delle applicazioni Flatpak del profilo utente.")
                            }
                            Item { Layout.fillHeight: true }
                            RowLayout {
                                Controls.Button {
                                    text: qsTr("Controlla")
                                    icon.name: "view-refresh"
                                    enabled: !utilityBackend.busy && SystemBackend.programAvailable("flatpak")
                                    onClicked: utilityBackend.runFlatpak("updates", "")
                                }
                                Controls.Button {
                                    text: qsTr("Aggiorna tutto")
                                    icon.name: "system-software-update"
                                    enabled: !utilityBackend.busy && SystemBackend.programAvailable("flatpak")
                                    onClicked: flatpakDialog.open()
                                }
                            }
                            OutputCard {
                    visible: utilityBackend.operationId.indexOf("flatpak.") === 0 && utilityBackend.output.length > 0
                    embedded: true
                    text: utilityBackend.output
                }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("Cronologia") }
                            Controls.Button {
                                text: qsTr("Aggiorna")
                                icon.name: "view-refresh"
                                onClicked: root.historyEntries = SystemBackend.operationHistoryEntries()
                            }
                            Controls.Button {
                                text: qsTr("Pulisci")
                                icon.name: "edit-clear-history"
                                enabled: root.historyEntries.length > 0
                                onClicked: clearHistoryDialog.open()
                            }
                        }

                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            visible: root.historyEntries.length === 0
                            type: Kirigami.MessageType.Information
                            text: qsTr("Nessuna operazione registrata.")
                        }

                        Repeater {
                            model: root.historyEntries
                            delegate: Kirigami.AbstractCard {
                                required property var modelData
                                Layout.fillWidth: true
                                contentItem: ColumnLayout {
                                    spacing: Kirigami.Units.smallSpacing
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Controls.Label {
                                            Layout.fillWidth: true
                                            font.bold: false
                                            text: modelData.action
                                            elide: Text.ElideRight
                                        }
                                        Controls.Label {
                                            font.bold: false
                                            text: modelData.state
                                        }
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        opacity: UiMetrics.secondaryOpacity
                                        text: [modelData.time, modelData.category].filter(function(x) { return !!x }).join(" · ")
                                        elide: Text.ElideRight
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        visible: modelData.detail.length > 0
                                        wrapMode: Text.WordWrap
                                        opacity: UiMetrics.secondaryOpacity
                                        text: modelData.detail
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
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Salute del sistema") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Controlli leggibili e non distruttivi su unità fallite, overlay /usr, spazio, rk e stato BootC.")
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Button { text: qsTr("Controlla salute"); icon.name: "tools-report-bug"; enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("health") }
                            Controls.Button { text: qsTr("Sicurezza"); icon.name: "security-high"; enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("security") }
                            Controls.Button { text: qsTr("Unità fallite"); icon.name: "dialog-warning"; enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("failed-units") }
                            Controls.Button { text: qsTr("Errori ultimo avvio"); icon.name: "view-list-text"; enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("journal-errors") }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Servizi") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Stato rapido dei servizi principali e vista completa dei servizi attivi.")
                        }
                        Repeater {
                            model: root.services
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Controls.Label { Layout.fillWidth: true; font.bold: false; text: modelData.title }
                                Controls.Label {
                                    Layout.preferredWidth: 120
                                    horizontalAlignment: Text.AlignHCenter
                                    text: SystemBackend.serviceStates[modelData.id] || qsTr("non disponibile")
                                }
                                Controls.Button { icon.name: "view-refresh";
                                    Layout.preferredWidth: 110
                                    text: qsTr("Riavvia")
                                    onClicked: {
                                        root.pendingService = modelData.id
                                        root.pendingServiceTitle = modelData.title
                                        restartServiceDialog.open()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Controls.Button { text: qsTr("Aggiorna stati"); icon.name: "view-refresh"; onClicked: SystemBackend.refreshServiceStates() }
                            Controls.Button { text: qsTr("Mostra tutti gli attivi"); icon.name: "view-list-details"; enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("services-active") }
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Information
                    text: qsTr("krisCC mostra lo stato disponibile ma non modifica Secure Boot, SELinux o firewall da questa pagina.")
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.operationId === "bookmark.health"
                          || utilityBackend.operationId === "bookmark.security"
                          || utilityBackend.operationId === "bookmark.failed-units"
                          || utilityBackend.operationId === "bookmark.journal-errors"
                          || utilityBackend.operationId === "bookmark.services-active"
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading { Layout.fillWidth: true; level: 3; font.bold: true; text: utilityBackend.title }
                        }
                        OutputCard {
                    embedded: true
                    text: utilityBackend.output
                }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: SystemBackend.bootEntriesError.length > 0
                    type: Kirigami.MessageType.Warning
                    text: SystemBackend.bootEntriesError
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Prossimo avvio UEFI") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("BootNext vale per un solo riavvio e non cambia il BootOrder permanente.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button {
                                text: qsTr("Leggi voci UEFI")
                                icon.name: "view-refresh"
                                enabled: !SystemBackend.bootEntriesBusy && SystemBackend.uefiBootAvailable
                                onClicked: SystemBackend.refreshUefiEntries()
                            }
                            Controls.ComboBox {
                                id: uefiCombo
                                Layout.fillWidth: true
                                model: SystemBackend.uefiEntries
                                textRole: "label"
                                valueRole: "code"
                                enabled: count > 0
                            }
                            Controls.Button { icon.name: "go-next";
                                text: qsTr("Usa al prossimo avvio")
                                enabled: uefiCombo.count > 0 && SystemBackend.canSelectNextBoot
                                onClicked: {
                                    nextUefiDialog.token = uefiCombo.currentValue
                                    nextUefiDialog.label = uefiCombo.currentText
                                    nextUefiDialog.open()
                                }
                            }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Voci GRUB / BLS") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Leggi le voci GRUB/BLS disponibili e scegli, quando supportato, una voce solo per il prossimo avvio.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button {
                                text: qsTr("Leggi voci")
                                icon.name: "view-refresh"
                                enabled: !SystemBackend.bootEntriesBusy && SystemBackend.grubEntriesAvailable
                                onClicked: SystemBackend.refreshGrubEntries()
                            }
                            Controls.ComboBox {
                                id: grubCombo
                                Layout.fillWidth: true
                                model: SystemBackend.grubEntries
                                textRole: "label"
                                valueRole: "id"
                                enabled: count > 0
                            }
                            Controls.Button { icon.name: "go-next";
                                text: qsTr("Prossimo avvio")
                                enabled: grubCombo.count > 0 && SystemBackend.grubNextBootAvailable && SystemBackend.canSelectNextBoot
                                onClicked: {
                                    nextGrubDialog.entryId = grubCombo.currentValue
                                    nextGrubDialog.label = grubCombo.currentText
                                    nextGrubDialog.open()
                                }
                            }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Partizioni e ordine mount") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Vista read-only di partizioni, mount correnti e configurazione persistente. krisCC non riscrive fstab automaticamente.")
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Button { icon.name: "drive-harddisk"; text: qsTr("Partizioni"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("partitions") }
                            Controls.Button { icon.name: "view-list-sort"; text: qsTr("Ordine mount"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("fstab-order") }
                            Controls.Button { icon.name: "drive-multidisk"; text: qsTr("Mount attivi"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("mounts") }
                            Controls.Button { icon.name: "drive-harddisk"; text: qsTr("Spazio"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("disk-space") }
                            Controls.Button {
                                text: qsTr("Partition Manager")
                                icon.name: "partitionmanager"
                                enabled: SystemBackend.toolAvailable("partitionmanager")
                                onClicked: SystemBackend.launchTool("partitionmanager")
                            }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.operationId === "bookmark.partitions"
                          || utilityBackend.operationId === "bookmark.fstab-order"
                          || utilityBackend.operationId === "bookmark.mounts"
                          || utilityBackend.operationId === "bookmark.disk-space"
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 3; font.bold: true; text: utilityBackend.title }
                        OutputCard {
                    embedded: true
                    text: utilityBackend.output
                }
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
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Plasma") }
                            Controls.Button { text: qsTr("Impostazioni di sistema"); icon.name: "settings-configure"; enabled: SystemBackend.toolAvailable("systemsettings"); onClicked: SystemBackend.launchTool("systemsettings") }
                            Controls.Button { text: qsTr("Info Center"); icon.name: "hwinfo"; enabled: SystemBackend.toolAvailable("kinfocenter"); onClicked: SystemBackend.launchTool("kinfocenter") }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Diagnostica") }
                            Controls.Button { text: qsTr("KSystemLog"); icon.name: "utilities-log-viewer"; enabled: SystemBackend.toolAvailable("ksystemlog"); onClicked: SystemBackend.launchTool("ksystemlog") }
                            Controls.Button { text: qsTr("Monitor di sistema"); icon.name: "utilities-system-monitor"; enabled: SystemBackend.toolAvailable("systemmonitor"); onClicked: SystemBackend.launchTool("systemmonitor") }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Pulizia") }
                            Controls.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: UiMetrics.secondaryOpacity
                                text: qsTr("Le pulizie dei cestini usano solo i privilegi del tuo utente e operano soltanto sugli scope predefiniti.")
                            }
                            Controls.Button {
                                text: qsTr("Svuota cestino home")
                                icon.name: "user-trash"
                                enabled: MaintenanceBackend.available
                                onClicked: {
                                    trashDialog.scope = "home"
                                    trashDialog.scopeLabel = qsTr("il cestino della home")
                                    trashDialog.open()
                                }
                            }
                            Controls.Button {
                                text: qsTr("Svuota cestini altre partizioni")
                                icon.name: "drive-harddisk"
                                enabled: MaintenanceBackend.available
                                onClicked: {
                                    trashDialog.scope = "system"
                                    trashDialog.scopeLabel = qsTr("i cestini delle partizioni montate")
                                    trashDialog.open()
                                }
                            }
                            Controls.Button {
                                text: qsTr("Svuota tutti i cestini")
                                icon.name: "edit-delete"
                                enabled: MaintenanceBackend.available
                                onClicked: {
                                    trashDialog.scope = "all"
                                    trashDialog.scopeLabel = qsTr("tutti i cestini dell'utente")
                                    trashDialog.open()
                                }
                            }
                            Kirigami.InlineMessage {
                                Layout.fillWidth: true
                                visible: MaintenanceBackend.resultState !== "idle"
                                type: MaintenanceBackend.resultState === "success" ? Kirigami.MessageType.Positive
                                      : MaintenanceBackend.resultState === "error" ? Kirigami.MessageType.Error
                                      : Kirigami.MessageType.Information
                                text: MaintenanceBackend.running
                                      ? qsTr("Pulizia in corso…")
                                      : MaintenanceBackend.output
                            }
                            Controls.Button { icon.name: "edit-clear";
                                text: qsTr("Flatpak inutilizzati")
                                enabled: SystemBackend.programAvailable("flatpak") && !utilityBackend.busy
                                onClicked: unusedFlatpakDialog.open()
                            }
                            Controls.Button { icon.name: "edit-find";
                                text: qsTr("RPM non necessari")
                                enabled: SystemBackend.programAvailable("dnf5") && !utilityBackend.busy
                                onClicked: utilityBackend.runBookmark("unneeded-rpms")
                            }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Storage") }
                            Controls.Button {
                                text: SystemBackend.toolAvailable("qdirstat") ? qsTr("QDirStat") : qsTr("QDirStat · non installato")
                                icon.name: "folder-chart"
                                enabled: SystemBackend.toolAvailable("qdirstat")
                                onClicked: SystemBackend.launchTool("qdirstat")
                            }
                            Controls.Button { icon.name: "partitionmanager"; text: qsTr("Partition Manager"); enabled: SystemBackend.toolAvailable("partitionmanager"); onClicked: SystemBackend.launchTool("partitionmanager") }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.operationId === "bookmark.unneeded-rpms"
                          || utilityBackend.operationId === "flatpak.remove-unused"
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 3; font.bold: true; text: utilityBackend.title }
                        OutputCard {
                    embedded: true
                    text: utilityBackend.output
                }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: restartServiceDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Riavviare %1?").arg(root.pendingServiceTitle)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Il servizio verrà interrotto e riavviato. Per NetworkManager la rete può cadere per alcuni secondi.")
        }
        onAccepted: {
            if (root.pendingService.length > 0)
                SystemBackend.restartService(root.pendingService)
        }
    }

    Controls.Dialog {
        id: applyDialog
        property bool downloadOnly: false
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: downloadOnly ? qsTr("Applicare l'aggiornamento KrisOS?")
                            : qsTr("Riavviare nel nuovo aggiornamento?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: applyDialog.downloadOnly
                  ? qsTr("L'immagine scaricata verrà preparata e il sistema verrà riavviato.")
                  : qsTr("L'aggiornamento è già predisposto per il prossimo avvio; il sistema verrà riavviato ora. Il riavvio è autorizzato secondo la policy della sessione.")
        }
        onAccepted: {
            if (downloadOnly)
                BootcBackend.applyDownloaded()
            else
                SystemBackend.requestReboot()
        }
    }

    Controls.Dialog {
        id: syncDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Risincronizzare i pacchetti persistenti?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Esegue rk sync sul layer persistente corrente. Non modifica la lista dei pacchetti richiesti.")
        }
        onAccepted: RkBackend.sync()
    }

    Controls.Dialog {
        id: trashDialog
        property string scope: ""
        property string scopeLabel: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Confermare la pulizia?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Verranno svuotati %1 con i privilegi del tuo utente. L'operazione è irreversibile.").arg(trashDialog.scopeLabel)
        }
        onAccepted: MaintenanceBackend.cleanTrash(scope)
    }

    Controls.Dialog {
        id: flatpakDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Aggiornare tutti i Flatpak utente?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            text: qsTr("Aggiornare le applicazioni e i runtime Flatpak del tuo utente?")
            wrapMode: Text.WordWrap
        }
        onAccepted: utilityBackend.runFlatpak("update-all", "")
    }

    Controls.Dialog {
        id: unusedFlatpakDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Rimuovere i Flatpak inutilizzati?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Rimuove dal profilo utente i runtime e le dipendenze Flatpak non più necessari.")
        }
        onAccepted: utilityBackend.runFlatpak("remove-unused", "")
    }

    Controls.Dialog {
        id: clearHistoryDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Cancellare la cronologia krisCC?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: {
            SystemBackend.clearOperationHistory()
            root.historyEntries = SystemBackend.operationHistoryEntries()
        }
    }

    Controls.Dialog {
        id: nextUefiDialog
        property string token: ""
        property string label: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Usare questa voce al prossimo avvio?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { wrapMode: Text.WordWrap; text: nextUefiDialog.label }
        onAccepted: SystemBackend.selectNextUefi(nextUefiDialog.token)
    }

    Controls.Dialog {
        id: nextGrubDialog
        property string entryId: ""
        property string label: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Usare questa voce GRUB al prossimo avvio?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { wrapMode: Text.WordWrap; text: nextGrubDialog.label }
        onAccepted: SystemBackend.selectNextGrub(nextGrubDialog.entryId)
    }
}
