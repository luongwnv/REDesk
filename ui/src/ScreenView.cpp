#include "ui/src/ScreenView.h"

#include <QPainter>

#include "ui/src/LocalScreenSource.h"

namespace redesk::ui {

ScreenView::ScreenView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setFillColor(QColor(QStringLiteral("#0E1116")));
}

void ScreenView::setSource(LocalScreenSource* s) {
    if (source_ == s) return;
    if (source_) disconnect(source_, nullptr, this, nullptr);
    source_ = s;
    if (source_) {
        // Repaint whenever a new frame lands.
        connect(source_, &LocalScreenSource::frameArrived, this,
                [this]() { update(); });
    }
    emit sourceChanged();
    update();
}

void ScreenView::paint(QPainter* painter) {
    const QRectF bounds = boundingRect();
    if (!source_) return;

    const QImage frame = source_->currentFrame();
    if (frame.isNull()) {
        painter->setPen(QColor(QStringLiteral("#5B6573")));
        painter->drawText(bounds, Qt::AlignCenter,
                          QStringLiteral("No signal"));
        return;
    }

    // Letterbox: scale to fit while preserving aspect ratio.
    const QSizeF imgSize = frame.size();
    const qreal scale = qMin(bounds.width() / imgSize.width(),
                             bounds.height() / imgSize.height());
    const QSizeF drawSize = imgSize * scale;
    const QRectF target(bounds.center().x() - drawSize.width() / 2.0,
                        bounds.center().y() - drawSize.height() / 2.0,
                        drawSize.width(), drawSize.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(target, frame);
}

}  // namespace redesk::ui
