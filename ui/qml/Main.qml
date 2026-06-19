// REDesk — main control panel (ADR-001 §3.5, AnyDesk-style layout).
//
// Left panel  "This Desk"  : your REDesk ID + safety-number, and the
//                            "Set password for unattended access" affordance.
// Right panel "Remote Desk": an address-bar-style ID input + Connect button,
//                            plus a recent-sessions list.
// On a successful connection the SessionController emits sessionStarted() and we
// push the fullscreen SessionView.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import REDesk.UI

ApplicationWindow {
    id: win
    width: 960
    height: 620
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    title: qsTr("REDesk")
    color: Theme.windowBg

    // SessionController is a QML singleton (registered from C++).
    readonly property var session: SessionController

    // Push SessionView when a session goes live; pop when it ends.
    Connections {
        target: SessionController
        function onSessionStarted() { stack.push(sessionViewComponent) }
        function onSessionEnded()   { if (stack.depth > 1) stack.pop() }
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: homeComponent
    }

    Component { id: sessionViewComponent; SessionView {} }

    // Local screen preview window (Phase-1 demo of the capture pipeline).
    LocalScreenPreview { id: localPreview }

    // ---------------------------------------------------------------------
    // Home (control panel)
    // ---------------------------------------------------------------------
    Component {
        id: homeComponent
        Item {
            // Header bar.
            RowLayout {
                id: header
                anchors { left: parent.left; right: parent.right; top: parent.top }
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Rectangle {
                    width: 30; height: 30; radius: 8
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: "R"; color: Theme.textOnAccent
                        font.pixelSize: Theme.fontLg; font.weight: Font.Black
                    }
                }
                Text {
                    text: qsTr("REDesk")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLg
                    font.weight: Font.Bold
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("v%1").arg(typeof appVersion !== "undefined" ? appVersion : "0.1.0")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSm
                }
            }

            RowLayout {
                anchors {
                    left: parent.left; right: parent.right
                    top: header.bottom; bottom: parent.bottom
                }
                anchors.margins: Theme.spacingLg
                anchors.topMargin: Theme.spacingMd
                spacing: Theme.spacingLg

                // ---------- LEFT: This Desk ----------
                Card {
                    Layout.preferredWidth: 320
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        Text {
                            text: qsTr("This Desk")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMd
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: qsTr("Your REDesk ID")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSm
                        }
                        Text {
                            text: SessionController.myId
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontXl
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }

                        // Safety number (key fingerprint) — identity per ADR §3.6.
                        RowLayout {
                            spacing: Theme.spacingXs
                            Rectangle { width: 8; height: 8; radius: 4; color: Theme.success }
                            Text {
                                text: qsTr("Safety number  %1").arg(SessionController.myFingerprint)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSm
                                font.family: "monospace"
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.cardBorder }

                        // Unattended access affordance.
                        Text {
                            text: qsTr("Unattended access")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSm
                        }
                        RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: SessionController.unattendedConfigured ? Theme.success : Theme.textMuted
                            }
                            Text {
                                text: SessionController.unattendedConfigured
                                      ? qsTr("Password is set")
                                      : qsTr("Not configured")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSm
                            }
                        }
                        PrimaryButton {
                            Layout.fillWidth: true
                            subtle: true
                            text: SessionController.unattendedConfigured
                                  ? qsTr("Change password…")
                                  : qsTr("Set password for unattended access")
                            onClicked: unattendedDialog.open()
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.cardBorder }

                        // Phase-1 demo: preview this machine's real screen via the
                        // platform capturer (proves the capture pipeline works).
                        PrimaryButton {
                            Layout.fillWidth: true
                            text: qsTr("Preview my screen")
                            onClicked: localPreview.show()
                        }

                        Item { Layout.fillHeight: true }

                        // Being-controlled indicator (host role) — ADR §3.6.
                        Pill {
                            visible: SessionController.beingControlled
                            Layout.alignment: Qt.AlignHCenter
                            pillColor: Theme.accent
                            text: qsTr("Being controlled by %1").arg(SessionController.controllerName)
                        }
                    }
                }

                // ---------- RIGHT: Remote Desk ----------
                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        Text {
                            text: qsTr("Remote Desk")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMd
                            font.weight: Font.DemiBold
                        }

                        // Address-bar + Connect.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm
                            IdField {
                                id: idField
                                Layout.fillWidth: true
                                onAccepted: SessionController.connectToId(text)
                            }
                            PrimaryButton {
                                text: qsTr("Connect")
                                implicitWidth: 120
                                implicitHeight: 44
                                enabled: idField.text.trim().length > 0
                                         && SessionController.state === SessionController.Idle
                                onClicked: SessionController.connectToId(idField.text)
                            }
                        }

                        // Live status line for the connect flow.
                        Text {
                            Layout.fillWidth: true
                            text: SessionController.statusText
                            color: SessionController.state === SessionController.Failed
                                   ? Theme.accent : Theme.textMuted
                            font.pixelSize: Theme.fontSm
                            elide: Text.ElideRight
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.cardBorder }

                        Text {
                            text: qsTr("Recent sessions")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSm
                        }

                        ListView {
                            id: recents
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: Theme.spacingXs
                            // Demo data (ADR §3.5 stub build: no daemon). Real list
                            // comes from the daemon's audit log keyed by fingerprint.
                            model: ListModel {
                                ListElement { rid: "412 908 663"; name: "Workshop-PC"; when: "Today 14:02" }
                                ListElement { rid: "905 117 240"; name: "Mac-Studio";  when: "Yesterday" }
                                ListElement { rid: "338 660 921"; name: "Lab-Linux";   when: "Mon" }
                            }
                            delegate: ItemDelegate {
                                width: recents.width
                                height: 52
                                onClicked: { idField.text = rid; SessionController.connectToId(rid) }
                                background: Rectangle {
                                    radius: Theme.radiusInput
                                    color: hovered ? Theme.fieldBg : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: Theme.spacingMd
                                    Rectangle {
                                        width: 34; height: 34; radius: 8
                                        color: Theme.fieldBg
                                        Text {
                                            anchors.centerIn: parent
                                            text: name.length > 0 ? name.charAt(0) : "?"
                                            color: Theme.textSecondary
                                            font.weight: Font.Bold
                                        }
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Text {
                                            text: name; color: Theme.textPrimary
                                            font.pixelSize: Theme.fontMd; font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: rid; color: Theme.textMuted
                                            font.pixelSize: Theme.fontSm; font.letterSpacing: 1
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: when; color: Theme.textMuted
                                        font.pixelSize: Theme.fontSm
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // Unattended-access password dialog. The plaintext is handed straight to the
    // controller (which forwards to the daemon for Argon2id/OPAQUE) — the UI
    // never hashes or persists it (ADR §3.6).
    // ---------------------------------------------------------------------
    Dialog {
        id: unattendedDialog
        anchors.centerIn: parent
        width: 380
        modal: true
        title: qsTr("Unattended access")
        standardButtons: Dialog.Save | Dialog.Cancel

        onAccepted: SessionController.setUnattendedPassword(pwField.text)
        onOpened: pwField.text = ""

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Set a password to allow connecting to this machine when no one is present. " +
                           "It is stored hashed (Argon2id) by the service and never leaves this device.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSm
            }
            TextField {
                id: pwField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("New password")
            }
        }
    }
}
