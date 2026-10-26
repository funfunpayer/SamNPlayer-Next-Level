#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QUrl>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "samn/engine/generate.h"
#include "samn/settings.h"

namespace samn::ui {

// The Generator mode (P3.3, 0.7 generator.js): 1 video, 2 tip box on a
// frame, 3 feel, 4 create with progress and cancel, 5 review in the Player.
// The run itself is samn::engine::GenerateRunner on its own thread (D-142);
// its events come back to this object on the UI thread.
class GeneratorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString videoPath READ videoPath NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    // For the MediaPlayer (D-154).
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY videoChanged)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY videoChanged)
    Q_PROPERTY(QString videoInfo READ videoInfo NOTIFY videoChanged)  // "0:08 · 320×240 · 25 fps"
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoChanged)
    // The marking frame and its time (0.7 poster: 10 % in, at most 30 s).
    Q_PROPERTY(double previewMs READ previewMs NOTIFY previewChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY previewChanged)
    // The tip box in video pixels; the CSRT tracker follows only this box.
    Q_PROPERTY(QRect box READ box WRITE setBox NOTIFY boxChanged)
    Q_PROPERTY(bool hasBox READ hasBox NOTIFY boxChanged)
    Q_PROPERTY(QString boxLabel READ boxLabel NOTIFY boxChanged)
    // Feel: "standard" (Normal) | "weich" (Soft) | "autotune".
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY optionsChanged)
    Q_PROPERTY(bool invert READ invert WRITE setInvert NOTIFY optionsChanged)
    Q_PROPERTY(bool cameraCompensation READ cameraCompensation WRITE setCameraCompensation NOTIFY optionsChanged)
    Q_PROPERTY(bool sceneCuts READ sceneCuts WRITE setSceneCuts NOTIFY optionsChanged)
    Q_PROPERTY(bool autoOZone READ autoOZone WRITE setAutoOZone NOTIFY optionsChanged)
    // Opt-in, off by default (0.7 behaviour when off): bridge tracking gaps up
    // to kGapBridgeMs and leave longer ones without actions; treat single-
    // frame outliers as lost; keep raw tracks in config_dir()/cache/track so
    // a new feel does not track again (generator.gap_handling,
    // generator.outlier_rejection, generator.track_cache).
    Q_PROPERTY(bool gapHandling READ gapHandling WRITE setGapHandling NOTIFY optionsChanged)
    Q_PROPERTY(bool outlierRejection READ outlierRejection WRITE setOutlierRejection NOTIFY optionsChanged)
    Q_PROPERTY(bool trackCache READ trackCache WRITE setTrackCache NOTIFY optionsChanged)
    // Contact vibration (0.7 Step 2 "Tip & contact", on by default): the
    // tip's class, and contact areas the tip's path can reach; the Player
    // vibrates when it does. contactAreas: {x, y, w, h, cls} in video pixels,
    // the first is the primary. markContact: the next box drawn is a contact
    // area instead of the tip box.
    Q_PROPERTY(bool contactVibration READ contactVibration WRITE setContactVibration NOTIFY optionsChanged)
    // 0.7 Step 3 "Sensitivity" (0.40 deep only … 0.95) and "Contact curve"
    // (linear | soft | peak | impulse; soft by default).
    Q_PROPERTY(double contactSpan READ contactSpan WRITE setContactSpan NOTIFY optionsChanged)
    Q_PROPERTY(QString contactCurve READ contactCurve WRITE setContactCurve NOTIFY optionsChanged)
    // 0.8 "Optimize after Create": "" (off), "general" or "neo2" (D-156).
    Q_PROPERTY(QString optimizeTarget READ optimizeTarget WRITE setOptimizeTarget NOTIFY optionsChanged)
    // 0.7 "0–100 on video" over the preview (default on; generator.pos_overlay).
    Q_PROPERTY(bool posOverlay READ posOverlay WRITE setPosOverlay NOTIFY optionsChanged)
    // 0.7 gen_feel.js "How it feels" follows the device. feelDevice: "auto"
    // (the connected one; a Sam Neo 2 without one), "neo2", "vibrator" or
    // "stroker"; connectedKind is set by the page from the device; feelKind
    // is the result. Only what that device can use shows unless showAllFeel.
    // The Sam Neo 2 feel (sceneFeel "auto" / "" Classic / oral / intercourse
    // / breast / hand, feelBalance -100 vibration .. 100 suction, vibFollow)
    // is written into the new script right after Create (0.7
    // SaveFeelSettings); vibSource ("", "sound+contact", "sound") is the
    // vibrator's vibration. Remembered (0.7 localStorage gen.feel).
    Q_PROPERTY(QString feelDevice READ feelDevice WRITE setFeelDevice NOTIFY feelChanged)
    Q_PROPERTY(QString connectedKind READ connectedKind WRITE setConnectedKind NOTIFY feelChanged)
    Q_PROPERTY(QString feelKind READ feelKind NOTIFY feelChanged)
    Q_PROPERTY(bool showAllFeel READ showAllFeel WRITE setShowAllFeel NOTIFY feelChanged)
    Q_PROPERTY(QString sceneFeel READ sceneFeel WRITE setSceneFeel NOTIFY feelChanged)
    Q_PROPERTY(QString autoScene READ autoScene NOTIFY feelChanged)
    Q_PROPERTY(int feelBalance READ feelBalance WRITE setFeelBalance NOTIFY feelChanged)
    Q_PROPERTY(bool vibFollow READ vibFollow WRITE setVibFollow NOTIFY feelChanged)
    Q_PROPERTY(QString vibSource READ vibSource WRITE setVibSource NOTIFY feelChanged)
    Q_PROPERTY(QString feelSummary READ feelSummary NOTIFY feelChanged)
    Q_PROPERTY(QString feelHint READ feelHint NOTIFY feelChanged)
    Q_PROPERTY(QString tipClass READ tipClass WRITE setTipClass NOTIFY optionsChanged)
    Q_PROPERTY(QString contactClass READ contactClass WRITE setContactClass NOTIFY optionsChanged)
    Q_PROPERTY(QStringList tipClasses READ tipClasses CONSTANT)
    Q_PROPERTY(QStringList contactClasses READ contactClasses CONSTANT)
    Q_PROPERTY(bool markContact READ markContact WRITE setMarkContact NOTIFY contactChanged)
    // 0.7 "+ Another contact area": the next box drawn is an extra contact
    // area (magenta) of extraClass ("" = any; generator.extra_class).
    Q_PROPERTY(bool markExtra READ markExtra WRITE setMarkExtra NOTIFY contactChanged)
    Q_PROPERTY(QString extraClass READ extraClass WRITE setExtraClass NOTIFY optionsChanged)
    // 0.7 "+ Ignore region": the next box drawn is a soft mask (black dashed);
    // punched out of camera-motion compensation, does not drive the stroke.
    Q_PROPERTY(bool markIgnore READ markIgnore WRITE setMarkIgnore NOTIFY contactChanged)
    Q_PROPERTY(QVariantList maskRois READ maskRois NOTIFY contactChanged)
    // G2b / 0.7 "Partner fixed (pixels)" and "Stay fixed" for extras
    // (generator.partner_fixed, generator.extras_stay_fixed). They set the
    // ContactMarkBox.fixed flag written into .samn. Generate records the
    // gold partner path (fixed centres or second CSRT → trajectory.partner);
    // extras multi-path still follows; Tf/Tj distance stroke is in the engine.
    Q_PROPERTY(bool partnerFixed READ partnerFixed WRITE setPartnerFixed NOTIFY optionsChanged)
    Q_PROPERTY(bool extrasStayFixed READ extrasStayFixed WRITE setExtrasStayFixed NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList contactAreas READ contactAreas NOTIFY contactChanged)
    // The run.
    Q_PROPERTY(bool running READ running NOTIFY runChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)  // -1 = unknown length
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputChanged)  // the .samn to open
    Q_PROPERTY(bool scriptExists READ scriptExists NOTIFY outputChanged)  // ask before replacing
    // After Create (0.7 quality box and post-generate review): {quality
    // (0-100), qualityPassed, qualityWarnings, trackingReason, lostFrames,
    // oStartS, oEndS (O-marker set; -1 none), direction, suggestInvert,
    // ozone}; empty before a run.
    Q_PROPERTY(QVariantMap review READ review NOTIFY outputChanged)
    Q_PROPERTY(int step READ step NOTIFY stepChanged)  // 1 video, 2 tip, 3 feel, 4 create, 5 review
    // Find tip (0.7 auto_roi): running, and the other proposals to pick from
    // ({x, y, w, h, score} in video pixels, strongest first).
    Q_PROPERTY(bool finding READ finding NOTIFY findingChanged)
    Q_PROPERTY(QVariantList tipCandidates READ tipCandidates NOTIFY tipCandidatesChanged)
    // The queue (0.7 "Create queue"): {path, name, status, message}; status is
    // queued | running | done | skipped | failed | canceled.
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(bool queueRunning READ queueRunning NOTIFY queueChanged)
    Q_PROPERTY(QString queueSummary READ queueSummary NOTIFY queueChanged)

public:
    explicit GeneratorController(QObject* parent = nullptr);
    ~GeneratorController() override;

    QString videoPath() const { return video_path_; }
    QString videoName() const;
    QUrl videoUrl() const { return videoPath().isEmpty() ? QUrl() : QUrl::fromLocalFile(videoPath()); }
    bool hasVideo() const { return !video_path_.isEmpty(); }
    double durationMs() const { return duration_ms_; }
    QString videoInfo() const { return video_info_; }
    QSize videoSize() const { return video_size_; }
    double previewMs() const { return preview_ms_; }
    QImage frame() const { return frame_; }
    QRect box() const { return box_; }
    void setBox(const QRect& box);
    bool hasBox() const { return box_.isValid() && !box_.isEmpty(); }
    QString boxLabel() const;
    QString profile() const { return profile_; }
    void setProfile(const QString& p);
    bool invert() const { return invert_; }
    void setInvert(bool v);
    bool cameraCompensation() const { return camera_; }
    void setCameraCompensation(bool v);
    bool sceneCuts() const { return scene_cuts_; }
    void setSceneCuts(bool v);
    bool autoOZone() const { return auto_ozone_; }
    void setAutoOZone(bool v);
    bool gapHandling() const { return gap_handling_; }
    void setGapHandling(bool v);
    bool outlierRejection() const { return outlier_rejection_; }
    void setOutlierRejection(bool v);
    bool trackCache() const { return track_cache_; }
    void setTrackCache(bool v);
    bool contactVibration() const { return contact_vibration_; }
    void setContactVibration(bool v);
    double contactSpan() const { return contact_span_; }
    void setContactSpan(double span);
    QString contactCurve() const { return contact_curve_; }
    void setContactCurve(const QString& curve);
    QString optimizeTarget() const { return optimize_target_; }
    bool posOverlay() const { return pos_overlay_; }
    void setPosOverlay(bool on);
    QString feelDevice() const { return feel_device_; }
    void setFeelDevice(const QString& d);
    QString connectedKind() const { return connected_kind_; }
    void setConnectedKind(const QString& k);
    QString feelKind() const;
    bool showAllFeel() const { return show_all_feel_; }
    void setShowAllFeel(bool on);
    QString sceneFeel() const { return scene_feel_; }
    void setSceneFeel(const QString& s);
    QString autoScene() const;
    int feelBalance() const { return feel_balance_; }
    void setFeelBalance(int b);
    bool vibFollow() const { return vib_follow_; }
    void setVibFollow(bool on);
    QString vibSource() const { return vib_source_; }
    void setVibSource(const QString& s);
    QString feelSummary() const;
    QString feelHint() const;
    // The Sam Neo 2 feel written after Create into the new .samn and the
    // .funscript beside it; false when nothing was to change.
    bool applyFeel(const QString& samnPath);
    void setOptimizeTarget(const QString& target);
    QString tipClass() const { return tip_class_; }
    void setTipClass(const QString& c);
    QString contactClass() const { return contact_class_; }
    void setContactClass(const QString& c);
    QStringList tipClasses() const;
    QStringList contactClasses() const;
    bool markContact() const { return mark_contact_; }
    void setMarkContact(bool v);
    bool markExtra() const { return mark_extra_; }
    void setMarkExtra(bool v);
    QString extraClass() const { return extra_class_; }
    void setExtraClass(const QString& c);
    bool markIgnore() const { return mark_ignore_; }
    void setMarkIgnore(bool v);
    QVariantList maskRois() const;
    bool partnerFixed() const { return partner_fixed_; }
    void setPartnerFixed(bool v);
    bool extrasStayFixed() const { return extras_stay_fixed_; }
    void setExtrasStayFixed(bool v);
    QVariantList contactAreas() const;
    // Selection encoding for Delete selected / click: tip = -1, contact =
    // 0.., mask = kMaskSelectBase + index (1000..), none = -2.
    static constexpr int kMaskSelectBase = 1000;
    bool running() const { return running_; }
    int percent() const { return percent_; }
    QString progressText() const { return progress_text_; }
    QString status() const { return status_; }
    QStringList log() const { return log_; }
    QString outputPath() const { return output_path_; }
    QVariantMap review() const { return review_; }
    bool scriptExists() const;
    int step() const;
    bool finding() const { return finding_; }
    QVariantList tipCandidates() const { return tip_candidates_; }
    QVariantList queue() const { return queue_; }
    bool queueRunning() const { return queue_running_; }
    QString queueSummary() const;

    // A video path or file URL. false with the status set when it cannot be read.
    Q_INVOKABLE bool openVideo(const QString& pathOrUrl);
    // Loads the marking frame at ms (clamped to the clip).
    Q_INVOKABLE void seekPreview(double ms);
    Q_INVOKABLE void clearBox();
    // The tip box drawn by hand, with 0.7's line on what comes next.
    Q_INVOKABLE bool markTip(const QRect& box);
    // A contact area (inside the frame, at least 8 px; false otherwise),
    // with 0.7's line on it. While marking an extra it is appended with the
    // extra class; otherwise it is the primary (contact class), replacing
    // one drawn before. Marking ends after one area.
    Q_INVOKABLE bool addContactArea(const QRect& area);
    Q_INVOKABLE void removeContactArea(int index);
    // Soft mask / ignore region (inside the frame, at least 8 px).
    Q_INVOKABLE bool addMaskRoi(const QRect& area);
    Q_INVOKABLE void removeMaskRoi(int index);
    Q_INVOKABLE void clearMaskRois();
    // The class of area index (the primary takes a contact class, an extra
    // also "" for any).
    Q_INVOKABLE void setAreaClass(int index, const QString& cls);
    Q_INVOKABLE void clearContactAreas();
    // 0.7 "Clear extras": extras and soft masks go; tip and primary stay.
    Q_INVOKABLE void clearExtras();
    // The preview's selection, removed (0.7 Delete selected): -1 the tip box,
    // 0.. a contact area, kMaskSelectBase+i a soft mask.
    Q_INVOKABLE void deleteMark(int index);
    // A line from the page on the status (0.7 "Mark deselected.").
    Q_INVOKABLE void showStatus(const QString& s) { setStatus(s); }
    // Starts the run. Without overwrite an existing script stops it (the page
    // asks first, 0.7 confirm). false when it cannot start.
    Q_INVOKABLE bool create(bool overwrite = false);
    Q_INVOKABLE void cancel();
    // Proposes the tip box from the video's rhythmic motion, on its own
    // thread; the box is set and the other candidates are offered. false
    // when there is no video or a search or run is going.
    Q_INVOKABLE bool findTip();
    Q_INVOKABLE void cancelFind();
    // Takes candidate i as the tip box.
    Q_INVOKABLE void pickCandidate(int index);
    // Adds videos (paths or file URLs; duplicates once) to the queue.
    Q_INVOKABLE int addToQueue(const QStringList& pathsOrUrls);
    Q_INVOKABLE void removeFromQueue(int index);
    Q_INVOKABLE void clearQueue();
    // Creates every queued video in order on its own thread: Find tip, then
    // the script beside it with the current feel settings. Existing scripts
    // are skipped unless overwrite. cancel() stops the rest.
    Q_INVOKABLE bool runQueue(bool overwrite = false);
    // Tests: waits for the run on the worker to end (events still arrive queued).
    void waitForRun();

signals:
    void videoChanged();
    void previewChanged();
    void boxChanged();
    void optionsChanged();
    void feelChanged();
    void runChanged();
    void progressChanged();
    void statusChanged();
    void logChanged();
    void outputChanged();
    void stepChanged();
    void findingChanged();
    void tipCandidatesChanged();
    void contactChanged();
    void queueChanged();
    void queueFinished(int done, int skipped, int failed);
    // The script is written: the page opens it for review (D-137).
    void created(const QString& path);

private:
    void onEvent(const samn::engine::GenerateEvent& e);
    void buildReview(const std::filesystem::path& funscript);
    void setStatus(const QString& s);
    void appendLog(const QString& line);
    std::filesystem::path funscriptPath() const;

    QString video_path_;
    double duration_ms_ = 0;
    QString video_info_;
    QSize video_size_;
    double preview_ms_ = 0;
    QImage frame_;
    QRect box_;
    QString profile_ = QStringLiteral("standard");
    bool invert_ = false, camera_ = true, scene_cuts_ = true, auto_ozone_ = true;
    bool gap_handling_ = false, outlier_rejection_ = false, track_cache_ = false;
    bool contact_vibration_ = true;
    double contact_span_ = 0.75;
    QString contact_curve_ = QStringLiteral("soft");
    QString optimize_target_;
    bool pos_overlay_ = true;
    QString feel_device_ = QStringLiteral("auto"), connected_kind_, scene_feel_ = QStringLiteral("auto"), vib_source_;
    bool show_all_feel_ = false, vib_follow_ = false;
    int feel_balance_ = 0;
    // The choices are remembered (0.7 "generator.*" settings); the invert
    // switch belongs to one video and is not.
    std::unique_ptr<samn::Settings> settings_;
    void loadSettings();
    void saveSetting(const char* key, const nlohmann::json& value);
    // The opt-in gap, outlier and cache switches into a run's options.
    void applySwitches(engine::SignalOptions& signal, std::filesystem::path& track_cache_dir) const;
    QString tip_class_ = QStringLiteral("glans");
    QString contact_class_ = QStringLiteral("nipples");
    QString extra_class_;
    // 0.7 defaults with Contact vib: Partner fixed off, Stay fixed for extras
    // follows trajectory capture (we default Stay fixed on until track_multi).
    bool partner_fixed_ = false;
    bool extras_stay_fixed_ = true;
    bool mark_contact_ = false, mark_extra_ = false, mark_ignore_ = false;
    std::vector<samn::ContactMarkBox> contact_areas_;
    std::vector<QRect> mask_rois_;
    bool running_ = false;
    int percent_ = 0;
    QString progress_text_;
    QString status_;
    QStringList log_;
    QString output_path_;
    QVariantMap review_;
    // Create without a tip box finds the tip first (0.7 "Finding tip, then creating…").
    bool create_after_find_ = false, create_overwrite_ = false;
    uint64_t run_id_ = 0;
    QElapsedTimer run_clock_;
    std::unique_ptr<samn::engine::GenerateRunner> runner_;
    bool finding_ = false;
    QVariantList tip_candidates_;
    std::shared_ptr<std::atomic<bool>> find_cancel_;
    std::thread finder_;
    QVariantList queue_;
    bool queue_running_ = false;
    std::shared_ptr<std::atomic<bool>> queue_cancel_;
    std::thread queue_worker_;
    void setQueueItem(int index, const QString& status, const QString& message);
    void finishQueue();
};

}  // namespace samn::ui
