#include "heatmap_item.h"

#include <QPainter>
#include <algorithm>

namespace samn::ui {

HeatmapItem::HeatmapItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    connect(this, &HeatmapItem::viewChanged, this, [this] { update(); });
}

void HeatmapItem::setValues(std::vector<float> values, qint64 startMs, qint64 endMs) {
    values_ = std::move(values);
    start_ = startMs;
    end_ = endMs;
    rebuildStrip();
    emit valuesChanged();
    update();
}

void HeatmapItem::setPlayheadMs(qint64 ms) {
    if (ms == playhead_) return;
    playhead_ = ms;
    emit playheadMsChanged();
    update();
}

QColor HeatmapItem::colorFor(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return QColor::fromHslF((220.0f - v * 220.0f) / 360.0f, 0.75f, 0.35f + v * 0.20f);
}

void HeatmapItem::rebuildStrip() {
    if (values_.empty()) {
        strip_ = QImage();
        return;
    }
    strip_ = QImage(static_cast<int>(values_.size()), 1, QImage::Format_RGB32);
    for (size_t i = 0; i < values_.size(); ++i) strip_.setPixelColor(static_cast<int>(i), 0, colorFor(values_[i]));
}

double HeatmapItem::xOf(double ms) const {
    const double t0 = viewEnd_ > viewStart_ ? viewStart_ : double(start_);
    const double t1 = viewEnd_ > viewStart_ ? viewEnd_ : double(end_);
    return (ms - t0) / std::max(1.0, t1 - t0) * width();
}

void HeatmapItem::paint(QPainter* p) {
    if (strip_.isNull() || end_ <= start_) return;
    // The buckets cover [start, end]; place that span on the view and let
    // the clip cut what is outside.
    const double x0 = xOf(double(start_)), x1 = xOf(double(end_));
    p->setClipRect(boundingRect());
    p->drawImage(QRectF(x0, 0, std::max(1.0, x1 - x0), height()), strip_);
    if (markerEnd_ > markerStart_) {
        const double a = xOf(markerStart_), b = xOf(markerEnd_);
        p->setPen(QPen(Qt::white, 2));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(a, 1, std::max(1.0, b - a), height() - 2));
    }
    if (playhead_ >= 0) {
        const double x = xOf(double(playhead_));
        p->setPen(QPen(Qt::white, 2));
        p->drawLine(QPointF(x, 0), QPointF(x, height()));
    }
}

}  // namespace samn::ui
