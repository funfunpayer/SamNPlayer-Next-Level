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
#include <random>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/samn_file.h"
#include "samn/playback.h"
#include "samn/playlist.h"
#include "samn/settings.h"
#include "device_controller.h"
#include "playback_output.h"

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
    // Marks of the script ({name, startMs, endMs, label}, {name, timeMs,
    // label}, {kind, startMs, endMs, intensity, label}); edits save at once.
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY marksChanged)
    // Play list (0.7 playlist): {name, path, active}; shown with 2+ items.
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(int playlistIndex READ playlistIndex NOTIFY playlistChanged)
    Q_PROPERTY(bool playlistVisible READ playlistVisible NOTIFY playlistChanged)
    Q_PROPERTY(bool playlistHasNext READ playlistHasNext NOTIFY playlistChanged)
    Q_PROPERTY(bool playlistAuto READ playlistAuto WRITE setPlaylistAuto NOTIFY playlistChanged)
    Q_PROPERTY(bool playlistShuffle READ playlistShuffle WRITE setPlaylistShuffle NOTIFY playlistChanged)
    Q_PROPERTY(bool playlistRepeat READ playlistRepeat WRITE setPlaylistRepeat NOTIFY playlistChanged)
    // Chapter bands on the timeline (0.7 renderChapterBands): sorted, open
    // ends run to the next chapter or the script end; name, startMs, endMs,
    // index (colour), tip.
    Q_PROPERTY(QVariantList chapterBands READ chapterBands NOTIFY marksChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY marksChanged)
    Q_PROPERTY(QVariantList oMarkers READ oMarkers NOTIFY marksChanged)
    // Intensity for a new secondary O-marker (0.7 playback.omarker_intensity).
    Q_PROPERTY(double oMarkerIntensity READ oMarkerIntensity WRITE setOMarkerIntensity NOTIFY marksChanged)
    // Speed highlights on the curve (0.7 playback.speed_highlights, default on;
    // playback.speed_highlight_thresh 150-800, default 400). Display only.
    Q_PROPERTY(bool speedHighlights READ speedHighlights WRITE setSpeedHighlights NOTIFY speedHighlightChanged)
    Q_PROPERTY(double speedHighlightThresh READ speedHighlightThresh WRITE setSpeedHighlightThresh NOTIFY speedHighlightChanged)
    // Beat grid on the curve: BPM 20-240 (0 = none yet) and on/off.
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
    Q_PROPERTY(bool bpmGrid READ bpmGrid WRITE setBpmGrid NOTIFY bpmChanged)
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
    Q_PROPERTY(int strokeNow READ strokeNow NOTIFY playheadMsChanged)
    Q_PROPERTY(int vibrationNow READ vibrationNow NOTIFY playheadMsChanged)
    // Audio Level lane under the curve (0.7: on by default, remembered).
    Q_PROPERTY(bool audioLane READ audioLane WRITE setAudioLane NOTIFY viewOptionsChanged)
    // 0-100 position gauge on the video (0.7: on by default, remembered).
    Q_PROPERTY(bool posOverlay READ posOverlay WRITE setPosOverlay NOTIFY viewOptionsChanged)
    Q_PROPERTY(int suctionNow READ suctionNow NOTIFY playheadMsChanged)
    // The device the script plays on (one DeviceController for the app) and
    // what goes to it while playing (the stroke filter, safety range ...).
    Q_PROPERTY(samn::ui::DeviceController* device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(samn::ui::PlaybackOutput* output READ output CONSTANT)
    // D-132: which lanes show. Without a connected device: what the script
    // carries (the stroke; vibration / suction only when the file has them).
    // With one: the channels that device can use (a stroker: the stroke; a
    // Sam Neo 2: vibration and suction).
    Q_PROPERTY(bool showStrokeLane READ showStrokeLane NOTIFY lanesChanged)
    Q_PROPERTY(bool showVibrationLane READ showVibrationLane NOTIFY lanesChanged)
    Q_PROPERTY(bool showSuctionLane READ showSuctionLane NOTIFY lanesChanged)
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
    // Several dropped files: the scripts become the play list (0.7
    // drop:script) and the first one opens; else the first video opens.
    // Further videos and unsupported files are named in lastError, and a
    // drop with nothing usable says so (never silent).
    Q_INVOKABLE bool openPaths(const QStringList& pathsOrUrls);
    // Play list (0.7): add (the loaded script comes first in a new list),
    // remove (loads the next one when the active one goes), pick, next.
    Q_INVOKABLE bool addToPlaylist(const QString& pathOrUrl);
    Q_INVOKABLE bool removeFromPlaylist(int index);
    Q_INVOKABLE bool pickPlaylist(int index);
    Q_INVOKABLE bool playlistNext();
    // The video ended: true when Auto-advance opened the next script (the
    // page then starts playing it).
    Q_INVOKABLE bool playlistFinished();
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
    // The video player reports a failure (QMediaPlayer::Error and its
    // text): the error bar names the class and the file (0.7 0.5.41).
    // The message goes away once the video plays.
    Q_INVOKABLE void reportMediaError(int error, const QString& detail);
    // Tooltip text for the heatmap at a script time: "12.3s · 45%".
    Q_INVOKABLE QString heatTooltip(double ms) const;
    // Bookmarks: at the playhead (video time); name default "Bookmark", 80 chars.
    Q_INVOKABLE bool addBookmark(const QString& name, qint64 videoMs);
    Q_INVOKABLE bool renameBookmark(int index, const QString& name);
    Q_INVOKABLE bool removeBookmark(int index);
    Q_INVOKABLE qint64 bookmarkVideoMs(int index) const;
    // Chapters: from the marked range; name default "Chapter".
    Q_INVOKABLE bool addChapterFromSelection(const QString& name);
    // Scene marker at a video time (0.7 addSceneMarkerAt): a chapter start
    // "Scene <n>"; the chapter around it is cut there.
    Q_INVOKABLE bool addSceneMarker(qint64 videoMs);
    Q_INVOKABLE bool renameChapter(int index, const QString& name);
    Q_INVOKABLE bool removeChapter(int index);
    // O-markers: from the marked range; "primary" (intensity 1) or
    // "secondary" (oMarkerIntensity).
    Q_INVOKABLE bool addOMarkerFromSelection(const QString& kind);
    Q_INVOKABLE bool removeOMarker(int index);
    // Link a video to the open script (0.7 "Select a video file…").
    Q_INVOKABLE bool attachVideo(const QString& pathOrUrl);
    // 0.7 ExportScriptHeatmapPNG: "<script>.heatmap.png" beside the script,
    // 960 x 56 with chapter ticks. The path, or "" with lastError.
    Q_INVOKABLE QString exportHeatmapPng();
    // 0.7 Script Doctor (ScriptQuality): signal quality of the loaded script
    // from its file. {score 0-1, passed, warnings[], error}.
    Q_INVOKABLE QVariantMap scriptQuality();
    // 0.7 SavePlaybackProject: "<video or script>.snp.json" with video,
    // script, offset, the video position and the marked range. The path, or
    // "" with lastError.
    Q_INVOKABLE QString saveProject(qint64 videoMs);
    // 0.7 LoadPlaybackProject: opens the project's script and video and
    // restores offset and marked range. The video position to seek to
    // (0 = none), or -1 with lastError.
    Q_INVOKABLE qint64 loadProject(const QString& pathOrUrl);
    Q_INVOKABLE QString localPath(const QString& pathOrUrl) const { return toLocalPath(pathOrUrl); }
    // Snapshot of the current frame at full video resolution (0.7 saves a
    // PNG named "<video>_MM-SS.png"). videoSink is the VideoOutput's sink.
    Q_INVOKABLE QString snapshotName(qint64 videoMs) const;
    Q_INVOKABLE QString snapshotFolder() const;
    Q_INVOKABLE bool saveSnapshot(QObject* videoSink, const QString& pathOrUrl);
    // Small remembered UI choices (selected tab …), kept in the settings
    // under "ui.<key>".
    Q_INVOKABLE QVariant uiValue(const QString& key, const QVariant& fallback);
    Q_INVOKABLE void setUiValue(const QString& key, const QVariant& value);
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
    QVariantList chapters() const;
    QVariantList chapterBands() const;
    QVariantList bookmarks() const;
    QVariantList oMarkers() const;
    double oMarkerIntensity() const { return oMarkerIntensity_; }
    bool speedHighlights() const { return speedHL_; }
    void setSpeedHighlights(bool on);
    double speedHighlightThresh() const { return speedHLThresh_; }
    void setSpeedHighlightThresh(double v);
    void setOMarkerIntensity(double v);
    double bpm() const { return bpm_; }
    void setBpm(double v);
    bool bpmGrid() const { return bpmGrid_; }
    void setBpmGrid(bool on);
    // Beat period for the grid in ms, 0 when off.
    double beatPeriodMs() const;
    QStringList scriptFolders() const;
    bool hasMarker() const { return marker_.Valid(); }
    double markerStartMs() const { return double(marker_.start_ms); }
    double markerEndMs() const { return double(marker_.end_ms); }
    QString markerLabel() const;
    bool loopEnabled() const { return loopEnabled_; }
    void setLoopEnabled(bool on);
    bool loopActive() const { return loopEnabled_ && marker_.Valid(); }
    int strokeNow() const;
    int vibrationNow() const;
    bool audioLane() const;
    void setAudioLane(bool on);
    bool posOverlay() const;
    void setPosOverlay(bool on);
    int suctionNow() const;
    int sleepMinutes() const { return sleepMinutes_; }
    void setSleepMinutes(int minutes);
    bool vibrationFromScript() const { return vibrationFromScript_; }
    bool suctionFromScript() const { return suctionFromScript_; }
    DeviceController* device() const { return device_; }
    void setDevice(DeviceController* device);
    PlaybackOutput* output() { return &output_; }
    bool showStrokeLane() const;
    bool showVibrationLane() const;
    bool showSuctionLane() const;
    double viewStartMs() const { return double(view_.start_ms); }
    double viewEndMs() const { return double(view_.end_ms); }
    bool zoomed() const { return samn::IsZoomed(view_, timelineMs()); }
    QString zoomLabel() const;
    int64_t timelineMs() const;
    QString videoPath() const { return videoPath_; }
    const std::vector<samn::Action>& actions() const { return stroke_; }

    static QString toLocalPath(const QString& pathOrUrl);

    QVariantList playlist() const;
    int playlistIndex() const { return int(playlist_.Index()); }
    bool playlistVisible() const { return playlist_.Visible(); }
    bool playlistHasNext() const { return playlist_.HasNext(); }
    bool playlistAuto() const { return playlist_.Auto(); }
    bool playlistShuffle() const { return playlist_.Shuffle(); }
    bool playlistRepeat() const { return playlist_.Repeat(); }
    void setPlaylistAuto(bool on);
    void setPlaylistShuffle(bool on);
    void setPlaylistRepeat(bool on);

signals:
    void deviceChanged();
    // The Emergency stop was pressed: the page pauses the video.
    void emergencyStopped();
    void lanesChanged();
    void playlistChanged();
    void speedHighlightChanged();
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
    void marksChanged();
    void bpmChanged();
    void viewOptionsChanged();
    // For the Library: a script was opened, a video linked, marks edited.
    void scriptOpened(const QString& path);
    void videoLinked(const QString& path);
    void scriptEdited(const QString& path);
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
    // Saves one list; on failure the old list comes back and lastError says why.
    template <typename T, typename Save>
    bool commitMarks(std::vector<T>& list, std::vector<T> next, Save save);
    void pushMarks();
    samn::Settings& settings();

    std::unique_ptr<samn::Settings> settings_;
    PlaybackOutput output_;
    QPointer<DeviceController> device_;
    std::vector<samn::Action> stroke_, vibration_, suction_;
    samn::ScriptMarks marks_;
    std::vector<float> heat_;
    QVariantList variants_;
    int variantIndex_ = -1;
    QString scriptPath_, videoPath_, lastError_;
    bool mediaError_ = false;  // lastError_ came from reportMediaError
    bool loadPlaylistCurrent();
    samn::Playlist playlist_;
    std::mt19937 rng_{std::random_device{}()};
    int offsetMs_ = 0;
    bool vibrationFromScript_ = false, suctionFromScript_ = false;
    qint64 videoDurationMs_ = 0;
    samn::ViewWindow view_;
    qint64 videoMs_ = 0, playheadMs_ = -1;
    samn::RangeMarker marker_;
    double oMarkerIntensity_ = 0.5, bpm_ = 0;
    bool speedHL_ = true;
    double speedHLThresh_ = 400;
    bool bpmGrid_ = false;
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
