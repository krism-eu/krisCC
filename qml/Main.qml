import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kriscc

Kirigami.ApplicationWindow {
    id: root
    width: 1120
    height: 780
    minimumWidth: 820
    minimumHeight: 600
    visible: !KrisccStartHidden
    title: qsTr("krisCC")
    property int currentSection: 0

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

    function openById(pageId) {
        if (pageId === "software") showIndex(1)
        else if (pageId === "flatpak") showIndex(2)
        else if (pageId === "podman") showIndex(3)
        else if (pageId === "system" || pageId === "bootc" || pageId === "tools") showIndex(4)
        else if (pageId === "commands") showIndex(5)
        else if (pageId === "recovery") showIndex(6)
        else showIndex(0)
    }

    header: Controls.ToolBar {
        implicitHeight: Kirigami.Units.gridUnit * 2.6
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Controls.Label {
                font.bold: true
                font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
                text: qsTr("krisCC")
            }

            Flickable {
                id: navigationFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: navigationRow.implicitWidth
                contentHeight: height
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                RowLayout {
                    id: navigationRow
                    height: parent.height
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: [
                            qsTr("Panoramica"),
                            qsTr("RPM"),
                            qsTr("Flatpak"),
                            qsTr("Container"),
                            qsTr("Sistema"),
                            qsTr("Comandi"),
                            qsTr("Backup")
                        ]
                        delegate: Controls.ToolButton {
                            required property int index
                            required property string modelData
                            checkable: true
                            checked: root.currentSection === index
                            text: modelData
                            font.bold: checked
                            onClicked: root.showIndex(index)

                            contentItem: Controls.Label {
                                text: parent.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font: parent.font
                                color: parent.checked ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                            }
                            background: Item {
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 2
                                    visible: parent.parent.checked
                                    color: Kirigami.Theme.highlightColor
                                }
                            }
                        }
                    }
                }
            }

            Controls.Label {
                opacity: 0.72
                text: qsTr("KrisOS Control Center · %1").arg(Qt.application.version)
            }
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: ""
        padding: 0

        StackLayout {
            anchors.fill: parent
            currentIndex: root.currentSection

            DashboardModule {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpenRequested: function(pageId) { root.openById(pageId) }
            }
            SoftwareModule { Layout.fillWidth: true; Layout.fillHeight: true }
            FlatpakModule { Layout.fillWidth: true; Layout.fillHeight: true }
            PodmanModule { Layout.fillWidth: true; Layout.fillHeight: true }
            SystemModule { Layout.fillWidth: true; Layout.fillHeight: true }
            CommandsModule { Layout.fillWidth: true; Layout.fillHeight: true }
            RecoveryModule { Layout.fillWidth: true; Layout.fillHeight: true }
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
