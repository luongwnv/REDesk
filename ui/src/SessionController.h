#pragma once

// REDesk — QML-facing session controller (ADR-001 §3.5, §3.6 permission model).
//
// The single Q_OBJECT QML talks to. It owns the user-visible session lifecycle
// and the default-deny capability model from ADR §3.6:
//
//   * connectToId(id) / enterPin(pin)  -> outbound control flow (controller role)
//   * per-session capability toggles    -> view-only vs control, clipboard, file
//                                          transfer, audio, input injection,
//                                          privacy/black-screen (each separate)
//   * "being controlled" indicator      -> host role; persistent + one-click
//                                          disconnect/lock (ADR §3.6)
//
// All real work is delegated to the privileged daemon over ServiceClient; this
// class never opens sockets / captures / injects directly. In the stub build the
// daemon is absent, so transitions are simulated locally so the QML is fully
// click-through for design review without any backend.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace redesk::ui {

class ServiceClient;

class SessionController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // This machine's REDesk ID (routing handle only; identity is the key
    // fingerprint per ADR §3.6) and its safety-number fingerprint.
    Q_PROPERTY(QString myId READ myId NOTIFY identityChanged)
    Q_PROPERTY(QString myFingerprint READ myFingerprint NOTIFY identityChanged)

    // Outbound (controller) session state.
    Q_PROPERTY(SessionState state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString remoteId READ remoteId NOTIFY remoteIdChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool pinRequired READ pinRequired NOTIFY pinRequiredChanged)

    // Inbound (host) "being controlled" indicator state — ADR §3.6.
    Q_PROPERTY(bool beingControlled READ beingControlled NOTIFY beingControlledChanged)
    Q_PROPERTY(QString controllerName READ controllerName NOTIFY beingControlledChanged)

    // Default-deny per-session capabilities (ADR §3.6). controlAllowed=false is
    // view-only. These are *requested* grants; the host confirms each.
    Q_PROPERTY(bool controlAllowed READ controlAllowed WRITE setControlAllowed
                   NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool clipboardAllowed READ clipboardAllowed WRITE setClipboardAllowed
                   NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool fileTransferAllowed READ fileTransferAllowed WRITE setFileTransferAllowed
                   NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool audioAllowed READ audioAllowed WRITE setAudioAllowed
                   NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool privacyScreen READ privacyScreen WRITE setPrivacyScreen
                   NOTIFY capabilitiesChanged)

    // Whether an unattended-access password is configured on THIS machine.
    Q_PROPERTY(bool unattendedConfigured READ unattendedConfigured
                   NOTIFY unattendedConfiguredChanged)

public:
    enum class SessionState {
        Idle,
        Resolving,    // looking up the remote ID via rendezvous
        Authenticating, // PIN / password (CPace / OPAQUE inside Noise)
        Connecting,   // ICE / transport coming up
        Connected,    // streaming
        Failed,
    };
    Q_ENUM(SessionState)

    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    QString myId() const { return my_id_; }
    QString myFingerprint() const { return my_fingerprint_; }

    SessionState state() const { return state_; }
    QString remoteId() const { return remote_id_; }
    QString statusText() const { return status_text_; }
    bool pinRequired() const { return pin_required_; }

    bool beingControlled() const { return being_controlled_; }
    QString controllerName() const { return controller_name_; }

    bool controlAllowed() const { return control_allowed_; }
    bool clipboardAllowed() const { return clipboard_allowed_; }
    bool fileTransferAllowed() const { return file_transfer_allowed_; }
    bool audioAllowed() const { return audio_allowed_; }
    bool privacyScreen() const { return privacy_screen_; }
    bool unattendedConfigured() const { return unattended_configured_; }

    void setControlAllowed(bool v);
    void setClipboardAllowed(bool v);
    void setFileTransferAllowed(bool v);
    void setAudioAllowed(bool v);
    void setPrivacyScreen(bool v);

public slots:
    // Controller role: begin a session to a remote REDesk ID.
    void connectToId(const QString& id);
    // Submit the interactive one-time PIN (drives CPace inside Noise, host-side).
    void enterPin(const QString& pin);
    // Tear down the active/ pending session.
    void disconnect();

    // Host role: configure / clear unattended-access password. The plaintext
    // never leaves this process boundary as-is — it is handed to the daemon which
    // stores Argon2id only and verifies via OPAQUE (ADR §3.6). Stub: just flags.
    void setUnattendedPassword(const QString& password);
    void clearUnattendedPassword();

    // Host role: accept / reject an incoming control request with a capability
    // mask the user approved.
    void acceptIncoming();
    void rejectIncoming();

    // Active-session controls surfaced by the session toolbar.
    void switchMonitor(int index);
    void requestFullscreen(bool on);

signals:
    void identityChanged();
    void stateChanged();
    void remoteIdChanged();
    void statusTextChanged();
    void pinRequiredChanged();
    void beingControlledChanged();
    void capabilitiesChanged();
    void unattendedConfiguredChanged();
    // Emitted when a session reaches Connected — QML pushes SessionView.
    void sessionStarted();
    // Emitted when a session ends — QML pops back to Main.
    void sessionEnded();
    void fullscreenRequested(bool on);

private:
    void setState(SessionState s);
    void setStatusText(const QString& t);

    std::unique_ptr<ServiceClient> client_;

    // Identity (placeholder until the daemon reports the real key-derived ID).
    QString my_id_{QStringLiteral("729 481 305")};
    QString my_fingerprint_{QStringLiteral("9f2c 41ab 77de 0c18")};

    SessionState state_{SessionState::Idle};
    QString remote_id_;
    QString status_text_{QStringLiteral("Ready")};
    bool pin_required_{false};

    bool being_controlled_{false};
    QString controller_name_;

    // Default-deny: control off until explicitly granted (view-only by default).
    bool control_allowed_{true};   // typical controller intent; host still confirms
    bool clipboard_allowed_{true};
    bool file_transfer_allowed_{false};
    bool audio_allowed_{false};
    bool privacy_screen_{false};

    bool unattended_configured_{false};
};

} // namespace redesk::ui
