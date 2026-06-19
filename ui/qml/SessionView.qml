// REDesk — active-session remote view (ADR-001 §3.5).
//
// Fullscreen remote video (VideoItem / QQuickRhiItem, zero-copy under the chrome)
// with a floating, auto-hiding session toolbar (disconnect, fullscreen, monitor
// switch, clipboard, file transfer, settings) and a persistent
// "you are controlling X" pill (ADR §3.5/§3.6).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import REDesk.UI

Item {
    id: root
    focus: true

    // Dark backdrop behind the (possibly letterboxed) video.
    Rectangle { anchors.fill: parent; color: Theme.videoBg }

    // The remote video surface. In the stub build it clears to placeholderColor;
    // the real decode pipeline pushes native textures into it (see VideoItem.h).
    VideoItem {
        id: video
        anchors.fill: parent
        placeholderColor: Theme.videoBg
    }

    // "No frame yet" hint until the first decoded frame lands.
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingSm
        visible: !video.hasFrame
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Waiting for remote display…")
            color: Theme.textOnDark
            font.pixelSize: Theme.fontLg
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: SessionController.statusText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSm
        }
    }

    // Persistent "you are controlling X" pill (top-center).
    Pill {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.spacingMd
        pillColor: Theme.accent
        textColor: Theme.textOnAccent
        text: SessionController.controlAllowed
              ? qsTr("You are controlling %1").arg(SessionController.remoteId)
              : qsTr("Viewing %1 (view-only)").arg(SessionController.remoteId)
    }

    // Reveal the toolbar on pointer activity; auto-hide after a short idle.
    HoverHandler {
        id: hover
        onPointChanged: { toolbar.opacity = 1.0; hideTimer.restart() }
    }
    Timer {
        id: hideTimer
        interval: 2500
        running: true
        onTriggered: if (!toolbarHover.hovered) toolbar.opacity = 0.0
    }

    // ---------------------------------------------------------------------
    // Floating session toolbar (bottom-center).
    // ---------------------------------------------------------------------
    Rectangle {
        id: toolbar
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacingLg
        radius: Theme.radiusPill
        color: Theme.toolbarBg
        height: 52
        width: row.implicitWidth + Theme.spacingLg * 2
        opacity: 1.0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        HoverHandler { id: toolbarHover }

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.spacingSm

            component ToolButton2: Button {
                property string glyph: ""
                property string tip: ""
                flat: true
                implicitWidth: 40
                implicitHeight: 40
                ToolTip.visible: hovered
                ToolTip.text: tip
                contentItem: Text {
                    text: parent.glyph
                    color: Theme.textOnDark
                    font.pixelSize: Theme.fontLg
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusInput
                    color: parent.down ? "#2D333B" : (parent.hovered ? "#262C34" : "transparent")
                }
            }

            ToolButton2 { glyph: "⛶"; tip: qsTr("Fullscreen")
                onClicked: { root.fullscreen = !root.fullscreen
                             SessionController.requestFullscreen(root.fullscreen) } }
            ToolButton2 { glyph: "▦"; tip: qsTr("Switch monitor")
                onClicked: monitorMenu.open() }
            ToolButton2 { glyph: "✂"; tip: qsTr("Clipboard sync")
                enabled: SessionController.clipboardAllowed }
            ToolButton2 { glyph: "↑"; tip: qsTr("File transfer")
                enabled: SessionController.fileTransferAllowed
                onClicked: SessionController.setFileTransferAllowed(true) }
            ToolButton2 { glyph: "⚙"; tip: qsTr("Settings")
                onClicked: settingsMenu.open() }

            Rectangle { width: 1; height: 28; color: "#3A424C"
                        Layout.alignment: Qt.AlignVCenter }

            // Disconnect — red accent, ADR §3.6 one-click teardown.
            Button {
                implicitWidth: 44; implicitHeight: 40
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Disconnect")
                onClicked: SessionController.disconnect()
                contentItem: Text {
                    text: "✕"; color: Theme.textOnAccent
                    font.pixelSize: Theme.fontLg; font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusInput
                    color: parent.down ? Theme.accentPressed
                                       : (parent.hovered ? Theme.accentHover : Theme.accent)
                }
            }
        }

        Menu {
            id: monitorMenu
            MenuItem { text: qsTr("Monitor 1"); onTriggered: SessionController.switchMonitor(0) }
            MenuItem { text: qsTr("Monitor 2"); onTriggered: SessionController.switchMonitor(1) }
            MenuItem { text: qsTr("All monitors"); onTriggered: SessionController.switchMonitor(-1) }
        }

        // Per-session capability toggles (default-deny model, ADR §3.6).
        Menu {
            id: settingsMenu
            MenuItem {
                text: qsTr("Allow control"); checkable: true
                checked: SessionController.controlAllowed
                onToggled: SessionController.controlAllowed = checked
            }
            MenuItem {
                text: qsTr("Clipboard sync"); checkable: true
                checked: SessionController.clipboardAllowed
                onToggled: SessionController.clipboardAllowed = checked
            }
            MenuItem {
                text: qsTr("File transfer"); checkable: true
                checked: SessionController.fileTransferAllowed
                onToggled: SessionController.fileTransferAllowed = checked
            }
            MenuItem {
                text: qsTr("Remote audio"); checkable: true
                checked: SessionController.audioAllowed
                onToggled: SessionController.audioAllowed = checked
            }
            MenuItem {
                text: qsTr("Privacy screen (black host)"); checkable: true
                checked: SessionController.privacyScreen
                onToggled: SessionController.privacyScreen = checked
            }
        }
    }

    property bool fullscreen: false

    // Esc disconnects (matches the AnyDesk muscle-memory of exiting a session).
    Keys.onEscapePressed: SessionController.disconnect()
}
