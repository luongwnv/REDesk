// REDesk — primary action button (ADR-001 §3.5). AnyDesk-red accent, used for
// the main call-to-action (Connect, etc.). A `flat` variant renders as a subtle
// neutral button for secondary actions.
import QtQuick
import QtQuick.Controls.Basic
import REDesk.UI

Button {
    id: control
    property bool flat: false

    implicitHeight: 40
    padding: Theme.spacingMd
    font.pixelSize: Theme.fontMd
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.flat ? Theme.textPrimary : Theme.textOnAccent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusInput
        border.width: control.flat ? 1 : 0
        border.color: Theme.cardBorder
        color: {
            if (control.flat)
                return control.down ? "#E9ECEF" : (control.hovered ? "#F1F3F5" : Theme.cardBg)
            if (!control.enabled)
                return "#F2A8A3"
            if (control.down)
                return Theme.accentPressed
            return control.hovered ? Theme.accentHover : Theme.accent
        }
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
