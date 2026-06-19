// REDesk — local screen preview window (Phase-1 demo).
//
// Shows THIS machine's real screen using the platform capturer
// (ScreenCaptureKit on macOS) painted by ScreenView. This is the first visible
// proof the capture pipeline works end-to-end, before any networking. It is a
// preview of the *local* screen — not a remote session.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import REDesk.UI

Window {
    id: previewWin
    width: 960
    height: 600
    minimumWidth: 480
    minimumHeight: 320
    title: qsTr("REDesk — Local screen preview")
    color: Theme.windowBg

    // Start capturing when shown; stop when closed (releases SCK + the stream).
    onVisibleChanged: {
        if (visible) screenSource.start()
        else screenSource.stop()
    }

    LocalScreenSource { id: screenSource }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        // Status bar.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Rectangle {
                width: 10; height: 10; radius: 5
                color: screenSource.active ? Theme.success : Theme.textMuted
            }
            Text {
                text: screenSource.statusText
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSm
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                visible: screenSource.active
                text: screenSource.usingRealBackend
                      ? qsTr("real • %1×%2").arg(screenSource.frameSize.width).arg(screenSource.frameSize.height)
                      : qsTr("stub pattern")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSm
            }
            Button {
                text: screenSource.active ? qsTr("Stop") : qsTr("Start")
                onClicked: screenSource.active ? screenSource.stop() : screenSource.start()
            }
        }

        // The actual painted screen.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusCard
            color: "#0E1116"
            border.color: Theme.cardBorder
            clip: true

            ScreenView {
                anchors.fill: parent
                anchors.margins: 1
                source: screenSource
            }

            // Hint shown before the first frame / on permission denial.
            Text {
                anchors.centerIn: parent
                visible: !screenSource.active
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Click Start to preview this machine's screen.\n" +
                           "On first run macOS will ask for Screen Recording permission.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSm
            }
        }
    }
}
