#include "heatmap_item.h"

#include <QPainter>
#include <algorithm>

namespace samn::ui {

HeatmapItem::HeatmapItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

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
    // Flow look: calm blue → cyan → orange for intense parts.
    v = std::clamp(v, 0.0f, 1.0f);
    const QColor a(0x0b, 0x3a, 0x78), b(0x00, 0xd9, 0xf4), c(0xff, 0x8a, 0x23);
    auto mix = [](const QColor& x, const QColor& y, float t) {
        return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t, x.greenF() + (y.greenF() - x.greenF()) * t,
                                x.blueF() + (y.blueF() - x.blueF()) * t);
    };
    return v < 0.5f ? mix(a, b, v * 2) : mix(b, c, (v - 0.5f) * 2);
}

void HeatmapItem::rebuildStrip() {
    if (values_.empty()) {
        strip_ = QImage();
        return;
    }
    strip_ = QImage(static_cast<int>(values_.size()), 1, QImage::Format_RGB32);
    for (size_t i = 0; i < values_.size(); ++i) strip_.setPixelColor(static_cast<int>(i), 0, colorFor(values_[i]));
}

void HeatmapItem::paint(QPainter* p) {
    if (strip_.isNull()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(boundingRect(), strip_);
    if (playhead_ >= 0 && end_ > start_) {
        const double x = (double(playhead_ - start_) / double(end_ - start_)) * width();
        p->setPen(QPen(Qt::white, 2));
        p->drawLine(QPointF(x, 0), QPointF(x, height()));
    }
}

}  // namespace samn::ui
