import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Sistema")

    UtilityBackend { id: utilityBackend }

    property bool ownBootAction: false
    property string bootActionKind: ""
    property bool ownBootcOperation: false
    property var bootProgressLines: []
    property string pendingService: ""
    property string pendingServiceTitle: ""
    property var historyEntries: []
    property int servicesRefreshToken: 0
    property var services: [
        { id: "NetworkManager.service", title: qsTr("NetworkManager") },
        { id: "cups.service", title: qsTr("Stampa (CUPS)") },
        { id: "bluetooth.service", title: qsTr("Bluetooth") }
    ]

    function runBootc(args) {
        root.ownBootcOperation = true
        root.bootProgressLines = []
        PolkitHelper.execute("/usr/bin/bootc", args)
    }

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

    function uefiEntries() {
        if (utilityBackend.operationId !== "bookmark.uefi" || utilityBackend.resultState !== "success")
            return []
        var result = []
        var lines = utilityBackend.output.split("\n")
        for (var i = 0; i < lines.length; ++i) {
            var match = lines[i].match(/^Boot([0-9A-Fa-f]{4})\*?\s+(.+)$/)
            if (match)
                result.push({ code: match[1].toUpperCase(), label: match[1].toUpperCase() + " · " + match[2] })
        }
        return result
    }

    function grubEntries() {
        if (utilityBackend.operationId !== "bookmark.grub-entries" || utilityBackend.resultState !== "success")
            return []
        var result = []
        var current = {}
        var lines = utilityBackend.output.split("\n")
        function commit() {
            if (current.id) {
                var label = current.title ? current.title : current.id
                result.push({ id: current.id, label: label })
            }
            current = {}
        }
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i].trim()
            if (line.indexOf("index=") === 0) {
                commit()
            } else if (line.indexOf("title=") === 0) {
                current.title = line.substring(6).replace(/^"|"$/g, "")
            } else if (line.indexOf("id=") === 0) {
                current.id = line.substring(3).replace(/^"|"$/g, "")
            }
        }
        commit()
        return result
    }

    Component.onCompleted: {
        root.historyEntries = SystemBackend.operationHistoryEntries()
        BootcBackend.refreshStatus()
    }

    Connections {
        target: PolkitHelper
        function onLine(text) {
            if (root.ownBootcOperation)
                root.bootProgressLines = root.bootProgressLines.concat([text]).slice(-14)
        }
        function onFinished(success, output) {
            if (root.ownBootcOperation) {
                root.ownBootcOperation = false
                root.bootProgressLines = root.bootProgressLines.concat([
                    success ? qsTr("--- completato ---") : qsTr("--- fallito ---")
                ]).slice(-14)
                BootcBackend.refreshStatus()
                BootcBackend.refreshPackages()
                root.historyEntries = SystemBackend.operationHistoryEntries()
                return
            }
            if (!root.ownBootAction)
                return
            root.ownBootAction = false
            if (root.bootActionKind === "uefi")
                utilityBackend.runBookmark("uefi")
            else if (root.bootActionKind === "grub")
                utilityBackend.runBookmark("grub-entries")
            root.historyEntries = SystemBackend.operationHistoryEntries()
        }
    }

    Connections {
        target: SystemBackend
        function onRebootFinished(success, message) {
            if (!success && message.length > 0)
                root.bootProgressLines = root.bootProgressLines.concat([message]).slice(-14)
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.72
            text: qsTr("Aggiornamenti, salute, avvio e strumenti essenziali. Le normali preferenze desktop restano nelle Impostazioni di sistema Plasma.")
        }

        Controls.TabBar {
            id: sections
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Aggiornamenti") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Salute") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Avvio e dischi") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Strumenti") }
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
                                Controls.BusyIndicator { visible: BootcBackend.busy || PolkitHelper.running; running: visible }
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
                                font.bold: true
                                elide: Text.ElideMiddle
                                text: root.bootedDeployment().image || qsTr("Immagine non disponibile")
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: 0.72
                                text: {
                                    var d = root.bootedDeployment()
                                    return [d.version || "", root.shortDigest(d.digest || "")].filter(function(x) { return !!x }).join(" · ")
                                }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: 0.72
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
                                    enabled: !BootcBackend.busy && !PolkitHelper.running
                                    onClicked: BootcBackend.refreshStatus()
                                }
                                Controls.Button {
                                    text: qsTr("Controlla immagine")
                                    icon.name: "system-search"
                                    enabled: BootcBackend.bootcAvailable && !PolkitHelper.running
                                    onClicked: root.runBootc(["upgrade", "--check"])
                                }
                                Controls.Button {
                                    text: qsTr("Scarica")
                                    icon.name: "download"
                                    enabled: BootcBackend.bootcAvailable && !PolkitHelper.running
                                    onClicked: root.runBootc(["upgrade", "--download-only"])
                                }
                                Controls.Button {
                                    text: qsTr("Prepara aggiornamento")
                                    icon.name: "system-software-update"
                                    enabled: BootcBackend.bootcAvailable && !PolkitHelper.running
                                    onClicked: root.runBootc(["upgrade"])
                                }
                                Controls.Button {
                                    text: root.stagedDeployment().downloadOnly === true
                                          ? qsTr("Applica e riavvia") : qsTr("Riavvia ora")
                                    icon.name: "system-reboot"
                                    enabled: BootcBackend.bootcAvailable && root.hasStagedDeployment() && !PolkitHelper.running
                                    onClicked: {
                                        applyDialog.downloadOnly = root.stagedDeployment().downloadOnly === true
                                        applyDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Risincronizza RPM")
                                    icon.name: "view-refresh"
                                    enabled: !PolkitHelper.running && SystemBackend.programAvailable("rk")
                                    onClicked: syncDialog.open()
                                }
                            }
                            Kirigami.AbstractCard {
                                Layout.fillWidth: true
                                visible: root.bootProgressLines.length > 0
                                contentItem: ColumnLayout {
                                    Controls.Label { font.bold: true; text: qsTr("Operazione BootC") }
                                    Repeater {
                                        model: root.bootProgressLines
                                        delegate: Controls.Label {
                                            required property string modelData
                                            Layout.fillWidth: true
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            font.family: Kirigami.Theme.defaultFixedWidthFont.family
                                            text: modelData
                                        }
                                    }
                                }
                            }

                            Controls.CheckBox {
                                id: bootTechnicalDetails
                                text: qsTr("Dettagli tecnici")
                            }
                            Controls.TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150
                                visible: bootTechnicalDetails.checked
                                readOnly: true
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                font.family: Kirigami.Theme.defaultFixedWidthFont.family
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
                                opacity: 0.72
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
                            Controls.TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                visible: utilityBackend.operationId.indexOf("flatpak.") === 0 && utilityBackend.output.length > 0
                                readOnly: true
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                font.family: Kirigami.Theme.defaultFixedWidthFont.family
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
                                            font.bold: true
                                            text: modelData.action
                                            elide: Text.ElideRight
                                        }
                                        Controls.Label {
                                            font.bold: true
                                            text: modelData.state
                                        }
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        opacity: 0.72
                                        text: [modelData.time, modelData.category].filter(function(x) { return !!x }).join(" · ")
                                        elide: Text.ElideRight
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        visible: modelData.detail.length > 0
                                        wrapMode: Text.WordWrap
                                        opacity: 0.72
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
                            opacity: 0.72
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
                            opacity: 0.72
                            text: qsTr("Stato rapido dei servizi principali e vista completa dei servizi attivi.")
                        }
                        Repeater {
                            model: root.services
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Controls.Label { Layout.fillWidth: true; font.bold: true; text: modelData.title }
                                Controls.Label {
                                    Layout.preferredWidth: 120
                                    horizontalAlignment: Text.AlignHCenter
                                    text: { root.servicesRefreshToken; return SystemBackend.serviceState(modelData.id) }
                                }
                                Controls.Button {
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
                            Controls.Button { text: qsTr("Aggiorna stati"); icon.name: "view-refresh"; onClicked: root.servicesRefreshToken++ }
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
                            Controls.Button {
                                text: qsTr("Copia")
                                icon.name: "edit-copy"
                                enabled: utilityBackend.output.length > 0
                                onClicked: SystemBackend.copyToClipboard(utilityBackend.output)
                            }
                        }
                        Controls.TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 330
                            readOnly: true
                            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                            font.family: Kirigami.Theme.defaultFixedWidthFont.family
                            text: utilityBackend.output
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Prossimo avvio UEFI") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.72
                            text: qsTr("BootNext vale per un solo riavvio e non cambia il BootOrder permanente.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button {
                                text: qsTr("Leggi voci UEFI")
                                icon.name: "view-refresh"
                                enabled: !utilityBackend.busy && SystemBackend.programAvailable("efibootmgr")
                                onClicked: utilityBackend.runBookmark("uefi")
                            }
                            Controls.ComboBox {
                                id: uefiCombo
                                Layout.fillWidth: true
                                model: root.uefiEntries()
                                textRole: "label"
                                valueRole: "code"
                                enabled: count > 0
                            }
                            Controls.Button {
                                text: qsTr("Usa al prossimo avvio")
                                enabled: uefiCombo.count > 0 && !PolkitHelper.running
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
                            opacity: 0.72
                            text: qsTr("Le voci vengono lette da grubby. Se grub2-reboot è disponibile puoi scegliere una voce solo per il prossimo avvio.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button {
                                text: qsTr("Leggi voci")
                                icon.name: "view-refresh"
                                enabled: !utilityBackend.busy && SystemBackend.programAvailable("grubby")
                                onClicked: utilityBackend.runBookmark("grub-entries")
                            }
                            Controls.ComboBox {
                                id: grubCombo
                                Layout.fillWidth: true
                                model: root.grubEntries()
                                textRole: "label"
                                valueRole: "id"
                                enabled: count > 0
                            }
                            Controls.Button {
                                text: qsTr("Prossimo avvio")
                                enabled: grubCombo.count > 0 && SystemBackend.programAvailable("grub2-reboot") && !PolkitHelper.running
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
                            opacity: 0.72
                            text: qsTr("Vista read-only di partizioni, mount correnti e configurazione persistente. krisCC non riscrive fstab automaticamente.")
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Button { text: qsTr("Partizioni"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("partitions") }
                            Controls.Button { text: qsTr("Ordine mount"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("fstab-order") }
                            Controls.Button { text: qsTr("Mount attivi"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("mounts") }
                            Controls.Button { text: qsTr("Spazio"); enabled: !utilityBackend.busy; onClicked: utilityBackend.runBookmark("disk-space") }
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
                    visible: utilityBackend.operationId === "bookmark.uefi"
                          || utilityBackend.operationId === "bookmark.grub-entries"
                          || utilityBackend.operationId === "bookmark.partitions"
                          || utilityBackend.operationId === "bookmark.fstab-order"
                          || utilityBackend.operationId === "bookmark.mounts"
                          || utilityBackend.operationId === "bookmark.disk-space"
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 3; font.bold: true; text: utilityBackend.title }
                        Controls.TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 300
                            readOnly: true
                            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                            font.family: Kirigami.Theme.defaultFixedWidthFont.family
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
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Plasma") }
                            Controls.Button { text: qsTr("Impostazioni di sistema"); icon.name: "settings-configure"; enabled: SystemBackend.toolAvailable("systemsettings"); onClicked: SystemBackend.launchTool("systemsettings") }
                            Controls.Button { text: qsTr("Info Center"); icon.name: "hwinfo"; enabled: SystemBackend.toolAvailable("kinfocenter"); onClicked: SystemBackend.launchTool("kinfocenter") }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Diagnostica") }
                            Controls.Button { text: qsTr("KSystemLog"); icon.name: "utilities-log-viewer"; enabled: SystemBackend.toolAvailable("ksystemlog"); onClicked: SystemBackend.launchTool("ksystemlog") }
                            Controls.Button { text: qsTr("Monitor di sistema"); icon.name: "utilities-system-monitor"; enabled: SystemBackend.toolAvailable("systemmonitor"); onClicked: SystemBackend.launchTool("systemmonitor") }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Pulizia") }
                            Controls.Button {
                                text: qsTr("Flatpak inutilizzati")
                                enabled: SystemBackend.programAvailable("flatpak") && !utilityBackend.busy
                                onClicked: unusedFlatpakDialog.open()
                            }
                            Controls.Button {
                                text: qsTr("RPM non necessari")
                                enabled: SystemBackend.programAvailable("dnf5") && !utilityBackend.busy
                                onClicked: utilityBackend.runBookmark("unneeded-rpms")
                            }
                        }
                    }

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Storage") }
                            Controls.Button {
                                text: SystemBackend.toolAvailable("qdirstat") ? qsTr("QDirStat") : qsTr("QDirStat · non installato")
                                icon.name: "folder-chart"
                                enabled: SystemBackend.toolAvailable("qdirstat")
                                onClicked: SystemBackend.launchTool("qdirstat")
                            }
                            Controls.Button { text: qsTr("Partition Manager"); enabled: SystemBackend.toolAvailable("partitionmanager"); onClicked: SystemBackend.launchTool("partitionmanager") }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.operationId === "bookmark.unneeded-rpms"
                          || utilityBackend.operationId === "flatpak.remove-unused"
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 3; font.bold: true; text: utilityBackend.title }
                        Controls.TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 260
                            readOnly: true
                            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                            font.family: Kirigami.Theme.defaultFixedWidthFont.family
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
            if (downloadOnly) {
                root.runBootc(["upgrade", "--from-downloaded", "--apply"])
            } else {
                root.bootProgressLines = []
                SystemBackend.requestReboot()
            }
        }
    }

    Controls.Dialog {
        id: syncDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Risincronizzare i pacchetti persistenti?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Esegue rk sync sul layer persistente corrente. Non modifica la lista dei pacchetti richiesti.")
        }
        onAccepted: PolkitHelper.execute("/usr/bin/rk", ["sync"])
    }

    Controls.Dialog {
        id: flatpakDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Aggiornare tutti i Flatpak utente?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: utilityBackend.runFlatpak("update-all", "")
    }

    Controls.Dialog {
        id: unusedFlatpakDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
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
        title: qsTr("Usare questa voce al prossimo avvio?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { wrapMode: Text.WordWrap; text: nextUefiDialog.label }
        onAccepted: {
            root.ownBootAction = true
            root.bootActionKind = "uefi"
            PolkitHelper.execute("/usr/bin/efibootmgr", ["-n", nextUefiDialog.token])
        }
    }

    Controls.Dialog {
        id: nextGrubDialog
        property string entryId: ""
        property string label: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Usare questa voce GRUB al prossimo avvio?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { wrapMode: Text.WordWrap; text: nextGrubDialog.label }
        onAccepted: {
            root.ownBootAction = true
            root.bootActionKind = "grub"
            PolkitHelper.execute("/usr/bin/grub2-reboot", [nextGrubDialog.entryId])
        }
    }
}
