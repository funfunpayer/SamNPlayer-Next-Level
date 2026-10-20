#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "samn/curve_geometry.h"
#include "samn/edit_session.h"
#include "samn/samn_file.h"
#include "samn/scenes.h"

namespace samn::ui {

class AudioLevelDecoder;
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
    Q_PROPERTY(QStringList lanes READ lanes NOTIFY lanesChanged)
    // The video the script belongs to: the sound for "Vibration from the sound".
    Q_PROPERTY(QString videoPath READ videoPath WRITE setVideoPath NOTIFY videoPathChanged)
    // A create tool is working (decoding the sound, reading the beat bar).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)  // 0..100 while busy
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
    // The Scene Map in the Editor (D-145, 0.7 scene_panel.js): the script's
    // chapters as scenes, each {index, name, startMs, endMs, feel, hasFeel,
    // suggested, source, confidence}; a feel per scene is a Sam Neo 2 feel
    // ("" = Classic). Suggestions come from <clip>.scene.json beside the
    // video or the contact mark's class; nothing is applied on its own.
    Q_PROPERTY(QVariantList scenes READ scenes NOTIFY scenesChanged)
    Q_PROPERTY(QStringList sceneFeels READ sceneFeels CONSTANT)
    Q_PROPERTY(bool hasSceneSuggestions READ hasSceneSuggestions NOTIFY scenesChanged)
    // Confirmed phases from the AI Train label editor (<clip>.labels.json, D-157).
    Q_PROPERTY(bool hasPhases READ hasPhases NOTIFY scenesChanged)
    // How many phases shape the playback (the .samn recipe's phase_ranges).
    Q_PROPERTY(int playbackPhases READ playbackPhases NOTIFY scenesChanged)

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    QString path() const { return path_; }
    QString name() const;
    QString playPath() const { return samn_path_.isEmpty() ? path_ : samn_path_; }
    bool hasScript() const { return !path_.isEmpty(); }
    QStringList lanes() const;
    QString videoPath() const { return video_path_; }
    void setVideoPath(const QString& path);
    bool busy() const { return busy_; }
    int progress() const { return progress_; }
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
    QVariantList scenes() const;
    QStringList sceneFeels() const;
    bool hasSceneSuggestions() const;
    bool hasPhases() const;
    int playbackPhases() const { return int(playback_phases_.size()); }

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

    // Create tools (one undo step each; they write the .samn, which is made
    // when the script has none).
    // Vibration and suction from the stroke with the script's feel recipe
    // (0.7 BakeNeoAxes; a plain funscript uses the standard recipe).
    Q_INVOKABLE bool axesFromStroke();
    // "Optimize for" (0.8 OptimizeLoadedFor, D-156): "general" heals the
    // tracker-loss windows and fills long gaps in the stroke so the script works
    // on any device; "neo2" also turns contact vibration on (soft, 0.75) and
    // writes vibration and suction from the stroke. One undo step; returns the
    // message (empty when nothing could be done).
    Q_INVOKABLE QString optimizeFor(const QString& target);
    // Vibration from the video's sound (0.7 "Write as vibration curve");
    // withContact keeps the script's own vibration where it is stronger.
    // Asynchronous: busy until the sound is decoded. A script without a
    // suction lane gets its mapped suction first, so it plays the same.
    Q_INVOKABLE bool vibrationFromSound(bool withContact);
    // The stroke from the video's beat bar (Cock Hero / Fap Hero; 0.7
    // CreateBeatBarScript): finds the bar, times every beat, one stroke per
    // beat. Runs on its own thread. Without an open script it opens or
    // creates <video>.funscript beside the video; the stroke then replaces
    // the lane as one undo step, so nothing is lost.
    Q_INVOKABLE bool strokeFromBeatBar();
    // Stops a running create tool (the beat bar).
    Q_INVOKABLE void cancelTool();

    // Scenes: a new one starts at ms (0.7 addSceneMarkerAt; refused within
    // 250 ms of another start), rename, delete. Saved at once into the
    // .funscript and the .samn.
    Q_INVOKABLE bool addScene(double ms);
    Q_INVOKABLE bool renameScene(int index, const QString& name);
    Q_INVOKABLE bool deleteScene(int index);
    // The scene's feel ("" = Classic); clearSceneFeel leaves it to the script.
    Q_INVOKABLE bool setSceneFeel(int index, const QString& feel);
    Q_INVOKABLE bool clearSceneFeel(int index);
    // Gives every scene with a suggestion that feel (0.7 "Apply suggestions").
    Q_INVOKABLE int applySceneSuggestions();
    // A scene where each confirmed phase begins, named after it (D-157).
    Q_INVOKABLE int scenesFromPhases();
    // The confirmed intro, empty, climax and cool-down phases shape what the
    // device gets when the .samn plays (D-157); the script stays as it is.
    Q_INVOKABLE int phasesToPlayback();
    Q_INVOKABLE bool clearPlaybackPhases();

    // The stroke's 0-100 position at ms (the "0–100 on video" gauge); -1
    // without a script.
    Q_INVOKABLE int strokeAt(double ms) const;
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    Q_INVOKABLE void zoomAt(double factor, double fraction);
    Q_INVOKABLE void zoomToMarker();
    Q_INVOKABLE void showAll();

    const samn::EditSession& session() const { return session_; }

signals:
    void scriptChanged();
    void lanesChanged();
    void videoPathChanged();
    void busyChanged();
    void progressChanged();
    void curvesChanged();
    void durationChanged();
    void activeLaneChanged();
    void historyChanged();
    void draggingChanged();
    void viewChanged();
    void markerChanged();
    void fpsSnapChanged();
    void statusChanged();
    void scenesChanged();

private:
    std::string laneKey(const CurveItem* curve) const;
    int64_t snap(double ms) const;
    int64_t timelineMs() const;
    bool applyRange(const QString& label, const std::function<std::vector<samn::Action>(const std::vector<samn::Action>&)>& op,
                    bool needsMarker);
    bool stepDone();
    samn::Document companion() const;  // the .samn as saved, or a new one
    void onSoundEnvelope(const std::vector<double>& envelope);
    void setBusy(bool busy);
    void setProgress(int percent);
    void onBeats(bool found, const std::vector<double>& beats, double bpm, const QString& error);  // saves; false when the save failed
    bool save();
    void pushCurves();
    void setView(samn::ViewWindow v);
    void setStatus(const QString& s);
    void loadSceneProposals();
    std::vector<samn::ChapterScene> chapterScenes() const;
    bool saveScenes(std::vector<samn::Chapter> chapters, std::vector<samn::SceneFeelRange> ranges);

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
    QStringList lanes_;
    QString video_path_;
    std::vector<samn::Chapter> chapters_;          // sorted by start
    std::vector<samn::SceneFeelRange> scene_ranges_;
    std::vector<samn::SceneProposal> proposals_;
    std::vector<samn::LabelPhase> phases_;
    std::vector<samn::PhaseRange> playback_phases_;
    bool savePlaybackPhases(std::vector<samn::PhaseRange> ranges);
    std::optional<samn::ContactMarks> contact_marks_;
    bool busy_ = false;
    bool sound_with_contact_ = false;
    AudioLevelDecoder* sound_ = nullptr;
    int progress_ = 0;
    std::shared_ptr<std::atomic<bool>> tool_cancel_;
    std::thread tool_;
    QPointer<CurveItem> drag_curve_;
    std::string drag_lane_;
    int drag_index_ = -1;
};

}  // namespace samn::ui
