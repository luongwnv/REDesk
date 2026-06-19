#include "ui/src/LocalScreenSource.h"

#include <QMetaObject>
#include <Qt>

#include <utility>

#include "core/capture/capturer.h"
#include "platform/platform_backends.h"

namespace redesk::ui {

LocalScreenSource::LocalScreenSource(QObject* parent) : QObject(parent) {}

LocalScreenSource::~LocalScreenSource() { stop(); }

void LocalScreenSource::setStatus(const QString& s) {
    if (status_text_ == s) return;
    status_text_ = s;
    emit statusTextChanged();
}

QImage LocalScreenSource::currentFrame() const {
    std::lock_guard<std::mutex> lk(frame_mutex_);
    return last_frame_;
}

void LocalScreenSource::start() {
    if (active_) return;

    // Prefer the native backend; fall back to the core stub (synthetic frames).
    capturer_ = redesk::platform::CreateNativeCapturer();
    using_real_backend_ = static_cast<bool>(capturer_);
    if (!capturer_) capturer_ = redesk::capture::createCapturer();
    if (!capturer_) {
        setStatus(QStringLiteral("No capturer available"));
        return;
    }

    capturer_->setFrameCallback([this](const redesk::VideoFrame& f) {
        if (f.cpu_pixels.empty() || f.size.width == 0 || f.size.height == 0)
            return;
        // Wrap tight-packed BGRA into a QImage, then deep-copy so it owns its
        // bytes once the capture buffer is recycled. Format_ARGB32 matches BGRA
        // byte order on little-endian (B,G,R,A in memory).
        QImage img(f.cpu_pixels.data(), static_cast<int>(f.size.width),
                   static_cast<int>(f.size.height),
                   static_cast<int>(f.size.width) * 4, QImage::Format_ARGB32);
        QImage owned = img.copy();
        // Marshal to the GUI thread (callback runs on the capture thread).
        QMetaObject::invokeMethod(
            this, "deliverFrame", Qt::QueuedConnection, Q_ARG(QImage, owned));
    });

    redesk::capture::CaptureConfig cfg;
    cfg.target_fps = 60;
    cfg.capture_cursor = true;
    cfg.preferred_format = redesk::PixelFormat::BGRA8;  // demo CPU path
    // display_id empty -> backend picks the primary display.

    redesk::Status st = capturer_->start(cfg);
    if (!st.ok()) {
        QString msg = QString::fromStdString(st.message);
        if (st.code == redesk::ErrorCode::PermissionDenied)
            msg = QStringLiteral(
                "Screen Recording permission needed — grant REDesk in System "
                "Settings ▸ Privacy & Security ▸ Screen Recording, then retry.");
        setStatus(msg);
        capturer_.reset();
        return;
    }

    active_ = true;
    setStatus(using_real_backend_
                  ? QStringLiteral("Capturing real screen")
                  : QStringLiteral("Capturing synthetic test pattern (stub)"));
    emit activeChanged();
}

void LocalScreenSource::stop() {
    if (capturer_) {
        capturer_->stop();
        capturer_.reset();
    }
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    setStatus(QStringLiteral("Idle"));
}

void LocalScreenSource::deliverFrame(const QImage& img) {
    {
        std::lock_guard<std::mutex> lk(frame_mutex_);
        last_frame_ = img;
    }
    emit frameArrived();
}

}  // namespace redesk::ui
