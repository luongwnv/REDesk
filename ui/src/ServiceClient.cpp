// REDesk — UI <-> service local IPC client, stub implementation (ADR-001 §3.5).
//
// Real build: completes a token handshake then exchanges proto/-defined frames.
// Stub build: there is no daemon, so connecting fails closed gracefully and the
// rest of the UI degrades to demo data. The untrusted-input framing logic below
// is REAL (not stubbed) — it is the security-relevant part and must behave
// correctly the moment a daemon exists.

#include "ui/src/ServiceClient.h"

#include <QLocalSocket>
#include <QtEndian>

namespace redesk::ui {

ServiceClient::ServiceClient(QObject* parent) : QObject(parent) {
    socket_ = std::make_unique<QLocalSocket>(this);
    connect(socket_.get(), &QLocalSocket::connected, this, &ServiceClient::onConnected);
    connect(socket_.get(), &QLocalSocket::disconnected, this, &ServiceClient::onDisconnected);
    connect(socket_.get(), &QLocalSocket::readyRead, this, &ServiceClient::onReadyRead);
    connect(socket_.get(), &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { onSocketError(); });
}

ServiceClient::~ServiceClient() = default;

QString ServiceClient::defaultServerName() {
    // ADR §3.5: per-user name, NOT in the per-session object namespace, with a
    // hardened ACL applied service-side. Real name is salted with the user SID /
    // uid by the installer; this is the stub default.
    return QStringLiteral("redesk.service.ipc");
}

void ServiceClient::setState(State s) {
    if (state_ == s)
        return;
    state_ = s;
    emit stateChanged(state_);
}

void ServiceClient::connectToService(const QString& serverName, const QByteArray& authToken) {
    if (state_ == State::Connecting || state_ == State::Authenticating || state_ == State::Ready)
        return;

    auth_token_ = authToken;
    rx_buffer_.clear();
    setState(State::Connecting);

    const QString name = serverName.isEmpty() ? defaultServerName() : serverName;
    socket_->connectToServer(name);
    // In the stub build no daemon is listening; errorOccurred -> onSocketError
    // fires and we transition to Disconnected with an informative reason.
}

void ServiceClient::disconnectFromService() {
    if (socket_->state() != QLocalSocket::UnconnectedState)
        socket_->disconnectFromServer();
    setState(State::Disconnected);
}

void ServiceClient::onConnected() {
    setState(State::Authenticating);
    beginAuth();
}

void ServiceClient::beginAuth() {
    // TODO(ADR §3.5/§3.6): prove the out-of-band shared token here (challenge/
    // response bound to a per-boot nonce) BEFORE any session command is sent or
    // accepted. Fail closed on mismatch. In the stub we have no peer to talk to,
    // so we simply mark Ready once the (test) socket is up — there is no real
    // daemon in the portable build.
    if (auth_token_.isEmpty()) {
        // No token provided (portable/dev path): stay Authenticating rather than
        // pretending we authenticated a non-existent privileged peer.
        return;
    }
    // Real path would write the auth frame and await the server's challenge.
    setState(State::Ready);
}

void ServiceClient::onDisconnected() {
    setState(State::Disconnected);
}

void ServiceClient::onSocketError() {
    const QString reason = socket_ ? socket_->errorString()
                                   : QStringLiteral("local socket error");
    emit errorOccurred(reason);
    setState(State::Error);
}

bool ServiceClient::sendCommand(const QByteArray& framedCommand) {
    if (state_ != State::Ready)
        return false;
    if (static_cast<quint32>(framedCommand.size()) > kMaxFrameBytes)
        return false; // refuse to emit an oversized frame
    const qint64 written = socket_->write(framedCommand);
    return written == framedCommand.size();
}

void ServiceClient::onReadyRead() {
    rx_buffer_.append(socket_->readAll());
    drainFrames();
}

void ServiceClient::drainFrames() {
    // Wire framing (untrusted): [u32 length][u16 opcode][payload...], length
    // covers opcode+payload. Validate everything; a hostile/misconfigured peer
    // must not be able to over-allocate or desync us. (ADR §3.5)
    constexpr int kLenBytes = 4;
    constexpr int kOpBytes = 2;

    for (;;) {
        if (rx_buffer_.size() < kLenBytes)
            return; // need the length prefix

        const quint32 frameLen =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(rx_buffer_.constData()));

        if (frameLen < kOpBytes || frameLen > kMaxFrameBytes) {
            // Malformed / hostile: drop the whole connection rather than guess.
            emit errorOccurred(QStringLiteral("IPC frame size out of bounds"));
            disconnectFromService();
            rx_buffer_.clear();
            return;
        }

        const int total = kLenBytes + static_cast<int>(frameLen);
        if (rx_buffer_.size() < total)
            return; // wait for the rest of the frame

        const quint16 opcode = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(rx_buffer_.constData() + kLenBytes));
        const QByteArray payload =
            rx_buffer_.mid(kLenBytes + kOpBytes, static_cast<int>(frameLen) - kOpBytes);

        rx_buffer_.remove(0, total);

        // TODO(ADR §3.5): dispatch opcode against the proto/ schema (known set
        // only; unknown opcode -> log + ignore, never crash). Higher layers must
        // still re-validate payload structure before acting on it.
        emit messageReceived(opcode, payload);
    }
}

} // namespace redesk::ui
