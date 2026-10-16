#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// The time ruler above the curve (0.7 redrawRuler): labels every 1 s … 1 h,
// whichever step keeps them 72 px apart, small ticks, and the playhead as a
// triangle in the vibration colour.
class RulerItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double viewStartMs MEMBER viewStart_ NOTIFY changed)
    Q_PROPERTY(double viewEndMs MEMBER viewEnd_ NOTIFY changed)
    Q_PROPERTY(double playheadMs MEMBER playhead_ NOTIFY changed)
    Q_PROPERTY(QColor textColor MEMBER textColor_ NOTIFY changed)
    Q_PROPERTY(QColor markColor MEMBER markColor_ NOTIFY changed)
public:
    explicit RulerItem(QQuickItem* parent = nullptr);
    void paint(QPainter* p) override;

signals:
    void changed();

private:
    double viewStart_ = 0, viewEnd_ = 1, playhead_ = -1;
    QColor textColor_{0x9d, 0xb8, 0xca};
    QColor markColor_{0x00, 0xbf, 0xe9};
};

}  // namespace samn::ui
