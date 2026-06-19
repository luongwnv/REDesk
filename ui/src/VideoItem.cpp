// REDesk — VideoItem stub renderer (ADR-001 §3.5).
//
// Stub behavior: no decoder, no native-texture import. The renderer clears the
// RHI render target to the placeholder color each frame so the QML scene
// composes and lays out correctly. All real zero-copy work is marked TODO and
// must stay behind this single isolated interface (QRhi is QPA-tier / version-
// pinned — see VideoItem.h).

#include "ui/src/VideoItem.h"

#include <QQuickRhiItemRenderer>
#include <rhi/qrhi.h>

namespace redesk::ui {

// ---------------------------------------------------------------------------
// Render-thread side. Lives and dies on the scene-graph render thread; never
// touch GUI-thread state directly from here.
// ---------------------------------------------------------------------------
class VideoItemRenderer final : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* /*cb*/) override {
        // TODO(ADR §3.5): create the sampler + pipeline that samples the imported
        // decoder texture (NV12/P010 -> RGB in a shader). In the stub we only
        // clear, so no pipeline/resources are needed.
    }

    void synchronize(QQuickRhiItem* item) override {
        // Runs while the GUI thread is blocked: safe to copy item state across.
        auto* video = static_cast<VideoItem*>(item);
        clear_color_ = video->placeholderColor();
        // TODO(ADR §3.5): pick up the latest presentable frame here (mailbox:
        // keep only the newest, drop stale) and stage its native-texture import
        // so render() can sample it. Decouple from hard vsync — present ASAP.
    }

    void render(QRhiCommandBuffer* cb) override {
        QRhiRenderTarget* rt = renderTarget();
        if (!rt)
            return;

        // Stub: clear the whole surface to the placeholder color. The real path
        // would bind the sampling pipeline and draw a full-surface quad reading
        // the imported decoder texture. QRhiCommandBuffer::beginPass takes the
        // clear color as a QColor directly.
        cb->beginPass(rt, clear_color_, {1.0f, 0});
        cb->endPass();
    }

private:
    QColor clear_color_{QStringLiteral("#0E1116")};
};

// ---------------------------------------------------------------------------
// GUI-thread side.
// ---------------------------------------------------------------------------
VideoItem::VideoItem(QQuickItem* parent) : QQuickRhiItem(parent) {
    // ADR §3.5: live video should not be gated on chrome vsync. The fine-grained
    // mailbox/tear-allowed present is a render-thread concern; here we just make
    // sure the item repaints whenever a new frame is staged.
    setMirrorVertically(false);
}

VideoItem::~VideoItem() = default;

QQuickRhiItemRenderer* VideoItem::createRenderer() {
    return new VideoItemRenderer;
}

void VideoItem::setPlaceholderColor(const QColor& c) {
    if (placeholder_color_ == c)
        return;
    placeholder_color_ = c;
    emit placeholderColorChanged();
    update();
}

void VideoItem::presentStubFrame(const QSize& sourceSize) {
    if (source_size_ != sourceSize) {
        source_size_ = sourceSize;
        emit sourceSizeChanged();
    }
    if (!has_frame_) {
        has_frame_ = true;
        emit hasFrameChanged();
    }
    // TODO(ADR §3.5): in the real build, stage the native texture + sync object
    // for the renderer and request an ASAP present instead of this no-op.
    update();
}

} // namespace redesk::ui
