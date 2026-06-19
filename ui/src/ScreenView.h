#pragma once

// REDesk — simple CPU-path screen painter for the Phase-1 demo (ADR-001 §3.5).
//
// Paints a QImage (BGRA frames from LocalScreenSource) letterboxed into the item.
// This is deliberately the SAFE path: QQuickPaintedItem + QImage, no QRhi/QPA
// private headers, so it builds and runs reliably on any Qt 6. It is NOT the
// low-latency production path — that is VideoItem (QQuickRhiItem, zero-copy
// IOSurface->MTLTexture) which stays in the tree for Phase 3.

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace redesk::ui {

class LocalScreenSource;

class ScreenView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(redesk::ui::LocalScreenSource* source READ source WRITE setSource
                   NOTIFY sourceChanged)

public:
    explicit ScreenView(QQuickItem* parent = nullptr);

    LocalScreenSource* source() const { return source_; }
    void setSource(LocalScreenSource* s);

    void paint(QPainter* painter) override;

signals:
    void sourceChanged();

private:
    LocalScreenSource* source_ = nullptr;
};

}  // namespace redesk::ui
