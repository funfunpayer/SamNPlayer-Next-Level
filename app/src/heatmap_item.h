#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace samn::ui {

// The heatmap bar (0.7): one value 0-1 per time bucket, coloured
// hsl(220 - v x 220, 75 %, 35 + v x 20 %) from calm blue to intense red. It
// follows the timeline's zoom window and shows the marked range as a white
// frame and the playhead as a white line. The colours are rendered once
// into a 1 px-per-bucket image, so painting is a scaled blit.
class HeatmapItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY playheadMsChanged)
    Q_PROPERTY(double viewStartMs MEMBER viewStart_ NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs MEMBER viewEnd_ NOTIFY viewChanged)
    Q_PROPERTY(double markerStartMs MEMBER markerStart_ NOTIFY viewChanged)
    Q_PROPERTY(double markerEndMs MEMBER markerEnd_ NOTIFY viewChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY valuesChanged)
public:
    explicit HeatmapItem(QQuickItem* parent = nullptr);
    void setValues(std::vector<float> values, qint64 startMs, qint64 endMs);
    qint64 playheadMs() const { return playhead_; }
    void setPlayheadMs(qint64 ms);
    bool empty() const { return values_.empty(); }
    const std::vector<float>& values() const { return values_; }
    static QColor colorFor(float v);
    void paint(QPainter* p) override;

signals:
    void playheadMsChanged();
    void valuesChanged();
    void viewChanged();

private:
    void rebuildStrip();
    double xOf(double ms) const;
    std::vector<float> values_;
    qint64 start_ = 0, end_ = 0, playhead_ = -1;
    double viewStart_ = 0, viewEnd_ = -1, markerStart_ = 0, markerEnd_ = 0;
    QImage strip_;
};

}  // namespace samn::ui
