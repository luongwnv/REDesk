// REDesk — address-bar-style REDesk ID input (ADR-001 §3.5, AnyDesk aesthetic).
// The remote-ID entry that reads like a browser address bar: a pill-rounded
// field with a leading glyph and a placeholder. Emits accepted() on Enter.
import QtQuick
import QtQuick.Controls.Basic
import REDesk.UI

Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: qsTr("Remote REDesk ID")
    signal accepted()

    implicitHeight: 44
    radius: Theme.radiusInput
    color: Theme.fieldBg
    border.width: input.activeFocus ? 2 : 1
    border.color: input.activeFocus ? Theme.accent : Theme.fieldBorder
    Behavior on border.color { ColorAnimation { duration: 90 } }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        // Leading "remote" glyph (drawn, no icon-font dependency).
        Item {
            width: 18; height: parent.height
            Rectangle {
                anchors.centerIn: parent
                width: 14; height: 12; radius: 2
                color: "transparent"
                border.width: 2
                border.color: Theme.textMuted
            }
        }

        TextField {
            id: input
            width: parent.width - 18 - Theme.spacingSm
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            placeholderText: root.placeholder
            placeholderTextColor: Theme.textMuted
            color: Theme.textPrimary
            font.pixelSize: Theme.fontLg
            font.letterSpacing: 1.5
            selectByMouse: true
            background: Item {}        // host rectangle provides the chrome
            inputMethodHints: Qt.ImhPreferNumbers
            onAccepted: root.accepted()
        }
    }
}
