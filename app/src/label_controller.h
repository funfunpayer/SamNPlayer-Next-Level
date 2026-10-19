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
#include <memory>
#include <thread>
#include <vector>

#include "samn/label_track.h"

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
    bool suggesting() const { return suggesting_; }
    int suggestProgress() const { return suggest_progress_; }
    Q_INVOKABLE int exportDataset(const QString& dirOrUrl, double everyMs);

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

    void onPictureSignals(const std::vector<double>& luma, const std::vector<double>& motion, const QString& error);
    void finishSuggest();
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
