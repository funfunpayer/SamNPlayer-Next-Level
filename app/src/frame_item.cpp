#include "frame_item.h"

#include <QPainter>

namespace samn::ui {

FrameItem::FrameItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void FrameItem::setImage(const QImage& image) {
    const bool size_changed = image.size() != image_.size();
    image_ = image;
    emit imageChanged();
    if (size_changed) emit imageRectChanged();
    update();
}

QRectF FrameItem::imageRect() const {
    if (image_.isNull() || width() <= 0 || height() <= 0) return {};
    const QSizeF s = QSizeF(image_.size()).scaled(size(), Qt::KeepAspectRatio);
    return QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

void FrameItem::paint(QPainter* painter) {
    if (image_.isNull()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(imageRect(), image_);
}

void FrameItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) emit imageRectChanged();
}

}  // namespace samn::ui
