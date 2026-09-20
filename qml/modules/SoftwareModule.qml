import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Software RPM")

    UtilityBackend { id: utilityBackend }
    property string operationDomain: ""
    property string searchError: ""
    property string listError: ""
    property string installFilter: "all"
    property var detailPackage: null
    property string repoValidationError: ""
    property string pendingAction: ""
    property string pendingValue: ""
    property string pendingTitle: ""
    property string pendingMessage: ""

    function humanSize(bytes) {
        if (!bytes || bytes <= 0)
            return qsTr("non disponibile")
        if (bytes >= 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
        if (bytes >= 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(1) + " KiB"
        return bytes + " B"
    }

    function packageState(model) {
        if (model.owned)
            return qsTr("BASE")
        if (model.persistent)
            return qsTr("PERSISTENTE")
        if (model.installed)
            return qsTr("LOCALE")
        return ""
    }

    function showPackageDetails(model) {
        root.detailPackage = {
            name: model.name,
            summary: model.summary || "",
            version: model.version || "",
            arch: model.arch || "",
            repository: model.repository || "",
            downloadSize: model.downloadSize || 0,
            installSize: model.installSize || 0,
            state: root.packageState(model)
        }
        utilityBackend.previewRpmInstall(model.name)
        packageDialog.open()
    }

    function requestAction(action, value, title, message) {
        root.pendingAction = action
        root.pendingValue = value
        root.pendingTitle = title
        root.pendingMessage = message
        privilegedConfirmDialog.open()
    }

    function runAction(action, value) {
        if (action === "rk-add") {
            root.operationDomain = "rk"
            return RkBackend.addPackage(value)
        }
        if (action === "rk-remove") {
            root.operationDomain = "rk"
            return RkBackend.removePackage(value)
        }
        if (action === "rk-sync") {
            root.operationDomain = "rk"
            return RkBackend.sync()
        }
        if (action === "repo-disable") {
            root.operationDomain = "repo"
            return SoftwareBackend.disableRepository(value)
        }
        return false
    }

    function refreshAfterOperation() {
        BootcBackend.refreshPackages()
        SoftwareBackend.refreshRepositories()
        if (searchField.text.trim().length >= 2)
            searchModel.search(searchField.text)
        root.refreshCurrent()
    }

    function refreshCurrent() {
        root.listError = ""
        if (tabs.currentIndex === 1) installedModel.loadInstalled(root.installFilter)
        else if (tabs.currentIndex === 2) upgradesModel.loadUpgrades()
        else if (tabs.currentIndex === 3) recentModel.loadRecent()
        else if (tabs.currentIndex === 4) SoftwareBackend.refreshRepositories()
    }

    Component.onCompleted: SoftwareBackend.refreshRepositories()

    PackageSearch { id: searchModel; onSearchError: function(message) { root.searchError = message } }
    PackageSearch { id: installedModel; onSearchError: function(message) { root.listError = message } }
    PackageSearch { id: upgradesModel; onSearchError: function(message) { root.listError = message } }
    PackageSearch { id: recentModel; onSearchError: function(message) { root.listError = message } }

    Connections {
        target: RkBackend
        function onOperationFinished(ok, output) {
            root.refreshAfterOperation()
        }
    }

    Connections {
        target: SoftwareBackend
        function onOperationFinished(ok, output) {
            root.refreshAfterOperation()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Pacchetti RPM") }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.72
                text: qsTr("Base immutabile, pacchetti persistenti gestiti da rk e pacchetti locali vengono distinti chiaramente. La ricerca usa i repository DNF abilitati; l'installazione persistente resta validata dalla policy rk.")
            }
        }

        Controls.TabBar {
            id: tabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            onCurrentIndexChanged: root.refreshCurrent()
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Cerca") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Installati") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Aggiornabili") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Novità repository") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: checked; text: qsTr("Repository") }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.listError.length > 0
            type: Kirigami.MessageType.Error
            text: root.listError
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Controls.TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Cerca un pacchetto, es. btop, krita, lutris…")
                    selectByMouse: true
                    onTextChanged: searchTimer.restart()
                }
                Timer {
                    id: searchTimer
                    interval: 350
                    onTriggered: {
                        root.searchError = ""
                        searchModel.search(searchField.text)
                    }
                }
                Controls.BusyIndicator { visible: searchModel.searching; running: visible; Layout.alignment: Qt.AlignHCenter }
                Kirigami.InlineMessage { Layout.fillWidth: true; visible: root.searchError.length > 0; type: Kirigami.MessageType.Error; text: root.searchError }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: searchModel
                    clip: true
                    spacing: Kirigami.Units.smallSpacing
                    delegate: Kirigami.AbstractCard {
                        width: ListView.view.width
                        contentItem: ColumnLayout {
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.Label {
                                    Layout.fillWidth: true
                                    font.bold: true
                                    font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                                    text: model.name
                                    elide: Text.ElideRight
                                }
                                Controls.Label {
                                    visible: root.packageState(model).length > 0
                                    text: root.packageState(model)
                                    font.bold: true
                                    opacity: 0.7
                                }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                visible: model.summary.length > 0
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                text: model.summary
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: 0.72
                                elide: Text.ElideRight
                                text: [model.version, model.arch, model.repository].filter(function(x) { return !!x }).join(" · ")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.Label { opacity: 0.72; text: qsTr("Download: %1").arg(root.humanSize(model.downloadSize)) }
                                Controls.Label { opacity: 0.72; text: qsTr("Installato: %1").arg(root.humanSize(model.installSize)) }
                                Item { Layout.fillWidth: true }
                                Controls.Button {
                                    text: qsTr("Dettagli")
                                    icon.name: "documentinfo"
                                    onClicked: root.showPackageDetails(model)
                                }
                                Controls.Button {
                                    visible: model.persistent || (!model.owned && !model.persistent)
                                    enabled: RkBackend.canChangePackages
                                    text: model.persistent ? qsTr("Rimuovi")
                                          : model.installed ? qsTr("Rendi persistente")
                                                            : qsTr("Installa")
                                    icon.name: model.persistent ? "edit-delete" : "list-add"
                                    onClicked: root.requestAction(
                                        model.persistent ? "rk-remove" : "rk-add",
                                        model.name,
                                        model.persistent ? qsTr("Rimuovere %1?").arg(model.name)
                                                         : model.installed
                                                           ? qsTr("Rendere persistente %1?").arg(model.name)
                                                           : qsTr("Installare %1?").arg(model.name),
                                        model.persistent
                                            ? qsTr("Il pacchetto verrà rimosso dal layer RPM persistente.")
                                            : qsTr("L'operazione passa da rk e richiede autorizzazione amministrativa.")
                                    )
                                }
                            }
                        }
                    }
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: searchField.text.trim().length >= 2 && !searchModel.searching && searchModel.count === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessun risultato.")
                }
            }

            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Pacchetti presenti nel sistema, filtrabili per provenienza.") }
                    Controls.Button { text: qsTr("Aggiorna"); icon.name: "view-refresh"; onClicked: installedModel.loadInstalled(root.installFilter) }
                }
                Controls.ButtonGroup { id: installFilterGroup }
                RowLayout {
                    Controls.RadioButton {
                        text: qsTr("Tutti")
                        checked: true
                        Controls.ButtonGroup.group: installFilterGroup
                        onClicked: { root.installFilter = "all"; installedModel.loadInstalled(root.installFilter) }
                    }
                    Controls.RadioButton {
                        text: qsTr("Base")
                        Controls.ButtonGroup.group: installFilterGroup
                        onClicked: { root.installFilter = "base"; installedModel.loadInstalled(root.installFilter) }
                    }
                    Controls.RadioButton {
                        text: qsTr("Persistenti")
                        Controls.ButtonGroup.group: installFilterGroup
                        onClicked: { root.installFilter = "persistent"; installedModel.loadInstalled(root.installFilter) }
                    }
                    Controls.RadioButton {
                        text: qsTr("Locali")
                        Controls.ButtonGroup.group: installFilterGroup
                        onClicked: { root.installFilter = "local"; installedModel.loadInstalled(root.installFilter) }
                    }
                    Item { Layout.fillWidth: true }
                }
                Controls.BusyIndicator { visible: installedModel.searching; running: visible; Layout.alignment: Qt.AlignHCenter }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.installFilter === "persistent" && !installedModel.searching
                             && BootcBackend.persistentPackageCount === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessun pacchetto RPM persistente richiesto.")
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: installedModel
                    clip: true
                    spacing: Kirigami.Units.smallSpacing
                    delegate: Kirigami.AbstractCard {
                        width: ListView.view.width
                        contentItem: RowLayout {
                            ColumnLayout {
                                Layout.fillWidth: true
                                Controls.Label { Layout.fillWidth: true; font.bold: true; text: model.name + (model.arch ? "." + model.arch : "") }
                                Controls.Label { Layout.fillWidth: true; opacity: 0.72; text: (model.version || "") + (model.repository ? " · " + model.repository : ""); elide: Text.ElideRight }
                            }
                            Controls.Label { text: root.packageState(model); opacity: 0.7; font.bold: model.persistent || model.owned }
                            Controls.Button {
                                visible: model.persistent || (!model.owned && model.installed)
                                enabled: RkBackend.canChangePackages
                                text: model.persistent ? qsTr("Rimuovi") : qsTr("Rendi persistente")
                                icon.name: model.persistent ? "edit-delete" : "list-add"
                                onClicked: root.requestAction(
                                    model.persistent ? "rk-remove" : "rk-add",
                                    model.name,
                                    model.persistent ? qsTr("Rimuovere %1?").arg(model.name)
                                                     : qsTr("Rendere persistente %1?").arg(model.name),
                                    model.persistent
                                        ? qsTr("Il pacchetto verrà rimosso dal layer RPM persistente.")
                                        : qsTr("Il pacchetto locale verrà aggiunto alle richieste persistenti gestite da rk.")
                                )
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                RowLayout {
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Aggiornamenti RPM disponibili nei repository DNF abilitati. La base resta aggiornata tramite BootC.") }
                    Controls.Button {
                        text: qsTr("Risincronizza persistenti")
                        icon.name: "view-refresh"
                        enabled: RkBackend.canSync && BootcBackend.persistentPackageCount > 0
                        onClicked: root.requestAction(
                            "rk-sync", "",
                            qsTr("Risincronizzare i pacchetti persistenti?"),
                            qsTr("rk riallineerà il layer RPM alle richieste persistenti salvate.")
                        )
                    }
                    Controls.Button { text: qsTr("Aggiorna"); icon.name: "view-refresh"; onClicked: upgradesModel.loadUpgrades() }
                }
                Controls.BusyIndicator { visible: upgradesModel.searching; running: visible; Layout.alignment: Qt.AlignHCenter }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: upgradesModel
                    clip: true
                    spacing: Kirigami.Units.smallSpacing
                    delegate: Kirigami.AbstractCard {
                        width: ListView.view.width
                        contentItem: RowLayout {
                            Controls.Label { Layout.fillWidth: true; font.bold: true; text: model.name + (model.arch ? "." + model.arch : "") }
                            Controls.Label { text: model.version || ""; opacity: 0.72 }
                            Controls.Label { text: model.repository || ""; opacity: 0.72 }
                            Controls.Button {
                                text: qsTr("Dettagli")
                                icon.name: "documentinfo"
                                onClicked: root.showPackageDetails(model)
                            }
                        }
                    }
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !upgradesModel.searching && upgradesModel.count === 0
                    type: Kirigami.MessageType.Positive
                    text: qsTr("Nessun aggiornamento RPM disponibile nei repository abilitati.")
                }
            }

            ColumnLayout {
                RowLayout {
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Pacchetti cambiati di recente nei repository DNF abilitati. Non indica la cronologia delle installazioni locali.") }
                    Controls.Button { text: qsTr("Aggiorna"); icon.name: "view-refresh"; onClicked: recentModel.loadRecent() }
                }
                Controls.BusyIndicator { visible: recentModel.searching; running: visible; Layout.alignment: Qt.AlignHCenter }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: recentModel
                    clip: true
                    spacing: Kirigami.Units.smallSpacing
                    delegate: Kirigami.AbstractCard {
                        width: ListView.view.width
                        contentItem: RowLayout {
                            Controls.Label { Layout.fillWidth: true; font.bold: true; text: model.name + (model.arch ? "." + model.arch : "") }
                            Controls.Label { text: model.version || ""; opacity: 0.72 }
                            Controls.Label { text: model.repository || ""; opacity: 0.72 }
                            Controls.Button {
                                text: qsTr("Dettagli")
                                icon.name: "documentinfo"
                                onClicked: root.showPackageDetails(model)
                            }
                        }
                    }
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !recentModel.searching && recentModel.count === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessuna novità repository disponibile.")
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: qsTr("Repository DNF configurati nel sistema. Puoi aggiungere un file .repo remoto via HTTPS e abilitare o disabilitare repository esistenti. rk usa solo repository abilitati e verifica le firme RPM prima di ogni transazione.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Button {
                        text: qsTr("Aggiungi repository")
                        icon.name: "list-add"
                        enabled: !SoftwareBackend.busy && SoftwareBackend.canModifyRepositories
                        onClicked: addRepoDialog.open()
                    }
                    Item { Layout.fillWidth: true }
                    Controls.Button { text: qsTr("Aggiorna"); icon.name: "view-refresh"; onClicked: SoftwareBackend.refreshRepositories() }
                }
                Controls.BusyIndicator { visible: SoftwareBackend.busy || SoftwareBackend.operationRunning; running: visible; Layout.alignment: Qt.AlignHCenter }
                Kirigami.InlineMessage { Layout.fillWidth: true; visible: SoftwareBackend.errorText.length > 0; type: Kirigami.MessageType.Error; text: SoftwareBackend.errorText }
                Kirigami.InlineMessage { Layout.fillWidth: true; visible: root.repoValidationError.length > 0; type: Kirigami.MessageType.Error; text: root.repoValidationError }

                Repeater {
                    model: SoftwareBackend.repositories
                    delegate: Kirigami.AbstractCard {
                        required property var modelData
                        Layout.fillWidth: true
                        contentItem: RowLayout {
                            ColumnLayout {
                                Layout.fillWidth: true
                                Controls.Label { font.bold: true; text: modelData.name }
                                Controls.Label { text: modelData.id; opacity: 0.72 }
                            }
                            Controls.Label {
                                Layout.preferredWidth: 110
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData.enabled ? qsTr("attivo") : qsTr("inattivo")
                                font.bold: true
                                opacity: modelData.enabled ? 1.0 : 0.68
                            }
                            Controls.Button {
                                Layout.preferredWidth: 120
                                enabled: SoftwareBackend.canModifyRepositories
                                text: modelData.enabled ? qsTr("Disattiva") : qsTr("Attiva")
                                icon.name: modelData.enabled ? "media-playback-stop" : "media-playback-start"
                                onClicked: {
                                    if (modelData.enabled) {
                                        root.requestAction(
                                            "repo-disable", modelData.id,
                                            qsTr("Disattivare %1?").arg(modelData.id),
                                            qsTr("I pacchetti di questo repository non saranno più disponibili per ricerca e transazioni rk finché non verrà riattivato.")
                                        )
                                    } else {
                                        root.operationDomain = "repo"
                                        SoftwareBackend.enableRepository(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            visible: root.operationDomain === "rk"
                     ? RkBackend.operationLines.length > 0
                     : SoftwareBackend.operationLines.length > 0
            contentItem: ColumnLayout {
                Kirigami.Heading { level: 3; font.bold: true; text: qsTr("Operazione") }
                Repeater {
                    model: root.operationDomain === "rk"
                           ? RkBackend.operationLines
                           : SoftwareBackend.operationLines
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
    }

    Controls.Dialog {
        id: addRepoDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Aggiungi repository DNF")
        standardButtons: Controls.Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Inserisci l'URL HTTPS di un file .repo. HTTP e altri schemi non sono consentiti.")
            }
            Controls.TextField {
                id: repoUrlField
                Layout.fillWidth: true
                placeholderText: qsTr("https://esempio.invalid/repository.repo")
                selectByMouse: true
                onTextChanged: root.repoValidationError = ""
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.repoValidationError.length > 0
                type: Kirigami.MessageType.Error
                text: root.repoValidationError
            }
            Controls.Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Aggiungi")
                icon.name: "list-add"
                enabled: SoftwareBackend.canModifyRepositories
                onClicked: {
                    var url = repoUrlField.text.trim()
                    root.repoValidationError = ""
                    root.operationDomain = "repo"
                    if (!SoftwareBackend.addRepository(url)) {
                        root.repoValidationError = SoftwareBackend.errorText
                        return
                    }
                    repoUrlField.clear()
                    addRepoDialog.close()
                }
            }
        }
        onRejected: {
            root.repoValidationError = ""
            repoUrlField.clear()
        }
    }

    Controls.Dialog {
        id: packageDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 800)
        height: Math.min(root.height - 48, 650)
        title: root.detailPackage ? root.detailPackage.name : qsTr("Dettagli pacchetto")
        standardButtons: Controls.Dialog.Close

        contentItem: Controls.ScrollView {
            clip: true

            ColumnLayout {
                width: Math.max(320, packageDialog.width - Kirigami.Units.gridUnit * 4)
                spacing: Kirigami.Units.smallSpacing

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                    text: root.detailPackage ? root.detailPackage.summary : ""
                }
                Controls.Label {
                    Layout.fillWidth: true
                    opacity: 0.72
                    wrapMode: Text.WordWrap
                    text: root.detailPackage
                        ? [root.detailPackage.version, root.detailPackage.arch,
                           root.detailPackage.repository, root.detailPackage.state]
                          .filter(function(x) { return !!x }).join(" · ")
                        : ""
                }

                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Controls.Label { text: qsTr("Download"); opacity: 0.72 }
                            Controls.Label {
                                font.bold: true
                                text: root.detailPackage ? root.humanSize(root.detailPackage.downloadSize) : ""
                            }
                        }
                    }
                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Controls.Label { text: qsTr("Spazio installato"); opacity: 0.72 }
                            Controls.Label {
                                font.bold: true
                                text: root.detailPackage ? root.humanSize(root.detailPackage.installSize) : ""
                            }
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }
                Kirigami.Heading { level: 3; font.bold: true; text: qsTr("Piano rk") }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: qsTr("Il piano mostrato qui sotto è l'output reale di rk plan. Non viene reinterpretato dalla UI.")
                }
                Controls.BusyIndicator {
                    visible: utilityBackend.busy && utilityBackend.operationId === "rpm.plan"
                    running: visible
                    Layout.alignment: Qt.AlignHCenter
                }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !utilityBackend.busy
                             && utilityBackend.operationId === "rpm.plan"
                             && utilityBackend.resultState === "error"
                    type: Kirigami.MessageType.Error
                    text: utilityBackend.output
                }
                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.operationId === "rpm.plan"
                             && utilityBackend.output.length > 0
                             && utilityBackend.resultState !== "error"
                    contentItem: Controls.Label {
                        width: parent.width
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.family: Kirigami.Theme.defaultFixedWidthFont.family
                        text: utilityBackend.output
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: privilegedConfirmDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: root.pendingTitle
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: root.pendingMessage
        }
        onAccepted: {
            if (root.pendingAction.length > 0)
                root.runAction(root.pendingAction, root.pendingValue)
        }
    }
}
