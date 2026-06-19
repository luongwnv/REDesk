#pragma once

// REDesk — zero-copy remote-video surface (ADR-001 §3.5).
//
// Presents decoded remote frames *under* the QML chrome via QQuickRhiItem,
// wrapping the native decoder texture with QRhiTexture::createFrom(). This is the
// only place in the UI that touches Qt's RHI, deliberately isolated because:
//
//   * QRhi / QQuickRhiItem are QPA-tier with NO source/binary-compat guarantee
//     across Qt minor versions (require Qt::GuiPrivate). Pin the Qt version and
//     regression-test this file on every upgrade (esp. 6.8 -> 6.12). (ADR §3.5)
//
//   * createFrom() is NOT an automatic cross-device import. The decode output
//     texture must either live on the SAME device as Qt's RHI, or be created
//     SHARED and synchronized with a keyed mutex / DMA-BUF fence:
//       - Windows : same ID3D11Device as Qt's RHI, OR
//                   D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX + keyed-mutex sync.
//       - Linux   : VAAPI -> Vulkan/EGL DMA-BUF import (NV12 multi-plane, must
//                   validate DRM format modifiers against the importer).
//       - macOS   : IOSurface -> MTLTexture (VideoToolbox is IOSurface-backed).
//     A format/modifier mismatch silently degrades to a CPU copy — assert/log it.
//     (ADR §3.5 "real zero-copy contract" + Risk #3.)
//
//   * The live-video surface is DECOUPLED from hard local vsync: present the
//     latest decoded frame ASAP (mailbox / tear-allowed on the video region) and
//     keep chrome vsync'd. Optimize for glass-to-glass latency, not vsync
//     correctness. (ADR §3.5 "latency vs vsync".)
//
// STUB BUILD (REDESK_USE_REAL_BACKENDS=OFF): this compiles with Qt only. It owns
// no decoder and imports no native texture; render() simply clears the surface to
// a placeholder color so the QML scene composes correctly. Every real-backend
// branch is a TODO pointing back at the ADR.

#include <QColor>
#include <QQuickRhiItem>
#include <QtQml/qqmlregistration.h>

#include <cstdint>

namespace redesk::ui {

class VideoItemRenderer; // forward decl — the QRhi-side renderer

// QML-instantiable item: `VideoItem { ... }`. The frame source (a presentable
// texture handle pushed from the decode pipeline) is wired separately so this
// header stays free of decoder/SDK types.
class VideoItem : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT

    // Placeholder fill shown before the first frame arrives / in the stub build.
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor
                   NOTIFY placeholderColorChanged)
    // Last frame's source resolution, surfaced to QML for letterboxing / overlays.
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    // True once a real decoded frame has been presented (vs. the placeholder).
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)

public:
    explicit VideoItem(QQuickItem* parent = nullptr);
    ~VideoItem() override;

    // QQuickRhiItem contract: build the renderer that runs on the render thread.
    QQuickRhiItemRenderer* createRenderer() override;

    QColor placeholderColor() const { return placeholder_color_; }
    void setPlaceholderColor(const QColor& c);

    QSize sourceSize() const { return source_size_; }
    bool hasFrame() const { return has_frame_; }

    // Called from the decode pipeline (see ServiceClient/transport) when a new
    // presentable frame is ready. In the real build this hands over a native
    // texture handle + sync primitive; the stub overload just bumps stats so the
    // UI can show "connected/streaming" state without a decoder.
    //
    // TODO(ADR §3.5): real signature takes the native handle + keyed-mutex/fence
    // + PixelFormat, performs QRhiTexture::createFrom() on the RHI thread, and
    // requests an ASAP (mailbox) present. Keep it on this isolated interface.
    void presentStubFrame(const QSize& sourceSize);

signals:
    void placeholderColorChanged();
    void sourceSizeChanged();
    void hasFrameChanged();

private:
    friend class VideoItemRenderer;

    QColor placeholder_color_{QStringLiteral("#0E1116")};
    QSize source_size_{0, 0};
    bool has_frame_{false};
};

} // namespace redesk::ui
