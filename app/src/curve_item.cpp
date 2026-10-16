#include "curve_item.h"

#include <QFont>
#include <QQuickWindow>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace samn::ui {

CurveItem::CurveItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &CurveItem::styleChanged, this, [this] { updateMode(); invalidate(); });
    connect(this, &QQuickItem::widthChanged, this, [this] { updateMode(); invalidate(); });
    connect(this, &QQuickItem::heightChanged, this, &CurveItem::invalidate);
    connect(this, &CurveItem::markerChanged, this, &CurveItem::invalidate);
}

void CurveItem::invalidate() {
    layerValid_ = false;
    update();
}

void CurveItem::setActions(std::vector<samn::Action> actions) {
    actions_ = std::move(actions);
    emit actionsChanged();
    updateMode();
    invalidate();
}

void CurveItem::setMarks(std::vector<int64_t> bookmarks, std::vector<std::pair<int64_t, int64_t>> chapters) {
    bookmarks_ = std::move(bookmarks);
    chapters_ = std::move(chapters);
    invalidate();
}

void CurveItem::setOverlays(std::vector<Band> bands, double beatPeriodMs) {
    bands_ = std::move(bands);
    beatPeriod_ = beatPeriodMs;
    invalidate();
}

void CurveItem::setPlayheadMs(qint64 ms) {
    if (ms == playhead_) return;
    playhead_ = ms;
    emit playheadMsChanged();
    update();
}

void CurveItem::setViewStartMs(double ms) {
    if (ms == viewStart_) return;
    viewStart_ = ms;
    emit viewChanged();
    updateMode();
    invalidate();
}

void CurveItem::setViewEndMs(double ms) {
    if (ms == viewEnd_) return;
    viewEnd_ = ms;
    emit viewChanged();
    updateMode();
    invalidate();
}

std::pair<double, double> CurveItem::timeWindow() const {
    if (viewEnd_ > viewStart_) return {viewStart_, viewEnd_};
    if (actions_.empty()) return {0, 1};
    const double t0 = double(actions_.front().at), t1 = double(actions_.back().at);
    return {t0, std::max(t0 + 1, t1)};
}

double CurveItem::xOf(double ms, double w) const {
    const auto [t0, t1] = timeWindow();
    return (ms - t0) / (t1 - t0) * w;
}

double CurveItem::yOf(double pos, double h) const {
    const double pad = label_.isEmpty() ? kPad : 3;
    return pad + (1 - pos / 100.0) * (h - pad * 2);
}

CurveItem::Mode CurveItem::modeFor(double widthPx) const {
    if (actions_.size() < 2 || widthPx <= 0) return Mode::Empty;
    const auto [t0, t1] = timeWindow();
    const size_t visible = samn::CountInWindow(actions_, int64_t(std::floor(t0)), int64_t(std::ceil(t1)));
    if (!samn::IsDense(visible, widthPx, editMode_ ? samn::kEditMinGapPx : samn::kDotMinGapPx)) return Mode::Smooth;
    return lane_ == QLatin1String("stroke") ? Mode::Tempo : Mode::Fine;
}

double CurveItem::msAtX(double x) const {
    const auto [t0, t1] = timeWindow();
    return t0 + std::clamp(x / std::max(1.0, width()), 0.0, 1.0) * (t1 - t0);
}

QString CurveItem::hoverText(double x) const {
    if (actions_.empty()) return {};
    const double ms = msAtX(x);
    const auto pos = samn::PositionAt(actions_, int64_t(ms));
    return QString::fromStdString(samn::FormatTooltipTime(int64_t(ms))) +
           QStringLiteral(" · %1").arg(std::lround(pos.value_or(0)));
}

QString CurveItem::modeName() const {
    switch (mode_) {
    case Mode::Smooth: return QStringLiteral("smooth");
    case Mode::Fine: return QStringLiteral("fine");
    case Mode::Tempo: return QStringLiteral("tempo");
    default: return QStringLiteral("empty");
    }
}

void CurveItem::updateMode() {
    const Mode m = modeFor(width());
    if (m == mode_) return;
    mode_ = m;
    emit modeChanged();
}

namespace {

// QPainter has no shadow blur: a glow is a few wide, faint strokes under the
// line, falling off like 0.7's canvas shadowBlur.
void StrokeWithGlow(QPainter* p, const QPainterPath& path, const QColor& color, qreal width, qreal glow) {
    if (glow > 0) {
        const qreal steps[] = {1.0, 0.6, 0.3};
        const int alphas[] = {18, 34, 60};
        for (int i = 0; i < 3; ++i) {
            QColor c = color;
            c.setAlpha(alphas[i]);
            p->setPen(QPen(c, width + glow * steps[i], Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(path);
        }
    }
    p->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
}

void FillToBase(QPainter* p, QPainterPath path, const QColor& color, qreal alpha, qreal top, qreal baseY) {
    const QPointF last = path.currentPosition();
    path.lineTo(last.x(), baseY);
    path.lineTo(path.elementAt(0).x, baseY);
    path.closeSubpath();
    QLinearGradient g(0, top, 0, baseY);
    QColor c = color;
    c.setAlphaF(alpha);
    g.setColorAt(0, c);
    c.setAlpha(0);
    g.setColorAt(1, c);
    p->fillPath(path, g);
}

}  // namespace

void CurveItem::paint(QPainter* p) {
    const double w = width(), h = height();
    if (w <= 0 || h <= 0) return;
    const Mode mode = modeFor(w);
    const qreal dpr = QQuickItem::window() ? QQuickItem::window()->effectiveDevicePixelRatio() : 1.0;
    const QSize px(int(std::ceil(w * dpr)), int(std::ceil(h * dpr)));
    if (!layerValid_ || layer_.size() != px || layer_.devicePixelRatio() != dpr) {
        layer_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
        layer_.setDevicePixelRatio(dpr);
        layer_.fill(Qt::transparent);
        QPainter lp(&layer_);
        lp.setRenderHint(QPainter::Antialiasing);
        paintLayer(&lp, w, h, mode);
        layerValid_ = true;
        ++layerRenders_;
    }
    p->drawImage(QPointF(0, 0), layer_);

    if (showPlayhead_ && playhead_ > 0) {
        p->setRenderHint(QPainter::Antialiasing);
        const double x = std::round(xOf(double(playhead_), w)) + 0.5;
        p->setPen(QPen(playheadColor_, 1.5));
        p->drawLine(QPointF(x, 0), QPointF(x, h));
        // The tempo view shows speed, not position: no position dot on it.
        if (mode != Mode::Tempo && mode != Mode::Empty) {
            if (const auto pos = samn::PositionAt(actions_, playhead_)) {
                p->setBrush(lineColor_);
                p->setPen(QPen(QColor(255, 255, 255), 1.25));
                p->drawEllipse(QPointF(x, yOf(*pos, h)), 5, 5);
            }
        }
    }
}

void CurveItem::paintLayer(QPainter* p, double w, double h, Mode mode) {
    auto X = [&](double ms) { return xOf(ms, w); };
    auto Y = [&](double pos) { return yOf(pos, h); };

    if (showGrid_) {
        p->setPen(QPen(QColor(255, 255, 255, 26), 1));
        for (int pos : {0, 50, 100}) {
            const double y = std::round(Y(pos)) + 0.5;
            p->drawLine(QPointF(0, y), QPointF(w, y));
        }
    }
    for (size_t i = 0; i < chapters_.size(); ++i) {
        const auto [a, b] = chapters_[i];
        QColor band = i % 2 == 0 ? QColor(0x00, 0xd9, 0xf4) : QColor(0xb8, 0x96, 0xe8);
        band.setAlpha(26);
        const double xa = X(double(a)), xb = X(double(std::max(a, b)));
        p->fillRect(QRectF(xa, 0, std::max(2.0, xb - xa), h), band);
    }
    // Beat grid (0.7 drawBpmGrid): every 4th line is a bar line.
    const auto [g0, g1] = timeWindow();
    if (beatPeriod_ > 0) {
        long long n = static_cast<long long>(std::floor(g0 / beatPeriod_));
        int drawn = 0;
        for (double t = double(n) * beatPeriod_; t <= g1 && drawn < 240; t = double(++n) * beatPeriod_) {
            if (t < g0) continue;
            const bool bar = n % 4 == 0;
            const double x = std::round(X(t)) + 0.5;
            p->setPen(QPen(QColor(184, 150, 232, bar ? 97 : 41), bar ? 1.25 : 1.0));
            p->drawLine(QPointF(x, 0), QPointF(x, h));
            ++drawn;
        }
    }
    for (const auto& b : bands_) {
        if (double(b.end_ms) < g0 || double(b.start_ms) > g1) continue;
        const double x0 = X(double(b.start_ms)), x1 = X(double(b.end_ms));
        const QColor c = b.primary ? QColor(255, 60, 60, 89) : QColor(255, 160, 40, int((0.18 + b.intensity * 0.15) * 255));
        p->fillRect(QRectF(x0, 0, std::max(1.0, x1 - x0), h), c);
    }
    if (markerEnd_ > markerStart_) {
        const double x0 = X(markerStart_), x1 = X(markerEnd_);
        p->fillRect(QRectF(x0, 0, x1 - x0, h), QColor(0, 200, 255, 31));
    }
    p->setPen(QPen(QColor(0xe0, 0xa8, 0x6a), 1, Qt::DashLine));
    for (int64_t t : bookmarks_) {
        const double x = X(double(t));
        if (x >= 0 && x <= w) p->drawLine(QPointF(x, 0), QPointF(x, h));
    }

    const double baseY = Y(0), top = Y(100);
    const auto [t0, t1] = timeWindow();
    if (mode == Mode::Tempo) {
        const auto wave = samn::SpeedWave(actions_, t0, t1, w, samn::SpeedBinPx(
            samn::CountInWindow(actions_, int64_t(std::floor(t0)), int64_t(std::ceil(t1))), w));
        if (wave.size() >= 2) {
            // Calm path through the bins: quadratic curves via the midpoints.
            QPainterPath path(QPointF(wave[0].at, Y(wave[0].pos)));
            for (size_t i = 1; i + 1 < wave.size(); ++i) {
                const QPointF c(wave[i].at, Y(wave[i].pos));
                const QPointF m((wave[i].at + wave[i + 1].at) / 2, (Y(wave[i].pos) + Y(wave[i + 1].pos)) / 2);
                path.quadTo(c, m);
            }
            path.lineTo(wave.back().at, Y(wave.back().pos));
            FillToBase(p, path, lineColor_, 0.22, top, baseY);
            StrokeWithGlow(p, path, lineColor_, std::max<qreal>(1.6, lineWidth_), std::max<qreal>(8, glow_));
            QFont f(QStringLiteral("Figtree"));
            f.setPixelSize(10);
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            QColor c = lineColor_;
            c.setAlphaF(0.85);
            p->setPen(c);
            p->drawText(QPointF(8, top + 4 + 10), tr("TEMPO · zoom in for strokes"));
        }
    } else if (mode != Mode::Empty) {
        const auto [first, last] = samn::VisibleRange(actions_, int64_t(std::floor(t0)), int64_t(std::ceil(t1)));
        auto smoothPath = [&, first = first, last = last] {
            QPainterPath path(QPointF(X(double(actions_[first].at)), Y(actions_[first].pos)));
            for (const auto& s : samn::CatmullRomBezier(actions_, first, last))
                path.cubicTo(QPointF(X(s.c1.at), Y(s.c1.pos)), QPointF(X(s.c2.at), Y(s.c2.pos)),
                             QPointF(X(s.p2.at), Y(s.p2.pos)));
            return path;
        };
        if (mode == Mode::Smooth) {
            const QPainterPath path = smoothPath();
            if (fill_ > 0) FillToBase(p, path, lineColor_, fill_, top, baseY);
            StrokeWithGlow(p, path, lineColor_, lineWidth_, glow_);
            // Keyframe dots: they are readable here.
            const size_t n = last - first + 1;
            const size_t step = n > size_t(kMaxDots) ? (n + kMaxDots - 1) / kMaxDots : 1;
            p->setPen(Qt::NoPen);
            p->setBrush(editMode_ ? QColor(255, 255, 255) : QColor(255, 255, 255, 217));
            const qreal r = editMode_ ? 3.5 : 2.2;
            for (size_t i = first; i <= last; i += step)
                p->drawEllipse(QPointF(X(double(actions_[i].at)), Y(actions_[i].pos)), r, r);
        } else if (last - first > size_t(w) * 3) {
            // Far more points than pixels: each 1 px column draws the span of
            // its points. Looks the same as the fine line, costs O(width).
            QPainterPath cols;
            double colX = -1, lo = 0, hi = 0;
            auto flush = [&] {
                if (colX < 0) return;
                cols.moveTo(colX + 0.5, Y(lo));
                cols.lineTo(colX + 0.5, Y(hi) - (hi == lo ? 0.5 : 0));
            };
            for (size_t i = first; i <= last; ++i) {
                const double cx = std::floor(X(double(actions_[i].at)));
                const double v = actions_[i].pos;
                if (cx != colX) {
                    flush();
                    colX = cx;
                    lo = hi = v;
                } else {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
            flush();
            StrokeWithGlow(p, cols, lineColor_, 1.0, 0);
        } else {
            StrokeWithGlow(p, smoothPath(), lineColor_, 1.0, 0);
        }
    }

    if (!label_.isEmpty()) {
        QFont f(QStringLiteral("Figtree"));
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(lineColor_);
        p->drawText(QPointF(6, 3 + 10), label_);
    }

}

}  // namespace samn::ui
