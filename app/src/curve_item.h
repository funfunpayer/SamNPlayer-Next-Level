#pragma once

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include <utility>
#include <vector>

#include "samn/funscript.h"

namespace samn::ui {

// The script curve (position 0-100 over time) drawn in the timeline. Long
// scripts have far more points than pixels, so each pixel column draws the
// min-max span of its points (level of detail).
class CurveItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY playheadMsChanged)
    Q_PROPERTY(QColor lineColor MEMBER lineColor_ NOTIFY styleChanged)
    Q_PROPERTY(QColor playheadColor MEMBER playheadColor_ NOTIFY styleChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY actionsChanged)
public:
    struct Column {
        int x, lo, hi;
        bool operator==(const Column& o) const { return x == o.x && lo == o.lo && hi == o.hi; }
    };

    explicit CurveItem(QQuickItem* parent = nullptr);
    void setActions(std::vector<samn::Action> actions);
    void setMarks(std::vector<int64_t> bookmarks, std::vector<std::pair<int64_t, int64_t>> chapters);
    qint64 playheadMs() const { return playhead_; }
    void setPlayheadMs(qint64 ms);  // < 0 = none
    bool empty() const { return actions_.empty(); }

    std::vector<Column> columns(int width) const;
    std::optional<int> xFor(int64_t ms, int width) const;
    void paint(QPainter* p) override;

signals:
    void playheadMsChanged();
    void actionsChanged();
    void styleChanged();

private:
    std::vector<samn::Action> actions_;
    std::vector<int64_t> bookmarks_;
    std::vector<std::pair<int64_t, int64_t>> chapters_;
    qint64 playhead_ = -1;
    QColor lineColor_{0xff, 0x8a, 0x23};
    QColor playheadColor_{0xe8, 0xf6, 0xff};
};

}  // namespace samn::ui
