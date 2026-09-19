import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Backup e recovery")

    UtilityBackend { id: utilityBackend }

    property int backupProfileIndex: 0
    property var backupFiles: []
    property string restorePath: ""
    property string restoreName: ""
    property string restoreKind: ""
    property bool ownRkAction: false

    function humanSize(bytes) {
        if (!bytes || bytes <= 0) return "0 B"
        if (bytes >= 1024 * 1024 * 1024) return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
        if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KiB"
        return bytes + " B"
    }

    function refreshBackups() {
        root.backupFiles = SystemBackend.backups()
    }

    Component.onCompleted: refreshBackups()

    Connections {
        target: SystemBackend
        function onBackupStatusChanged() {
            if (!SystemBackend.backupBusy)
                root.refreshBackups()
        }
    }

    Connections {
        target: PolkitHelper
        function onFinished(success, output) {
            if (!root.ownRkAction)
                return
            root.ownRkAction = false
            utilityBackend.runBookmark("rk-status")
            BootcBackend.refreshPackages()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Crea backup") }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: qsTr("Archivi locali tar.gz in ~/krisCC Backups. Configurazione e home restano dati utente e non modificano il deployment BootC.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label { text: qsTr("Profilo:"); font.bold: true }
                    Controls.ComboBox {
                        id: backupProfile
                        Layout.preferredWidth: 260
                        model: [qsTr("Configurazione utente"), qsTr("Home personale")]
                        onCurrentIndexChanged: root.backupProfileIndex = currentIndex
                    }
                    Item { Layout.fillWidth: true }
                    Controls.Button {
                        text: qsTr("Apri cartella")
                        icon.name: "folder-open"
                        onClicked: SystemBackend.openBackupFolder()
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: backupProfile.currentIndex === 0
                          ? qsTr("Include le configurazioni utente supportate. Minimo 1 GiB libero.")
                          : qsTr("Include la home, escludendo cache, cestino e backup precedenti. Minimo 5 GiB liberi.")
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        Kirigami.Heading { level: 3; font.bold: true; text: qsTr("Contenuto del backup") }
                        Controls.Label {
                            Layout.fillWidth: true
                            opacity: 0.72
                            text: qsTr("Destinazione: ~/krisCC Backups")
                        }
                        Repeater {
                            model: {
                                root.backupProfileIndex
                                return SystemBackend.backupPreview(backupProfile.currentIndex === 0 ? "config" : "home")
                            }
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Kirigami.Icon {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    source: modelData.included ? "dialog-ok-apply" : "list-remove"
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    font.bold: modelData.included
                                    text: (modelData.included ? qsTr("Incluso: ") : qsTr("Escluso: ")) + modelData.path
                                }
                                Controls.Label {
                                    visible: modelData.exists !== undefined
                                    opacity: 0.72
                                    text: modelData.exists ? qsTr("presente") : qsTr("assente")
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Controls.Button {
                        text: qsTr("Crea backup")
                        icon.name: "document-save-all"
                        enabled: !SystemBackend.backupBusy
                        onClicked: backupProfile.currentIndex === 0
                                   ? SystemBackend.createSnapshot("config")
                                   : homeDialog.open()
                    }
                    Controls.Button {
                        visible: SystemBackend.backupBusy
                        text: qsTr("Annulla")
                        icon.name: "process-stop"
                        onClicked: SystemBackend.cancelSnapshot()
                    }
                    Controls.BusyIndicator { visible: SystemBackend.backupBusy; running: visible }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: SystemBackend.backupStatus.length > 0
                    type: SystemBackend.backupState === "success" ? Kirigami.MessageType.Positive
                          : SystemBackend.backupState === "warning" ? Kirigami.MessageType.Warning
                          : SystemBackend.backupState === "error" ? Kirigami.MessageType.Error
                          : Kirigami.MessageType.Information
                    text: SystemBackend.backupStatus + (SystemBackend.backupPath.length > 0 ? "\n" + SystemBackend.backupPath : "")
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("Backup disponibili") }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        onClicked: root.refreshBackups()
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.backupFiles.length === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessun backup creato da krisCC.")
                }

                Repeater {
                    model: root.backupFiles
                    delegate: Kirigami.AbstractCard {
                        required property var modelData
                        Layout.fillWidth: true
                        contentItem: RowLayout {
                            ColumnLayout {
                                Layout.fillWidth: true
                                Controls.Label { Layout.fillWidth: true; font.bold: true; text: modelData.name }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    opacity: 0.72
                                    text: (modelData.kind === "home" ? qsTr("Home") : qsTr("Configurazione"))
                                          + " · " + root.humanSize(modelData.size)
                                          + " · " + modelData.modified
                                }
                            }
                            Controls.Button {
                                text: qsTr("Verifica")
                                icon.name: "dialog-ok"
                                enabled: !SystemBackend.backupBusy
                                onClicked: SystemBackend.verifySnapshot(modelData.path)
                            }
                            Controls.Button {
                                text: qsTr("Ripristina")
                                icon.name: "edit-undo"
                                enabled: !SystemBackend.backupBusy
                                onClicked: {
                                    root.restorePath = modelData.path
                                    root.restoreName = modelData.name
                                    root.restoreKind = modelData.kind
                                    restoreDialog.open()
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
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Recovery KrisOS") }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: qsTr("KrisOS mantiene un solo deployment supportato. Il recovery del layer RPM usa il contratto rk: stato e risincronizzazione delle richieste persistenti, senza reset distruttivi automatici.")
                }
                RowLayout {
                    Controls.Button {
                        text: qsTr("Mostra stato rk")
                        icon.name: "documentinfo"
                        enabled: !utilityBackend.busy
                        onClicked: utilityBackend.runBookmark("rk-status")
                    }
                    Controls.Button {
                        text: qsTr("Risincronizza pacchetti")
                        icon.name: "view-refresh"
                        enabled: !PolkitHelper.running
                        onClicked: syncDialog.open()
                    }
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.72
                    text: qsTr("Se una richiesta persistente non è più disponibile, puoi dimenticarla senza disinstallare direttamente gli RPM già presenti.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.TextField {
                        id: forgetPackageField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Nome richiesta, es. pacchetto")
                        validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}$/ }
                        onAccepted: {
                            if (acceptableInput && !PolkitHelper.running) {
                                forgetDialog.packageName = text.trim()
                                forgetDialog.open()
                            }
                        }
                    }
                    Controls.Button {
                        text: qsTr("Dimentica richiesta")
                        icon.name: "edit-delete"
                        enabled: forgetPackageField.acceptableInput && !PolkitHelper.running
                        onClicked: {
                            forgetDialog.packageName = forgetPackageField.text.trim()
                            forgetDialog.open()
                        }
                    }
                }
                Controls.TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    visible: utilityBackend.operationId === "bookmark.rk-status"
                    readOnly: true
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    font.family: Kirigami.Theme.defaultFixedWidthFont.family
                    text: utilityBackend.output
                }
            }
        }
    }

    Controls.Dialog {
        id: homeDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Creare il backup della home?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("La home può essere grande e contenere dati sensibili. Cache, cestino, runtime/app Flatpak (~/.local/share/flatpak), storage Podman inclusi volumi (~/.local/share/containers) e backup precedenti vengono esclusi. I dati personali delle app Flatpak in ~/.var/app restano inclusi.")
        }
        onAccepted: SystemBackend.createSnapshot("home")
    }

    Controls.Dialog {
        id: restoreDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Ripristinare %1?").arg(root.restoreName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: root.restoreKind === "home"
                  ? qsTr("ATTENZIONE: il ripristino della home sovrascrive i file esistenti con lo stesso percorso. Runtime/app Flatpak e storage Podman esclusi dal backup non vengono ripristinati; i dati in ~/.var/app possono invece essere sovrascritti. Il deployment KrisOS non viene modificato.")
                  : qsTr("Le configurazioni esistenti con lo stesso percorso possono essere sovrascritte. Il ripristino avviene come utente, senza modificare il deployment KrisOS.")
        }
        onAccepted: SystemBackend.restoreSnapshot(root.restorePath)
    }

    Controls.Dialog {
        id: syncDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Risincronizzare il layer RPM?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Esegue rk sync con autorizzazione amministrativa sulle richieste persistenti già salvate.")
        }
        onAccepted: {
            root.ownRkAction = true
            PolkitHelper.execute("/usr/bin/rk", ["sync"])
        }
    }

    Controls.Dialog {
        id: forgetDialog
        property string packageName: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Dimenticare la richiesta %1?").arg(packageName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Rimuove solo la richiesta persistente salvata. Non disinstalla direttamente gli RPM già presenti; il layer verrà riallineato con la successiva sincronizzazione.")
        }
        onAccepted: {
            root.ownRkAction = true
            PolkitHelper.execute("/usr/bin/rk", ["forget", packageName])
            forgetPackageField.clear()
        }
    }
}
