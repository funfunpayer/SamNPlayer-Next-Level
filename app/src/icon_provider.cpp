#include "icon_provider.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace samn::ui {

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QByteArray IconProvider::svgFor(const QString& name, const QColor& color) {
    // Names are a fixed set of files: no paths from outside.
    for (QChar c : name)
        if (!c.isLetterOrNumber()) return {};
    QFile f(QStringLiteral(":/qt/qml/SamN/qml/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = f.readAll();
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    if (color.alpha() < 255) svg.replace("<svg ", "<svg opacity=\"" + QByteArray::number(color.alphaF()) + "\" ");
    return svg;
}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const int q = id.indexOf(QLatin1Char('?'));
    const QString name = q < 0 ? id : id.left(q);
    const QColor color = q < 0 ? QColor(Qt::white) : QColor(id.mid(q + 1));
    const QSize px = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(24, 24);
    if (size) *size = px;
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QSvgRenderer svg(svgFor(name, color.isValid() ? color : QColor(Qt::white)));
    if (!svg.isValid()) return img;  // unknown icon: empty, never a dummy
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    svg.render(&p);
    return img;
}

}  // namespace samn::ui
