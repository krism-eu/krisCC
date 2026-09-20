pragma Singleton
import QtQuick
import org.kde.kirigami as Kirigami
QtObject {
    readonly property real pageMargin: Kirigami.Units.largeSpacing * 1.5
    readonly property real secondaryOpacity: 0.72
    readonly property real outputHeight: Kirigami.Units.gridUnit * 12
}
