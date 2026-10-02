import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ScrollablePage {
    id: root
    padding: UiMetrics.pageMargin
    title: qsTr("Backup e recovery")

    function overlayLabel() {
        if (RkBackend.busy) return qsTr("Verifica…")
        if (!RkBackend.statusValid) return qsTr("Non disponibile")
        return RkBackend.overlayState === "ready" ? qsTr("Pronto") : qsTr("Degradato")
    }

    Component.onCompleted: RkBackend.refreshStatus()

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    source: "document-save-all"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        level: 2
                        font.bold: true
                        text: qsTr("Backup personali")
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: UiMetrics.secondaryOpacity
                        text: qsTr("Per i backup dei file personali KrisOS usa Back In Time, applicazione grafica dedicata.")
                    }
                }

                Controls.Button {
                    text: qsTr("Apri Back In Time")
                    icon.name: "document-save-all"
                    enabled: SystemBackend.toolAvailable("backintime")
                    onClicked: SystemBackend.launchTool("backintime")
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
                        spacing: Kirigami.Units.smallSpacing
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
                            Controls.Label { font.bold: false; text: qsTr("Da sincronizzare") }
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
                        validator: RegularExpressionValidator { regularExpression: /^(?!.*\.(?:rpm|i686|x86_64|noarch)$)[A-Za-z0-9][A-Za-z0-9+_.-]{0,127}$/ }
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
                    outputText: RkBackend.statusText
                }
            }
        }
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
