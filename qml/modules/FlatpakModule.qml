import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Flatpak")

    UtilityBackend { id: utilityBackend }
    UtilityBackend { id: systemScopeBackend }
    property string mode: "search"
    property string lastQuery: ""
    property bool refreshUpdatesAfterAction: false
    property bool refreshSearchAfterInstall: false
    property bool refreshRemotesAfterAdd: false
    property bool refreshInstalledAfterAction: false
    property bool systemScopeChecked: false

    function expectedOperationId() {
        return "flatpak." + root.mode
    }

    function refreshSystemScope() {
        if (!SystemBackend.programAvailable("flatpak") || systemScopeBackend.busy)
            return
        root.systemScopeChecked = true
        systemScopeBackend.runFlatpak("system-installed", "")
    }

    function modeIndex(newMode) {
        if (newMode === "installed") return 1
        if (newMode === "updates") return 2
        if (newMode === "remotes") return 3
        return 0
    }

    function run(newMode, query) {
        root.mode = newMode
        flatpakTabs.currentIndex = root.modeIndex(newMode)
        root.lastQuery = query || ""
        utilityBackend.runFlatpak(newMode, query || "")
    }

    function updateFlatpaks(action, query) {
        root.mode = "updates"
        root.refreshUpdatesAfterAction = true
        utilityBackend.runFlatpak(action, query || "")
    }

    function preferredRemote(value) {
        if (!value) return "flathub"
        var first = value.split(",")[0].trim()
        return first.length > 0 ? first : "flathub"
    }

    function installFlatpak(appId, remote) {
        root.refreshSearchAfterInstall = true
        utilityBackend.runFlatpak("install", appId, root.preferredRemote(remote))
    }

    function removeFlatpak(appId) {
        root.refreshInstalledAfterAction = true
        utilityBackend.runFlatpak("remove", appId)
    }

    onVisibleChanged: if (visible && !root.systemScopeChecked) root.refreshSystemScope()
    Component.onCompleted: if (visible) root.refreshSystemScope()

    Connections {
        target: utilityBackend
        function onStateChanged() {
            if (root.refreshUpdatesAfterAction && !utilityBackend.busy) {
                root.refreshUpdatesAfterAction = false
                root.run("updates", "")
                return
            }
            if (root.refreshSearchAfterInstall && !utilityBackend.busy
                    && utilityBackend.operationId === "flatpak.install") {
                root.refreshSearchAfterInstall = false
                if (root.lastQuery.length >= 2)
                    root.run("search", root.lastQuery)
                return
            }
            if (root.refreshRemotesAfterAdd && !utilityBackend.busy
                    && utilityBackend.operationId === "flatpak.flathub-add") {
                root.refreshRemotesAfterAdd = false
                root.run("remotes", "")
                return
            }
            if (root.refreshInstalledAfterAction && !utilityBackend.busy
                    && utilityBackend.operationId === "flatpak.remove") {
                root.refreshInstalledAfterAction = false
                root.run("installed", "")
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Controls.TabBar {
            id: flatpakTabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            currentIndex: 0
            Controls.TabButton {
                implicitHeight: Kirigami.Units.gridUnit * 2.1
                font.bold: true
                text: qsTr("Cerca")
                onClicked: {
                    root.mode = "search"
                    root.lastQuery = ""
                    searchField.clear()
                }
            }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Installati"); onClicked: root.run("installed", "") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Aggiornamenti"); onClicked: root.run("updates", "") }
            Controls.TabButton { implicitHeight: Kirigami.Units.gridUnit * 2.1; font.bold: true; text: qsTr("Remote"); onClicked: root.run("remotes", "") }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.mode === "search"
            Controls.TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Cerca applicazioni, es. firefox, pdf, inkscape…")
                selectByMouse: true
                onAccepted: if (text.trim().length >= 2) root.run("search", text.trim())
            }
            Controls.Button {
                text: qsTr("Cerca")
                icon.name: "system-search"
                enabled: !utilityBackend.busy && searchField.text.trim().length >= 2
                onClicked: root.run("search", searchField.text.trim())
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.mode === "updates"
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Aggiornamenti disponibili per le applicazioni Flatpak installate dall'utente.")
            }
            Controls.Button {
                text: qsTr("Aggiorna elenco")
                icon.name: "view-refresh"
                enabled: !utilityBackend.busy
                onClicked: root.run("updates", "")
            }
            Controls.Button {
                text: qsTr("Aggiorna tutto")
                icon.name: "system-software-update"
                enabled: !utilityBackend.busy && utilityBackend.rows.length > 0
                onClicked: updateAllDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.mode === "remotes"
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Remote configurati per Flatpak.")
            }
            Controls.Button {
                text: qsTr("Aggiungi Flathub")
                icon.name: "list-add"
                enabled: !utilityBackend.busy && SystemBackend.programAvailable("flatpak")
                onClicked: flathubDialog.open()
            }
            Controls.Button {
                text: qsTr("Aggiorna")
                icon.name: "view-refresh"
                enabled: !utilityBackend.busy
                onClicked: root.run("remotes", "")
            }
        }

        Controls.BusyIndicator {
            visible: utilityBackend.busy
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !SystemBackend.programAvailable("flatpak")
            type: Kirigami.MessageType.Warning
            text: qsTr("Flatpak non è installato.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.systemScopeChecked
                     && !systemScopeBackend.busy
                     && systemScopeBackend.resultState === "success"
                     && systemScopeBackend.rows.length > 0
            type: Kirigami.MessageType.Information
            text: qsTr("Sono presenti %1 applicazioni Flatpak installate a livello di sistema. Questa pagina gestisce solo il profilo utente.").arg(systemScopeBackend.rows.length)
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !utilityBackend.busy && utilityBackend.operationId.indexOf("flatpak.") === 0
                     && utilityBackend.resultState !== "idle" && utilityBackend.output.length > 0 && utilityBackend.rows.length === 0
                     && !(root.mode === "search" && root.lastQuery.length < 2)
            type: utilityBackend.resultState === "success" ? Kirigami.MessageType.Information : Kirigami.MessageType.Error
            text: utilityBackend.output
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 24)
            Layout.minimumHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 8)
            interactive: contentHeight > height
            clip: true
            spacing: Kirigami.Units.smallSpacing
            model: root.mode === "search" && root.lastQuery.length < 2 ? [] : utilityBackend.rows

            delegate: Kirigami.AbstractCard {
                required property var modelData
                width: ListView.view.width

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        Kirigami.Icon {
                            Layout.preferredWidth: Kirigami.Units.iconSizes.large
                            Layout.preferredHeight: Kirigami.Units.iconSizes.large
                            Layout.alignment: Qt.AlignTop
                            source: {
                                var appId = root.mode === "search" ? (modelData[2] || "") : (modelData[1] || "")
                                var resolved = SystemBackend.flatpakIconPath(appId)
                                return resolved.length > 0 ? resolved : "package-x-generic"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Label {
                                Layout.fillWidth: true
                                font.bold: false
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                                text: modelData[0] || ""
                                elide: Text.ElideRight
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                visible: root.mode === "search"
                                text: modelData[1] || ""
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                opacity: UiMetrics.secondaryOpacity
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: UiMetrics.secondaryOpacity
                                elide: Text.ElideRight
                                text: {
                                    if (root.mode === "search")
                                        return [modelData[2], modelData[3], modelData[4], modelData[5]].filter(function(x) { return !!x }).join(" · ")
                                    if (root.mode === "installed" || root.mode === "updates")
                                        return [modelData[1], modelData[2], modelData[3]].filter(function(x) { return !!x }).join(" · ")
                                    return [modelData[1], modelData[2], modelData[3]].filter(function(x) { return !!x }).join(" · ")
                                }
                            }
                        }

                        Controls.Button {
                            visible: root.mode === "search" && modelData.length >= 3
                            text: qsTr("Installa")
                            icon.name: "list-add"
                            enabled: !utilityBackend.busy
                            onClicked: root.installFlatpak(modelData[2], modelData[5] || "")
                        }

                        Controls.Button {
                            visible: root.mode === "installed" && modelData.length >= 2
                            text: qsTr("Avvia")
                            icon.name: "media-playback-start"
                            enabled: !utilityBackend.busy
                            onClicked: SystemBackend.launchFlatpak(modelData[1])
                        }

                        Controls.Button {
                            visible: root.mode === "installed" && modelData.length >= 2
                            text: qsTr("Rimuovi")
                            icon.name: "edit-delete"
                            enabled: !utilityBackend.busy
                            onClicked: removeDialog.openFor(modelData[1], modelData[0])
                        }

                        Controls.Button {
                            visible: root.mode === "updates" && modelData.length >= 2
                            text: qsTr("Aggiorna")
                            icon.name: "system-software-update"
                            enabled: !utilityBackend.busy
                            onClicked: updateOneDialog.openFor(modelData[1], modelData[0])
                        }
                    }


                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.mode === "search" && root.lastQuery.length < 2 && !utilityBackend.busy
            type: Kirigami.MessageType.Information
            text: qsTr("Inserisci almeno due caratteri per cercare applicazioni.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.mode === "search" && root.lastQuery.length >= 2 && !utilityBackend.busy
                  && utilityBackend.operationId === "flatpak.search" && utilityBackend.resultState === "success"
                  && utilityBackend.rows.length === 0
            type: Kirigami.MessageType.Information
            text: qsTr("Nessun risultato.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.mode === "updates" && !utilityBackend.busy
                  && utilityBackend.operationId === "flatpak.updates" && utilityBackend.resultState === "success"
                  && utilityBackend.rows.length === 0
            type: Kirigami.MessageType.Positive
            text: qsTr("Nessun aggiornamento Flatpak disponibile.")
        }
    }

    Controls.Dialog {
        id: flathubDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Aggiungere Flathub?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Aggiunge Flathub solo per il tuo utente. Se esiste già, non viene duplicato.")
        }
        onAccepted: {
            root.refreshRemotesAfterAdd = true
            root.mode = "remotes"
            flatpakTabs.currentIndex = 3
            utilityBackend.addFlathubUser()
        }
    }

    Controls.Dialog {
        id: removeDialog
        property string appId: ""
        property string appName: ""
        function openFor(id, name) {
            appId = id
            appName = name
            open()
        }
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Rimuovere %1?").arg(appName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Rimuove il Flatpak %1 dal tuo utente.").arg(removeDialog.appId)
        }
        onAccepted: root.removeFlatpak(appId)
    }

    Controls.Dialog {
        id: updateOneDialog
        property string appId: ""
        property string appName: ""
        function openFor(id, name) {
            appId = id
            appName = name
            open()
        }
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Aggiornare %1?").arg(appName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Aggiorna %1 e le dipendenze necessarie nel profilo Flatpak dell'utente.").arg(updateOneDialog.appId)
        }
        onAccepted: root.updateFlatpaks("update", appId)
    }

    Controls.Dialog {
        id: updateAllDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Aggiornare tutte le applicazioni Flatpak?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Aggiorna tutte le applicazioni e i runtime Flatpak installati nel profilo utente.")
        }
        onAccepted: root.updateFlatpaks("update-all", "")
    }
}