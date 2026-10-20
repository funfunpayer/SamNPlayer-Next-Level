#include "label_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "audio_level.h"
#include "frame_cache.h"
#include "samn/engine/label_tracking.h"
#include "samn/phase_rules.h"
#include "samn/settings.h"
#include "samn/teacher.h"
#include "samn/teacher_votes.h"
#include "samn/vision/picture_signals.h"
#include "samn/vision/frame_reader.h"
#include "samn/vision/onnx_detector.h"

#include <opencv2/core.hpp>

namespace samn::ui {

namespace {
QString LocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
constexpr int kSoundGraceMs = 4000;

std::filesystem::path Fs(const QString& p) { return std::filesystem::path(p.toStdU16String()); }
constexpr size_t kUndoMax = 200;
}  // namespace

LabelController::LabelController(QObject* parent) : QObject(parent) {
    try {
        const auto text = [this](const char* key) {
            const auto v = settings().get(key, "");
            return v.is_string() ? QString::fromStdString(v.get<std::string>()) : QString();
        };
        teacher_url_ = text("aitrain.teacher.url");
        teacher_model_ = text("aitrain.teacher.model");
        teacher_key_ = text("aitrain.teacher.key");
        if (text("aitrain.teacher.kind") == QLatin1String("nudenet")) teacher_kind_ = QStringLiteral("nudenet");
        if (const auto v = settings().get("aitrain.retrack_after_drag", true); v.is_boolean()) retrack_after_drag_ = v.get<bool>();
        nudenet_path_ = text("aitrain.nudenet.path");
    } catch (const std::exception&) {
        // Unreadable settings: start without a teacher.
    }
}

samn::Settings& LabelController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

void LabelController::saveSetting(const char* key, const QString& value) {
    try {
        settings().set(key, value.toStdString());
    } catch (const std::exception&) {
    }
}

void LabelController::setTeacherUrl(const QString& url) {
    QString u = url.trimmed();
    while (u.endsWith('/')) u.chop(1);
    if (u == teacher_url_) return;
    teacher_url_ = u;
    saveSetting("aitrain.teacher.url", u);
    emit teacherChanged();
}

void LabelController::setTeacherModel(const QString& model) {
    if (model.trimmed() == teacher_model_) return;
    teacher_model_ = model.trimmed();
    saveSetting("aitrain.teacher.model", teacher_model_);
    emit teacherChanged();
}

void LabelController::setTeacherKey(const QString& key) {
    if (key.trimmed() == teacher_key_) return;
    teacher_key_ = key.trimmed();
    saveSetting("aitrain.teacher.key", teacher_key_);
    emit teacherChanged();
}

void LabelController::setTeacherStatus(const QString& s) {
    teacher_status_ = s;
    emit teacherChanged();
    setStatus(s);  // the editor's line shows it too
}

QNetworkReply* LabelController::teacherRequest(const QString& path, const QByteArray& body) {
    const QUrl url(teacher_url_ + path);
    if (teacher_url_.isEmpty() || !url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
        setTeacherStatus(tr("Enter the server's address, e.g. http://192.168.1.20:11434/v1 (Ollama) or :1234/v1 (LM Studio)."));
        return nullptr;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setTransferTimeout(kTeacherTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!teacher_key_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + teacher_key_.toUtf8());
    return body.isEmpty() ? net_->get(req) : net_->post(req, body);
}

bool LabelController::testTeacher() {
    if (teacher_reply_) return false;
    QNetworkReply* r = teacherRequest(QStringLiteral("/models"), {});
    if (!r) return false;
    teacher_reply_ = r;
    setTeacherStatus(tr("Asking %1 …").arg(teacher_url_));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        teacher_reply_ = nullptr;
        if (r->error() != QNetworkReply::NoError) {
            setTeacherStatus(tr("No answer: %1").arg(r->errorString()));
            return;
        }
        QStringList models;
        for (const auto& m : samn::ParseModelList(r->readAll().toStdString())) models << QString::fromStdString(m);
        teacher_models_ = models;
        if (teacher_model_.isEmpty() && !models.isEmpty()) setTeacherModel(models.front());
        setTeacherStatus(models.isEmpty() ? tr("The server answers but lists no models.")
                                          : tr("Connected: %n model(s).", "", int(models.size())));
    });
    return true;
}

std::vector<std::string> LabelController::teacherClasses() const {
    std::vector<std::string> classes;
    for (const auto& c : project_.Classes())
        if (c != "contact" && !samn::IsExcludeClass(c)) classes.push_back(c);
    if (classes.empty())
        for (const auto& c : presetLabels())
            if (c != "contact" && c != QLatin1String(samn::kExcludeClass)) classes.push_back(c.toStdString());
    return classes;
}

bool LabelController::startServer(const QImage& frame, const std::vector<std::string>& classes, BoxesDone done) {
    if (teacher_model_.isEmpty()) {
        setTeacherStatus(tr("Choose a model first (Test connection lists them)."));
        return false;
    }
    // The frame at most 1024 px; the boxes come back in its fractions.
    QImage img = frame;
    if (std::max(img.width(), img.height()) > 1024) img = img.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPG", 88);
    const std::string body =
        samn::TeacherRequestBody(teacher_model_.toStdString(), samn::TeacherPrompt(classes), jpeg.toBase64().toStdString());
    QNetworkReply* r = teacherRequest(QStringLiteral("/chat/completions"), QByteArray::fromStdString(body));
    if (!r) return false;
    teacher_reply_ = r;
    const double w = video_size_.width() > 0 ? video_size_.width() : frame.width();
    const double h = video_size_.height() > 0 ? video_size_.height() : frame.height();
    connect(r, &QNetworkReply::finished, this, [this, r, w, h, classes, done] {
        r->deleteLater();
        teacher_reply_ = nullptr;
        const QByteArray data = r->readAll();
        std::string error;
        const auto content = samn::ChatContent(data.toStdString(), &error);
        if (!content) {
            done(std::nullopt, r->error() != QNetworkReply::NoError && error.empty() ? r->errorString()
                                                                                     : QString::fromStdString(error));
            return;
        }
        done(samn::ParseTeacherBoxes(*content, w, h, classes), {});
    });
    emit teacherChanged();
    return true;
}

bool LabelController::startLocal(const QImage& frame, const std::vector<std::string>& classes, BoxesDone done) {
    if (!localTeacherAvailable()) {
        setTeacherStatus(tr("This build cannot run models itself (no ONNX Runtime)."));
        return false;
    }
    const QString path = nudenetPath();
    if (path.isEmpty()) {
        setTeacherStatus(tr("Fetch NudeNet first (Teachers and steps)."));
        return false;
    }
    const QImage bgr = frame.convertToFormat(QImage::Format_BGR888);
    cv::Mat mat = cv::Mat(bgr.height(), bgr.width(), CV_8UC3, const_cast<uchar*>(bgr.constBits()),
                          size_t(bgr.bytesPerLine())).clone();
    // The frame may be smaller than the video: boxes go back in video pixels.
    const double sx = video_size_.width() > 0 ? double(video_size_.width()) / bgr.width() : 1.0;
    const double sy = video_size_.height() > 0 ? double(video_size_.height()) / bgr.height() : 1.0;
    if (!detector_) detector_ = std::make_shared<samn::vision::OnnxDetector>();
    if (local_worker_.joinable()) local_worker_.join();
    local_busy_ = true;
    QPointer<LabelController> self(this);
    local_worker_ = std::thread([self, det = detector_, path = Fs(path), mat = std::move(mat), sx, sy, classes, done] {
        std::string why;
        std::vector<samn::TeacherBox> boxes;
        if (det->loaded() || det->Load(path, &why)) {
            for (const auto& d : det->Detect(mat, 0.3f, &why)) {
                const auto& names = samn::vision::NudeNetClasses();
                if (d.cls < 0 || size_t(d.cls) >= names.size()) continue;
                const std::string label = samn::vision::NudeNetToLabel(names[size_t(d.cls)]);
                if (label.empty() || std::find(classes.begin(), classes.end(), label) == classes.end()) continue;
                boxes.push_back({label, {d.box.x * sx, d.box.y * sy, d.box.width * sx, d.box.height * sy}});
            }
        }
        QMetaObject::invokeMethod(qApp, [self, boxes, done, error = QString::fromStdString(why)] {
            if (!self) return;
            self->local_busy_ = false;
            emit self->teacherChanged();
            if (!error.isEmpty() && boxes.empty()) done(std::nullopt, error);
            else done(boxes, {});
        }, Qt::QueuedConnection);
    });
    emit teacherChanged();
    return true;
}

std::vector<std::string> LabelController::localClasses(const std::vector<std::string>& asked) const {
    std::vector<std::string> out;
    for (const auto& c : samn::vision::NudeNetClasses()) {
        const std::string label = samn::vision::NudeNetToLabel(c);
        if (!label.empty() && std::find(asked.begin(), asked.end(), label) != asked.end() &&
            std::find(out.begin(), out.end(), label) == out.end())
            out.push_back(label);
    }
    return out;
}

void LabelController::applyProposals(int64_t ms, const std::vector<samn::TeacherBox>& boxes, const std::string& source,
                                     const QString& who) {
    if (boxes.empty()) {
        setTeacherStatus(tr("%1 sees nothing it was asked for here.").arg(who));
        return;
    }
    int set = 0;
    change(tr("Proposals from %1").arg(who), [&](samn::LabelProject& p) {
        set = samn::ApplyTeacherBoxes(p, ms, boxes, source, tolerance());
        return set > 0;
    });
    setTeacherStatus(set > 0 ? tr("%n proposal(s) from %1: check them, drag or confirm.", "", set).arg(who)
                             : tr("%1's boxes are all where you already confirmed one.").arg(who));
}

bool LabelController::askTeacher() {
    if (teacherBusy() || !hasVideo() || frame_.isNull()) return false;
    const int64_t ms = int64_t(std::llround(preview_ms_));
    const QString at = QString::number(ms / 1000.0, 'f', 1) + " s";
    const bool local = teacher_kind_ == QLatin1String("nudenet");
    const QString who = local ? QStringLiteral("NudeNet") : teacher_model_;
    const std::string source = local ? "teacher:nudenet" : "teacher:" + teacher_model_.toStdString();
    const auto done = [this, ms, who, source](std::optional<std::vector<samn::TeacherBox>> boxes, const QString& error) {
        if (!boxes) setTeacherStatus(tr("No answer from %1: %2").arg(who, error));
        else applyProposals(ms, *boxes, source, who);
    };
    const bool started = local ? startLocal(frame_, teacherClasses(), done) : startServer(frame_, teacherClasses(), done);
    if (started) setTeacherStatus(tr("%1 is looking at %2 …").arg(who, at));
    return started;
}

// Both teachers at once on one frame (D-155 step 4, votes): done gets the
// votes of the teachers that answered and the errors of the others.
bool LabelController::startVote(const QImage& frame, VoteDone done, bool skipServer, bool skipLocal) {
    const std::vector<std::string> classes = teacherClasses();
    const bool server = !skipServer && !teacher_url_.isEmpty() && !teacher_model_.isEmpty();
    const bool local = !skipLocal && localTeacherAvailable() && !nudenetPath().isEmpty();
    if (!server && !local) {
        setTeacherStatus(tr("Set up a teacher first: NudeNet here or a model server (Teachers and steps)."));
        return false;
    }
    struct State {
        int pending = 0;
        std::vector<samn::TeacherVote> votes;
        QStringList errors;
    };
    auto state = std::make_shared<State>();
    const auto answer = [state, done](const std::string& teacher, const std::vector<std::string>& sees) {
        return [state, done, teacher, sees](std::optional<std::vector<samn::TeacherBox>> boxes, const QString& error) {
            if (boxes) state->votes.push_back({teacher, sees, *boxes});
            else state->errors << QString::fromStdString(teacher) + ": " + (error.isEmpty() ? tr("no answer") : error);
            if (--state->pending == 0) done(state->votes, state->errors);
        };
    };
    // Counted before either starts: a teacher may answer before the other is asked.
    state->pending = int(server) + int(local);
    if (server && !startServer(frame, classes, answer(teacher_model_.toStdString(), classes))) --state->pending;
    if (local && !startLocal(frame, classes, answer("nudenet", localClasses(classes)))) --state->pending;
    return state->pending > 0;
}

bool LabelController::askAllTeachers() {
    if (teacherBusy() || !hasVideo() || frame_.isNull()) return false;
    const int64_t ms = int64_t(std::llround(preview_ms_));
    const bool started = startVote(frame_, [this, ms](const std::vector<samn::TeacherVote>& votes, const QStringList& errors) {
        if (votes.empty()) {
            setTeacherStatus(tr("No teacher answered: %1").arg(errors.join("; ")));
            return;
        }
        std::string names;
        for (const auto& v : votes) names += (names.empty() ? "" : "+") + v.teacher;
        const auto voted = samn::VoteTeacherBoxes(votes);
        int set = 0, unsure = 0;
        if (!voted.empty())
            change(tr("Teachers' vote"), [&](samn::LabelProject& p) {
                set = samn::ApplyTeacherBoxes(p, ms, samn::VotedBoxes(voted), "teachers:" + names, tolerance());
                return set > 0;
            });
        for (const auto& v : voted) unsure += v.box.uncertain ? 1 : 0;
        QString msg = voted.empty() ? tr("The teachers see nothing they were asked for here.")
                                    : tr("%n proposal(s) from %1 teacher(s), %2 uncertain (N jumps there).", "", set)
                                          .arg(votes.size())
                                          .arg(unsure);
        if (!errors.isEmpty()) msg += ' ' + tr("Not answered: %1").arg(errors.join("; "));
        setTeacherStatus(msg);
    });
    if (started) setTeacherStatus(tr("The teachers are looking at %1 …").arg(QString::number(ms / 1000.0, 'f', 1) + " s"));
    return started;
}

bool LabelController::teachClip(double stepMs) {
    if (teacherBusy() || !hasVideo()) return false;
    stepMs = std::max(200.0, stepMs);
    clip_times_.clear();
    for (double t = 0; t <= duration_ms_; t += stepMs) clip_times_.push_back(t);
    if (clip_times_.empty()) clip_times_.push_back(0);
    clip_index_ = 0;
    clip_results_.clear();
    clip_errors_.clear();
    clip_skip_server_ = clip_skip_local_ = false;
    clip_teaching_ = true;
    clip_cancel_ = false;
    emit teacherChanged();
    teachNextFrame();
    return clip_teaching_;
}

void LabelController::cancelTeachClip() {
    if (clip_teaching_) clip_cancel_ = true;
}

void LabelController::teachNextFrame() {
    if (!clip_teaching_) return;
    if (clip_cancel_ || clip_index_ >= clip_times_.size()) {
        finishTeachClip();
        return;
    }
    const FrameCache::Frame f = FrameCache::shared().At(video_path_, clip_times_[clip_index_]);
    if (f.image.isNull()) {  // nothing to see there: go on
        ++clip_index_;
        QTimer::singleShot(0, this, [this] { teachNextFrame(); });
        return;
    }
    const int64_t ms = int64_t(std::llround(f.ms));
    setTeacherStatus(tr("The teachers go through the clip: %1 of %2 …").arg(clip_index_ + 1).arg(clip_times_.size()));
    const bool started = startVote(f.image, [this, ms](const std::vector<samn::TeacherVote>& votes, const QStringList& errors) {
        for (const auto& v : votes)
            if (std::find(clip_teachers_.begin(), clip_teachers_.end(), v.teacher) == clip_teachers_.end())
                clip_teachers_.push_back(v.teacher);
        if (!votes.empty()) clip_results_.push_back({ms, samn::VoteTeacherBoxes(votes)});
        // A teacher that failed once sits out the rest of the run: an
        // unreachable server would otherwise cost its timeout at every frame.
        for (const auto& e : errors) {
            if (e.startsWith(QLatin1String("nudenet: "))) clip_skip_local_ = true;
            else clip_skip_server_ = true;
            if (!clip_errors_.contains(e)) clip_errors_ << e;
        }
        ++clip_index_;
        emit teacherChanged();
        QTimer::singleShot(0, this, [this] { teachNextFrame(); });
    }, clip_skip_server_, clip_skip_local_);
    if (!started) finishTeachClip();
}

void LabelController::finishTeachClip() {
    clip_teaching_ = false;
    const bool canceled = clip_cancel_;
    std::string names;
    for (const auto& t : clip_teachers_) names += (names.empty() ? "" : "+") + t;
    int set = 0, unsure = 0;
    if (!clip_results_.empty())
        change(tr("Teachers through the clip"), [&](samn::LabelProject& p) {
            for (const auto& [ms, voted] : clip_results_) {
                set += samn::ApplyTeacherBoxes(p, ms, samn::VotedBoxes(voted), "teachers:" + names, tolerance());
                for (const auto& v : voted) unsure += v.box.uncertain ? 1 : 0;
            }
            return set > 0;
        });
    QString msg = clip_results_.empty() && !clip_errors_.isEmpty()
                      ? tr("No teacher answered: %1").arg(clip_errors_.join("; "))
                      : tr("%n proposal(s) at %1 frame(s), %2 uncertain (N jumps there).", "", set)
                            .arg(clip_results_.size())
                            .arg(unsure);
    if (canceled) msg = tr("Stopped.") + ' ' + msg;
    if (!clip_errors_.isEmpty() && !clip_results_.empty()) msg += ' ' + tr("Not always answered: %1").arg(clip_errors_.join("; "));
    clip_results_.clear();
    clip_teachers_.clear();
    clip_errors_.clear();
    setTeacherStatus(msg);
}

void LabelController::setTeacherKind(const QString& kind) {
    const QString k = kind == QLatin1String("nudenet") ? kind : QStringLiteral("server");
    if (k == teacher_kind_) return;
    teacher_kind_ = k;
    saveSetting("aitrain.teacher.kind", k);
    emit teacherChanged();
}

bool LabelController::localTeacherAvailable() const { return samn::vision::OnnxDetector::Available(); }

QString LabelController::nudenetPath() const {
    if (!nudenet_path_.isEmpty() && QFileInfo::exists(nudenet_path_)) return nudenet_path_;
    return {};
}

bool LabelController::downloadNudeNet() {
    if (download_reply_) return false;
    if (!localTeacherAvailable()) {
        setTeacherStatus(tr("This build cannot run models itself (no ONNX Runtime)."));
        return false;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest req(nudenet_source_.url);
    req.setTransferTimeout(60000);  // a minute without data gives up
    QNetworkReply* r = net_->get(req);
    download_reply_ = r;
    model_download_ = 0;
    setTeacherStatus(tr("Fetching %1 …").arg(nudenet_source_.name));
    constexpr qint64 kMaxBytes = 64LL * 1024 * 1024;
    connect(r, &QNetworkReply::downloadProgress, this, [this, r](qint64 got, qint64 total) {
        if (got > kMaxBytes) {
            r->abort();
            return;
        }
        const int p = total > 0 ? int(got * 100 / total) : 0;
        if (p != model_download_) {
            model_download_ = p;
            emit teacherChanged();
        }
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        download_reply_ = nullptr;
        model_download_ = -1;
        if (r->error() != QNetworkReply::NoError) {
            setTeacherStatus(r->error() == QNetworkReply::OperationCanceledError
                                 ? tr("The download was stopped.")
                                 : tr("The download failed: %1").arg(r->errorString()));
            return;
        }
        QString error;
        const auto model = ExtractModel(r->readAll(), nudenet_source_, &error);
        if (!model) {
            setTeacherStatus(tr("The download cannot be used: %1").arg(error));
            return;
        }
        const QString dir = QString::fromStdU16String((samn::config_dir() / "models").u16string());
        QDir().mkpath(dir);
        const QString path = QDir(dir).filePath(nudenet_source_.file_name);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(*model) != model->size() || !f.commit()) {
            setTeacherStatus(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        nudenet_path_ = path;
        saveSetting("aitrain.nudenet.path", path);
        detector_.reset();
        teacher_kind_ = QStringLiteral("nudenet");
        saveSetting("aitrain.teacher.kind", teacher_kind_);
        setTeacherStatus(tr("%1 is ready: Ask the teacher (A) uses it.").arg(nudenet_source_.name));
    });
    emit teacherChanged();
    return true;
}

void LabelController::cancelModelDownload() {
    if (download_reply_) download_reply_->abort();
}

bool LabelController::chooseNudeNet(const QString& pathOrUrl) {
    if (local_busy_) return false;
    const QString path = LocalPath(pathOrUrl);
    auto det = std::make_shared<samn::vision::OnnxDetector>();
    std::string why;
    if (!det->Load(Fs(path), &why)) {
        setTeacherStatus(tr("That model does not load: %1").arg(QString::fromStdString(why)));
        return false;
    }
    if (det->classCount() != int(samn::vision::NudeNetClasses().size())) {
        setTeacherStatus(tr("That is not NudeNet: it knows %n class(es), NudeNet has 18.", "", det->classCount()));
        return false;
    }
    nudenet_path_ = path;
    saveSetting("aitrain.nudenet.path", path);
    detector_ = det;
    teacher_kind_ = QStringLiteral("nudenet");
    saveSetting("aitrain.teacher.kind", teacher_kind_);
    setTeacherStatus(tr("NudeNet is ready: Ask the teacher (A) uses it."));
    return true;
}

LabelController::~LabelController() {
    if (local_worker_.joinable()) local_worker_.join();
    if (download_reply_) {
        download_reply_->disconnect(this);
        download_reply_->abort();
    }
    if (suggest_cancel_) *suggest_cancel_ = true;
    if (suggest_worker_.joinable()) suggest_worker_.join();
    if (track_cancel_) *track_cancel_ = true;
    if (track_worker_.joinable()) track_worker_.join();
    if (teacher_reply_) {
        teacher_reply_->disconnect(this);  // no answer handling while going away
        teacher_reply_->abort();
    }
}

QString LabelController::videoName() const { return QFileInfo(video_path_).fileName(); }

QUrl LabelController::videoUrl() const { return video_path_.isEmpty() ? QUrl() : QUrl::fromLocalFile(video_path_); }

QStringList LabelController::presetLabels() const {
    // 0.7's body parts as classes (one class, several objects), and the
    // contact point where the stroke touches (0.8 teacher plan), and the
    // exclusion zones the tracker must not follow ("not this").
    return {"glans", "penis", "mouth", "hand", "breast", "nipple", "vagina", "face", "contact",
            QString::fromLatin1(samn::kExcludeClass)};
}

QStringList LabelController::labels() const {
    QStringList out;
    for (const auto& t : project_.tracks) out << QString::fromStdString(t.label());
    return out;
}

void LabelController::setCurrentLabel(const QString& label) {
    if (label == current_) return;
    current_ = label;
    emit currentLabelChanged();
}

bool LabelController::openVideo(const QString& pathOrUrl) {
    cancelSuggest();
    cancelTrack();
    const QString path = LocalPath(pathOrUrl);
    if (!QFileInfo(path).isFile()) {
        setStatus(tr("Not found: %1").arg(path));
        return false;
    }
    QImageReader image(path);
    if (image.canRead()) {
        still_ = true;
        const QImage img = image.read();
        if (img.isNull()) {
            setStatus(tr("Cannot read the image."));
            return false;
        }
        frame_ = img.convertToFormat(QImage::Format_RGB888);
        video_size_ = frame_.size();
        duration_ms_ = 0;
        frame_ms_ = 40;
    } else {
        std::string error;
        auto reader = vision::FrameReader::Open(Fs(path), &error);
        if (!reader) {
            setStatus(tr("Cannot open the video: %1").arg(QString::fromStdString(error)));
            return false;
        }
        still_ = false;
        video_size_ = QSize(reader->size().width, reader->size().height);
        const double fps = reader->fps() > 0 ? reader->fps() : 25;
        frame_ms_ = 1000.0 / fps;
        duration_ms_ = reader->frameCount() > 0 ? (reader->frameCount() - 1) * frame_ms_ : 0;
    }
    video_path_ = path;
    project_ = {};
    undo_.clear();
    redo_.clear();
    const auto file = samn::LabelProjectPath(Fs(path));
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        try {
            project_ = samn::LoadLabelProject(file);
        } catch (const std::exception& e) {
            setStatus(tr("The labels file could not be read (%1); starting empty.").arg(QString::fromUtf8(e.what())));
        }
    }
    project_.video = QFileInfo(path).fileName().toStdString();
    project_.duration_ms = int64_t(duration_ms_);
    current_ = project_.tracks.empty() ? QString() : QString::fromStdString(project_.tracks.front().label());
    emit videoChanged();
    emit tracksChanged();
    emit currentLabelChanged();
    preview_ms_ = -1;
    seek(0);
    if (status_.isEmpty() || !status_.startsWith(tr("The labels file")))
        setStatus(project_.tracks.empty() ? tr("Add a label, then drag its box on the frame.") : tr("Labels loaded."));
    return true;
}

void LabelController::seek(double ms) {
    if (video_path_.isEmpty()) return;
    if (still_) {
        preview_ms_ = 0;
        emit previewChanged();
        return;
    }
    ms = std::clamp(ms, 0.0, duration_ms_);
    const FrameCache::Frame f = FrameCache::shared().At(video_path_, ms);
    if (!f.image.isNull()) frame_ = f.image;
    preview_ms_ = f.image.isNull() ? ms : std::min(f.ms, duration_ms_);
    emit previewChanged();
}

void LabelController::step(int frames) { seek(preview_ms_ + frames * frame_ms_); }

bool LabelController::addLabel(const QString& cls) {
    const QString name = cls.trimmed().toLower().replace(' ', '_');
    if (name.isEmpty() || !hasVideo()) return false;
    QString added;
    change(tr("Add %1").arg(name), [&](samn::LabelProject& p) {
        added = QString::fromStdString(p.AddObject(name.toStdString()).label());
        return true;
    });
    setCurrentLabel(added);
    return true;
}

bool LabelController::setBox(const QRectF& box) {
    if (current_.isEmpty() || !hasVideo()) return false;
    const QRectF r = box.normalized().intersected(QRectF(QPointF(0, 0), QSizeF(video_size_)));
    if (r.width() < 4 || r.height() < 4) return false;
    bool done = false;
    change(tr("Box for %1").arg(current_), [&](samn::LabelProject& p) {
        samn::LabelKey k;
        k.ms = int64_t(std::llround(preview_ms_));
        k.box = {r.x(), r.y(), r.width(), r.height()};
        k.confirmed = true;
        k.source = "user";
        p.AddTrack(current_.toStdString()).SetKey(k, tolerance());
        return done = true;
    });
    // The stretches on both sides of the new box follow it (Owner 20 Oct:
    // at once after the drag, can be switched off).
    if (done && retrack_after_drag_ && !still_)
        if (const auto* t = project_.Track(current_.toStdString())) {
            const auto gaps = gapsOf(*t, int64_t(std::llround(preview_ms_)));
            if (!gaps.empty()) startTracking(current_, gaps, true);
        }
    return done;
}

std::vector<LabelController::Gap> LabelController::gapsOf(const samn::LabelTrack& t, std::optional<int64_t> around) const {
    // Each stretch between two confirmed boxes is one run; with `around`,
    // only the stretches that end or start at that key.
    std::vector<Gap> gaps;
    const samn::LabelKey* prev = nullptr;
    const int64_t near = tolerance();
    for (const auto& k : t.keys()) {
        if (!k.confirmed) continue;
        if (prev && !prev->absent && !k.absent && k.ms - prev->ms > int64_t(frame_ms_ * 2) &&
            (!around || std::llabs(prev->ms - *around) <= near || std::llabs(k.ms - *around) <= near))
            gaps.push_back({prev->ms, k.ms, prev->box, k.box});
        prev = &k;
    }
    return gaps;
}

void LabelController::setRetrackAfterDrag(bool on) {
    if (on == retrack_after_drag_) return;
    retrack_after_drag_ = on;
    try {
        settings().set("aitrain.retrack_after_drag", on);
    } catch (const std::exception&) {
    }
    emit trackingChanged();
}

bool LabelController::markAbsent(bool covered) {
    if (current_.isEmpty() || !hasVideo()) return false;
    bool done = false;
    change(covered ? tr("%1 covered").arg(current_) : tr("%1 not in the picture").arg(current_), [&](samn::LabelProject& p) {
        samn::LabelKey k;
        k.ms = int64_t(std::llround(preview_ms_));
        k.absent = true;
        k.covered = covered;
        k.confirmed = true;
        p.AddTrack(current_.toStdString()).SetKey(k, tolerance());
        return done = true;
    });
    return done;
}

bool LabelController::removeKey() {
    if (current_.isEmpty()) return false;
    bool done = false;
    change(tr("Remove the key of %1").arg(current_), [&](samn::LabelProject& p) {
        auto* t = p.Track(current_.toStdString());
        return done = t && t->RemoveKey(int64_t(std::llround(preview_ms_)), tolerance());
    });
    return done;
}

int LabelController::confirmAll() {
    int n = 0;
    change(tr("Confirm %1").arg(current_), [&](samn::LabelProject& p) {
        auto* t = p.Track(current_.toStdString());
        n = t ? t->Confirm(0, std::numeric_limits<int64_t>::max()) : 0;
        return n > 0;
    });
    return n;
}

QVariantList LabelController::phases() const {
    QVariantList out;
    for (const auto& p : project_.phases)
        out << QVariantMap{{"startMs", double(p.start_ms)},
                           {"endMs", double(p.end_ms)},
                           {"phase", QString::fromStdString(p.name)},
                           {"confirmed", p.confirmed},
                           {"source", QString::fromStdString(p.source)}};
    return out;
}

QStringList LabelController::phaseNames() const {
    QStringList out;
    for (const auto& n : samn::PhaseNames()) out << QString::fromStdString(n);
    return out;
}

QString LabelController::phaseAtPreview() const {
    const auto* p = project_.PhaseAt(int64_t(std::llround(preview_ms_)));
    return p ? QString::fromStdString(p->name) : QString();
}

bool LabelController::setPhase(double startMs, double endMs, const QString& phase) {
    if (!hasVideo()) return false;
    const double end = still_ ? std::max(endMs, startMs + 1) : std::min(endMs, duration_ms_);
    const int64_t a = int64_t(std::llround(std::max(0.0, startMs))), b = int64_t(std::llround(end));
    if (b <= a) return false;
    change(phase.isEmpty() ? tr("Phase cleared") : tr("Phase: %1").arg(phase), [&](samn::LabelProject& p) {
        p.SetPhase(a, b, phase.toStdString());
        return true;
    });
    return true;
}

int LabelController::confirmPhases() {
    int n = 0;
    change(tr("Phases confirmed"), [&](samn::LabelProject& p) {
        for (auto& ph : p.phases)
            if (!ph.confirmed) {
                ph.confirmed = true;
                ++n;
            }
        if (n == 0) return false;
        const auto copy = p.phases;
        p.phases.clear();
        for (const auto& ph : copy) p.SetPhase(ph.start_ms, ph.end_ms, ph.name, true, ph.source);
        return true;
    });
    return n;
}

bool LabelController::trackCurrent() {
    if (!hasVideo() || current_.isEmpty()) return false;  // a run still going is replaced
    if (still_) {
        setStatus(tr("Tracking needs a clip, not a still picture."));
        return false;
    }
    const auto* t = project_.Track(current_.toStdString());
    if (!t) return false;
    const std::vector<Gap> gaps = gapsOf(*t);
    if (gaps.empty()) {
        setStatus(tr("Set the box at two frames first; the tracker fills what lies between."));
        return false;
    }
    return startTracking(current_, gaps, false);
}

bool LabelController::startTracking(const QString& label, const std::vector<Gap>& gaps, bool automatic) {
    // A new run replaces one still going (a drag while the last one tracks).
    if (track_cancel_) *track_cancel_ = true;
    if (track_worker_.joinable()) track_worker_.join();
    track_cancel_ = std::make_shared<std::atomic<bool>>(false);
    tracking_ = true;
    track_progress_ = 0;
    emit trackingChanged();
    const int run = ++track_run_;
    setStatus(automatic ? tr("Tracking %1 to follow your box…").arg(label) : tr("Tracking %1…").arg(label));
    QPointer<LabelController> self(this);
    track_worker_ = std::thread([self, gaps, label, run, cancel = track_cancel_, video = Fs(video_path_)] {
        std::vector<std::vector<samn::LabelKey>> keys;
        QString error;
        for (size_t g = 0; g < gaps.size() && error.isEmpty(); ++g) {
            samn::engine::BetweenKeysOptions o;
            o.cancel = [cancel] { return cancel->load(); };
            o.progress = [self, g, n = gaps.size()](int p) {
                const int total = int((g * 100 + size_t(p)) / n);
                QMetaObject::invokeMethod(qApp, [self, total] {
                    if (self && self->tracking_) {
                        self->track_progress_ = total;
                        emit self->trackingChanged();
                    }
                }, Qt::QueuedConnection);
            };
            std::string why;
            std::optional<samn::engine::BetweenKeysResult> r;
            try {
                r = samn::engine::TrackBetweenKeys(video, gaps[g].from_ms, gaps[g].from_box, gaps[g].to_ms, gaps[g].to_box, o, &why);
            } catch (const std::exception& e) {
                why = e.what();
            }
            if (!r) error = QString::fromStdString(why);
            else keys.push_back(samn::BlendTracks(r->forward, r->backward, gaps[g].from_ms, gaps[g].to_ms));
        }
        QMetaObject::invokeMethod(qApp, [self, run, label, gaps, keys = std::move(keys), error] {
            if (self && run == self->track_run_) self->onTracked(label, gaps, keys, error);
        }, Qt::QueuedConnection);
    });
    return true;
}

void LabelController::cancelTrack() {
    if (!tracking_) return;
    if (track_cancel_) *track_cancel_ = true;
    tracking_ = false;
    emit trackingChanged();
    setStatus(tr("Tracking canceled."));
}

void LabelController::onTracked(const QString& label, const std::vector<Gap>& gaps,
                                const std::vector<std::vector<samn::LabelKey>>& keys, const QString& error) {
    if (!tracking_) return;  // canceled meanwhile
    tracking_ = false;
    track_progress_ = 100;
    emit trackingChanged();
    if (!error.isEmpty() && keys.empty()) {
        setStatus(error == QLatin1String("canceled") ? tr("Tracking canceled.") : tr("Tracking: %1").arg(error));
        return;
    }
    int added = 0, unsure = 0;
    change(tr("Tracked %1").arg(label), [&](samn::LabelProject& p) {
        auto* t = p.Track(label.toStdString());
        if (!t) return false;
        for (size_t g = 0; g < keys.size(); ++g) {
            // Undone or moved meanwhile: a stretch whose ends are gone stays as it is.
            const samn::LabelKey* a = t->KeyAt(gaps[g].from_ms);
            const samn::LabelKey* b = t->KeyAt(gaps[g].to_ms);
            if (!a || !b || !a->confirmed || !b->confirmed || a->absent || b->absent) continue;
            added += samn::FillBetweenKeys(*t, gaps[g].from_ms, gaps[g].to_ms, keys[g]);
            for (const auto& k : keys[g]) unsure += k.uncertain ? 1 : 0;
        }
        return added > 0;
    });
    QString text = tr("%1: %n tracked key(s)", "", added).arg(label);
    if (unsure > 0) text += tr(", %n uncertain: press N to check them", "", unsure);
    if (!error.isEmpty()) text += tr(" (stopped: %1)").arg(error);
    setStatus(text);
}

bool LabelController::nextUncertain() {
    const int64_t now = int64_t(std::llround(preview_ms_));
    auto at = samn::NextUncertain(project_, now, current_.toStdString());
    if (!at) at = samn::NextUncertain(project_, now);
    if (!at) {
        setStatus(tr("Nothing uncertain after here."));
        return false;
    }
    // The label of that key becomes the current one.
    for (const auto& t : project_.tracks)
        if (const auto* k = t.KeyAt(*at); k && k->uncertain && !k->confirmed) {
            setCurrentLabel(QString::fromStdString(t.label()));
            break;
        }
    seek(double(*at));
    setStatus(tr("Uncertain here: drag the box where it belongs, or confirm it."));
    return true;
}

bool LabelController::suggestPhases() {
    if (!hasVideo() || suggesting_) return false;
    if (still_) {
        setStatus(tr("Phases need a clip, not a still picture."));
        return false;
    }
    if (suggest_worker_.joinable()) suggest_worker_.join();
    suggest_cancel_ = std::make_shared<std::atomic<bool>>(false);
    suggesting_ = true;
    suggest_progress_ = 0;
    picture_done_ = audio_done_ = false;
    luma_.clear();
    motion_.clear();
    loudness_.clear();
    emit suggestingChanged();
    setStatus(tr("Looking for phases…"));

    // The sound: loudness per 50 ms; no sound is fine.
    if (!audio_) {
        audio_ = new AudioLevelDecoder(this);
        connect(audio_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
            if (!suggesting_ || audio_done_) return;
            loudness_ = env;
            audio_done_ = true;
            finishSuggest();
        });
        connect(audio_, &AudioLevelDecoder::failed, this, [this](const QString&) {
            if (!suggesting_ || audio_done_) return;
            audio_done_ = true;
            finishSuggest();
        });
    }
    audio_->start(video_path_);

    // The picture: brightness and change per step, on a worker.
    QPointer<LabelController> self(this);
    const int run = ++suggest_run_;
    suggest_worker_ = std::thread([self, run, cancel = suggest_cancel_, video = Fs(video_path_)] {
        samn::vision::PictureSignalOptions o;
        o.cancel = [cancel] { return cancel->load(); };
        o.progress = [self, run](int p) {
            QMetaObject::invokeMethod(qApp, [self, run, p] {
                if (self && self->suggesting_ && run == self->suggest_run_) {
                    self->suggest_progress_ = p;
                    emit self->suggestingChanged();
                }
            }, Qt::QueuedConnection);
        };
        std::string error;
        samn::vision::PictureSignals s;
        try {
            s = samn::vision::MeasurePictureSignals(video, o, &error);
        } catch (const std::exception& e) {
            error = e.what();
        }
        const QString err = s.luma.empty() ? QString::fromStdString(error) : QString();
        QMetaObject::invokeMethod(qApp, [self, run, luma = std::move(s.luma), motion = std::move(s.motion), err] {
            // A canceled run's late answer is not the new run's.
            if (self && run == self->suggest_run_) self->onPictureSignals(luma, motion, err);
        }, Qt::QueuedConnection);
    });
    return true;
}

void LabelController::cancelSuggest() {
    if (!suggesting_) return;
    if (suggest_cancel_) *suggest_cancel_ = true;
    if (audio_) audio_->cancel();
    suggesting_ = false;
    emit suggestingChanged();
    setStatus(tr("Phase search canceled."));
}

void LabelController::onPictureSignals(const std::vector<double>& luma, const std::vector<double>& motion,
                                       const QString& error) {
    if (!suggesting_) return;
    if (luma.empty()) {
        if (audio_) audio_->cancel();
        suggesting_ = false;
        emit suggestingChanged();
        setStatus(tr("Phases: cannot read the clip: %1").arg(error));
        return;
    }
    luma_ = luma;
    motion_ = motion;
    picture_done_ = true;
    if (!audio_done_) {
        // A clip without a sound track may never report back from the
        // decoder: after the picture, the sound gets a few seconds more.
        const int run = suggest_run_;
        QTimer::singleShot(kSoundGraceMs, this, [this, run] {
            if (!suggesting_ || run != suggest_run_ || audio_done_) return;
            if (audio_) audio_->cancel();
            loudness_.clear();
            audio_done_ = true;
            finishSuggest();
        });
    }
    finishSuggest();
}

void LabelController::finishSuggest() {
    if (!picture_done_ || !audio_done_ || !suggesting_) return;
    samn::PhaseSignals s;
    s.step_ms = 500;
    s.luma = luma_;
    s.motion = motion_;
    // The 50 ms loudness as the mean per step, against the clip's loudest step.
    if (!loudness_.empty()) {
        const size_t per = size_t(s.step_ms / samn::kAudioLevelStepMs);
        double top = 0;
        for (size_t i = 0; i < loudness_.size(); i += per) {
            double sum = 0;
            size_t k = 0;
            for (; k < per && i + k < loudness_.size(); ++k) sum += loudness_[i + k];
            s.loudness.push_back(sum / double(std::max<size_t>(1, k)));
            top = std::max(top, s.loudness.back());
        }
        if (top > 0)
            for (auto& v : s.loudness) v /= top;
        s.loudness.resize(s.luma.size(), 0.0);
    }
    const auto suggestions = samn::SuggestPhases(s);
    int added = 0;
    change(tr("Phase suggestions"), [&](samn::LabelProject& p) {
        added = samn::ApplyPhaseSuggestions(p, suggestions);
        return added > 0;
    });
    suggesting_ = false;
    suggest_progress_ = 100;
    emit suggestingChanged();
    setStatus(added > 0 ? tr("%n phase suggestion(s): check them and confirm.", "", added)
                        : tr("No phase suggestions for this clip."));
}

void LabelController::change(const QString& what, const std::function<bool(samn::LabelProject&)>& op) {
    samn::LabelProject next = project_;
    if (!op(next)) return;
    undo_.push_back(std::move(project_));
    if (undo_.size() > kUndoMax) undo_.erase(undo_.begin());
    redo_.clear();
    project_ = std::move(next);
    save();
    setStatus(what);
    emit tracksChanged();
    emit previewChanged();
}

bool LabelController::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(project_));
    project_ = std::move(undo_.back());
    undo_.pop_back();
    save();
    if (!labels().contains(current_)) setCurrentLabel(labels().value(0));
    emit tracksChanged();
    emit previewChanged();
    return true;
}

bool LabelController::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(project_));
    project_ = std::move(redo_.back());
    redo_.pop_back();
    save();
    emit tracksChanged();
    emit previewChanged();
    return true;
}

void LabelController::save() {
    if (video_path_.isEmpty()) return;
    try {
        samn::SaveLabelProject(samn::LabelProjectPath(Fs(video_path_)), project_);
    } catch (const std::exception& e) {
        setStatus(tr("Not saved: %1").arg(QString::fromUtf8(e.what())));
    }
}

void LabelController::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged();
}

QVariantList LabelController::boxes() const {
    QVariantList out;
    const auto classes = project_.Classes();
    const auto classIndex = [&](const std::string& c) { return int(std::find(classes.begin(), classes.end(), c) - classes.begin()); };
    const auto ms = int64_t(std::llround(preview_ms_));
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& t = project_.tracks[i];
        const samn::LabelState s = t.At(ms);
        const samn::LabelKey* key = t.KeyAt(ms, tolerance());
        out << QVariantMap{{"label", QString::fromStdString(t.label())},
                           {"cls", QString::fromStdString(t.cls())},
                           {"classIndex", classIndex(t.cls())},
                           {"index", int(i)},
                           {"x", s.box.x},
                           {"y", s.box.y},
                           {"w", s.box.w},
                           {"h", s.box.h},
                           {"present", s.present},
                           {"keyed", key != nullptr},
                           {"confirmed", key ? key->confirmed : s.reviewed},
                           {"reviewed", s.reviewed}};
    }
    return out;
}

QVariantList LabelController::lanes() const {
    QVariantList out;
    const auto classes = project_.Classes();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& t = project_.tracks[i];
        QVariantList keys, spans;
        const auto& k = t.keys();
        for (const auto& key : k)
            keys << QVariantMap{{"ms", double(key.ms)}, {"absent", key.absent}, {"covered", key.covered}, {"confirmed", key.confirmed},
                                {"source", QString::fromStdString(key.source)}, {"uncertain", key.uncertain}};
        // One span per stretch between keys (the last one runs to the end).
        for (size_t j = 0; j < k.size(); ++j) {
            const double end = j + 1 < k.size() ? double(k[j + 1].ms) : std::max(duration_ms_, double(k[j].ms));
            const bool reviewed = k[j].confirmed && (j + 1 == k.size() || k[j + 1].confirmed);
            spans << QVariantMap{{"startMs", double(k[j].ms)}, {"endMs", end}, {"present", !k[j].absent}, {"covered", k[j].covered},
                                 {"reviewed", reviewed}};
        }
        out << QVariantMap{{"label", QString::fromStdString(t.label())},
                           {"cls", QString::fromStdString(t.cls())},
                           {"classIndex", int(std::find(classes.begin(), classes.end(), t.cls()) - classes.begin())},
                           {"index", int(i)},
                           {"keys", keys},
                           {"spans", spans}};
    }
    return out;
}

int LabelController::addToTrainingSet(double everyMs) {
    if (!hasVideo()) return -1;
    const QString dir = QString::fromStdU16String((samn::config_dir() / "ai" / "datasets").u16string()) + "/" +
                        QFileInfo(video_path_).completeBaseName();
    QDir(dir).removeRecursively();
    const int n = exportDataset(dir, everyMs);
    if (n >= 0) setStatus(tr("%n sample(s) of %1 in the training set.", "", n).arg(QFileInfo(video_path_).fileName()));
    return n;
}

int LabelController::exportDataset(const QString& dirOrUrl, double everyMs) {
    if (!hasVideo()) return -1;
    const QString dir = LocalPath(dirOrUrl);
    if (dir.isEmpty() || !QDir().mkpath(dir + "/images") || !QDir().mkpath(dir + "/labels")) {
        setStatus(tr("Cannot write to %1").arg(dir));
        return -1;
    }
    const std::vector<std::string> classes = project_.Classes();
    const auto samples = samn::ExportSamples(project_, classes, std::max<int64_t>(1, int64_t(everyMs)));
    const QString stem = QFileInfo(video_path_).completeBaseName();
    int written = 0;
    for (const auto& s : samples) {
        QImage img;
        if (still_) img = frame_;
        else img = FrameCache::shared().At(video_path_, double(s.ms)).image;
        if (img.isNull()) continue;
        const QString name = QStringLiteral("%1_%2").arg(stem).arg(s.ms, 8, 10, QLatin1Char('0'));
        if (!img.save(dir + "/images/" + name + ".jpg", "JPG", 92)) continue;
        std::ofstream(Fs(dir + "/labels/" + name + ".txt"), std::ios::binary)
            << samn::YoloLines(s, img.width(), img.height());
        ++written;
    }
    std::ofstream classesOut(Fs(dir + "/classes.txt"), std::ios::binary);
    for (const auto& c : classes) classesOut << c << '\n';
    // The confirmed phases for the scene model (D-157); clips exported into
    // the same folder add their lines. Binary: "\n" on every system, as
    // the labels and classes.txt.
    QFile phasesOut(dir + "/phases.csv");
    const bool fresh = !phasesOut.exists();
    if (phasesOut.open(QIODevice::Append)) {
        if (fresh) phasesOut.write("clip,start_ms,end_ms,phase\n");
        for (const auto& p : project_.phases)
            if (p.confirmed)
                phasesOut.write(QStringLiteral("%1,%2,%3,%4\n")
                                    .arg(QFileInfo(video_path_).fileName().replace(',', '_'))
                                    .arg(p.start_ms)
                                    .arg(p.end_ms)
                                    .arg(QString::fromStdString(p.name))
                                    .toUtf8());
    }
    setStatus(tr("%n sample(s) written to %1", "", written).arg(dir));
    return written;
}

}  // namespace samn::ui
