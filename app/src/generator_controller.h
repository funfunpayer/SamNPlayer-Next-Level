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
    Q_PROPERTY(QString tipClass READ tipClass WRITE setTipClass NOTIFY optionsChanged)
    Q_PROPERTY(QString contactClass READ contactClass WRITE setContactClass NOTIFY optionsChanged)
    Q_PROPERTY(QStringList tipClasses READ tipClasses CONSTANT)
    Q_PROPERTY(QStringList contactClasses READ contactClasses CONSTANT)
    Q_PROPERTY(bool markContact READ markContact WRITE setMarkContact NOTIFY contactChanged)
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
    bool contactVibration() const { return contact_vibration_; }
    void setContactVibration(bool v);
    double contactSpan() const { return contact_span_; }
    void setContactSpan(double span);
    QString contactCurve() const { return contact_curve_; }
    void setContactCurve(const QString& curve);
    QString optimizeTarget() const { return optimize_target_; }
    bool posOverlay() const { return pos_overlay_; }
    void setPosOverlay(bool on);
    void setOptimizeTarget(const QString& target);
    QString tipClass() const { return tip_class_; }
    void setTipClass(const QString& c);
    QString contactClass() const { return contact_class_; }
    void setContactClass(const QString& c);
    QStringList tipClasses() const;
    QStringList contactClasses() const;
    bool markContact() const { return mark_contact_; }
    void setMarkContact(bool v);
    QVariantList contactAreas() const;
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
    // A contact area with the current contact class (inside the frame, at
    // least 8 px; false otherwise). Marking ends after one area.
    Q_INVOKABLE bool addContactArea(const QRect& area);
    Q_INVOKABLE void removeContactArea(int index);
    Q_INVOKABLE void clearContactAreas();
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
    bool contact_vibration_ = true;
    double contact_span_ = 0.75;
    QString contact_curve_ = QStringLiteral("soft");
    QString optimize_target_;
    bool pos_overlay_ = true;
    // The choices are remembered (0.7 "generator.*" settings); the invert
    // switch belongs to one video and is not.
    std::unique_ptr<samn::Settings> settings_;
    void loadSettings();
    void saveSetting(const char* key, const nlohmann::json& value);
    QString tip_class_ = QStringLiteral("glans");
    QString contact_class_ = QStringLiteral("nipples");
    bool mark_contact_ = false;
    std::vector<samn::ContactMarkBox> contact_areas_;
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
