import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Controls.Basic as Basic
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Comandi")

    UtilityBackend { id: utilityBackend }

    property string pendingCustomId: ""
    property string pendingCustomName: ""
    property string deleteCustomId: ""
    property string deleteCustomName: ""

    property var commands: [
        { id: "failed-units", title: qsTr("Unità di sistema fallite"), command: "systemctl --failed --no-pager --plain", note: qsTr("Servizi e unità systemd in errore.") },
        { id: "user-failed-units", title: qsTr("Unità utente fallite"), command: "systemctl --user --failed --no-pager --plain", note: qsTr("Servizi della sessione utente in errore.") },
        { id: "journal-errors", title: qsTr("Errori ultimo avvio"), command: "journalctl -b -p warning --no-pager -n 200", note: qsTr("Warning ed errori recenti del sistema.") },
        { id: "kernel-errors", title: qsTr("Warning kernel"), command: "journalctl -k -b -p warning --no-pager -n 200", note: qsTr("Messaggi kernel rilevanti.") },
        { id: "journal-size", title: qsTr("Spazio journal"), command: "journalctl --disk-usage", note: qsTr("Quanto spazio occupano i log persistenti.") },
        { id: "uptime", title: qsTr("Uptime"), command: "uptime -p", note: qsTr("Tempo trascorso dall'ultimo avvio.") },
        { id: "timers", title: qsTr("Timer systemd"), command: "systemctl list-timers --all --no-pager", note: qsTr("Timer e prossime attività pianificate.") },
        { id: "ports", title: qsTr("Porte in ascolto"), command: "ss -lntu", note: qsTr("Socket TCP/UDP in ascolto.") },
        { id: "network", title: qsTr("Interfacce rete"), command: "ip -brief address", note: qsTr("Interfacce e indirizzi in formato compatto.") },
        { id: "routes", title: qsTr("Route"), command: "ip route", note: qsTr("Tabella di routing corrente.") },
        { id: "dns", title: qsTr("DNS"), command: "resolvectl status", note: qsTr("Resolver e DNS per interfaccia.") },
        { id: "sessions", title: qsTr("Sessioni"), command: "loginctl list-sessions --no-legend", note: qsTr("Sessioni viste da systemd-logind.") },
        { id: "mounts", title: qsTr("Mount attivi"), command: "findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS", note: qsTr("Filesystem montati adesso.") },
        { id: "disk-space", title: qsTr("Spazio filesystem"), command: "df -hT -x tmpfs -x devtmpfs", note: qsTr("Utilizzo dei filesystem persistenti.") },
        { id: "inodes", title: qsTr("Inode filesystem"), command: "df -hi -x tmpfs -x devtmpfs", note: qsTr("Utile quando c'è spazio ma non si riescono più a creare file.") },
        { id: "partitions", title: qsTr("Dischi e partizioni"), command: "lsblk -e 7 -o NAME,PARTN,SIZE,FSTYPE,FSVER,LABEL,UUID,MOUNTPOINTS", note: qsTr("Dischi, partizioni, UUID e mount.") },
        { id: "selinux", title: qsTr("SELinux"), command: "getenforce", note: qsTr("Modalità SELinux attuale.") },
        { id: "boot-time", title: qsTr("Tempo di avvio"), command: "systemd-analyze time", note: qsTr("Tempo complessivo di avvio, utile per diagnosi occasionali.") },
        { id: "blame", title: qsTr("Servizi lenti"), command: "systemd-analyze blame", note: qsTr("Unità ordinate per tempo di avvio.") }
    ]

    function filteredCommands(filterText) {
        var query = filterText.trim().toLowerCase()
        if (query.length === 0)
            return root.commands
        var result = []
        for (var i = 0; i < root.commands.length; ++i) {
            var item = root.commands[i]
            var haystack = (item.title + " " + item.command + " " + item.note).toLowerCase()
            if (haystack.indexOf(query) >= 0)
                result.push(item)
        }
        return result
    }

    function stateLabel(state) {
        if (state === "running") return qsTr("In esecuzione")
        if (state === "success") return qsTr("Completato")
        if (state === "error") return qsTr("Errore")
        if (state === "timeout") return qsTr("Timeout")
        if (state === "cancelled") return qsTr("Annullato")
        return ""
    }

    function runCustom(action) {
        if (action.confirm) {
            root.pendingCustomId = action.id
            root.pendingCustomName = action.name
            runCustomDialog.open()
        } else {
            CustomActionsBackend.runAction(action.id)
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing
        Controls.TabBar {
            id: commandTabs
            Layout.fillWidth: true
            palette.highlight: Kirigami.Theme.highlightColor
            Controls.TabButton { text: qsTr("Predefiniti"); font.bold: true }
            Controls.TabButton { text: qsTr("Miei comandi"); font.bold: true }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: commandTabs.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    PageIntro { title: root.title; subtitle: qsTr("Comandi read-only difficili da ricordare ma utili nella diagnosi quotidiana. Le funzioni già coperte bene dalle pagine Flatpak, Container e dal Monitor di sistema non vengono duplicate qui.") }
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.TextField {
                            id: commandFilter
                            Layout.fillWidth: true
                            placeholderText: qsTr("Cerca per nome o comando…")
                        }
                        Controls.Button {
                            text: qsTr("Pulisci filtro")
                            icon.name: "edit-clear"
                            enabled: commandFilter.text.length > 0
                            onClicked: commandFilter.clear()
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 820 ? 2 : 1
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: root.filteredCommands(commandFilter.text)
                        delegate: Kirigami.AbstractCard {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: root.width > 820
                                                   ? (root.width - Kirigami.Units.largeSpacing) / 2
                                                   : root.width
                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing
                                RowLayout {
                                    Layout.fillWidth: true
                                    Controls.Label { Layout.fillWidth: true; font.bold: false; text: modelData.title }
                                    Controls.Button {
                                        flat: true
                                        icon.name: "edit-copy"
                                        display: Controls.AbstractButton.IconOnly
                                        onClicked: SystemBackend.copyToClipboard(modelData.command)
                                    }
                                    Controls.Button {
                                        text: qsTr("Esegui")
                                        icon.name: "utilities-terminal"
                                        enabled: !utilityBackend.busy
                                        onClicked: utilityBackend.runBookmark(modelData.id)
                                    }
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    font.family: Kirigami.Theme.fixedWidthFont.family
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    opacity: UiMetrics.secondaryOpacity
                                    text: modelData.command
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    opacity: UiMetrics.secondaryOpacity
                                    text: modelData.note
                                }
                            }
                        }
                    }
                }

                Controls.BusyIndicator {
                    visible: utilityBackend.busy
                    running: visible
                    Layout.alignment: Qt.AlignHCenter
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: utilityBackend.title.length > 0 || utilityBackend.output.length > 0
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading { Layout.fillWidth: true; level: 3; font.bold: true; text: utilityBackend.title || qsTr("Output") }
                            Controls.Label {
                                visible: utilityBackend.resultState !== "idle"
                                font.bold: false
                                text: root.stateLabel(utilityBackend.resultState)
                            }
                            Controls.Button {
                                visible: utilityBackend.busy
                                text: qsTr("Annulla")
                                icon.name: "process-stop"
                                onClicked: utilityBackend.cancel()
                            }
                            Controls.Button {
                                text: qsTr("Pulisci output")
                                icon.name: "edit-clear"
                                enabled: !utilityBackend.busy
                                onClicked: utilityBackend.clearResult()
                            }
                        }
                        OutputCard {
                    embedded: true
                    outputText: utilityBackend.output
                }
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Miei comandi") }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: UiMetrics.secondaryOpacity
                            text: qsTr("Comandi o script Bash personali salvati in ~/.config/krisCC. Restano nella home attraverso aggiornamenti RPM e BootC e rientrano nel backup della configurazione. Vengono eseguiti solo con i privilegi dell'utente corrente.")
                        }
                    }
                    Controls.Button {
                        text: qsTr("Nuovo")
                        icon.name: "list-add"
                        enabled: !CustomActionsBackend.running
                        onClicked: editActionDialog.openNew()
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: CustomActionsBackend.errorText.length > 0
                    type: Kirigami.MessageType.Error
                    text: CustomActionsBackend.errorText
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: CustomActionsBackend.actions.length === 0
                    type: Kirigami.MessageType.Information
                    text: qsTr("Nessun comando personale. Salva qui ciò che normalmente devi cercare o ricordare a memoria.")
                }

                Repeater {
                    model: CustomActionsBackend.actions
                    delegate: Kirigami.AbstractCard {
                        required property var modelData
                        Layout.fillWidth: true
                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        font.bold: false
                                        font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                                        text: modelData.name
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        visible: modelData.description.length > 0
                                        wrapMode: Text.WordWrap
                                        opacity: UiMetrics.secondaryOpacity
                                        text: modelData.description
                                    }
                                }
                                Controls.Button {
                                    text: qsTr("Modifica")
                                    icon.name: "document-edit"
                                    enabled: !CustomActionsBackend.running
                                    onClicked: editActionDialog.openFor(modelData)
                                }
                                Controls.Button {
                                    text: qsTr("Elimina")
                                    icon.name: "edit-delete"
                                    enabled: !CustomActionsBackend.running
                                    onClicked: {
                                        root.deleteCustomId = modelData.id
                                        root.deleteCustomName = modelData.name
                                        deleteCustomDialog.open()
                                    }
                                }
                                Controls.Button {
                                    text: CustomActionsBackend.runningId === modelData.id
                                          ? qsTr("In esecuzione…") : qsTr("Esegui")
                                    icon.name: "media-playback-start"
                                    enabled: !CustomActionsBackend.running
                                    onClicked: root.runCustom(modelData)
                                }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                font.family: Kirigami.Theme.fixedWidthFont.family
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 4
                                elide: Text.ElideRight
                                opacity: UiMetrics.secondaryOpacity
                                text: modelData.script
                            }
                            Controls.Label {
                                visible: modelData.confirm
                                opacity: UiMetrics.secondaryOpacity
                                text: qsTr("Richiede conferma prima dell'esecuzione")
                            }
                        }
                    }
                }

                Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    visible: CustomActionsBackend.running
                          || CustomActionsBackend.resultState !== "idle"
                          || CustomActionsBackend.output.length > 0
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading { Layout.fillWidth: true; level: 3; font.bold: true; text: qsTr("Output comando personale") }
                            Controls.Label {
                                font.bold: false
                                text: root.stateLabel(CustomActionsBackend.resultState)
                            }
                            Controls.Button {
                                visible: CustomActionsBackend.running
                                text: qsTr("Annulla")
                                icon.name: "process-stop"
                                onClicked: CustomActionsBackend.cancel()
                            }
                        }
                        Controls.BusyIndicator {
                            visible: CustomActionsBackend.running
                            running: visible
                            Layout.alignment: Qt.AlignHCenter
                        }
                        OutputCard {
                    embedded: true
                    outputText: CustomActionsBackend.output
                }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: editActionDialog
        property string actionId: ""
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 760)
        height: Math.min(root.height - 48, 650)
        title: actionId.length > 0 ? qsTr("Modifica comando personale") : qsTr("Nuovo comando personale")
        standardButtons: Controls.Dialog.Cancel

        function openNew() {
            actionId = ""
            actionName.text = ""
            actionDescription.text = ""
            actionScript.text = ""
            actionConfirm.checked = true
            open()
        }

        function openFor(action) {
            actionId = action.id
            actionName.text = action.name
            actionDescription.text = action.description
            actionScript.text = action.script
            actionConfirm.checked = action.confirm
            open()
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label { text: qsTr("Nome"); font.bold: false }
            Controls.TextField {
                id: actionName
                Layout.fillWidth: true
                placeholderText: qsTr("es. Riavvia PipeWire")
                selectByMouse: true
            }
            Controls.Label { text: qsTr("Descrizione") }
            Controls.TextField {
                id: actionDescription
                Layout.fillWidth: true
                placeholderText: qsTr("A cosa serve e quando usarlo")
                selectByMouse: true
            }
            Controls.Label { text: qsTr("Comando / script Bash"); font.bold: false }
            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 260
                // Breeze currently attaches a TextInput-only helper to multiline TextEdit.
                Basic.TextArea {
                    id: actionScript
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    placeholderText: qsTr("Puoi inserire più righe, pipe e sequenze di comandi.")
                    selectByMouse: true
                }
            }
            Controls.CheckBox {
                id: actionConfirm
                text: qsTr("Chiedi conferma prima dell'esecuzione")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: UiMetrics.secondaryOpacity
                text: qsTr("krisCC non aggiunge sudo o Polkit: lo script gira come il tuo utente.")
            }
            Controls.Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Salva")
                icon.name: "document-save"
                enabled: actionName.text.trim().length > 0 && actionScript.text.trim().length > 0
                onClicked: {
                    if (CustomActionsBackend.saveAction(
                            editActionDialog.actionId,
                            actionName.text,
                            actionDescription.text,
                            actionScript.text,
                            actionConfirm.checked))
                        editActionDialog.close()
                }
            }
        }
    }

    Controls.Dialog {
        id: runCustomDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Eseguire %1?").arg(root.pendingCustomName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label {
            wrapMode: Text.WordWrap
            text: qsTr("Lo script verrà eseguito con i privilegi del tuo utente.")
        }
        onAccepted: CustomActionsBackend.runAction(root.pendingCustomId)
    }

    Controls.Dialog {
        id: deleteCustomDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.largeSpacing * 2 : Kirigami.Units.gridUnit * 30)
        title: qsTr("Eliminare %1?").arg(root.deleteCustomName)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        onAccepted: CustomActionsBackend.removeAction(root.deleteCustomId)
    }
}