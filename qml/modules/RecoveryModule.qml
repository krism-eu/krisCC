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

    function overlayLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        return RkBackend.overlayState === "ready" ? qsTr("Pronto") : qsTr("Degradato")
    }

    Component.onCompleted: {
        refreshBackups()
        RkBackend.refreshStatus()
    }

    Connections {
        target: SystemBackend
        function onBackupStatusChanged() {
            if (!SystemBackend.backupBusy)
                root.refreshBackups()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        PageIntro { title: root.title; subtitle: qsTr("Archivi locali tar.gz in ~/krisCC Backups. Configurazione e home restano dati utente e non modificano il deployment BootC.") }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Crea backup") }

                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label { text: qsTr("Profilo:"); font.bold: false }
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
                    opacity: UiMetrics.secondaryOpacity
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
                            opacity: UiMetrics.secondaryOpacity
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
                                    opacity: UiMetrics.secondaryOpacity
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
                                Controls.Label { Layout.fillWidth: true; font.bold: false; text: modelData.name }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    opacity: UiMetrics.secondaryOpacity
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
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Recovery layer RPM") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Stato strutturato di rk. Le azioni vengono abilitate solo quando il contratto KrisOS le consente.")
                        }
                    }
                    Controls.Button {
                        text: qsTr("Aggiorna")
                        icon.name: "view-refresh"
                        enabled: !RkBackend.busy && !RkBackend.operationRunning
                        onClicked: RkBackend.refreshStatus()
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: RkBackend.errorText.length > 0
                    type: Kirigami.MessageType.Error
                    text: RkBackend.errorText
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 760 ? 3 : 1
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Controls.Label { font.bold: false; text: qsTr("Overlay /usr") }
                            Controls.Label { font.bold: false; text: root.overlayLabel() }
                        }
                    }
                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Controls.Label { font.bold: false; text: qsTr("Recovery pendente") }
                            Controls.Label {
                                font.bold: false
                                text: !RkBackend.statusValid ? qsTr("Non disponibile")
                                      : RkBackend.pendingRecovery ? qsTr("Sì · riavvio richiesto") : qsTr("No")
                            }
                        }
                    }
                    Kirigami.AbstractCard {
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            Controls.Label { font.bold: false; text: qsTr("Needs sync") }
                            Controls.Label {
                                font.bold: false
                                text: !RkBackend.statusValid ? qsTr("Non disponibile")
                                      : RkBackend.needsSync ? qsTr("Sì") : qsTr("No")
                            }
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: RkBackend.statusValid
                          && (RkBackend.overlayState === "degraded"
                              || RkBackend.pendingRecovery
                              || RkBackend.needsSync)
                    type: RkBackend.overlayState === "degraded" || RkBackend.pendingRecovery
                          ? Kirigami.MessageType.Error : Kirigami.MessageType.Warning
                    text: RkBackend.pendingRecovery
                          ? qsTr("È presente una transazione interrotta. Riavvia il sistema prima di eseguire altre operazioni rk.")
                          : RkBackend.overlayState === "degraded"
                            ? qsTr("L'overlay /usr è degradato: sync e forget restano disabilitati finché il layer non torna sano.")
                            : qsTr("Il deployment richiede il ripristino delle richieste RPM persistenti.")
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: RkBackend.statusValid && RkBackend.requests.length > 0
                          ? qsTr("Richieste persistenti: %1").arg(RkBackend.requests.join(", "))
                          : qsTr("Nessuna richiesta persistente rilevata.")
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Controls.Button {
                        text: qsTr("Sincronizza")
                        icon.name: "view-refresh"
                        enabled: RkBackend.canSync && !RkBackend.operationRunning
                        onClicked: syncDialog.open()
                    }
                    Controls.BusyIndicator {
                        visible: RkBackend.busy || RkBackend.operationRunning
                        running: visible
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: UiMetrics.secondaryOpacity
                    text: qsTr("Se rk sync segnala una richiesta non più disponibile, puoi dimenticare solo quella richiesta. L'operazione non disinstalla direttamente RPM già presenti.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    Controls.TextField {
                        id: forgetPackageField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Nome richiesta non disponibile")
                        validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}$/ }
                        enabled: RkBackend.canForget && !RkBackend.operationRunning
                        onAccepted: {
                            if (acceptableInput) {
                                forgetDialog.packageName = text.trim()
                                forgetDialog.open()
                            }
                        }
                    }
                    Controls.Button {
                        text: qsTr("Dimentica")
                        icon.name: "edit-delete"
                        enabled: forgetPackageField.acceptableInput
                              && RkBackend.canForget
                              && !RkBackend.operationRunning
                        onClicked: {
                            forgetDialog.packageName = forgetPackageField.text.trim()
                            forgetDialog.open()
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: RkBackend.operationState !== "idle"
                    type: RkBackend.operationState === "success" ? Kirigami.MessageType.Positive
                          : RkBackend.operationState === "error" ? Kirigami.MessageType.Error
                          : Kirigami.MessageType.Information
                    text: RkBackend.operationRunning
                          ? qsTr("Operazione amministrativa in corso…")
                          : (RkBackend.operationOutput.length > 0
                             ? RkBackend.operationOutput
                             : qsTr("Operazione completata."))
                }

                Controls.CheckBox {
                    id: rkTechnicalDetails
                    text: qsTr("Dettagli tecnici rk")
                }
                OutputCard {
                    visible: rkTechnicalDetails.checked
                    embedded: true
                    text: RkBackend.statusText
                }
            }
        }
    }

    Controls.Dialog {
        id: homeDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
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
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
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
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Risincronizzare il layer RPM?")
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Esegue rk sync con autorizzazione amministrativa. È disponibile solo con overlay sano, needs-sync attivo e nessuna transazione interrotta.")
        }
        onAccepted: RkBackend.sync()
    }

    Controls.Dialog {
        id: forgetDialog
        property string packageName: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Dimenticare la richiesta %1?").arg(packageName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Rimuove solo la richiesta persistente salvata. Non disinstalla direttamente RPM già presenti e non chiude il recovery: dopo va eseguita la sincronizzazione.")
        }
        onAccepted: {
            RkBackend.forget(packageName)
            forgetPackageField.clear()
        }
    }
}
