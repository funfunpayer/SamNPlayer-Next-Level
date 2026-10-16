#pragma once

#include <QQuickImageProvider>

namespace samn::ui {

// "image://icon/<name>?<color>": a Flow icon (0.7 flow_icons.js, 24 px
// grid, 1.6 px strokes, currentColor) rendered sharp at the requested size
// in any colour. The SVGs live in qml/icons/.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    // The SVG text with currentColor replaced; empty for an unknown name.
    static QByteArray svgFor(const QString& name, const QColor& color);
};

}  // namespace samn::ui
