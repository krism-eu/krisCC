import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Controls.Basic as Basic
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ApplicationWindow {
    id: root
    width: 1280
    height: 820
    minimumWidth: 920
    minimumHeight: 640
    visible: !KrisccStartHidden
    title: qsTr("krisCC")
    property int currentSection: 0

    readonly property var navigationModel: [
        { section: 0, label: qsTr("Dashboard"), icon: "go-home" },
        { section: 1, label: qsTr("Software"), icon: "package-x-generic" },
        { section: 2, label: qsTr("Flatpak"), icon: "applications-all" },
        { section: 3, label: qsTr("Container"), icon: "package" },
        { section: 4, label: qsTr("Sistema"), icon: "computer" },
        { section: 6, label: qsTr("Backup e Recovery"), icon: "document-save-all" },
        { section: 5, label: qsTr("Comandi"), icon: "utilities-terminal" }
    ]

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    function syncResourceMonitoring() {
        SystemBackend.setResourceMonitoringEnabled(root.visible && root.currentSection === 0)
    }

    onVisibleChanged: syncResourceMonitoring()
    onCurrentSectionChanged: syncResourceMonitoring()
    Component.onCompleted: syncResourceMonitoring()

    function showIndex(index) {
        if (index >= 0 && index <= 6)
            root.currentSection = index
    }

    function sectionTitle(index) {
        for (let i = 0; i < root.navigationModel.length; ++i) {
            if (root.navigationModel[i].section === index)
                return root.navigationModel[i].label
        }
        return qsTr("krisCC")
    }

    function openById(pageId) {
        if (pageId === "software") showIndex(1)
        else if (pageId === "flatpak") showIndex(2)
        else if (pageId === "podman") showIndex(3)
        else if (pageId === "system" || pageId === "bootc" || pageId === "tools") showIndex(4)
        else if (pageId === "commands") showIndex(5)
        else if (pageId === "recovery") showIndex(6)
        else showIndex(0)
    }

    pageStack.initialPage: Kirigami.Page {
        title: ""
        padding: 0

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: sidebar
                Layout.preferredWidth: root.width < 1080 ? 205 : 228
                Layout.fillHeight: true
                color: Kirigami.Theme.alternateBackgroundColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            Layout.preferredWidth: 46
                            Layout.preferredHeight: 46
                            source: "krisCC"
                            fallback: "security-high"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Controls.Label {
                                text: qsTr("krisCC")
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 4
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                text: qsTr("Centro di controllo KrisOS")
                                opacity: UiMetrics.secondaryOpacity
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Repeater {
                        model: root.navigationModel
                        delegate: Controls.ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            checkable: true
                            checked: root.currentSection === modelData.section
                            hoverEnabled: true
                            leftPadding: Kirigami.Units.largeSpacing
                            rightPadding: Kirigami.Units.largeSpacing
                            Accessible.name: modelData.label
                            onClicked: root.showIndex(modelData.section)

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing
                                Kirigami.Icon {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    source: modelData.icon
                                    color: parent.parent.checked
                                           ? Kirigami.Theme.highlightColor
                                           : Kirigami.Theme.textColor
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    font.bold: parent.parent.checked
                                    color: parent.parent.checked
                                           ? Kirigami.Theme.highlightColor
                                           : Kirigami.Theme.textColor
                                }
                            }

                            background: Rectangle {
                                radius: 9
                                color: parent.checked
                                     ? Qt.rgba(Kirigami.Theme.highlightColor.r,
                                               Kirigami.Theme.highlightColor.g,
                                               Kirigami.Theme.highlightColor.b, 0.12)
                                     : parent.hovered
                                       ? Qt.rgba(Kirigami.Theme.textColor.r,
                                                 Kirigami.Theme.textColor.g,
                                                 Kirigami.Theme.textColor.b, 0.05)
                                       : "transparent"

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 3
                                    height: parent.height - 12
                                    radius: 2
                                    visible: parent.parent.checked
                                    color: Kirigami.Theme.highlightColor
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Controls.ItemDelegate {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        text: qsTr("Impostazioni Plasma")
                        icon.name: "settings-configure"
                        onClicked: SystemBackend.launchTool("systemsettings")
                    }

                    Controls.ItemDelegate {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        text: qsTr("Informazioni")
                        icon.name: "help-about"
                        onClicked: informationDialog.open()
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Qt.rgba(Kirigami.Theme.textColor.r,
                               Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.10)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    color: Kirigami.Theme.backgroundColor

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.leftMargin: UiMetrics.pageMargin
                        anchors.right: versionLabel.left
                        anchors.rightMargin: Kirigami.Units.largeSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Controls.Label {
                            Layout.fillWidth: true
                            text: root.sectionTitle(root.currentSection)
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize + 3
                            elide: Text.ElideRight
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: qsTr("KrisOS Control Center")
                            opacity: UiMetrics.secondaryOpacity
                            elide: Text.ElideRight
                        }
                    }

                    Controls.Label {
                        id: versionLabel
                        anchors.right: parent.right
                        anchors.rightMargin: UiMetrics.pageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("v%1").arg(Qt.application.version)
                        opacity: UiMetrics.secondaryOpacity
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                       Kirigami.Theme.textColor.g,
                                       Kirigami.Theme.textColor.b, 0.10)
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.currentSection

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 0)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { DashboardModule {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onOpenRequested: function(pageId) { root.openById(pageId) }
                    } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 1)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { SoftwareModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 2)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { FlatpakModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 3)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { PodmanModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 4)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { SystemModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 5)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { CommandsModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property bool visited: false
                        active: visited || ((root.visible || KrisccSmokeTest) && root.currentSection === 6)
                        onLoaded: Qt.callLater(function() { visited = true })
                        sourceComponent: Component { RecoveryModule { Layout.fillWidth: true; Layout.fillHeight: true } }
                    }
                }
            }
        }
    }

    Controls.Dialog {
        id: informationDialog
        modal: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Kirigami.Units.gridUnit * 34,
                        parent ? parent.width - Kirigami.Units.largeSpacing * 2
                               : Kirigami.Units.gridUnit * 34)
        title: qsTr("Informazioni")
        standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Basic.TextArea {
                id: informationText
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 15
                readOnly: true
                selectByMouse: true
                wrapMode: Text.Wrap
                text: informationDialog.visible ? SystemBackend.quickSystemInfo() : ""
            }
            Controls.Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Copia")
                icon.name: "edit-copy"
                onClicked: SystemBackend.copyToClipboard(informationText.text)
            }
        }
    }

    Timer {
        id: smokePager
        property int nextIndex: 0
        interval: 220
        repeat: true
        running: KrisccSmokeTest
        onTriggered: {
            root.showIndex(nextIndex)
            nextIndex++
            if (nextIndex > 6)
                stop()
        }
    }
}