#include "clip_prep_controller.h"

#include <algorithm>
#include <cmath>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrl>

#include "samn/clip_prep.h"
#include "samn/funscript.h"
#include "samn/settings.h"

namespace samn::ui {

namespace {
QString FromFs(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
QString LocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
QString LastLine(const QByteArray& out) {
    const QList<QByteArray> lines = out.trimmed().split('\n');
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.last()).trimmed();
}
// A clip name from a marks file as a file name: no folders, no characters
// Windows refuses.
QString SafeName(QString name) {
    for (QChar& c : name)
        if (QStringLiteral("/\\:*?\"<>|").contains(c) || c.unicode() < 32) c = '_';
    name = name.trimmed();
    while (name.startsWith('.')) name.remove(0, 1);
    if (name.isEmpty()) name = QStringLiteral("clip");
    if (!name.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) name += QStringLiteral(".mp4");
    return name;
}
}  // namespace

ModelSource FfmpegSource() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return {QStringLiteral("ffmpeg 7.1"),
            QUrl(QStringLiteral("https://files.pythonhosted.org/packages/2c/c6/fa760e12a2483469e2bf5058c5faff664acf66cadb4df2ad6205b016a73d/"
                                "imageio_ffmpeg-0.6.0-py3-none-win_amd64.whl")),
            "02fa47c83703c37df6bfe4896aab339013f62bf02c5ebf2dce6da56af04ffc0a",
            QStringLiteral("imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe"),
            "2ce797a0f88d7f067180338fb227f7b1928ea727bd9a4d7a1d022f7c52af71a3", QStringLiteral("ffmpeg.exe")};
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return {QStringLiteral("ffmpeg 7.0.2"),
            QUrl(QStringLiteral("https://files.pythonhosted.org/packages/a0/2d/43c8522a2038e9d0e7dbdf3a61195ecc31ca576fb1527a528c877e87d973/"
                                "imageio_ffmpeg-0.6.0-py3-none-manylinux2014_x86_64.whl")),
            "c7e46fcec401dd990405049d2e2f475e2b397779df2519b544b8aab515195282",
            QStringLiteral("imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2"),
            "e7e7fb30477f717e6f55f9180a70386c62677ef8a4d4d1a5d948f4098aa3eb99", QStringLiteral("ffmpeg")};
#else
    return {};
#endif
}

ClipPrepController::ClipPrepController(QObject* parent) : QObject(parent), source_(FfmpegSource()) {
    try {
        const auto p = settings().get("clipprep.preset", "720p");
        if (p.is_string()) preset_ = QString::fromStdString(p.get<std::string>());
        const auto a = settings().get("clipprep.no_audio", false);
        if (a.is_boolean()) no_audio_ = a.get<bool>();
        const auto f = settings().get("clipprep.ffmpeg", "");
        if (f.is_string()) chosen_ = QString::fromStdString(f.get<std::string>());
    } catch (const std::exception&) {
    }
    if (preset_ != "960w" && preset_ != "1080p") preset_ = QStringLiteral("720p");
}

ClipPrepController::~ClipPrepController() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(3000);
        QFile::remove(job_.output);
    }
    if (download_) {
        download_->disconnect(this);
        download_->abort();
    }
}

samn::Settings& ClipPrepController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

QString ClipPrepController::toolsDir() { return FromFs(samn::config_dir() / "tools"); }

QString ClipPrepController::ffmpegPath() const {
    if (!program_.isEmpty()) return program_;
    if (!chosen_.isEmpty() && QFileInfo(chosen_).isFile()) return chosen_;
    if (!source_.file_name.isEmpty()) {
        const QString fetched = toolsDir() + "/" + source_.file_name;
        if (QFileInfo(fetched).isFile()) return fetched;
    }
    return {};
}

void ClipPrepController::setPreset(const QString& p) {
    const QString v = (p == "960w" || p == "1080p") ? p : QStringLiteral("720p");
    if (v == preset_) return;
    preset_ = v;
    try {
        settings().set("clipprep.preset", v.toStdString());
    } catch (const std::exception&) {
    }
    emit settingsChanged();
}

void ClipPrepController::setNoAudio(bool on) {
    if (on == no_audio_) return;
    no_audio_ = on;
    try {
        settings().set("clipprep.no_audio", on);
    } catch (const std::exception&) {
    }
    emit settingsChanged();
}

void ClipPrepController::setToolStatus(const QString& s) {
    tool_status_ = s;
    emit toolChanged();
}

void ClipPrepController::setStatus(const QString& s) {
    status_ = s;
    emit cutChanged();
}

bool ClipPrepController::fetchFfmpeg() {
    if (download_) return false;
    if (source_.url.isEmpty()) {
        setToolStatus(tr("ffmpeg cannot be fetched on this system (Windows and Linux x64); choose one of your own."));
        return false;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest req(source_.url);
    req.setTransferTimeout(60000);
    download_ = net_->get(req);
    connect(download_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) setToolStatus(tr("Fetching %1 … %2 %").arg(source_.name).arg(int(got * 100 / total)));
    });
    connect(download_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = download_;
        download_ = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setToolStatus(r->error() == QNetworkReply::OperationCanceledError ? tr("Stopped.")
                                                                              : tr("The download failed: %1").arg(r->errorString()));
            return;
        }
        QString error;
        const auto bin = ExtractModel(r->readAll(), source_, &error);
        if (!bin) {
            setToolStatus(tr("ffmpeg cannot be used: %1").arg(error));
            return;
        }
        const QString path = toolsDir() + "/" + source_.file_name;
        QDir().mkpath(toolsDir());
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(*bin) != bin->size() || !f.commit()) {
            setToolStatus(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        setToolStatus(tr("%1 is ready.").arg(source_.name));
    });
    setToolStatus(tr("Fetching %1 …").arg(source_.name));
    return true;
}

void ClipPrepController::cancelFetch() {
    if (download_) download_->abort();
}

bool ClipPrepController::chooseFfmpeg(const QString& pathOrUrl) {
    const QString path = LocalPath(pathOrUrl);
    if (!QFileInfo(path).isFile()) {
        setToolStatus(tr("There is no file %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    // A quick look that it is ffmpeg and runs here.
    QProcess p;
    p.start(path, {"-hide_banner", "-version"});
    const bool ran = p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (!ran || !p.readAllStandardOutput().startsWith("ffmpeg version")) {
        p.kill();
        setToolStatus(tr("That is not an ffmpeg that runs here."));
        return false;
    }
    chosen_ = path;
    try {
        settings().set("clipprep.ffmpeg", path.toStdString());
    } catch (const std::exception&) {
    }
    setToolStatus(tr("Using %1.").arg(QDir::toNativeSeparators(path)));
    return true;
}

QString ClipPrepController::suggestOutput(const QString& source, const QString& start, const QString& end) const {
    const QString path = LocalPath(source);
    const auto s = samn::ParseClipTime(start.toStdString());
    const auto e = samn::ParseClipTime(end.toStdString());
    if (path.isEmpty() || !s || !e || *e <= *s) return {};
    const QFileInfo fi(path);
    const QString name = QString::fromStdString(samn::SuggestClipName(fi.completeBaseName().toStdString(), start.toStdString(), end.toStdString()));
    return fi.dir().filePath(name);
}

bool ClipPrepController::cut(const QString& source, const QString& start, const QString& end, const QString& output) {
    if (proc_) return false;
    const QString input = LocalPath(source);
    const auto s = samn::ParseClipTime(start.toStdString());
    const auto e = samn::ParseClipTime(end.toStdString());
    if (input.isEmpty() || !QFileInfo(input).isFile()) {
        setStatus(tr("Open a video to cut first."));
        return false;
    }
    if (!s || !e) {
        setStatus(tr("Type the times as seconds (85), MM:SS (1:25) or HH:MM:SS."));
        return false;
    }
    if (*e <= *s) {
        setStatus(tr("The out point must be after the in point."));
        return false;
    }
    Job job;
    job.input = input;
    job.output = output.isEmpty() ? suggestOutput(input, start, end) : LocalPath(output);
    job.name = QFileInfo(job.output).fileName();
    job.start_s = *s;
    job.end_s = *e;
    job.max_width = samn::ClipPrepMaxWidth(preset_.toStdString());
    startJobs({job});
    return proc_ != nullptr;
}

bool ClipPrepController::cutMarks(const QString& marksFile) {
    if (proc_) return false;
    const QString path = LocalPath(marksFile);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Cannot read %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    std::string why;
    const auto marks = samn::ParseClipMarks(f.readAll().toStdString(), &why);
    if (!marks) {
        setStatus(tr("The marks file cannot be used: %1").arg(QString::fromStdString(why)));
        return false;
    }
    const QDir dir = QFileInfo(path).dir();
    const QString source = QString::fromStdString(marks->source);
    const QString input = source.isEmpty() ? QString() : QDir::cleanPath(dir.absoluteFilePath(source));
    if (input.isEmpty() || !QFileInfo(input).isFile()) {
        setStatus(tr("The marks file's video is not there: %1").arg(source.isEmpty() ? tr("(no source)") : QDir::toNativeSeparators(input)));
        return false;
    }
    QList<Job> jobs;
    for (const auto& c : marks->clips) {
        Job job;
        job.input = input;
        job.name = SafeName(QString::fromStdString(c.name));
        job.output = dir.filePath(job.name);
        job.start_s = c.start_s;
        job.end_s = c.end_s;
        job.max_width = marks->max_width;
        jobs << job;
    }
    startJobs(jobs);
    return proc_ != nullptr;
}

void ClipPrepController::startJobs(QList<Job> jobs) {
    if (!ffmpegReady()) {
        setStatus(tr("Fetch ffmpeg first (one click), or choose one of your own."));
        return;
    }
    for (const Job& j : jobs) {
        samn::ClipPrepOptions o{j.input.toStdString(), j.output.toStdString(), j.start_s, j.end_s, j.max_width, no_audio_};
        std::string why;
        if (QFileInfo(j.input).canonicalFilePath() == QFileInfo(j.output).canonicalFilePath() && QFileInfo::exists(j.output))
            why = "the cut cannot replace its own video";
        else
            samn::ClipPrepArgs(o, &why);
        if (!why.empty()) {
            setStatus(tr("%1: %2").arg(j.name, QString::fromStdString(why)));
            return;
        }
    }
    jobs_ = jobs;
    total_ = int(jobs.size());
    done_ = 0;
    progress_ = 0;
    scripts_.clear();
    cancel_asked_ = false;
    runNext();
}

void ClipPrepController::runNext() {
    if (jobs_.isEmpty()) {
        QString s = total_ == 1 ? tr("Done: %1").arg(QDir::toNativeSeparators(last_output_))
                                : tr("Done: %n clip(s) beside the marks file.", "", total_);
        if (!scripts_.isEmpty()) s += tr(" %n script(s) cut along.", "", int(scripts_.size()));
        progress_ = 100;
        endRun(s);
        return;
    }
    job_ = jobs_.takeFirst();
    samn::ClipPrepOptions o{job_.input.toStdString(), job_.output.toStdString(), job_.start_s, job_.end_s, job_.max_width, no_audio_};
    QStringList args = prefix_;
    const auto ffargs = samn::ClipPrepArgs(o);  // checked in startJobs
    for (const auto& a : *ffargs) args << QString::fromStdString(a);
    QDir().mkpath(QFileInfo(job_.output).path());
    proc_ = new QProcess(this);
    out_buffer_.clear();
    err_.clear();
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this] {
        out_buffer_ += proc_->readAllStandardOutput();
        for (int nl; (nl = int(out_buffer_.indexOf('\n'))) >= 0;) {
            onProgressLine(out_buffer_.left(nl).trimmed());
            out_buffer_.remove(0, nl + 1);
        }
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this] { err_ = (err_ + proc_->readAllStandardError()).right(4000); });
    connect(proc_, &QProcess::finished, this, &ClipPrepController::finishJob);
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !proc_) return;
        QProcess* p = proc_;
        proc_ = nullptr;
        p->deleteLater();
        jobs_.clear();
        endRun(tr("ffmpeg does not start: %1").arg(QDir::toNativeSeparators(ffmpegPath())));
    });
    setStatus(total_ == 1 ? tr("Cutting %1 …").arg(job_.name) : tr("Cutting %1 (%2 of %3) …").arg(job_.name).arg(done_ + 1).arg(total_));
    proc_->start(ffmpegPath(), args);
}

void ClipPrepController::onProgressLine(const QByteArray& line) {
    // -progress gives key=value lines; out_time_us (and the misnamed
    // out_time_ms) are microseconds into the cut.
    const int eq = int(line.indexOf('='));
    if (eq < 0) return;
    const QByteArray key = line.left(eq);
    if (key != "out_time_us" && key != "out_time_ms") return;
    bool ok = false;
    const double us = line.mid(eq + 1).toDouble(&ok);
    if (!ok || us < 0) return;
    const double within = std::clamp(us / 1e6 / std::max(0.001, job_.end_s - job_.start_s), 0.0, 1.0);
    const int p = std::clamp(int((done_ + within) * 100.0 / std::max(1, total_)), 0, 99);
    if (p != progress_) {
        progress_ = p;
        emit cutChanged();
    }
}

void ClipPrepController::finishJob(int code, QProcess::ExitStatus status) {
    QProcess* p = proc_;
    if (!p) return;
    err_ += p->readAllStandardError();
    proc_ = nullptr;
    p->deleteLater();
    if (cancel_asked_ || status != QProcess::NormalExit || code != 0 || !QFileInfo(job_.output).isFile()) {
        QFile::remove(job_.output);
        jobs_.clear();
        const QString last = LastLine(err_);
        endRun(cancel_asked_ ? tr("Stopped; the unfinished clip is removed.")
                             : tr("ffmpeg failed on %1: %2").arg(job_.name, last.isEmpty() ? tr("exit code %1").arg(code) : last));
        return;
    }
    last_output_ = job_.output;
    const QString script = cutScript(job_);
    if (!script.isEmpty()) scripts_ << script;
    ++done_;
    runNext();
}

QString ClipPrepController::cutScript(const Job& job) const {
    const QFileInfo in(job.input);
    const QString from = in.dir().filePath(in.completeBaseName() + ".funscript");
    QFile f(from);
    if (!f.open(QIODevice::ReadOnly)) return {};
    try {
        samn::Script s = samn::Parse(f.readAll().toStdString());
        const auto start = int64_t(std::llround(job.start_s * 1000));
        const auto end = int64_t(std::llround(job.end_s * 1000));
        s.actions = samn::CutScriptWindow(s.actions, start, end);
        if (s.actions.empty()) return {};
        s.duration_meta = end - start;
        const QFileInfo out(job.output);
        const QString to = out.dir().filePath(out.completeBaseName() + ".funscript");
        QSaveFile w(to);
        const std::string json = samn::Write(s);
        if (!w.open(QIODevice::WriteOnly) || w.write(json.data(), qint64(json.size())) != qint64(json.size()) || !w.commit()) return {};
        return to;
    } catch (const std::exception&) {
        return {};
    }
}

QString ClipPrepController::commandLine(const QString& source, const QString& start, const QString& end, const QString& output) const {
    const QString input = LocalPath(source);
    const auto s = samn::ParseClipTime(start.toStdString());
    const auto e = samn::ParseClipTime(end.toStdString());
    if (!s || !e) return {};
    const QString out = output.isEmpty() ? suggestOutput(input, start, end) : LocalPath(output);
    samn::ClipPrepOptions o{input.toStdString(), out.toStdString(), *s, *e, samn::ClipPrepMaxWidth(preset_.toStdString()), no_audio_};
    const auto args = samn::ClipPrepArgs(o);
    if (!args) return {};
    const auto quote = [](QString a) {
        if (!a.isEmpty() && !a.contains(QRegularExpression(QStringLiteral("[\\s\"'()]")))) return a;
        return '"' + a.replace('"', QStringLiteral("\\\"")) + '"';
    };
    QStringList parts{quote(ffmpegReady() && program_.isEmpty() ? QDir::toNativeSeparators(ffmpegPath()) : QStringLiteral("ffmpeg"))};
    // The progress pipe is for the app; a console shows its own.
    for (size_t i = 0; i < args->size(); ++i) {
        const std::string& a = (*args)[i];
        if (a == "-nostdin") continue;
        if (a == "-progress") {
            ++i;
            continue;
        }
        const QString q = QString::fromStdString(a);
        parts << quote(q == input || q == out ? QDir::toNativeSeparators(q) : q);
    }
    return parts.join(' ');
}

bool ClipPrepController::copyCommand(const QString& source, const QString& start, const QString& end, const QString& output) {
    const QString cmd = commandLine(source, start, end, output);
    if (cmd.isEmpty()) return false;
    if (auto* clip = QGuiApplication::clipboard()) clip->setText(cmd);
    setStatus(tr("The command is on the clipboard."));
    return true;
}

QString ClipPrepController::localPath(const QString& pathOrUrl) const { return LocalPath(pathOrUrl); }

void ClipPrepController::cancel() {
    if (!proc_) return;
    cancel_asked_ = true;
    proc_->kill();
}

void ClipPrepController::endRun(const QString& status) {
    status_ = status;
    emit cutChanged();
}

}  // namespace samn::ui
