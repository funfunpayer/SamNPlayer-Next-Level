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

#include "ai_tip.h"
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
    Q_PROPERTY(QString boxLabel READ boxLabel NOTIFY boxLabelChanged)
    // Preview / last-Create tip path: the overlay centres on tipAt(playheadMs)
    // when not dragging. revision ticks so QML re-reads tipAt after a replace.
    Q_PROPERTY(bool hasTipPath READ hasTipPath NOTIFY tipPathChanged)
    Q_PROPERTY(quint64 tipPathRevision READ tipPathRevision NOTIFY tipPathChanged)
    // Camera POV from the tip path + seed box (ClassifyTipPov). "unknown" |
    // "first_person" | "third_person". Hint is empty until known — not an
    // Everyday control. Never a second ROI (D-055).
    Q_PROPERTY(QString povKind READ povKind NOTIFY povChanged)
    Q_PROPERTY(double povConfidence READ povConfidence NOTIFY povChanged)
    Q_PROPERTY(QString povHint READ povHint NOTIFY povChanged)
    // Feel: "recommended" (default, D-160) | "standard" (Normal) | "weich" (Soft) | "autotune".
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY optionsChanged)
    Q_PROPERTY(bool invert READ invert WRITE setInvert NOTIFY optionsChanged)
    Q_PROPERTY(bool cameraCompensation READ cameraCompensation WRITE setCameraCompensation NOTIFY optionsChanged)
    Q_PROPERTY(bool sceneCuts READ sceneCuts WRITE setSceneCuts NOTIFY optionsChanged)
    // G4a Advanced: Sliding dynamics (dynamic_range_ms 3000/-1), Adaptive
    // Keyframes (adaptive_error 6/0), motion axis auto|x|y.
    Q_PROPERTY(bool slidingDynamics READ slidingDynamics WRITE setSlidingDynamics NOTIFY optionsChanged)
    Q_PROPERTY(bool adaptiveKeyframes READ adaptiveKeyframes WRITE setAdaptiveKeyframes NOTIFY optionsChanged)
    Q_PROPERTY(QString motionAxis READ motionAxis WRITE setMotionAxis NOTIFY optionsChanged)
    // G4b Expert (closed under Advanced): post-track knobs. 0 = profile default
    // for smooth / peak spacing / prominence; 0 = off for RDP and max speed.
    Q_PROPERTY(int smoothWindow READ smoothWindow WRITE setSmoothWindow NOTIFY optionsChanged)
    Q_PROPERTY(int minPeakDistanceMs READ minPeakDistanceMs WRITE setMinPeakDistanceMs NOTIFY optionsChanged)
    Q_PROPERTY(double peakProminence READ peakProminence WRITE setPeakProminence NOTIFY optionsChanged)
    Q_PROPERTY(double rdpTolerance READ rdpTolerance WRITE setRdpTolerance NOTIFY optionsChanged)
    Q_PROPERTY(int maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY optionsChanged)
    Q_PROPERTY(bool autoOZone READ autoOZone WRITE setAutoOZone NOTIFY optionsChanged)
    // Find the tip by itself when a video opens (D-159, on): a sure find
    // becomes the box, an unsure one only shows its candidates, and a box the
    // user draws meanwhile is never replaced (generator.auto_tip).
    Q_PROPERTY(bool autoTip READ autoTip WRITE setAutoTip NOTIFY optionsChanged)
    // Bridge tracking gaps up to kGapBridgeMs and leave longer ones without
    // actions (on, from the tracker's own loss, D-161); treat single-frame
    // outliers as lost (on, D-160); keep raw tracks in config_dir()/cache/track
    // so a new feel does not track again (off) (generator.gap_handling,
    // generator.outlier_rejection, generator.track_cache).
    Q_PROPERTY(bool gapHandling READ gapHandling WRITE setGapHandling NOTIFY optionsChanged)
    Q_PROPERTY(bool outlierRejection READ outlierRejection WRITE setOutlierRejection NOTIFY optionsChanged)
    Q_PROPERTY(bool trackCache READ trackCache WRITE setTrackCache NOTIFY optionsChanged)
    // Opt-in hybrid tip re-acquire: ONNX only after CSRT + appearance miss
    // (D-139; off = Everyday). Needs a student or NudeNet model on disk.
    Q_PROPERTY(bool hybridReacquire READ hybridReacquire WRITE setHybridReacquire NOTIFY optionsChanged)
    // Cap tracking analysis rate (0 = every frame). Everyday default 15 —
    // FunGen2-snappy Create; Expert can set every frame (GENERATOR_SPEC FR-7).
    Q_PROPERTY(int maxAnalysisFps READ maxAnalysisFps WRITE setMaxAnalysisFps NOTIFY optionsChanged)
    // Share of the Generator stage height kept for the curve editor (0.18–0.65).
    // Remembered so the video/curves SplitView stays where the user left it.
    Q_PROPERTY(double editorShare READ editorShare WRITE setEditorShare NOTIFY optionsChanged)
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
    Q_PROPERTY(QVariantMap contactFeelProbe READ contactFeelProbe NOTIFY optionsChanged)
    // 0.8 "Optimize after Create" (D-156): "" unset → Feel default, "off" keep
    // as created, "general" or "neo2".
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
    // SaveFeelSettings); vibSource ("sound+contact" default, "sound", or
    // "" motion only) is the vibrator's vibration. An unset key defaults to
    // sound+contact; a saved "" is Motion only. Remembered (0.7 gen.feel).
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
    // gold partner path (trajectory.partner) and magenta extras paths
    // (trajectory.extras / track_multi); Tf/Tj distance stroke is tip↔primary.
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
    // The search running (or waiting to start) was started by opening the
    // video and is nobody's yet: Find tip takes it over, Create and the
    // queue may end it.
    Q_PROPERTY(bool autoFinding READ autoFinding NOTIFY findingChanged)
    Q_PROPERTY(QVariantList tipCandidates READ tipCandidates NOTIFY tipCandidatesChanged)
    // Suggest tip (AI) / Show all AI tags: local student/NudeNet boxes on this
    // frame. Click = pending tip until Apply; Shift-click = contact (safe class
    // adds, else pending Apply contact). Never writes the curve (D-057).
    Q_PROPERTY(bool aiTipAvailable READ aiTipAvailable NOTIFY aiTipChanged)
    Q_PROPERTY(QString aiTipHint READ aiTipHint NOTIFY aiTipChanged)
    Q_PROPERTY(bool suggestingAiTip READ suggestingAiTip NOTIFY aiTipChanged)
    Q_PROPERTY(QVariantList aiDetections READ aiDetections NOTIFY aiTipChanged)
    Q_PROPERTY(QVariantMap aiTipProposal READ aiTipProposal NOTIFY aiTipChanged)
    Q_PROPERTY(bool hasAiTipProposal READ hasAiTipProposal NOTIFY aiTipChanged)
    Q_PROPERTY(QVariantMap aiContactProposal READ aiContactProposal NOTIFY aiTipChanged)
    Q_PROPERTY(bool hasAiContactProposal READ hasAiContactProposal NOTIFY aiTipChanged)
    // 0.7 Suggest Tip+2nd: tip-class + contact-class seeds on this frame.
    Q_PROPERTY(bool tipAndSecondSeedsAvailable READ tipAndSecondSeedsAvailable NOTIFY aiTipChanged)
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
    bool hasTipPath() const { return !tipPath_.empty(); }
    quint64 tipPathRevision() const { return tip_path_revision_; }
    QString povKind() const { return pov_kind_; }
    double povConfidence() const { return pov_confidence_; }
    QString povHint() const;
    // Tip place at a video time (the playhead) from the preview / last-Create
    // path ({x, y} in video pixels), empty when no sample is near.
    Q_INVOKABLE QVariantMap tipAt(double scriptMs) const;
    // Tests: inject a path without tracking ({at, x, y} in video pixels).
    void setTipPathForTest(const QVariantList& points);
    QString profile() const { return profile_; }
    void setProfile(const QString& p);
    bool invert() const { return invert_; }
    void setInvert(bool v);
    bool cameraCompensation() const { return camera_; }
    void setCameraCompensation(bool v);
    bool sceneCuts() const { return scene_cuts_; }
    void setSceneCuts(bool v);
    bool slidingDynamics() const { return sliding_; }
    void setSlidingDynamics(bool v);
    bool adaptiveKeyframes() const { return adaptive_keyframes_; }
    void setAdaptiveKeyframes(bool v);
    QString motionAxis() const { return motion_axis_; }
    void setMotionAxis(const QString& axis);
    int smoothWindow() const { return smooth_window_; }
    void setSmoothWindow(int v);
    int minPeakDistanceMs() const { return min_peak_distance_ms_; }
    void setMinPeakDistanceMs(int v);
    double peakProminence() const { return peak_prominence_; }
    void setPeakProminence(double v);
    double rdpTolerance() const { return rdp_tolerance_; }
    void setRdpTolerance(double v);
    int maxSpeed() const { return max_speed_; }
    void setMaxSpeed(int v);
    bool autoOZone() const { return auto_ozone_; }
    void setAutoOZone(bool v);
    bool autoTip() const { return auto_tip_; }
    void setAutoTip(bool v);
    bool gapHandling() const { return gap_handling_; }
    void setGapHandling(bool v);
    bool outlierRejection() const { return outlier_rejection_; }
    void setOutlierRejection(bool v);
    bool trackCache() const { return track_cache_; }
    void setTrackCache(bool v);
    bool hybridReacquire() const { return hybrid_reacquire_; }
    void setHybridReacquire(bool v);
    int maxAnalysisFps() const { return max_analysis_fps_; }
    void setMaxAnalysisFps(int v);
    double editorShare() const { return editor_share_; }
    void setEditorShare(double v);
    bool contactVibration() const { return contact_vibration_; }
    void setContactVibration(bool v);
    double contactSpan() const { return contact_span_; }
    void setContactSpan(double span);
    QString contactCurve() const { return contact_curve_; }
    void setContactCurve(const QString& curve);
    // Live Feel probe (0.7 PreviewContactVibration): {activePct, peak, hint,
    // stroke:[{x,y}], vib:[{x,y}], peaks:[{x,y}]} with x/y in 0..1.
    QVariantMap contactFeelProbe() const;
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
    bool autoFinding() const;
    QVariantList tipCandidates() const { return tip_candidates_; }
    bool aiTipAvailable() const;
    QString aiTipHint() const { return ai_tip_hint_; }
    bool suggestingAiTip() const { return suggesting_ai_tip_; }
    QVariantList aiDetections() const { return ai_detections_; }
    QVariantMap aiTipProposal() const { return ai_tip_proposal_; }
    bool hasAiTipProposal() const { return !ai_tip_proposal_.isEmpty(); }
    QVariantMap aiContactProposal() const { return ai_contact_proposal_; }
    bool hasAiContactProposal() const { return !ai_contact_proposal_.isEmpty(); }
    QVariantList queue() const { return queue_; }
    bool queueRunning() const { return queue_running_; }
    QString queueSummary() const;

    // A video path or file URL. false with the status set when it cannot be
    // read. lookForTip false: no automatic tip search (a script opened for editing).
    Q_INVOKABLE bool openVideo(const QString& pathOrUrl, bool lookForTip = true);
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
    // Compact 0.7 Create status suffix from optimizeFor / improveScript text.
    Q_INVOKABLE static QString createPolishSuffix(const QString& polish);
    // Starts the run. Without overwrite an existing script stops it (the page
    // asks first, 0.7 confirm). false when it cannot start.
    Q_INVOKABLE bool create(bool overwrite = false);
    Q_INVOKABLE void cancel();
    // Proposes the tip box from the video's rhythmic motion, on its own
    // thread; the box is set and the other candidates are offered. While the
    // automatic search runs it takes that one over (or restarts it from the
    // shown frame); while a search for another video or a canceled one ends,
    // the new one starts after it. false when there is no video, a run is
    // going or the user's own search runs.
    Q_INVOKABLE bool findTip();
    Q_INVOKABLE void cancelFind();
    // Takes candidate i as the tip box.
    Q_INVOKABLE void pickCandidate(int index);
    // Lists AI boxes on the shown frame and, when a tip-class box is found,
    // sets a pending proposal. Does not write the tip box. false when there
    // is no video, a run is going, a suggest is already running, or no model.
    Q_INVOKABLE bool suggestAiTip();
    // Same listing as Suggest, without auto-picking a tip-class box.
    Q_INVOKABLE bool listAiDetections();
    // 0.7 Suggest Tip+2nd: tip + partner proposals from seeds (Apply required).
    // Runs detect when the frame has no list yet; false when seeds missing.
    Q_INVOKABLE bool suggestTipAndSecond();
    bool tipAndSecondSeedsAvailable() const;
    // Commits the pending AI proposal as the tip box (Apply).
    Q_INVOKABLE bool applyAiTip();
    Q_INVOKABLE void dismissAiTip();
    // Chooses listed detection i as the pending tip proposal (still needs Apply).
    Q_INVOKABLE void pickAiDetection(int index);
    // Shift-click: safe contact-class labels add a contact area (when Contact
    // vibration is on); others stay a pending contact proposal. Never writes
    // the tip box. true only when an area was added.
    Q_INVOKABLE bool pickAiDetectionAsContact(int index);
    // Commits the pending AI contact proposal as a contact area.
    Q_INVOKABLE bool applyAiContact();
    // Tests: pretend a model is present and return these hits on Suggest.
    // Each map: {x, y, w, h, score, label, cls}. Empty list = model ready, no boxes.
    void setFakeAiHitsForTest(const QVariantList& hits);
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
    void boxLabelChanged();
    void tipPathChanged();
    void povChanged();
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
    void aiTipChanged();
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
    double box_ms_ = 0;  // the video time the box was drawn or found at
    QString profile_ = QStringLiteral("recommended");
    bool invert_ = false, camera_ = true, scene_cuts_ = true, auto_ozone_ = true;
    bool sliding_ = true, adaptive_keyframes_ = true;
    QString motion_axis_ = QStringLiteral("auto");
    // Expert defaults match PostOptions / 0.7 Everyday (smooth 11, spacing 150).
    int smooth_window_ = 11;
    int min_peak_distance_ms_ = 150;
    double peak_prominence_ = 0;
    double rdp_tolerance_ = 0;
    int max_speed_ = 0;
    bool gap_handling_ = true, outlier_rejection_ = true, track_cache_ = false;
    bool hybrid_reacquire_ = false;
    int max_analysis_fps_ = 15;  // Everyday: FunGen2-faster Create (0 = every frame)
    double editor_share_ = 0.40;
    bool auto_tip_ = true;
    bool find_auto_ = false;     // the running search was started by openVideo
    bool refind_ = false;        // a new search starts when the running one has ended
    bool pending_auto_ = false;  // that new search is the automatic one (a video opened meanwhile)
    QString find_video_;         // the video the running search looks at
    int64_t find_start_ms_ = 0;  // where the running search looks from
    quint64 box_edits_ = 0;      // counts box changes; a search compares it
    bool contact_vibration_ = true;
    double contact_span_ = 0.75;
    QString contact_curve_ = QStringLiteral("soft");
    QString optimize_target_;
    bool pos_overlay_ = true;
    QString feel_device_ = QStringLiteral("auto"), connected_kind_, scene_feel_ = QStringLiteral("auto"),
            vib_source_ = QStringLiteral("sound+contact");
    bool show_all_feel_ = false, vib_follow_ = false;
    int feel_balance_ = 0;
    // The choices are remembered (0.7 "generator.*" settings); the invert
    // switch belongs to one video and is not.
    std::unique_ptr<samn::Settings> settings_;
    void loadSettings();
    void saveSetting(const char* key, const nlohmann::json& value);
    // When a tip path exists and a local model is present, turn hybrid
    // re-acquire on unless the user saved Off (FunGen2: no Advanced toggle).
    void ensureHybridAssistForTipPath();
    // Gap / outlier / cache / Sliding / Adaptive / Expert into a run's signal options.
    void applySwitches(engine::SignalOptions& signal, std::filesystem::path& track_cache_dir) const;
    engine::TrackAxis trackAxis() const;
    QString tip_class_ = QStringLiteral("glans");
    QString contact_class_ = QStringLiteral("nipples");
    QString extra_class_;
    // 0.7 defaults with Contact vib: Partner fixed off, Stay fixed for extras
    // on (fixed centres unless the user turns Stay fixed off).
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
    // Create without a tip box finds the tip first (FunGen2 / 0.7 "Finding tip, then creating…").
    bool create_after_find_ = false, create_overwrite_ = false;
    bool find_resolve_ = false;  // the running search is ResolveTipForCreate
    uint64_t run_id_ = 0;
    QElapsedTimer run_clock_;
    std::unique_ptr<samn::engine::GenerateRunner> runner_;
    bool finding_ = false;
    QVariantList tip_candidates_;
    std::shared_ptr<std::atomic<bool>> find_cancel_;
    std::thread finder_;
    bool suggesting_ai_tip_ = false;
    bool fake_ai_ready_ = false;
    QVariantList fake_ai_hits_;
    QVariantList ai_detections_;
    QVariantMap ai_tip_proposal_;
    QVariantMap ai_contact_proposal_;
    QString ai_tip_hint_;
    quint64 ai_seq_ = 0;
    std::thread ai_worker_;
    void refreshAiTipHint();
    // 0 = list only, 1 = pick tip, 2 = pick tip + contact (Tip+2nd).
    bool runAiDetect(int mode);
    void finishAiSuggest(const QVariantList& hits, const QString& forVideo, quint64 seq, int mode);
    bool proposeTipAndSecondFromHits(const std::vector<AiTipHit>& hits);
    bool addContactFromDetection(const QVariantMap& d, const QString& cls);
    bool startTipSearch(bool resolve);
    QVariantList queue_;
    bool queue_running_ = false;
    std::shared_ptr<std::atomic<bool>> queue_cancel_;
    std::thread queue_worker_;
    void setQueueItem(int index, const QString& status, const QString& message);
    void finishQueue();
    void installBox(const QRect& box, double atMs);
    void scheduleTipPreview();
    void cancelTipPreview();
    void startTipPreview(quint64 seq);
    void applyTipPath(std::vector<samn::TrajectoryPoint> path);
    void clearTipPath();
    void loadTipPathFromSamn(const QString& samnPath);
    void refreshPov();

    std::vector<samn::TrajectoryPoint> tipPath_;
    QString pov_kind_ = QStringLiteral("unknown");
    double pov_confidence_ = 0;
    quint64 tip_path_revision_ = 0;
    quint64 preview_seq_ = 0;
    std::shared_ptr<std::atomic<bool>> preview_cancel_;
    std::thread preview_;
};

}  // namespace samn::ui
