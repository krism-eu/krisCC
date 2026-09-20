import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root
    title: qsTr("Panoramica")
    signal openRequested(string pageId)

    function overlayLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        return RkBackend.overlayState === "ready" ? qsTr("Pronto") : qsTr("Degradato")
    }

    function syncLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        if (RkBackend.pendingRecovery) return qsTr("Riavvio richiesto")
        return RkBackend.needsSync ? qsTr("Da sincronizzare") : qsTr("Allineato")
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Kirigami.Heading {
                    level: 1
                    font.bold: true
                    text: qsTr("Panoramica")
                }
                Controls.Label {
                    opacity: 0.72
                    text: qsTr("Stato essenziale di KrisOS e accesso rapido alle attività quotidiane.")
                }
            }
            Controls.Button {
                text: qsTr("Aggiorna stato")
                icon.name: "view-refresh"
                enabled: !RkBackend.busy && !BootcBackend.busy
                onClicked: {
                    RkBackend.refreshStatus()
                    BootcBackend.refreshStatus()
                    BootcBackend.refreshPackages()
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !BootcBackend.bootcAvailable
            type: Kirigami.MessageType.Warning
            text: qsTr("bootc non è disponibile in questo ambiente. Le funzioni image-based sono disabilitate.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: RkBackend.errorText.length > 0
            type: Kirigami.MessageType.Warning
            text: qsTr("Stato del layer RPM non disponibile: %1").arg(RkBackend.errorText)
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
                  ? qsTr("È presente una transazione RPM interrotta: riavvia prima di altre operazioni rk.")
                  : RkBackend.overlayState === "degraded"
                    ? qsTr("L'overlay /usr è degradato. Apri Recovery per i dettagli.")
                    : qsTr("Il layer RPM richiede una sincronizzazione sul deployment corrente.")
            actions: [
                Kirigami.Action {
                    text: qsTr("Apri Recovery")
                    icon.name: "edit-undo"
                    onTriggered: root.openRequested("recovery")
                }
            ]
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 1000 ? 4 : width > 620 ? 2 : 1
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "drive-multidisk" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("Overlay /usr") }
                        Controls.Label { font.bold: true; text: root.overlayLabel() }
                        Controls.Label {
                            opacity: 0.65
                            text: RkBackend.statusValid && RkBackend.overlayState === "ready"
                                  ? qsTr("Layer RPM operativo") : qsTr("Controlla Recovery")
                        }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "security-high" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("SELinux") }
                        Controls.Label { font.bold: true; text: SystemBackend.selinuxState }
                        Controls.Label { opacity: 0.65; text: qsTr("Protezione del sistema") }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "view-refresh" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("Sincronizzazione") }
                        Controls.Label { font.bold: true; text: root.syncLabel() }
                        Controls.Label {
                            opacity: 0.65
                            text: RkBackend.statusValid
                                  ? qsTr("%1 richieste persistenti").arg(RkBackend.requests.length)
                                  : qsTr("Stato rk non disponibile")
                        }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "drive-harddisk" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("Spazio") }
                        Controls.Label { font.bold: true; text: qsTr("Storage dati") }
                        Controls.Label { Layout.fillWidth: true; opacity: 0.65; elide: Text.ElideRight; text: SystemBackend.storageSummary }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "speedometer" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("CPU") }
                        Controls.Label {
                            font.bold: true
                            text: SystemBackend.cpuUsagePercent >= 0
                                  ? qsTr("%1%").arg(SystemBackend.cpuUsagePercent)
                                  : qsTr("n/d")
                        }
                        Controls.Label { opacity: 0.65; text: qsTr("Utilizzo corrente") }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "media-flash-memory-stick" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("RAM usata") }
                        Controls.Label {
                            font.bold: true
                            text: SystemBackend.memoryUsedMiB >= 0
                                  ? qsTr("%1 MiB").arg(SystemBackend.memoryUsedMiB)
                                  : qsTr("n/d")
                        }
                        Controls.Label {
                            opacity: 0.65
                            text: SystemBackend.memoryTotalMiB >= 0
                                  ? qsTr("su %1 MiB · swap esclusa").arg(SystemBackend.memoryTotalMiB)
                                  : qsTr("swap esclusa")
                        }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Kirigami.Icon { Layout.preferredWidth: 32; Layout.preferredHeight: 32; source: "temperature" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Controls.Label { font.bold: true; text: qsTr("Temperatura CPU") }
                        Controls.Label {
                            font.bold: true
                            text: SystemBackend.cpuTemperatureC >= 0
                                  ? qsTr("%1 °C").arg(SystemBackend.cpuTemperatureC.toFixed(0))
                                  : qsTr("n/d")
                        }
                        Controls.Label { opacity: 0.65; text: qsTr("Sensore hardware CPU") }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 760 ? 2 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Sistema") }
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: SystemBackend.osName }
                    Controls.Label { Layout.fillWidth: true; opacity: 0.7; text: SystemBackend.kernelVersion }
                    Controls.Label { Layout.fillWidth: true; opacity: 0.7; text: SystemBackend.storageSummary }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Controls.Button { text: qsTr("Salute"); icon.name: "tools-report-bug"; onClicked: root.openRequested("system") }
                        Controls.Button { text: qsTr("Info Center"); icon.name: "hwinfo"; enabled: SystemBackend.toolAvailable("kinfocenter"); onClicked: SystemBackend.launchTool("kinfocenter") }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Aggiornamenti") }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: BootcBackend.bootcAvailable ? qsTr("KrisOS image-based attivo") : qsTr("BootC non rilevato")
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        opacity: 0.7
                        text: qsTr("%1 pacchetti RPM persistenti").arg(BootcBackend.persistentPackageCount)
                    }
                    Item { Layout.fillHeight: true }
                    Controls.Button {
                        text: qsTr("Apri centro aggiornamenti")
                        icon.name: "system-software-update"
                        onClicked: root.openRequested("system")
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Software") }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("RPM persistenti tramite rk e applicazioni Flatpak utente.")
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Controls.Button { text: qsTr("RPM"); icon.name: "system-software-install"; onClicked: root.openRequested("software") }
                        Controls.Button { text: qsTr("Flatpak"); icon.name: "package-x-generic"; onClicked: root.openRequested("flatpak") }
                    }
                }
            }

            Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentItem: ColumnLayout {
                    Kirigami.Heading { level: 2; font.bold: true; text: qsTr("Container") }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: SystemBackend.programAvailable("podman") ? qsTr("Podman disponibile per l'utente corrente") : qsTr("Podman non installato")
                    }
                    Item { Layout.fillHeight: true }
                    Controls.Button {
                        text: qsTr("Apri Container")
                        icon.name: "package-x-generic"
                        enabled: SystemBackend.programAvailable("podman")
                        onClicked: root.openRequested("podman")
                    }
                }
            }
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading { Layout.fillWidth: true; level: 2; font.bold: true; text: qsTr("Informazioni rapide") }
                    Controls.Button {
                        text: qsTr("Copia")
                        icon.name: "edit-copy"
                        onClicked: SystemBackend.copyToClipboard(quickInfo.text)
                    }
                }
                Controls.TextArea {
                    id: quickInfo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    readOnly: true
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    font.family: Kirigami.Theme.defaultFixedWidthFont.family
                    text: SystemBackend.quickSystemInfo()
                }
                RowLayout {
                    Controls.Button { text: qsTr("Comandi utili"); icon.name: "utilities-terminal"; onClicked: root.openRequested("commands") }
                    Controls.Button { text: qsTr("Backup / recovery"); icon.name: "document-save-all"; onClicked: root.openRequested("recovery") }
                    Item { Layout.fillWidth: true }
                    Controls.Button { text: qsTr("Impostazioni Plasma"); icon.name: "settings-configure"; enabled: SystemBackend.toolAvailable("systemsettings"); onClicked: SystemBackend.launchTool("systemsettings") }
                }
            }
        }
    }
}
