#include "wordmark_item.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>

namespace samn::ui {

namespace {
// Room around the letters for the glow.
constexpr double kPad = 8;
}  // namespace

WordmarkItem::WordmarkItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    rebuild();
}

void WordmarkItem::setPixelSize(int size) {
    if (size == pixelSize_ || size <= 0) return;
    pixelSize_ = size;
    rebuild();
    emit pixelSizeChanged();
}

void WordmarkItem::rebuild() {
    QFont bold(QStringLiteral("Sora"));
    bold.setPixelSize(pixelSize_);
    bold.setWeight(QFont::Bold);
    bold.setLetterSpacing(QFont::AbsoluteSpacing, -0.6);
    QFont light = bold;
    light.setWeight(QFont::Light);
    light.setLetterSpacing(QFont::AbsoluteSpacing, 0);

    const QFontMetricsF boldMetrics(bold);
    ascent_ = boldMetrics.ascent();
    const double base = kPad + ascent_;
    double x = kPad;
    const auto add = [&](int i, const QFont& font, const QString& s, QColor top, QColor bottom, QColor glow) {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addText(x, base, font, s);
        words_[i] = {path, top, bottom, glow};
        x += QFontMetricsF(font).horizontalAdvance(s);
    };
    add(0, bold, QStringLiteral("SamN"), QColor(0xff, 0xd2, 0x3f), QColor(0xff, 0x7a, 0x14), QColor(255, 140, 30));
    add(1, bold, QStringLiteral("Player"), QColor(0x7a, 0xea, 0xff), QColor(0x1a, 0x95, 0xff), QColor(0, 190, 255));
    x += pixelSize_ * 0.28;
    add(2, light, QStringLiteral("Next"), QColor(0xff, 0xff, 0xff), QColor(0xc9, 0xd8, 0xea), QColor(255, 255, 255));

    setImplicitSize(x + kPad, base + boldMetrics.descent() + kPad);
    update();
}

void WordmarkItem::paint(QPainter* p) {
    p->setRenderHint(QPainter::Antialiasing);
    const double top = kPad + ascent_ * 0.18, bottom = kPad + ascent_ * 1.1;
    for (const Word& w : words_) {
        // The halo: wide, faint strokes under the letters.
        for (const auto& [width, alpha] : {std::pair{10.0, 0.06}, {6.0, 0.10}, {3.0, 0.16}}) {
            QColor c = w.glow;
            c.setAlphaF(w.glow == QColor(255, 255, 255) ? alpha * 0.6 : alpha);
            p->strokePath(w.path, QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        QLinearGradient g(0, top, 0, bottom);
        g.setColorAt(0, w.top);
        g.setColorAt(1, w.bottom);
        p->fillPath(w.path, g);
    }
}

}  // namespace samn::ui
