#pragma once
// Clip prep (0.7's Benchmark "Clip-Prep", D-155 step 6): short, smaller cuts
// of long videos for Compare, the golden clips and test media. ffmpeg does
// the cut; the app fetches it with one click (the imageio-ffmpeg wheel from
// PyPI, both hashes pinned, kept in config_dir()/tools) or uses one the user
// chooses. One cut from an in and an out point, or a batch from 0.7's
// marks.json; a script beside the video is cut to the same window.

#include <QObject>
#include <QProcess>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "model_download.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace samn {
class Settings;
}

namespace samn::ui {

class ClipPrepController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // ffmpeg: there, being fetched (with a line on it).
    Q_PROPERTY(bool ffmpegReady READ ffmpegReady NOTIFY toolChanged)
    Q_PROPERTY(bool fetching READ fetching NOTIFY toolChanged)
    Q_PROPERTY(QString toolStatus READ toolStatus NOTIFY toolChanged)
    Q_PROPERTY(QString ffmpegPath READ ffmpegPath NOTIFY toolChanged)
    // "720p" (1280 wide), "960w" or "1080p"; clipprep.preset.
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY settingsChanged)
    // clipprep.no_audio.
    Q_PROPERTY(bool noAudio READ noAudio WRITE setNoAudio NOTIFY settingsChanged)
    // A cut or a batch: busy, 0..100, a line on where it is, the last file made.
    Q_PROPERTY(bool cutting READ cutting NOTIFY cutChanged)
    Q_PROPERTY(int progress READ progress NOTIFY cutChanged)
    Q_PROPERTY(QString status READ status NOTIFY cutChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY cutChanged)

public:
    explicit ClipPrepController(QObject* parent = nullptr);
    ~ClipPrepController() override;

    bool ffmpegReady() const { return !ffmpegPath().isEmpty(); }
    bool fetching() const { return download_ != nullptr; }
    QString toolStatus() const { return tool_status_; }
    QString ffmpegPath() const;
    QString preset() const { return preset_; }
    void setPreset(const QString& p);
    bool noAudio() const { return no_audio_; }
    void setNoAudio(bool on);
    bool cutting() const { return proc_ != nullptr; }
    int progress() const { return progress_; }
    QString status() const { return status_; }
    QString lastOutput() const { return last_output_; }

    // Fetches ffmpeg (about 30 MB). Asynchronous; cancelFetch stops it.
    Q_INVOKABLE bool fetchFfmpeg();
    Q_INVOKABLE void cancelFetch();
    // An ffmpeg of the user's own (a path or a file URL).
    Q_INVOKABLE bool chooseFfmpeg(const QString& path);
    // 0.7's file name beside the source, <stem>_<start>-<end>.mp4; "" while
    // the times are not valid.
    Q_INVOKABLE QString suggestOutput(const QString& source, const QString& start, const QString& end) const;
    // Cuts [start, end] (seconds or MM:SS or HH:MM:SS) of source into output
    // ("" for the suggested name). Asynchronous.
    Q_INVOKABLE bool cut(const QString& source, const QString& start, const QString& end, const QString& output = {});
    // Every cut of a marks.json, the clips beside it; its source is relative
    // to the marks file unless absolute, its max_width wins over the preset.
    Q_INVOKABLE bool cutMarks(const QString& marksFile);
    // Stops the cut; the unfinished file is removed.
    Q_INVOKABLE void cancel();
    // 0.7's copy-paste command for the same cut (to run it elsewhere), ""
    // while the request is not valid; copyCommand puts it on the clipboard.
    Q_INVOKABLE QString commandLine(const QString& source, const QString& start, const QString& end, const QString& output = {}) const;
    Q_INVOKABLE bool copyCommand(const QString& source, const QString& start, const QString& end, const QString& output = {});
    // A file URL from a dialog as a path.
    Q_INVOKABLE QString localPath(const QString& pathOrUrl) const;

    // Where a fetched ffmpeg lives.
    static QString toolsDir();

    // Tests: ffmpeg from elsewhere (a program and its first arguments), and
    // the download.
    void setFfmpegCommand(const QString& program, const QStringList& prefix) {
        program_ = program;
        prefix_ = prefix;
        emit toolChanged();
    }
    void setFfmpegSource(const ModelSource& s) { source_ = s; }

signals:
    void toolChanged();
    void settingsChanged();
    void cutChanged();

private:
    struct Job {
        QString input, output, name;
        double start_s = 0, end_s = 0;
        int max_width = 1280;
    };
    void startJobs(QList<Job> jobs);
    void runNext();
    void finishJob(int code, QProcess::ExitStatus status);
    void onProgressLine(const QByteArray& line);
    void endRun(const QString& status);
    void setToolStatus(const QString& s);
    void setStatus(const QString& s);
    QString cutScript(const Job& job) const;
    samn::Settings& settings();

    std::unique_ptr<samn::Settings> settings_;
    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* download_ = nullptr;
    QProcess* proc_ = nullptr;
    ModelSource source_;
    QString program_, chosen_, tool_status_, status_, last_output_, preset_ = QStringLiteral("720p");
    QStringList prefix_, scripts_;
    bool no_audio_ = false, cancel_asked_ = false;
    int progress_ = 0, done_ = 0, total_ = 0;
    QList<Job> jobs_;
    Job job_;
    QByteArray out_buffer_;
    QByteArray err_;
};

// ffmpeg 7 from the imageio-ffmpeg 0.6.0 wheel for this platform (both
// hashes pinned); an empty url where there is none.
ModelSource FfmpegSource();

}  // namespace samn::ui
