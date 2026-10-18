#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/edit_session.h"

namespace samn::ui {

class CurveItem;

// The Editor inside the Generator mode (D-137, D-144): the curves of one
// script, edited with the mouse and the range tools, with undo (D-082).
// Lanes: "stroke" (the .funscript actions), "vibration" and "suction" (the
// companion .samn, when there is one).
//
// Like 0.7, every finished change is saved at once (atomic write; metadata
// and every other field stay as they are). A failed save keeps the edit on
// screen and says so; `dirty` stays true until a save works.
class EditorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path NOTIFY scriptChanged)
    Q_PROPERTY(QString name READ name NOTIFY scriptChanged)
    // What the Player should open: the .samn when there is one (it carries
    // the recipe), else the .funscript.
    Q_PROPERTY(QString playPath READ playPath NOTIFY scriptChanged)
    Q_PROPERTY(bool hasScript READ hasScript NOTIFY scriptChanged)
    Q_PROPERTY(QStringList lanes READ lanes NOTIFY scriptChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY curvesChanged)
    Q_PROPERTY(double durationMs READ durationMs WRITE setDurationMs NOTIFY durationChanged)
    // The lane the range tools work on: the one last touched.
    Q_PROPERTY(QString activeLane READ activeLane WRITE setActiveLane NOTIFY activeLaneChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    // Shared time window of all lanes.
    Q_PROPERTY(double viewStartMs READ viewStartMs NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs READ viewEndMs NOTIFY viewChanged)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY viewChanged)
    // The marked range (the selection of the range tools); start == end = none.
    Q_PROPERTY(double markerStartMs READ markerStartMs NOTIFY markerChanged)
    Q_PROPERTY(double markerEndMs READ markerEndMs NOTIFY markerChanged)
    Q_PROPERTY(bool hasMarker READ hasMarker NOTIFY markerChanged)
    Q_PROPERTY(QString markerText READ markerText NOTIFY markerChanged)
    // New and dragged points snap to this frame rate; 0 = off (0.7 FPS-Snap).
    Q_PROPERTY(double fpsSnap READ fpsSnap WRITE setFpsSnap NOTIFY fpsSnapChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit EditorController(QObject* parent = nullptr);

    QString path() const { return path_; }
    QString name() const;
    QString playPath() const { return samn_path_.isEmpty() ? path_ : samn_path_; }
    bool hasScript() const { return !path_.isEmpty(); }
    QStringList lanes() const;
    int pointCount() const;
    double durationMs() const { return duration_ms_; }
    void setDurationMs(double ms);
    QString activeLane() const { return active_lane_; }
    void setActiveLane(const QString& lane);
    bool canUndo() const { return session_.CanUndo(); }
    bool canRedo() const { return session_.CanRedo(); }
    QString undoText() const;
    QString redoText() const;
    bool dirty() const { return session_.dirty(); }
    bool dragging() const { return drag_index_ >= 0; }
    double viewStartMs() const { return double(view_.start_ms); }
    double viewEndMs() const { return double(view_.end_ms); }
    bool zoomed() const;
    double markerStartMs() const { return double(marker_start_); }
    double markerEndMs() const { return double(marker_end_); }
    bool hasMarker() const { return marker_end_ > marker_start_; }
    QString markerText() const;
    double fpsSnap() const { return fps_snap_; }
    void setFpsSnap(double fps);
    QString status() const { return status_; }

    // A .funscript (its companion .samn is read too) or a .samn path.
    Q_INVOKABLE bool open(const QString& path);
    Q_INVOKABLE void close();
    Q_INVOKABLE void attachCurve(samn::ui::CurveItem* curve);

    // Mouse on a lane (item pixels). press grabs the point under the mouse
    // or sets a new one there (0.7: set and fine-tune in one gesture);
    // dragTo moves it; release records one undo step and saves. Each
    // returns the time to show in the video, or -1.
    Q_INVOKABLE double press(samn::ui::CurveItem* curve, double x, double y);
    Q_INVOKABLE double dragTo(double x, double y);
    Q_INVOKABLE void release();
    // Double-click: removes the point under the mouse (at least 2 stay).
    Q_INVOKABLE bool deleteAt(samn::ui::CurveItem* curve, double x, double y);

    Q_INVOKABLE void setMarker(double startMs, double endMs);
    Q_INVOKABLE void clearMarker();

    // Range tools on the active lane, inside the marker. Invert works on the
    // whole lane without a marker (0.7).
    Q_INVOKABLE bool deleteRange();
    Q_INVOKABLE bool capSpeed(double maxIntensity = 0);
    Q_INVOKABLE bool scaleRange(double factor, bool softEdges);
    Q_INVOKABLE bool simplifyRange(double maxError = 2);
    Q_INVOKABLE bool invert();

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    Q_INVOKABLE void zoomAt(double factor, double fraction);
    Q_INVOKABLE void zoomToMarker();
    Q_INVOKABLE void showAll();

    const samn::EditSession& session() const { return session_; }

signals:
    void scriptChanged();
    void curvesChanged();
    void durationChanged();
    void activeLaneChanged();
    void historyChanged();
    void draggingChanged();
    void viewChanged();
    void markerChanged();
    void fpsSnapChanged();
    void statusChanged();

private:
    std::string laneKey(const CurveItem* curve) const;
    int64_t snap(double ms) const;
    int64_t timelineMs() const;
    bool applyRange(const QString& label, const std::function<std::vector<samn::Action>(const std::vector<samn::Action>&)>& op,
                    bool needsMarker);
    bool stepDone();  // saves; false when the save failed
    bool save();
    void pushCurves();
    void setView(samn::ViewWindow v);
    void setStatus(const QString& s);

    samn::EditSession session_;
    QString path_;          // the .funscript
    QString samn_path_;     // its companion .samn, empty when there is none
    double duration_ms_ = 0;
    QString active_lane_ = QStringLiteral("stroke");
    samn::ViewWindow view_{};
    int64_t marker_start_ = 0, marker_end_ = 0;
    double fps_snap_ = 0;
    QString status_;
    std::vector<QPointer<CurveItem>> curves_;
    QPointer<CurveItem> drag_curve_;
    std::string drag_lane_;
    int drag_index_ = -1;
};

}  // namespace samn::ui
