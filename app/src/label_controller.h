#pragma once

#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <functional>
#include <optional>
#include <memory>
#include <thread>
#include <vector>

#include "model_download.h"
#include "samn/label_track.h"
#include "samn/teacher_votes.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace samn {
class Settings;
}
namespace samn::vision {
class OnnxDetector;
}

namespace samn::ui {

class AudioLevelDecoder;

// The AI Train label editor (D-155 §2.2): a clip, its label tracks and the
// frame under the playhead. Every change is one undo step and is saved at
// once beside the clip as <clip>.labels.json. A box the user draws or moves
// is a confirmed key at the current frame.
class LabelController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString videoPath READ videoPath NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY videoChanged)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY videoChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoChanged)
    Q_PROPERTY(double frameMs READ frameMs NOTIFY videoChanged)  // one frame
    Q_PROPERTY(double previewMs READ previewMs NOTIFY previewChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY previewChanged)
    Q_PROPERTY(QStringList labels READ labels NOTIFY tracksChanged)
    Q_PROPERTY(QString currentLabel READ currentLabel WRITE setCurrentLabel NOTIFY currentLabelChanged)
    // At the preview: {label, cls, classIndex, index, x, y, w, h, present, keyed, confirmed, reviewed}.
    Q_PROPERTY(QVariantList boxes READ boxes NOTIFY previewChanged)
    // Per object: {label, cls, classIndex, index, keys: [{ms, absent, confirmed, source}],
    // spans: [{startMs, endMs, present, reviewed}]}.
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY tracksChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY tracksChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY tracksChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // The classes offered (body parts and the contact point); any other name
    // is an own class. Each can have several objects ("hand", "hand 2").
    Q_PROPERTY(QStringList presetLabels READ presetLabels CONSTANT)
    // The phase lane (D-157): [{startMs, endMs, phase, confirmed, source}].
    Q_PROPERTY(QVariantList phases READ phases NOTIFY tracksChanged)
    Q_PROPERTY(QStringList phaseNames READ phaseNames CONSTANT)
    Q_PROPERTY(QString phaseAtPreview READ phaseAtPreview NOTIFY previewChanged)
    // The rule-based phase suggestions run (D-157): busy and 0..100.
    Q_PROPERTY(bool suggesting READ suggesting NOTIFY suggestingChanged)
    Q_PROPERTY(int suggestProgress READ suggestProgress NOTIFY suggestingChanged)
    // A teacher on a model server (D-155 step 4): an OpenAI-compatible
    // address (Ollama http://<host>:11434/v1, LM Studio http://<host>:1234/v1),
    // the model, an optional key. Kept as aitrain.teacher.*.
    Q_PROPERTY(QString teacherUrl READ teacherUrl WRITE setTeacherUrl NOTIFY teacherChanged)
    Q_PROPERTY(QString teacherModel READ teacherModel WRITE setTeacherModel NOTIFY teacherChanged)
    Q_PROPERTY(QString teacherKey READ teacherKey WRITE setTeacherKey NOTIFY teacherChanged)
    Q_PROPERTY(QStringList teacherModels READ teacherModels NOTIFY teacherChanged)
    Q_PROPERTY(bool teacherBusy READ teacherBusy NOTIFY teacherChanged)
    Q_PROPERTY(QString teacherStatus READ teacherStatus NOTIFY teacherChanged)
    // Who answers "Ask the teacher": "server" (above) or "nudenet" (NudeNet
    // on this computer with ONNX Runtime). Kept as aitrain.teacher.kind.
    Q_PROPERTY(QString teacherKind READ teacherKind WRITE setTeacherKind NOTIFY teacherChanged)
    // Whether this build runs models itself (ONNX Runtime).
    Q_PROPERTY(bool localTeacherAvailable READ localTeacherAvailable CONSTANT)
    // NudeNet's model file, "" until fetched or chosen (aitrain.nudenet.path).
    Q_PROPERTY(QString nudenetPath READ nudenetPath NOTIFY teacherChanged)
    // The teachers going through the clip (votes): busy and 0..100.
    Q_PROPERTY(bool clipTeaching READ clipTeaching NOTIFY teacherChanged)
    Q_PROPERTY(int clipTeachProgress READ clipTeachProgress NOTIFY teacherChanged)
    // The model download: -1 when none runs, else 0..100.
    Q_PROPERTY(int modelDownload READ modelDownload NOTIFY teacherChanged)
    // Tracking between keyframes (D-155 step 3): busy and 0..100.
    Q_PROPERTY(bool tracking READ tracking NOTIFY trackingChanged)
    Q_PROPERTY(int trackProgress READ trackProgress NOTIFY trackingChanged)
    // After a box is drawn or dragged, the stretches to the confirmed boxes
    // on both sides are tracked again at once (aitrain.retrack_after_drag).
    Q_PROPERTY(bool retrackAfterDrag READ retrackAfterDrag WRITE setRetrackAfterDrag NOTIFY trackingChanged)

public:
    explicit LabelController(QObject* parent = nullptr);
    ~LabelController() override;

    QString videoPath() const { return video_path_; }
    QString videoName() const;
    QUrl videoUrl() const;
    bool hasVideo() const { return !video_path_.isEmpty(); }
    double durationMs() const { return duration_ms_; }
    QSize videoSize() const { return video_size_; }
    double frameMs() const { return frame_ms_; }
    double previewMs() const { return preview_ms_; }
    QImage frame() const { return frame_; }
    QStringList labels() const;
    QString currentLabel() const { return current_; }
    void setCurrentLabel(const QString& label);
    QVariantList boxes() const;
    QVariantList lanes() const;
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString status() const { return status_; }
    QStringList presetLabels() const;
    const samn::LabelProject& project() const { return project_; }

    // A video or an image (a clip of one frame); loads <clip>.labels.json.
    Q_INVOKABLE bool openVideo(const QString& pathOrUrl);
    Q_INVOKABLE void seek(double ms);
    Q_INVOKABLE void step(int frames);
    // A new object of the class ("hand", then "hand 2" …), made current.
    Q_INVOKABLE bool addLabel(const QString& cls);
    // The current label's box at this frame (video pixels), confirmed.
    Q_INVOKABLE bool setBox(const QRectF& box);
    // The current label is not in the picture from this frame on (a negative
    // sample), or with covered: it is there but hidden (no sample at all).
    Q_INVOKABLE bool markAbsent(bool covered = false);
    // Removes the current label's key at this frame.
    Q_INVOKABLE bool removeKey();
    // Confirms the current label's keys in the whole clip (accepts proposals).
    Q_INVOKABLE int confirmAll();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // Writes images/ and labels/ (YOLO) and classes.txt into dir; every_ms
    // apart. Returns the number of samples, -1 on error (status says why).
    // Sets [startMs, endMs) to the phase; "" clears it.
    Q_INVOKABLE bool setPhase(double startMs, double endMs, const QString& phase);
    // Confirms every phase suggestion.
    Q_INVOKABLE int confirmPhases();
    // Measures the picture and the sound and adds phase suggestions where no
    // phase is set yet (empty, intro, climax, cool-down). Asynchronous.
    Q_INVOKABLE bool suggestPhases();
    Q_INVOKABLE void cancelSuggest();
    // Fills the current label between its confirmed boxes: CSRT forward and
    // backward, blended; unconfirmed keys, uncertain where the runs disagree.
    // Asynchronous, one undo step.
    Q_INVOKABLE bool trackCurrent();
    Q_INVOKABLE void cancelTrack();
    // Jumps to the next uncertain key (the current label first, else any);
    // false when there is none.
    Q_INVOKABLE bool nextUncertain();
    // Lists the server's models (GET <url>/models).
    Q_INVOKABLE bool testTeacher();
    // The teacher looks at the frame on screen and proposes boxes for the
    // project's classes (all presets but contact and exclude when there are
    // none yet): unconfirmed keys, one undo step.
    Q_INVOKABLE bool askTeacher();
    QString teacherUrl() const { return teacher_url_; }
    void setTeacherUrl(const QString& url);
    QString teacherModel() const { return teacher_model_; }
    void setTeacherModel(const QString& model);
    QString teacherKey() const { return teacher_key_; }
    void setTeacherKey(const QString& key);
    QStringList teacherModels() const { return teacher_models_; }
    bool teacherBusy() const { return teacher_reply_ != nullptr || local_busy_ || clip_teaching_; }
    bool clipTeaching() const { return clip_teaching_; }
    int clipTeachProgress() const {
        return clip_times_.empty() ? 0 : int(clip_index_ * 100 / clip_times_.size());
    }
    // Every teacher set up (NudeNet here, the model server) looks at the
    // frame on screen; their boxes are voted on (D-155 step 4): what all
    // that can see a class found is a plain proposal, what only some found
    // is kept as uncertain. One undo step.
    Q_INVOKABLE bool askAllTeachers();
    // The same vote every stepMs through the whole clip, in the background;
    // one undo step at the end (or when stopped, for what was done).
    Q_INVOKABLE bool teachClip(double stepMs = 2000);
    Q_INVOKABLE void cancelTeachClip();
    QString teacherKind() const { return teacher_kind_; }
    void setTeacherKind(const QString& kind);
    bool localTeacherAvailable() const;
    QString nudenetPath() const;
    int modelDownload() const { return model_download_; }
    // Fetches NudeNet (its PyPI wheel, both hashes checked) into
    // config_dir()/models and makes it the teacher. Asynchronous.
    Q_INVOKABLE bool downloadNudeNet();
    Q_INVOKABLE void cancelModelDownload();
    // A NudeNet model file the user already has (it must load and have
    // NudeNet's 18 classes); made the teacher.
    Q_INVOKABLE bool chooseNudeNet(const QString& pathOrUrl);
    // Tests: where NudeNet is fetched from.
    void setNudeNetSource(const ModelSource& source) { nudenet_source_ = source; }
    QString teacherStatus() const { return teacher_status_; }
    // Seconds before a teacher request gives up (a big model on a laptop is slow).
    static constexpr int kTeacherTimeoutMs = 180000;
    bool tracking() const { return tracking_; }
    bool retrackAfterDrag() const { return retrack_after_drag_; }
    void setRetrackAfterDrag(bool on);
    int trackProgress() const { return track_progress_; }
    bool suggesting() const { return suggesting_; }
    int suggestProgress() const { return suggest_progress_; }
    Q_INVOKABLE int exportDataset(const QString& dirOrUrl, double everyMs);
    // The same into the student's training set (config_dir()/ai/datasets/<clip>),
    // replacing what this clip had there (D-155 step 5).
    Q_INVOKABLE int addToTrainingSet(double everyMs);

    QVariantList phases() const;
    QStringList phaseNames() const;
    QString phaseAtPreview() const;

signals:
    void videoChanged();
    void previewChanged();
    void tracksChanged();
    void currentLabelChanged();
    void statusChanged();
    void suggestingChanged();
    void trackingChanged();
    void teacherChanged();

private:
    void change(const QString& what, const std::function<bool(samn::LabelProject&)>& op);
    void save();
    void setStatus(const QString& s);
    int64_t tolerance() const { return int64_t(frame_ms_ / 2); }

    QString video_path_;
    QSize video_size_;
    double duration_ms_ = 0, frame_ms_ = 40, preview_ms_ = 0;
    bool still_ = false;
    QImage frame_;
    QString current_;
    samn::LabelProject project_;
    std::vector<samn::LabelProject> undo_, redo_;
    QString status_;

    samn::Settings& settings();
    void saveSetting(const char* key, const QString& value);
    QNetworkReply* teacherRequest(const QString& path, const QByteArray& body);
    void setTeacherStatus(const QString& s);
    std::unique_ptr<samn::Settings> settings_;
    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* teacher_reply_ = nullptr;
    QString teacher_url_, teacher_model_, teacher_key_, teacher_status_;
    QStringList teacher_models_;

    using BoxesDone = std::function<void(std::optional<std::vector<samn::TeacherBox>>, const QString&)>;
    using VoteDone = std::function<void(const std::vector<samn::TeacherVote>&, const QStringList&)>;
    bool startServer(const QImage& frame, const std::vector<std::string>& classes, BoxesDone done);
    bool startLocal(const QImage& frame, const std::vector<std::string>& classes, BoxesDone done);
    bool startVote(const QImage& frame, VoteDone done, bool skipServer = false, bool skipLocal = false);
    void applyProposals(int64_t ms, const std::vector<samn::TeacherBox>& boxes, const std::string& source, const QString& who);
    std::vector<std::string> localClasses(const std::vector<std::string>& asked) const;
    std::vector<std::string> teacherClasses() const;
    void teachNextFrame();
    void finishTeachClip();
    bool clip_teaching_ = false, clip_cancel_ = false, clip_skip_server_ = false, clip_skip_local_ = false;
    std::vector<double> clip_times_;
    size_t clip_index_ = 0;
    std::vector<std::pair<int64_t, std::vector<samn::VotedBox>>> clip_results_;
    std::vector<std::string> clip_teachers_;
    QStringList clip_errors_;
    QString teacher_kind_ = QStringLiteral("server");
    QString nudenet_path_;
    ModelSource nudenet_source_ = NudeNetSource();
    int model_download_ = -1;
    QNetworkReply* download_reply_ = nullptr;
    std::shared_ptr<samn::vision::OnnxDetector> detector_;
    bool local_busy_ = false;
    std::thread local_worker_;

    void onPictureSignals(const std::vector<double>& luma, const std::vector<double>& motion, const QString& error);
    void finishSuggest();
    struct Gap {
        int64_t from_ms, to_ms;
        samn::LabelBox from_box, to_box;
    };
    std::vector<Gap> gapsOf(const samn::LabelTrack& t, std::optional<int64_t> around = std::nullopt) const;
    bool startTracking(const QString& label, const std::vector<Gap>& gaps, bool automatic);
    bool retrack_after_drag_ = true;
    int track_run_ = 0;
    void onTracked(const QString& label, const std::vector<Gap>& gaps, const std::vector<std::vector<samn::LabelKey>>& keys,
                   const QString& error);
    bool tracking_ = false;
    int track_progress_ = 0;
    std::shared_ptr<std::atomic<bool>> track_cancel_;
    std::thread track_worker_;
    bool suggesting_ = false;
    int suggest_progress_ = 0;
    int suggest_run_ = 0;
    std::shared_ptr<std::atomic<bool>> suggest_cancel_;
    std::thread suggest_worker_;
    AudioLevelDecoder* audio_ = nullptr;
    bool picture_done_ = false, audio_done_ = false;
    std::vector<double> luma_, motion_, loudness_;
};

}  // namespace samn::ui
