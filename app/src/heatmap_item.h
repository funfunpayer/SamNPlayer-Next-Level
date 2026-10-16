#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace samn::ui {

// The intensity strip under the transport (0.7 heatmap): one value 0-1 per
// time bucket, coloured cool → warm. The colour strip is rendered once into
// an image when the values change, so painting while playing is a blit.
class HeatmapItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY playheadMsChanged)
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

private:
    void rebuildStrip();
    std::vector<float> values_;
    qint64 start_ = 0, end_ = 0, playhead_ = -1;
    QImage strip_;
};

}  // namespace samn::ui
