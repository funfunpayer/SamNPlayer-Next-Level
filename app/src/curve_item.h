#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <utility>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/funscript.h"

namespace samn::ui {

// One script curve as SamNPlayer 0.7 draws it: a soft Catmull-Rom line with
// a glow and a faint fill to the baseline, keyframe dots once they are
// readable, a live position dot at the playhead. A dense stroke curve turns
// into the calm tempo wave ("TEMPO · zoom in for strokes"); other dense
// curves become a fine line. The geometry lives in samn/curve_geometry.h.
//
// All lanes share one time axis: [viewStartMs, viewEndMs]. When the view is
// not set, the script's own span is used.
class CurveItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY playheadMsChanged)
    Q_PROPERTY(double viewStartMs READ viewStartMs WRITE setViewStartMs NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs READ viewEndMs WRITE setViewEndMs NOTIFY viewChanged)
    Q_PROPERTY(QColor lineColor MEMBER lineColor_ NOTIFY styleChanged)
    Q_PROPERTY(QColor playheadColor MEMBER playheadColor_ NOTIFY styleChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY actionsChanged)
    // Which lane the controller feeds: "stroke" (default), "vibration", "suction".
    Q_PROPERTY(QString lane MEMBER lane_ NOTIFY styleChanged)
    // Shown in the lane's top left corner (thin lanes); empty = none.
    Q_PROPERTY(QString label MEMBER label_ NOTIFY styleChanged)
    Q_PROPERTY(bool showPlayhead MEMBER showPlayhead_ NOTIFY styleChanged)
    // The 0 / 50 / 100 guide lines (main curve).
    Q_PROPERTY(bool showGrid MEMBER showGrid_ NOTIFY styleChanged)
    // Editing keeps single strokes down to 4 px instead of 14 px.
    Q_PROPERTY(bool editMode MEMBER editMode_ NOTIFY styleChanged)
    Q_PROPERTY(qreal lineWidth MEMBER lineWidth_ NOTIFY styleChanged)
    Q_PROPERTY(qreal glow MEMBER glow_ NOTIFY styleChanged)
    Q_PROPERTY(qreal fill MEMBER fill_ NOTIFY styleChanged)
    Q_PROPERTY(QString mode READ modeName NOTIFY modeChanged)
public:
    enum class Mode { Empty, Smooth, Fine, Tempo };
    static constexpr int kPad = 6;          // 0.7 CURVE_PAD
    static constexpr int kMaxDots = 600;    // 0.7 DOT_MAX_DRAW

    explicit CurveItem(QQuickItem* parent = nullptr);
    void setActions(std::vector<samn::Action> actions);
    void setMarks(std::vector<int64_t> bookmarks, std::vector<std::pair<int64_t, int64_t>> chapters);
    qint64 playheadMs() const { return playhead_; }
    void setPlayheadMs(qint64 ms);  // < 0 = none
    // Time window in ms (QML numbers are doubles).
    double viewStartMs() const { return viewStart_; }
    double viewEndMs() const { return viewEnd_; }
    void setViewStartMs(double ms);
    void setViewEndMs(double ms);
    bool empty() const { return actions_.empty(); }
    QString lane() const { return lane_; }

    // How the curve draws at this width: smooth, fine line or tempo wave.
    Mode modeFor(double widthPx) const;
    QString modeName() const;
    void paint(QPainter* p) override;
    // How often the static layer (everything but the playhead) was drawn.
    int layerRenders() const { return layerRenders_; }

signals:
    void playheadMsChanged();
    void viewChanged();
    void actionsChanged();
    void styleChanged();
    void modeChanged();

private:
    std::pair<double, double> timeWindow() const;
    double xOf(double ms, double w) const;
    double yOf(double pos, double h) const;
    void updateMode();
    void invalidate();
    void paintLayer(QPainter* p, double w, double h, Mode mode);

    std::vector<samn::Action> actions_;
    std::vector<int64_t> bookmarks_;
    std::vector<std::pair<int64_t, int64_t>> chapters_;
    qint64 playhead_ = -1;
    double viewStart_ = 0, viewEnd_ = -1;
    QColor lineColor_{0xff, 0x8a, 0x23};
    QColor playheadColor_{0xff, 0xff, 0xff};
    QString lane_ = QStringLiteral("stroke"), label_;
    bool showPlayhead_ = true, showGrid_ = false, editMode_ = false;
    qreal lineWidth_ = 1.5, glow_ = 8, fill_ = 0.16;
    Mode mode_ = Mode::Empty;
    // The curve without the playhead, redrawn only when data, view, size or
    // style change: while playing, a frame is one blit plus a line.
    QImage layer_;
    bool layerValid_ = false;
    int layerRenders_ = 0;
};

}  // namespace samn::ui
