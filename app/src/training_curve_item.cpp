#include "training_curve_item.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace samn::ui {

TrainingCurveItem::TrainingCurveItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &TrainingCurveItem::viewChanged, this, [this] { update(); });
}

void TrainingCurveItem::setController(TrainingController* c) {
    if (c == ctl_) return;
    if (ctl_) disconnect(ctl_, nullptr, this, nullptr);
    ctl_ = c;
    if (ctl_) {
        connect(ctl_, &TrainingController::planChanged, this, [this] { update(); });
        connect(ctl_, &TrainingController::samplesChanged, this, [this] { update(); });
    }
    emit controllerChanged();
    update();
}

void TrainingCurveItem::paint(QPainter* p) {
    if (!ctl_) return;
    const auto& plan = ctl_->plan();
    const auto& vs = ctl_->vibrationSamples();
    const auto& ss = ctl_->suctionSamples();
    const double sampleMs = ctl_->sampleMs();
    const double nowMs = vs.size() * sampleMs;
    const bool windowed = viewEnd_ > viewStart_;
    const double total = std::max<double>(plan.total_ms, live_ ? nowMs : 0);
    const double t0 = windowed ? viewStart_ : 0, t1 = windowed ? viewEnd_ : std::max(total, 1.0);

    const double left = live_ ? 34 : 0, top = live_ ? 4 : 2, bottom = live_ ? 4 : 2;
    const QRectF r(left, top, width() - left - (live_ ? 2 : 0), height() - top - bottom);
    if (r.width() < 4 || r.height() < 4) return;
    const auto xOf = [&](double ms) { return r.left() + (ms - t0) / (t1 - t0) * r.width(); };
    const auto yOf = [&](double level) { return r.bottom() - std::clamp(level, 0.0, 1.0) * r.height(); };

    if (live_) {
        p->setFont(QFont("Figtree", 8));
        for (int i = 0; i <= 2; ++i) {
            const double lvl = i / 2.0, y = yOf(lvl);
            p->setPen(QColor(120, 170, 220, 40));
            p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
            p->setPen(QColor("#9fb3c8"));
            p->drawText(QRectF(0, y - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(int(lvl * 100)) + "%");
        }
        p->setPen(QColor(120, 170, 220, 22));
        for (int i = 1; i < 12; ++i) p->drawLine(QPointF(r.left() + r.width() * i / 12, r.top()), QPointF(r.left() + r.width() * i / 12, r.bottom()));
    }

    p->save();
    p->setClipRect(r.adjusted(-1, -2, 1, 2));
    // The plan: dashed and faint behind the live curves, solid in the timeline tiles.
    const auto drawPlan = [&](const std::vector<samn::training::CurvePoint>& pts, QColor c) {
        if (pts.size() < 2) return;
        QPainterPath path;
        for (size_t i = 0; i < pts.size(); ++i) {
            const QPointF pt(xOf(pts[i].at_ms), yOf(pts[i].level));
            i ? path.lineTo(pt) : path.moveTo(pt);
        }
        if (live_) c.setAlphaF(0.45);
        QPen pen(c, live_ ? 1.4 : 1.6);
        if (live_) pen.setStyle(Qt::DashLine);
        pen.setJoinStyle(Qt::RoundJoin);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPath(path);
    };
    drawPlan(plan.suction, sucColor_);
    drawPlan(plan.vibration, vibColor_);

    if (live_ && vs.size() > 1) {
        const auto drawLive = [&](const std::vector<float>& v, const QColor& c) {
            QPainterPath line;
            for (size_t i = 0; i < v.size(); ++i) {
                const QPointF pt(xOf(i * sampleMs), yOf(v[i]));
                if (i == 0) line.moveTo(pt);
                else {  // smooth through the midpoints
                    const QPointF prev(xOf((i - 1) * sampleMs), yOf(v[i - 1]));
                    line.quadTo(prev, (prev + pt) / 2);
                }
            }
            QPainterPath fill = line;
            fill.lineTo(xOf((v.size() - 1) * sampleMs), r.bottom());
            fill.lineTo(xOf(0), r.bottom());
            fill.closeSubpath();
            QLinearGradient g(0, r.top(), 0, r.bottom());
            QColor a = c, b = c;
            a.setAlphaF(0.22);
            b.setAlphaF(0.02);
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawPath(fill);
            QColor glow = c;
            glow.setAlphaF(0.25);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(glow, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(line);
            p->setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(line);
        };
        drawLive(ss, sucColor_);
        drawLive(vs, vibColor_);
    }
    if (live_ && !vs.empty()) {
        p->setPen(QPen(QColor(255, 255, 255, 200), 1.2));
        const double x = xOf(nowMs);
        p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    p->restore();
}

}  // namespace samn::ui
