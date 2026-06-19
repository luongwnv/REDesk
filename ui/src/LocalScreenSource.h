#pragma once

// REDesk — local screen preview source (Phase-1 demo, ADR-001 §3.1/§3.5).
//
// Bridges a real platform ICapturer into QML. It prefers the native backend
// (redesk::platform::CreateNativeCapturer(), e.g. ScreenCaptureKit on macOS) and
// falls back to the core synthetic stub when real backends are off — exactly the
// dispatch pattern documented in platform/platform_backends.h, so core never has
// to depend on platform.
//
// This is the demo path that lets the app SHOW THIS MACHINE's real screen in a
// window before any networking exists. Frames arrive as BGRA cpu_pixels on the
// capture thread; we wrap them as a QImage and marshal to the GUI thread, where
// ScreenView (a QQuickPaintedItem) paints them. The zero-copy IOSurface->RHI path
// (VideoItem / QQuickRhiItem) remains the ADR §3.5 optimization for later.

#include <QImage>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <mutex>

namespace redesk::capture { class ICapturer; }

namespace redesk::ui {

class LocalScreenSource : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameArrived)
    Q_PROPERTY(bool usingRealBackend READ usingRealBackend NOTIFY activeChanged)

public:
    explicit LocalScreenSource(QObject* parent = nullptr);
    ~LocalScreenSource() override;

    bool active() const { return active_; }
    QString statusText() const { return status_text_; }
    QSize frameSize() const { return last_frame_.size(); }
    bool usingRealBackend() const { return using_real_backend_; }

    // Latest frame for painters. Thread-safe snapshot (copied under lock).
    QImage currentFrame() const;

public slots:
    // Begin previewing the primary display. Drives TCC prompt on first use.
    void start();
    void stop();

signals:
    void activeChanged();
    void statusTextChanged();
    void frameArrived();   // emitted on the GUI thread when a new frame is ready

private:
    void setStatus(const QString& s);
    // Invoked (queued) on the GUI thread with an owned QImage.
    Q_INVOKABLE void deliverFrame(const QImage& img);

    std::unique_ptr<redesk::capture::ICapturer> capturer_;
    mutable std::mutex frame_mutex_;
    QImage last_frame_;
    QString status_text_{QStringLiteral("Idle")};
    bool active_ = false;
    bool using_real_backend_ = false;
};

}  // namespace redesk::ui
