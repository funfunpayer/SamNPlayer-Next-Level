#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "training_controller.h"

namespace samn::ui {

// The training curves (0.7 "Live training curves" and the timeline's mini curves):
// the planned vibration / suction levels, and in live mode what the session really
// did, with a playhead. It repaints when the plan or a new sample arrives (a few
// times a second at most), never per frame.
class TrainingCurveItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(samn::ui::TrainingController* controller READ controller WRITE setController NOTIFY controllerChanged)
    // Axis labels (100 / 50 / 0 %) and a grid, the live curves and the playhead.
    Q_PROPERTY(bool live MEMBER live_ NOTIFY viewChanged)
    // The part of the plan to show in ms; end <= start = all of it.
    Q_PROPERTY(double viewStartMs MEMBER viewStart_ NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs MEMBER viewEnd_ NOTIFY viewChanged)
    Q_PROPERTY(QColor vibrationColor MEMBER vibColor_ NOTIFY viewChanged)
    Q_PROPERTY(QColor suctionColor MEMBER sucColor_ NOTIFY viewChanged)
public:
    explicit TrainingCurveItem(QQuickItem* parent = nullptr);
    TrainingController* controller() const { return ctl_; }
    void setController(TrainingController* c);
    void paint(QPainter* p) override;

signals:
    void controllerChanged();
    void viewChanged();

private:
    QPointer<TrainingController> ctl_;
    bool live_ = false;
    double viewStart_ = 0, viewEnd_ = 0;
    QColor vibColor_{"#36c8ff"}, sucColor_{"#ff8a23"};
};

}  // namespace samn::ui
