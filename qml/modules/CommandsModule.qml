import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Comandi utili")

    UtilityBackend { id: utilityBackend }

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
        { id: "top-cpu", title: qsTr("Top CPU"), command: "ps -eo pid,comm,%cpu,%mem --sort=-%cpu", note: qsTr("Processi ordinati per uso CPU.") },
        { id: "top-memory", title: qsTr("Top RAM"), command: "ps -eo pid,comm,%mem,%cpu --sort=-%mem", note: qsTr("Processi ordinati per memoria.") },
        { id: "selinux", title: qsTr("SELinux"), command: "getenforce", note: qsTr("Modalità SELinux attuale.") },
        { id: "flatpak-list", title: qsTr("Flatpak utente"), command: "flatpak list --user --app", note: qsTr("Applicazioni Flatpak del profilo utente.") },
        { id: "podman-images", title: qsTr("Immagini Podman"), command: "podman images", note: qsTr("Immagini container presenti per l'utente.") },
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

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Comandi quotidiani") }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.72
                text: qsTr("Diagnostica read-only per problemi comuni. Niente shell libera, input arbitrario o sudo generico.")
            }
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
                            Controls.Label { Layout.fillWidth: true; font.bold: true; text: modelData.title }
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
                            font.family: Kirigami.Theme.defaultFixedWidthFont.family
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            opacity: 0.82
                            text: modelData.command
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.72
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
                        font.bold: true
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
                    Controls.Button {
                        text: qsTr("Copia output")
                        icon.name: "edit-copy"
                        enabled: utilityBackend.output.length > 0
                        onClicked: SystemBackend.copyToClipboard(utilityBackend.output)
                    }
                }
                Controls.TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 340
                    readOnly: true
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    font.family: Kirigami.Theme.defaultFixedWidthFont.family
                    text: utilityBackend.output
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }
}
