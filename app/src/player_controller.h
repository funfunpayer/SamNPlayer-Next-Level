#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/funscript.h"
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
    Q_PROPERTY(bool loopActive READ loopActive NOTIFY loopChanged)
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
    static constexpr int kHeatmapBuckets = 240;
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
    // Where the player should seek to: the loop start once the loop end is
    // reached, else -1.
    Q_INVOKABLE qint64 loopJumpFor(qint64 videoMs) const;
    Q_INVOKABLE void setLoopStart(qint64 videoMs);
    Q_INVOKABLE void setLoopEnd(qint64 videoMs);
    Q_INVOKABLE void clearLoop();
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
    bool loopActive() const { return loop_.has_value(); }
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
    std::optional<samn::LoopRange> loop_;
    std::vector<QPointer<CurveItem>> curves_;
    QPointer<HeatmapItem> heatmap_;
};

}  // namespace samn::ui
