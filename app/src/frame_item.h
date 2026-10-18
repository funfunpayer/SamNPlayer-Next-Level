#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// One video frame, scaled to fit with its aspect ratio (the Generator's
// marking canvas). imageRect is where the frame lands inside the item, so
// QML can map a drag in item coordinates to video pixels.
class FrameItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
    Q_PROPERTY(QRectF imageRect READ imageRect NOTIFY imageRectChanged)

public:
    explicit FrameItem(QQuickItem* parent = nullptr);
    QImage image() const { return image_; }
    void setImage(const QImage& image);
    QRectF imageRect() const;
    void paint(QPainter* painter) override;

signals:
    void imageChanged();
    void imageRectChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    QImage image_;
};

}  // namespace samn::ui
