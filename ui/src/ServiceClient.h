#pragma once

// REDesk — UI <-> service local IPC client (ADR-001 §3.5, §2).
//
// The UI is unprivileged and impersonable; the service/daemon is privileged.
// This client connects over a QLocalSocket (Windows named pipe / Unix domain
// socket) to the daemon and treats the channel as a HARDENED, AUTHENTICATED, and
// fundamentally UNTRUSTED boundary:
//
//   * The service side sets an explicit security descriptor / ACL on the pipe
//     (only intended user SID(s) / Administrators / SYSTEM; deny Everyone &
//     anonymous; NOT in the per-session object namespace). This client must fail
//     closed if it cannot authenticate the peer. (ADR §3.5 "IPC hardening".)
//
//   * Authentication: after connect, both ends prove a per-boot shared token
//     (delivered out-of-band by the installer/keystore, NOT hard-coded). Until
//     the handshake completes, no session commands are accepted/sent.
//
//   * Every inbound frame is validated as untrusted: length-prefixed, bounded
//     size, known opcode, well-formed payload — malformed -> drop + disconnect.
//     Never trust the daemon's framing blindly even though it is privileged; the
//     pipe could be a spoof if ACLs are misconfigured.
//
// The wire/IPC schema is owned by proto/ (one source of truth). This client only
// depends on that schema, not on transport/crypto internals. In the stub build
// there is no daemon, so connect() simply transitions to a Disconnected state
// with an informative status and the controllers fall back to demo data.

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

class QLocalSocket;

namespace redesk::ui {

class ServiceClient : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Authenticating, // socket up, proving the shared token
        Ready,          // authenticated; commands may flow
        Error,
    };
    Q_ENUM(State)

    explicit ServiceClient(QObject* parent = nullptr);
    ~ServiceClient() override;

    State state() const { return state_; }

    // Default pipe/socket name; overridable for tests. The real name is derived
    // per-user so two desktop users can't collide (ADR §3.5 IPC hardening).
    static QString defaultServerName();

public slots:
    // Connect to the daemon and begin the auth handshake. `authToken` is the
    // out-of-band shared secret; empty in the stub build (no daemon present).
    void connectToService(const QString& serverName = QString(),
                          const QByteArray& authToken = QByteArray());
    void disconnectFromService();

    // Send an already-encoded, length-prefixed IPC command (built from proto/).
    // No-op unless State::Ready. Returns false if rejected (not ready / too big).
    bool sendCommand(const QByteArray& framedCommand);

signals:
    void stateChanged(redesk::ui::ServiceClient::State state);
    // A validated inbound message (opcode + payload) ready for higher layers.
    void messageReceived(quint16 opcode, const QByteArray& payload);
    void errorOccurred(const QString& reason);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();

private:
    void setState(State s);
    void beginAuth();
    // Pull complete, length-validated frames out of rx_buffer_; rejects oversized
    // or malformed frames (untrusted-input discipline).
    void drainFrames();

    // Hard cap on a single IPC frame. Anything larger is treated as hostile /
    // corrupt and tears the connection down. (ADR §3.5 / §3.6 65535-byte cap
    // applies to the media wire; IPC control frames are far smaller.)
    static constexpr quint32 kMaxFrameBytes = 1u << 20; // 1 MiB ceiling

    std::unique_ptr<QLocalSocket> socket_;
    QByteArray rx_buffer_;
    QByteArray auth_token_;
    State state_{State::Disconnected};
};

} // namespace redesk::ui
