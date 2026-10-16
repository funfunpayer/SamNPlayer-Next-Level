#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/playback.h"
#include "samn/settings.h"

namespace samn::ui {

class CurveItem;
class HeatmapItem;

// Play mode logic for QML. It opens videos and scripts (video first, with
// script folders, tolerant names and variants), keeps the offset per script,
// maps video time to script time and feeds the curve lanes, heatmap,
// chapters and bookmarks. The MediaPlayer lives in QML and reports its
// position through setVideoPositionMs().
class PlayerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString scriptPath READ scriptPath NOTIFY scriptChanged)
    Q_PROPERTY(QString scriptName READ scriptName NOTIFY scriptChanged)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(bool hasScript READ hasScript NOTIFY scriptChanged)
    Q_PROPERTY(bool noScriptForVideo READ noScriptForVideo NOTIFY scriptChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY scriptChanged)
    Q_PROPERTY(qint64 scriptDurationMs READ scriptDurationMs NOTIFY scriptChanged)
    Q_PROPERTY(int offsetMs READ offsetMs WRITE setOffsetMs NOTIFY offsetMsChanged)
    Q_PROPERTY(qint64 playheadMs READ playheadMs NOTIFY playheadMsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantList variants READ variants NOTIFY variantsChanged)
    Q_PROPERTY(int variantIndex READ variantIndex NOTIFY variantsChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY scriptChanged)
    Q_PROPERTY(QStringList scriptFolders READ scriptFolders NOTIFY scriptFoldersChanged)
    // The marked range (0.7: drag on the heatmap), in script time; saved
    // beside the script. Loop, zoom selection and the range tools use it.
    Q_PROPERTY(bool hasMarker READ hasMarker NOTIFY markerChanged)
    Q_PROPERTY(double markerStartMs READ markerStartMs NOTIFY markerChanged)
    Q_PROPERTY(double markerEndMs READ markerEndMs NOTIFY markerChanged)
    Q_PROPERTY(QString markerLabel READ markerLabel NOTIFY markerChanged)
    // 0.7 "Loop selection": repeats the marked range while on.
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled NOTIFY loopChanged)
    Q_PROPERTY(bool loopActive READ loopActive NOTIFY loopChanged)
    // Live channel values at the playhead, 0-100 (0.7 "Vibration n / Suction n").
    Q_PROPERTY(int vibrationNow READ vibrationNow NOTIFY playheadMsChanged)
    Q_PROPERTY(int suctionNow READ suctionNow NOTIFY playheadMsChanged)
    // Sleep timer: stops playing after this many minutes of play (0 = off).
    Q_PROPERTY(int sleepMinutes READ sleepMinutes WRITE setSleepMinutes NOTIFY sleepChanged)
    // False: the lane is the device preview from the stroke (mapper), not
    // a channel the script carries.
    Q_PROPERTY(bool vibrationFromScript READ vibrationFromScript NOTIFY scriptChanged)
    Q_PROPERTY(bool suctionFromScript READ suctionFromScript NOTIFY scriptChanged)
    // The time window of the curve and its lanes (script time, 0.7 zoom).
    Q_PROPERTY(double viewStartMs READ viewStartMs NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs READ viewEndMs NOTIFY viewChanged)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY viewChanged)
    Q_PROPERTY(QString zoomLabel READ zoomLabel NOTIFY viewChanged)
public:
    static constexpr int kOffsetLimitMs = 10000;
    static constexpr int kHeatmapBuckets = 300;  // 0.7 HEATMAP_BUCKETS
    static constexpr const char* kScriptFoldersKey = "library.script_folders";

    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // A video or a script (local path or file URL). Returns false and sets
    // lastError when it cannot be opened.
    Q_INVOKABLE bool openPath(const QString& pathOrUrl);
    Q_INVOKABLE bool openScript(const QString& path, const QString& video = {});
    Q_INVOKABLE bool chooseVariant(int index);
    Q_INVOKABLE void addScriptFolder(const QString& pathOrUrl);
    Q_INVOKABLE void setVideoPositionMs(qint64 videoMs);
    // Where the player should seek to (video time): the marked range's start
    // once its end is reached while the loop is on, else -1.
    Q_INVOKABLE qint64 loopJumpFor(qint64 videoMs) const;
    // A / B at the playhead (video time) set the marked range's ends.
    Q_INVOKABLE void setLoopStart(qint64 videoMs);
    Q_INVOKABLE void setLoopEnd(qint64 videoMs);
    // Marked range in script time; an empty range clears it.
    Q_INVOKABLE void setMarker(double startMs, double endMs);
    Q_INVOKABLE void clearMarker();
    Q_INVOKABLE void zoomToSelection();
    // Script time at a fraction (0-1) of the timeline's width.
    Q_INVOKABLE double msAtFraction(double fraction) const;
    // A heatmap drag ended: a range longer than 150 ms is marked (returns
    // -1); a shorter one is a click and returns the video time to seek to.
    Q_INVOKABLE qint64 finishHeatmapDrag(double startMs, double endMs);
    // Video time of n x 10 % of the timeline (keys 1-9).
    Q_INVOKABLE qint64 jumpTenthVideoMs(int n) const;
    // The player reports play / pause for the sleep timer.
    Q_INVOKABLE void setPlaying(bool playing);
    // Tooltip text for the heatmap at a script time: "12.3s · 45%".
    Q_INVOKABLE QString heatTooltip(double ms) const;
    // Video time of a chapter's start (script time + offset).
    Q_INVOKABLE qint64 chapterVideoMs(int index) const;
    Q_INVOKABLE void attachCurve(samn::ui::CurveItem* curve);
    Q_INVOKABLE void attachHeatmap(samn::ui::HeatmapItem* heatmap);
    Q_INVOKABLE QString formatTime(qint64 ms) const;
    // The video's length, so the timeline covers the video too.
    Q_INVOKABLE void setVideoDurationMs(qint64 ms);
    // Zoom: factor > 1 = in. zoomAt keeps the point under the mouse (a
    // fraction 0-1 of the curve's width) in place; zoomBy zooms around the
    // playhead (0.7 buttons).
    Q_INVOKABLE void zoomAt(double factor, double fraction);
    Q_INVOKABLE void zoomBy(double factor);
    Q_INVOKABLE void resetZoom();

    QString scriptPath() const { return scriptPath_; }
    QString scriptName() const;
    QUrl videoUrl() const;
    QString videoName() const;
    bool hasScript() const { return !stroke_.empty(); }
    bool noScriptForVideo() const { return stroke_.empty() && !videoPath_.isEmpty(); }
    int pointCount() const { return static_cast<int>(stroke_.size()); }
    qint64 scriptDurationMs() const { return stroke_.empty() ? 0 : stroke_.back().at; }
    int offsetMs() const { return offsetMs_; }
    void setOffsetMs(int ms);
    qint64 playheadMs() const { return playheadMs_; }
    QString lastError() const { return lastError_; }
    QVariantList variants() const { return variants_; }
    int variantIndex() const { return variantIndex_; }
    QVariantList chapters() const { return chapters_; }
    QStringList scriptFolders() const;
    bool hasMarker() const { return marker_.Valid(); }
    double markerStartMs() const { return double(marker_.start_ms); }
    double markerEndMs() const { return double(marker_.end_ms); }
    QString markerLabel() const;
    bool loopEnabled() const { return loopEnabled_; }
    void setLoopEnabled(bool on);
    bool loopActive() const { return loopEnabled_ && marker_.Valid(); }
    int vibrationNow() const;
    int suctionNow() const;
    int sleepMinutes() const { return sleepMinutes_; }
    void setSleepMinutes(int minutes);
    bool vibrationFromScript() const { return vibrationFromScript_; }
    bool suctionFromScript() const { return suctionFromScript_; }
    double viewStartMs() const { return double(view_.start_ms); }
    double viewEndMs() const { return double(view_.end_ms); }
    bool zoomed() const { return samn::IsZoomed(view_, timelineMs()); }
    QString zoomLabel() const;
    int64_t timelineMs() const;
    QString videoPath() const { return videoPath_; }
    const std::vector<samn::Action>& actions() const { return stroke_; }

    static QString toLocalPath(const QString& pathOrUrl);

signals:
    void scriptChanged();
    void videoChanged();
    void offsetMsChanged();
    void playheadMsChanged();
    void lastErrorChanged();
    void variantsChanged();
    void scriptFoldersChanged();
    void loopChanged();
    void viewChanged();
    void markerChanged();
    void sleepChanged();
    // The sleep timer ran out: the player stops.
    void sleepReached();

private:
    void setVideo(const QString& path);
    void setError(const QString& e);
    void setView(samn::ViewWindow v);
    void clearScript();
    void pushToViews();
    void loadMarks(const QString& path);
    samn::Settings& settings();

    std::unique_ptr<samn::Settings> settings_;
    std::vector<samn::Action> stroke_, vibration_, suction_;
    std::vector<int64_t> bookmarks_;
    std::vector<std::pair<int64_t, int64_t>> chapterRanges_;
    std::vector<float> heat_;
    QVariantList variants_, chapters_;
    int variantIndex_ = -1;
    QString scriptPath_, videoPath_, lastError_;
    int offsetMs_ = 0;
    bool vibrationFromScript_ = false, suctionFromScript_ = false;
    qint64 videoDurationMs_ = 0;
    samn::ViewWindow view_;
    qint64 videoMs_ = 0, playheadMs_ = -1;
    samn::RangeMarker marker_;
    std::optional<int64_t> pendingStart_;  // A pressed, B not yet
    bool loopEnabled_ = false;
    int sleepMinutes_ = 0;
    qint64 playedMs_ = 0;
    QElapsedTimer playClock_;
    QTimer sleepTimer_;
    std::vector<QPointer<CurveItem>> curves_;
    QPointer<HeatmapItem> heatmap_;
};

}  // namespace samn::ui
