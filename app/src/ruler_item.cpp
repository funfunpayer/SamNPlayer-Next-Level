#include "ruler_item.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "samn/curve_geometry.h"

namespace samn::ui {

RulerItem::RulerItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &RulerItem::changed, this, [this] { update(); });
}

void RulerItem::paint(QPainter* p) {
    const double w = width(), h = height();
    const double span = viewEnd_ - viewStart_;
    if (w <= 0 || h <= 0 || span <= 1) return;
    const int64_t step = samn::RulerStepMs(span, w);
    auto x = [&](double ms) { return (ms - viewStart_) / span * w; };
    QFont f(QStringLiteral("Figtree"));
    f.setPixelSize(11);
    f.setWeight(QFont::Medium);
    p->setFont(f);
    const QFontMetricsF fm(f);
    QColor tick = textColor_;
    tick.setAlphaF(0.5);
    for (double t = std::ceil(viewStart_ / double(step)) * double(step); t <= viewEnd_; t += double(step)) {
        const double tx = std::round(x(t)) + 0.5;
        p->fillRect(QRectF(tx, h - 5, 1, 5), tick);
        const QString label = QString::fromStdString(samn::FormatRulerTime(int64_t(t)));
        const double tw = fm.horizontalAdvance(label);
        if (tx + tw / 2 < w && tx - tw / 2 > 0) {
            p->setPen(textColor_);
            p->drawText(QPointF(tx - tw / 2, 2 + fm.ascent()), label);
        }
    }
    if (playhead_ > 0) {
        const double px = x(playhead_);
        QPainterPath tri;
        tri.moveTo(px - 5, h - 7);
        tri.lineTo(px + 5, h - 7);
        tri.lineTo(px, h);
        tri.closeSubpath();
        p->fillPath(tri, markColor_);
    }
}

}  // namespace samn::ui
