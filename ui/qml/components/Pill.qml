// REDesk — status pill (ADR-001 §3.5/§3.6). Used for the persistent
// "you are controlling X" / "being controlled" indicators and small status tags.
import QtQuick
import REDesk.UI

Rectangle {
    id: root
    property string text: ""
    property color pillColor: Theme.accent
    property color textColor: Theme.textOnAccent
    property bool showDot: true

    implicitHeight: 30
    implicitWidth: row.implicitWidth + Theme.spacingMd * 2
    radius: Theme.radiusPill
    color: pillColor

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingXs

        Rectangle {
            visible: root.showDot
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.textColor

            SequentialAnimation on opacity {
                running: root.showDot
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0;  duration: 700; easing.type: Easing.InOutQuad }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.textColor
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
    }
}
