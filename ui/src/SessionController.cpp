// REDesk — SessionController stub (ADR-001 §3.5, §3.6).
//
// Drives the QML through a realistic session lifecycle WITHOUT a backend so the
// AnyDesk-style UI is fully exercisable for design review. Each step is a TODO
// pointing at the daemon command it will eventually issue over ServiceClient.

#include "ui/src/SessionController.h"

#include "ui/src/ServiceClient.h"

#include <QTimer>

namespace redesk::ui {

SessionController::SessionController(QObject* parent) : QObject(parent) {
    client_ = std::make_unique<ServiceClient>(this);

    // When the daemon channel reports a validated message, route it into the
    // session state machine. Stub: nothing connects, so this stays dormant.
    connect(client_.get(), &ServiceClient::messageReceived, this,
            [](quint16 /*opcode*/, const QByteArray& /*payload*/) {
                // TODO(ADR §3.5): decode proto/ messages (session offer, PIN
                // request, capability grant, "controller attached", frame-ready
                // notifications) and update state accordingly.
            });
    connect(client_.get(), &ServiceClient::errorOccurred, this,
            [this](const QString& reason) {
                // Non-fatal in the stub: we run UI-local simulation regardless.
                setStatusText(QStringLiteral("Service unavailable: ") + reason);
            });

    // Best-effort connect to a (likely absent) daemon. Fails closed gracefully.
    client_->connectToService();
}

SessionController::~SessionController() = default;

void SessionController::setState(SessionState s) {
    if (state_ == s)
        return;
    state_ = s;
    emit stateChanged();
}

void SessionController::setStatusText(const QString& t) {
    if (status_text_ == t)
        return;
    status_text_ = t;
    emit statusTextChanged();
}

// --- Controller role --------------------------------------------------------

void SessionController::connectToId(const QString& id) {
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty()) {
        setStatusText(QStringLiteral("Enter a REDesk ID"));
        return;
    }
    if (state_ != SessionState::Idle && state_ != SessionState::Failed)
        return;

    remote_id_ = trimmed;
    emit remoteIdChanged();

    setState(SessionState::Resolving);
    setStatusText(QStringLiteral("Resolving %1…").arg(trimmed));

    // TODO(ADR §3.3/§3.6): send a "start session" command to the daemon, which
    // resolves the ID via rendezvous, runs ICE, and brings up the Noise channel.
    // Stub: simulate a short resolve then ask for the interactive PIN.
    QTimer::singleShot(450, this, [this] {
        if (state_ != SessionState::Resolving)
            return;
        pin_required_ = true;
        emit pinRequiredChanged();
        setState(SessionState::Authenticating);
        setStatusText(QStringLiteral("Enter the session PIN shown on the remote machine"));
    });
}

void SessionController::enterPin(const QString& pin) {
    if (state_ != SessionState::Authenticating)
        return;
    if (pin.trimmed().isEmpty()) {
        setStatusText(QStringLiteral("PIN required"));
        return;
    }

    setStatusText(QStringLiteral("Verifying…"));
    // TODO(ADR §3.6): drive CPace inside the Noise channel (bound to the
    // handshake hash). Never send the PIN as plaintext over IPC; hand it to the
    // daemon which runs the PAKE. Stub: accept and proceed to transport bring-up.
    QTimer::singleShot(350, this, [this] {
        pin_required_ = false;
        emit pinRequiredChanged();
        setState(SessionState::Connecting);
        setStatusText(QStringLiteral("Connecting…"));

        QTimer::singleShot(500, this, [this] {
            setState(SessionState::Connected);
            setStatusText(QStringLiteral("Connected to %1").arg(remote_id_));
            emit sessionStarted();
        });
    });
}

void SessionController::disconnect() {
    if (state_ == SessionState::Idle)
        return;
    // TODO(ADR §3.6): one-click disconnect command to the daemon; it tears down
    // transport + zeroizes session keys.
    pin_required_ = false;
    emit pinRequiredChanged();
    setState(SessionState::Idle);
    setStatusText(QStringLiteral("Ready"));
    emit sessionEnded();
}

// --- Host role: unattended access ------------------------------------------

void SessionController::setUnattendedPassword(const QString& password) {
    // TODO(ADR §3.6): forward to the daemon, which stores Argon2id ONLY (OWASP
    // m=19456/t=2/p=1 floor, tuned to ~0.5–1 s) and later verifies via OPAQUE.
    // The UI must not persist or hash the password itself.
    const bool nowSet = !password.trimmed().isEmpty();
    if (unattended_configured_ != nowSet) {
        unattended_configured_ = nowSet;
        emit unattendedConfiguredChanged();
    }
}

void SessionController::clearUnattendedPassword() {
    if (!unattended_configured_)
        return;
    // TODO(ADR §3.6): daemon clears the stored Argon2id verifier.
    unattended_configured_ = false;
    emit unattendedConfiguredChanged();
}

// --- Host role: incoming control request -----------------------------------

void SessionController::acceptIncoming() {
    // TODO(ADR §3.6): reply to the daemon with the user-approved capability mask;
    // raise the persistent "being controlled" indicator.
    being_controlled_ = true;
    if (controller_name_.isEmpty())
        controller_name_ = QStringLiteral("Remote operator");
    emit beingControlledChanged();
}

void SessionController::rejectIncoming() {
    being_controlled_ = false;
    controller_name_.clear();
    emit beingControlledChanged();
}

// --- Capability toggles (default-deny model) -------------------------------

void SessionController::setControlAllowed(bool v) {
    if (control_allowed_ == v) return;
    control_allowed_ = v;
    emit capabilitiesChanged();
}
void SessionController::setClipboardAllowed(bool v) {
    if (clipboard_allowed_ == v) return;
    clipboard_allowed_ = v;
    emit capabilitiesChanged();
}
void SessionController::setFileTransferAllowed(bool v) {
    if (file_transfer_allowed_ == v) return;
    file_transfer_allowed_ = v;
    emit capabilitiesChanged();
}
void SessionController::setAudioAllowed(bool v) {
    if (audio_allowed_ == v) return;
    audio_allowed_ = v;
    emit capabilitiesChanged();
}
void SessionController::setPrivacyScreen(bool v) {
    if (privacy_screen_ == v) return;
    privacy_screen_ = v;
    emit capabilitiesChanged();
}

// --- Active-session controls -----------------------------------------------

void SessionController::switchMonitor(int /*index*/) {
    // TODO(ADR §3.1): request a different SCDisplay / DXGI output / portal source
    // from the host via the daemon.
}

void SessionController::requestFullscreen(bool on) {
    emit fullscreenRequested(on);
}

} // namespace redesk::ui
