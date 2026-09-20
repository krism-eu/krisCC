import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Templates as Templates
import org.kde.kirigami as Kirigami
Kirigami.AbstractCard {
    id: root
    property bool embedded: false
    background: null
    padding: embedded ? 0 : Kirigami.Units.largeSpacing
    property string title: qsTr("Output")
    property alias text: output.text
    Layout.fillWidth: true
    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing
        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading { Layout.fillWidth: true; visible: !root.embedded; level: 3; font.bold: true; text: root.title; elide: Text.ElideRight }
            Controls.Button {
                text: qsTr("Copia")
                icon.name: "edit-copy"
                enabled: output.text.length > 0
                onClicked: { output.selectAll(); output.copy(); output.deselect() }
            }
        }
        Controls.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.preferredHeight: UiMetrics.outputHeight
            contentWidth: availableWidth
            // Native theme colors with the Qt text-area primitive avoid Breeze's
            // TextInput-only helper on a read-only multiline TextEdit.
            Templates.TextArea {
                id: output
                width: scroll.availableWidth
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                font: Kirigami.Theme.fixedWidthFont
                color: Kirigami.Theme.textColor
                selectionColor: Kirigami.Theme.highlightColor
                selectedTextColor: Kirigami.Theme.highlightedTextColor
            }
        }
    }
}
