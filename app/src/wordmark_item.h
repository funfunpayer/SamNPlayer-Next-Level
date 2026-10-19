#pragma once

#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// The banner's wordmark (D-151): "SamN" yellow to orange, "Player" cyan to
// blue, each with its own soft glow, then a light white "Next". Painted as
// text outlines so the vertical gradients and the halo stay crisp at any
// scale; the size follows the font's pixel size.
class WordmarkItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int pixelSize READ pixelSize WRITE setPixelSize NOTIFY pixelSizeChanged)
    Q_PROPERTY(QString text READ text CONSTANT)
public:
    explicit WordmarkItem(QQuickItem* parent = nullptr);
    int pixelSize() const { return pixelSize_; }
    void setPixelSize(int size);
    QString text() const { return QStringLiteral("SamNPlayer Next"); }
    void paint(QPainter* p) override;

signals:
    void pixelSizeChanged();

private:
    void rebuild();
    struct Word {
        QPainterPath path;
        QColor top, bottom, glow;
    };
    int pixelSize_ = 34;
    Word words_[3];
    double ascent_ = 0;
};

}  // namespace samn::ui
