#include "curve_item.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <map>

namespace samn::ui {

CurveItem::CurveItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &CurveItem::styleChanged, this, [this] { update(); });
}

void CurveItem::setActions(std::vector<samn::Action> actions) {
    actions_ = std::move(actions);
    emit actionsChanged();
    update();
}

void CurveItem::setMarks(std::vector<int64_t> bookmarks, std::vector<std::pair<int64_t, int64_t>> chapters) {
    bookmarks_ = std::move(bookmarks);
    chapters_ = std::move(chapters);
    update();
}

void CurveItem::setPlayheadMs(qint64 ms) {
    if (ms == playhead_) return;
    playhead_ = ms;
    emit playheadMsChanged();
    update();
}

std::vector<CurveItem::Column> CurveItem::columns(int width) const {
    std::vector<Column> out;
    if (actions_.empty() || width <= 0) return out;
    const int64_t t0 = actions_.front().at, span = std::max<int64_t>(1, actions_.back().at - t0);
    std::map<int, std::pair<int, int>> cols;
    for (const auto& a : actions_) {
        const int x = static_cast<int>(std::min<int64_t>(width - 1, (a.at - t0) * (width - 1) / span));
        auto [it, inserted] = cols.try_emplace(x, a.pos, a.pos);
        if (!inserted) {
            it->second.first = std::min(it->second.first, a.pos);
            it->second.second = std::max(it->second.second, a.pos);
        }
    }
    out.reserve(cols.size());
    for (const auto& [x, lohi] : cols) out.push_back({x, lohi.first, lohi.second});
    return out;
}

std::optional<int> CurveItem::xFor(int64_t ms, int width) const {
    if (actions_.empty() || width <= 0) return std::nullopt;
    const int64_t t0 = actions_.front().at, span = std::max<int64_t>(1, actions_.back().at - t0);
    return static_cast<int>(std::clamp<int64_t>((ms - t0) * (width - 1) / span, 0, width - 1));
}

void CurveItem::paint(QPainter* p) {
    const int w = static_cast<int>(width()), h = static_cast<int>(height());
    const auto cols = columns(w);
    if (cols.empty()) return;
    for (size_t i = 0; i < chapters_.size(); ++i) {
        const auto [a, b] = chapters_[i];
        const int xa = *xFor(a, w), xb = *xFor(std::max(a, b), w);
        QColor band = i % 2 == 0 ? QColor(0x00, 0xd9, 0xf4) : QColor(0xb8, 0x96, 0xe8);
        band.setAlpha(26);
        p->fillRect(xa, 0, std::max(2, xb - xa), h, band);
    }
    p->setPen(QPen(QColor(0xe0, 0xa8, 0x6a), 1, Qt::DashLine));
    for (int64_t t : bookmarks_) {
        const int x = *xFor(t, w);
        p->drawLine(x, 0, x, h);
    }
    auto y = [h](int pos) { return (h - 4) - pos * (h - 8) / 100.0; };
    p->setRenderHint(QPainter::Antialiasing);
    // Soft glow under the line, then the line (Flow look: orange stroke lane).
    QPainterPath path;
    if (cols.size() == actions_.size()) {
        path.moveTo(cols.front().x, y(cols.front().lo));
        for (const auto& c : cols) path.lineTo(c.x, y(c.lo));
    } else {
        for (const auto& c : cols) {
            path.moveTo(c.x, y(c.lo));
            path.lineTo(c.x, y(c.hi));
        }
    }
    QColor glow = lineColor_;
    glow.setAlpha(60);
    p->setPen(QPen(glow, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
    p->setPen(QPen(lineColor_, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
    if (playhead_ >= 0) {
        const int x = *xFor(playhead_, w);
        p->setPen(QPen(playheadColor_, 1.5));
        p->drawLine(x, 0, x, h);
    }
}

}  // namespace samn::ui
