import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Container")

    UtilityBackend { id: utilityBackend }

    property string mode: "containers"
    property var containers: []
    property var images: []
    property string parseError: ""
    property string selectedName: ""
    property string selectedImageId: ""
    property string selectedImageName: ""
    property bool refreshAfterAction: false

    function humanSize(bytes) {
        if (bytes === undefined || bytes === null || bytes === "")
            return qsTr("n/d")
        if (typeof bytes === "string") {
            var numeric = Number(bytes)
            if (isNaN(numeric))
                return bytes
            bytes = numeric
        }
        if (bytes >= 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
        if (bytes >= 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(1) + " KiB"
        return bytes + " B"
    }

    function refresh() {
        root.parseError = ""
        utilityBackend.runPodman(root.mode === "images" ? "images" : "list")
    }

    function applyResult() {
        if (utilityBackend.busy)
            return
        var expected = root.mode === "images" ? "podman.images" : "podman.list"
        if (utilityBackend.operationId !== expected)
            return
        if (utilityBackend.resultState !== "success") {
            if (root.mode === "images") root.images = []
            else root.containers = []
            root.parseError = utilityBackend.output.length > 0 ? utilityBackend.output : qsTr("Impossibile leggere i dati Podman.")
            return
        }
        if (root.mode === "images")
            root.images = utilityBackend.rows
        else
            root.containers = utilityBackend.rows
        root.parseError = ""
    }

    function containerName(item) {
        if (!item) return ""
        if (Array.isArray(item.Names)) return item.Names.length ? item.Names[0] : ""
        return item.Names || item.Name || ""
    }

    function containerImage(item) {
        return item ? (item.Image || item.ImageName || "") : ""
    }

    function containerState(item) {
        return item ? (item.Status || item.State || "") : ""
    }

    function containerSize(item) {
        if (!item) return qsTr("n/d")
        if (item.Size && typeof item.Size === "object") {
            var objectRw = item.Size.rwSize !== undefined ? item.Size.rwSize : (item.Size.RwSize || 0)
            var objectRoot = item.Size.rootFsSize !== undefined ? item.Size.rootFsSize : (item.Size.RootFsSize || 0)
            return qsTr("RW %1 · totale %2").arg(root.humanSize(objectRw)).arg(root.humanSize(objectRoot))
        }
        if (item.Size) return root.humanSize(item.Size)
        if (item.SizeRw !== undefined || item.SizeRootFs !== undefined) {
            var rw = item.SizeRw !== undefined ? item.SizeRw : 0
            var rootfs = item.SizeRootFs !== undefined ? item.SizeRootFs : 0
            return qsTr("RW %1 · totale %2").arg(root.humanSize(rw)).arg(root.humanSize(rootfs))
        }
        return qsTr("n/d")
    }

    function imageId(item) {
        if (!item) return ""
        return item.Id || item.ID || item.id || ""
    }

    function imageName(item) {
        if (!item) return qsTr("<senza tag>")
        if (Array.isArray(item.Names) && item.Names.length > 0)
            return item.Names.join(", ")
        if (item.Repository && item.Tag && item.Repository !== "<none>" && item.Tag !== "<none>")
            return item.Repository + ":" + item.Tag
        if (item.Names)
            return item.Names
        return qsTr("<senza tag>")
    }

    function imageCreated(item) {
        if (!item) return ""
        var value = item.CreatedAt || item.CreatedSince || item.Created || ""
        if (value === "") return ""
        var numeric = Number(value)
        var date = isNaN(numeric) ? new Date(value) : new Date(numeric * 1000)
        if (!isNaN(date.getTime()))
            return Qt.formatDateTime(date, "dd/MM/yyyy HH:mm")
        return value
    }

    function imageSize(item) {
        if (!item) return qsTr("n/d")
        return root.humanSize(item.Size !== undefined ? item.Size : item.VirtualSize)
    }

    function runAction(mode, name) {
        root.refreshAfterAction = true
        utilityBackend.runPodman(mode, name)
    }

    Component.onCompleted: if (SystemBackend.programAvailable("podman")) root.refresh()

    Connections {
        target: utilityBackend
        function onStateChanged() {
            if (utilityBackend.busy)
                return
            if (root.refreshAfterAction) {
                root.refreshAfterAction = false
                refreshTimer.restart()
            } else {
                root.applyResult()
            }
        }
    }

    Timer {
        id: refreshTimer
        interval: 250
        repeat: false
        onTriggered: root.refresh()
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Controls.TabBar {
            id: podmanTabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            currentIndex: 0
            Controls.TabButton {
                implicitHeight: Kirigami.Units.gridUnit * 2.1
                text: qsTr("Container")
                font.bold: true
                onClicked: {
                    root.mode = "containers"
                    root.refresh()
                }
            }
            Controls.TabButton {
                implicitHeight: Kirigami.Units.gridUnit * 2.1
                text: qsTr("Immagini")
                font.bold: true
                onClicked: {
                    root.mode = "images"
                    root.refresh()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: qsTr("Aggiorna")
                icon.name: "view-refresh"
                enabled: !utilityBackend.busy && SystemBackend.programAvailable("podman")
                onClicked: root.refresh()
            }
            Item { Layout.fillWidth: true }
            Controls.Label {
                opacity: UiMetrics.secondaryOpacity
                text: root.mode === "images"
                      ? qsTr("%1 immagini").arg(root.images.length)
                      : qsTr("%1 container").arg(root.containers.length)
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !SystemBackend.programAvailable("podman")
            type: Kirigami.MessageType.Information
            text: qsTr("Podman non è installato nel sistema.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.parseError.length > 0
            type: Kirigami.MessageType.Error
            text: root.parseError
        }

        Controls.BusyIndicator {
            visible: utilityBackend.busy
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.mode === "containers"

            Repeater {
                model: root.containers
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
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                                text: root.containerName(modelData)
                                elide: Text.ElideRight
                            }
                            Controls.Label {
                                font.bold: false
                                opacity: UiMetrics.secondaryOpacity
                                text: root.containerState(modelData)
                            }
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: root.containerImage(modelData)
                            opacity: UiMetrics.secondaryOpacity
                            elide: Text.ElideMiddle
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: qsTr("Dimensione: %1").arg(root.containerSize(modelData))
                            opacity: UiMetrics.secondaryOpacity
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Controls.Button { text: qsTr("Info"); enabled: !utilityBackend.busy; icon.name: "documentinfo"; onClicked: utilityBackend.runPodman("info", root.containerName(modelData)) }
                            Controls.Button { text: qsTr("Log"); enabled: !utilityBackend.busy; icon.name: "text-x-log"; onClicked: utilityBackend.runPodman("logs", root.containerName(modelData)) }
                            Item { Layout.fillWidth: true }
                            Controls.Button { icon.name: "media-playback-start"; text: qsTr("Avvia"); enabled: !utilityBackend.busy; onClicked: root.runAction("start", root.containerName(modelData)) }
                            Controls.Button { icon.name: "media-playback-stop"; text: qsTr("Ferma"); enabled: !utilityBackend.busy; onClicked: root.runAction("stop", root.containerName(modelData)) }
                            Controls.Button { icon.name: "view-refresh"; text: qsTr("Riavvia"); enabled: !utilityBackend.busy; onClicked: root.runAction("restart", root.containerName(modelData)) }
                            Controls.Button {
                                icon.name: "edit-delete"
                                text: qsTr("Elimina")
                                enabled: !utilityBackend.busy
                                onClicked: {
                                    root.selectedName = root.containerName(modelData)
                                    containerRemoveDialog.open()
                                }
                            }
                            Controls.Button { icon.name: "edit-rename";
                                text: qsTr("Rinomina")
                                enabled: !utilityBackend.busy
                                onClicked: {
                                    root.selectedName = root.containerName(modelData)
                                    renameField.text = root.selectedName
                                    renameDialog.open()
                                }
                            }
                        }
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: SystemBackend.programAvailable("podman") && !utilityBackend.busy && root.containers.length === 0 && root.parseError.length === 0
                type: Kirigami.MessageType.Information
                text: qsTr("Nessun container Podman presente per l'utente.")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.mode === "images"

            Repeater {
                model: root.images
                delegate: Kirigami.AbstractCard {
                    required property var modelData
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Controls.Label {
                                Layout.fillWidth: true
                                font.bold: false
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                                text: root.imageName(modelData)
                                elide: Text.ElideMiddle
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: UiMetrics.secondaryOpacity
                                text: qsTr("ID %1").arg(root.imageId(modelData).substring(0, 20))
                                elide: Text.ElideRight
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                opacity: UiMetrics.secondaryOpacity
                                text: [root.imageCreated(modelData), root.imageSize(modelData)].filter(function(x) { return !!x }).join(" · ")
                            }
                        }
                        Controls.Button {
                            text: qsTr("Elimina")
                            icon.name: "edit-delete"
                            enabled: !utilityBackend.busy && root.imageId(modelData).length > 0
                            onClicked: {
                                root.selectedImageId = root.imageId(modelData)
                                root.selectedImageName = root.imageName(modelData)
                                imageRemoveDialog.open()
                            }
                        }
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: SystemBackend.programAvailable("podman") && !utilityBackend.busy && root.images.length === 0 && root.parseError.length === 0
                type: Kirigami.MessageType.Information
                text: qsTr("Nessuna immagine Podman presente per l'utente.")
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            visible: utilityBackend.operationId.indexOf("podman.") === 0
                  && utilityBackend.operationId !== "podman.list"
                  && utilityBackend.operationId !== "podman.images"
                  && utilityBackend.output.length > 0
            contentItem: ColumnLayout {
                Controls.Label { font.bold: false; text: utilityBackend.title }
                OutputCard {
                    embedded: true
                    outputText: utilityBackend.output
                }
            }
        }
    }

    Controls.Dialog {
        id: containerRemoveDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Eliminare il container?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("%1\n\nIl container viene rimosso senza --force. Se è in esecuzione Podman rifiuterà l'operazione.").arg(root.selectedName)
        }
        onAccepted: root.runAction("remove", root.selectedName)
    }

    Controls.Dialog {
        id: renameDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Rinomina container")
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
        contentItem: ColumnLayout {
            Controls.Label { text: qsTr("Nuovo nome per %1").arg(root.selectedName) }
            Controls.TextField { id: renameField; Layout.fillWidth: true; selectByMouse: true }
        }
        onAccepted: {
            var next = renameField.text.trim()
            if (next.length > 0 && next !== root.selectedName) {
                root.refreshAfterAction = true
                utilityBackend.runPodman("rename", root.selectedName, next)
            }
        }
    }

    Controls.Dialog {
        id: imageRemoveDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Eliminare l'immagine?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("%1\n\nL'immagine viene rimossa senza --force. Se è ancora usata da un container Podman rifiuterà l'operazione.").arg(root.selectedImageName)
        }
        onAccepted: {
            root.refreshAfterAction = true
            utilityBackend.runPodman("image-remove", root.selectedImageId)
        }
    }
}