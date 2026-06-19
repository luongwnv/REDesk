// REDesk — rounded card surface (ADR-001 §3.5, AnyDesk-style chrome).
import QtQuick
import REDesk.UI

Rectangle {
    id: root
    default property alias content: contentArea.data
    property int padding: Theme.spacingLg

    color: Theme.cardBg
    radius: Theme.radiusCard
    border.color: Theme.cardBorder
    border.width: 1

    // Soft elevation via a layered backdrop rectangle (no QtGraphicalEffects dep).
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.topMargin: 2
        radius: root.radius
        color: "#1A000000"
    }

    Item {
        id: contentArea
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
