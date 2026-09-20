import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
ColumnLayout {
    property string title: ""
    property string subtitle: ""
    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing
    Kirigami.Heading { Layout.fillWidth: true; level: 1; font.bold: true; text: parent.title; wrapMode: Text.WordWrap }
    Controls.Label { Layout.fillWidth: true; text: parent.subtitle; visible: text.length > 0; wrapMode: Text.WordWrap; opacity: UiMetrics.secondaryOpacity }
}
