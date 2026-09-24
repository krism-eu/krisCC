import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Sistema & Boot")

    UtilityBackend { id: utilityBackend }

    property var historyEntries: []
    property bool rebootAfterDownloadedApply: false

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

    function refreshPage() {
        root.historyEntries = SystemBackend.operationHistoryEntries()
        BootcBackend.refreshStatus()
        BootcBackend.refreshPackages()
        RkBackend.refreshStatus()
        if (SystemBackend.uefiBootAvailable)
            SystemBackend.refreshUefiEntries()
        if (SystemBackend.grubEntriesAvailable)
            SystemBackend.refreshGrubEntries()
    }

    onVisibleChanged: if (visible) root.refreshPage()
    Component.onCompleted: if (visible) root.refreshPage()

    Connections {
        target: BootcBackend
        function onOperationFinished(success, output) {
            root.historyEntries = SystemBackend.operationHistoryEntries()
            if (root.rebootAfterDownloadedApply) {
                root.rebootAfterDownloadedApply = false
                if (success)
                    SystemBackend.requestReboot()
            }
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

        Controls.TabBar {
            id: sections
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Aggiornamenti") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Avvio e dischi") }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: sections.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                GridLayout {
                    Layout.fillWidth: true
                    columns: 1
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
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
                            Flow {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Controls.Button {
                                    text: qsTr("Firmware UEFI")
                                    icon.name: "system-reboot"
                                    enabled: SystemBackend.uefiBootAvailable
                                    onClicked: firmwareRebootDialog.open()
                                }
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

                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Control Center") }
                                Controls.Label {
                                    text: qsTr("Versione installata: %1").arg(Qt.application.version)
                                    opacity: UiMetrics.secondaryOpacity
                                }
                            }
                            Controls.BusyIndicator {
                                visible: SystemBackend.controlCenterUpdateBusy
                                running: visible
                            }
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: SystemBackend.controlCenterUpdateStatus.length > 0
                                  ? SystemBackend.controlCenterUpdateStatus
                                  : qsTr("Controlla la release stable ufficiale di krisCC. Gli aggiornamenti vengono applicati insieme all'immagine KrisOS dal riquadro BootC qui sopra.")
                            opacity: UiMetrics.secondaryOpacity
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button {
                                text: qsTr("Verifica aggiornamenti")
                                icon.name: "view-refresh"
                                enabled: !SystemBackend.controlCenterUpdateBusy
                                onClicked: SystemBackend.checkControlCenterUpdate()
                            }
                            Item { Layout.fillWidth: true }
                            Controls.Label {
                                visible: SystemBackend.controlCenterLatestVersion.length > 0
                                text: qsTr("Stable: %1").arg(SystemBackend.controlCenterLatestVersion)
                                opacity: UiMetrics.secondaryOpacity
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

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: SystemBackend.bootEntriesError.length > 0
                    type: Kirigami.MessageType.Warning
                    text: SystemBackend.bootEntriesError
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Parametri kernel attivi") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Vista read-only di /proc/cmdline. krisCC non modifica i kernel arguments.")
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Repeater {
                                model: SystemBackend.kernelArguments()
                                delegate: Controls.Label {
                                    required property string modelData
                                    text: modelData
                                    padding: Kirigami.Units.smallSpacing
                                    background: Rectangle {
                                        radius: 6
                                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                                       Kirigami.Theme.textColor.g,
                                                       Kirigami.Theme.textColor.b, 0.06)
                                    }
                                }
                            }
                        }
                    }
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
                    outputText: utilityBackend.output
                }
                    }
                }
            }

        }
    }

    Controls.Dialog {
        id: firmwareRebootDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Riavviare nel setup UEFI?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Il prossimo riavvio entrerà direttamente nel firmware UEFI, se supportato dal sistema.")
        }
        onAccepted: SystemBackend.requestFirmwareReboot()
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
            if (downloadOnly) {
                root.rebootAfterDownloadedApply = true
                if (!BootcBackend.applyDownloaded())
                    root.rebootAfterDownloadedApply = false
            } else {
                SystemBackend.requestReboot()
            }
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
