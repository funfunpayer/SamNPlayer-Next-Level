#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>

#include "samn/engine/generate.h"

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
    // The run.
    Q_PROPERTY(bool running READ running NOTIFY runChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)  // -1 = unknown length
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputChanged)  // the .samn to open
    Q_PROPERTY(bool scriptExists READ scriptExists NOTIFY outputChanged)  // ask before replacing
    Q_PROPERTY(int step READ step NOTIFY stepChanged)  // 1 video, 2 tip, 3 feel, 4 create, 5 review
    // Find tip (0.7 auto_roi): running, and the other proposals to pick from
    // ({x, y, w, h, score} in video pixels, strongest first).
    Q_PROPERTY(bool finding READ finding NOTIFY findingChanged)
    Q_PROPERTY(QVariantList tipCandidates READ tipCandidates NOTIFY tipCandidatesChanged)

public:
    explicit GeneratorController(QObject* parent = nullptr);
    ~GeneratorController() override;

    QString videoPath() const { return video_path_; }
    QString videoName() const;
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
    bool running() const { return running_; }
    int percent() const { return percent_; }
    QString progressText() const { return progress_text_; }
    QString status() const { return status_; }
    QStringList log() const { return log_; }
    QString outputPath() const { return output_path_; }
    bool scriptExists() const;
    int step() const;
    bool finding() const { return finding_; }
    QVariantList tipCandidates() const { return tip_candidates_; }

    // A video path or file URL. false with the status set when it cannot be read.
    Q_INVOKABLE bool openVideo(const QString& pathOrUrl);
    // Loads the marking frame at ms (clamped to the clip).
    Q_INVOKABLE void seekPreview(double ms);
    Q_INVOKABLE void clearBox();
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
    // The script is written: the page opens it for review (D-137).
    void created(const QString& path);

private:
    void onEvent(const samn::engine::GenerateEvent& e);
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
    bool running_ = false;
    int percent_ = 0;
    QString progress_text_;
    QString status_;
    QStringList log_;
    QString output_path_;
    uint64_t run_id_ = 0;
    QElapsedTimer run_clock_;
    std::unique_ptr<samn::engine::GenerateRunner> runner_;
    bool finding_ = false;
    QVariantList tip_candidates_;
    std::shared_ptr<std::atomic<bool>> find_cancel_;
    std::thread finder_;
};

}  // namespace samn::ui
