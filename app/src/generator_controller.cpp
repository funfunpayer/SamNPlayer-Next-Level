#include "generator_controller.h"

#include "frame_cache.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "samn/engine/tip_find.h"
#include "samn/samn_file.h"
#include "samn/vision/frame_reader.h"

namespace samn::ui {

namespace {

QString LocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString Clock(double ms) {
    const int s = int(ms / 1000);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

GeneratorController::GeneratorController(QObject* parent) : QObject(parent) {
    // Events arrive on the worker thread; each one is handed to this object
    // on the UI thread. A destroyed controller drops them (QPointer).
    QPointer<GeneratorController> self(this);
    runner_ = std::make_unique<engine::GenerateRunner>([self](const engine::GenerateEvent& e) {
        if (!self) return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, e] {
                if (self) self->onEvent(e);
            },
            Qt::QueuedConnection);
    });
}

// The runner cancels and joins first (GD-25: nothing outlives the app).
GeneratorController::~GeneratorController() {
    cancelFind();
    if (finder_.joinable()) finder_.join();
    runner_.reset();
}

bool GeneratorController::findTip() {
    if (video_path_.isEmpty() || running_ || finding_) return false;
    if (finder_.joinable()) finder_.join();
    find_cancel_ = std::make_shared<std::atomic<bool>>(false);
    finding_ = true;
    emit findingChanged();
    setStatus(tr("Looking for the rhythmic motion…"));
    QPointer<GeneratorController> self(this);
    finder_ = std::thread([self, cancel = find_cancel_, forVideo = video_path_,
                           video = std::filesystem::path(video_path_.toStdU16String())] {
        samn::engine::TipFindOptions o;
        o.cancel = [cancel] { return cancel->load(); };
        std::string error;
        std::optional<samn::engine::TipFindResult> r;
        try {
            r = samn::engine::FindTip(video, o, &error);
        } catch (const std::exception& e) {
            error = e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, r = std::move(r), error, forVideo] {
            if (!self) return;
            self->finding_ = false;
            emit self->findingChanged();
            if (forVideo != self->video_path_) return;  // another video was opened meanwhile
            if (!r) {
                self->setStatus(error == "canceled" ? tr("Find tip: canceled.")
                                                    : tr("Find tip: %1").arg(QString::fromStdString(error)));
                return;
            }
            QVariantList list;
            for (const auto& c : r->candidates)
                list.append(QVariantMap{{"x", c.box.x}, {"y", c.box.y}, {"w", c.box.width}, {"h", c.box.height}, {"score", c.score}});
            self->tip_candidates_ = list;
            emit self->tipCandidatesChanged();
            self->setBox(QRect(r->box.x, r->box.y, r->box.width, r->box.height));
            // Below this the "rhythm" is compression noise (a still clip
            // scores about 0.002, a clear stroke above 1).
            const bool weak = r->candidates.empty() || r->candidates.front().score < 0.05;
            self->setStatus(weak ? tr("Only weak motion found: check the box, or drag it by hand.")
                                 : tr("Tip found. The dashed boxes are other candidates; click one to use it."));
        }, Qt::QueuedConnection);
    });
    return true;
}

void GeneratorController::cancelFind() {
    if (find_cancel_) *find_cancel_ = true;
}

void GeneratorController::pickCandidate(int index) {
    if (index < 0 || index >= tip_candidates_.size()) return;
    const QVariantMap c = tip_candidates_.at(index).toMap();
    setBox(QRect(c.value("x").toInt(), c.value("y").toInt(), c.value("w").toInt(), c.value("h").toInt()));
}

QString GeneratorController::videoName() const { return QFileInfo(video_path_).fileName(); }

bool GeneratorController::openVideo(const QString& pathOrUrl) {
    if (running_) {
        setStatus(tr("A script is being created — cancel it first."));
        return false;
    }
    const QString path = LocalPath(pathOrUrl);
    std::string error;
    auto reader = vision::FrameReader::Open(std::filesystem::path(path.toStdU16String()), &error);
    if (!reader) {
        setStatus(tr("Cannot open the video: %1").arg(QString::fromStdString(error)));
        return false;
    }
    video_path_ = path;
    video_size_ = QSize(reader->size().width, reader->size().height);
    duration_ms_ = reader->frameCount() > 0 ? reader->frameCount() * 1000.0 / reader->fps() : 0;
    video_info_ = QStringLiteral("%1 · %2×%3 · %4 fps")
                      .arg(Clock(duration_ms_))
                      .arg(video_size_.width())
                      .arg(video_size_.height())
                      .arg(std::round(reader->fps() * 100) / 100);
    box_ = QRect();
    cancelFind();  // a search on the old video ends; its result is not used
    tip_candidates_.clear();
    emit tipCandidatesChanged();
    output_path_.clear();
    log_.clear();
    percent_ = 0;
    progress_text_.clear();
    emit videoChanged();
    emit boxChanged();
    emit outputChanged();
    emit logChanged();
    emit progressChanged();
    emit stepChanged();
    // 0.7's poster: 10 % into the clip, at most 30 s in.
    seekPreview(std::min(duration_ms_ * 0.1, 30000.0));
    setStatus(tr("Drag a box around the moving tip on the frame."));
    return true;
}

void GeneratorController::seekPreview(double ms) {
    if (video_path_.isEmpty()) return;
    ms = std::clamp(ms, 0.0, std::max(0.0, duration_ms_ - 1));
    // The shared cache keeps the video open: dragging a curve point seeks
    // on every mouse move (D-142 size limit).
    const FrameCache::Frame f = FrameCache::shared().At(video_path_, ms);
    if (f.image.isNull()) return;
    frame_ = f.image;
    preview_ms_ = f.ms;
    emit previewChanged();
}

void GeneratorController::setBox(const QRect& box) {
    // Inside the frame; 0.7 treats boxes under 8 px as a click, not a box.
    const QRect r = box.normalized().intersected(QRect(QPoint(0, 0), video_size_));
    const QRect next = r.width() >= 8 && r.height() >= 8 ? r : QRect();
    if (next == box_) return;
    box_ = next;
    emit boxChanged();
    emit stepChanged();
}

void GeneratorController::clearBox() { setBox(QRect()); }

QString GeneratorController::boxLabel() const {
    if (!hasBox()) return tr("No tip box yet — drag one on the frame.");
    return tr("Tip (stroke tracker): x=%1 y=%2 w=%3 h=%4 — CSRT follows this box only.")
        .arg(box_.x())
        .arg(box_.y())
        .arg(box_.width())
        .arg(box_.height());
}

void GeneratorController::setProfile(const QString& p) {
    // Tf/Tj need a contact mark (P3.4); the Generator offers the stroke styles.
    const QString v = p == QLatin1String("weich") || p == QLatin1String("autotune") ? p : QStringLiteral("standard");
    if (v == profile_) return;
    profile_ = v;
    emit optionsChanged();
}

#define SAMN_SETTER(Name, member)              \
    void GeneratorController::Name(bool v) {   \
        if (member == v) return;               \
        member = v;                            \
        emit optionsChanged();                 \
    }
SAMN_SETTER(setInvert, invert_)
SAMN_SETTER(setCameraCompensation, camera_)
SAMN_SETTER(setSceneCuts, scene_cuts_)
SAMN_SETTER(setAutoOZone, auto_ozone_)
#undef SAMN_SETTER

std::filesystem::path GeneratorController::funscriptPath() const {
    // 0.7 scriptPathForVideo: the video's name with .funscript, beside it.
    const QFileInfo fi(video_path_);
    return std::filesystem::path((fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".funscript"))
                                     .toStdU16String());
}

bool GeneratorController::scriptExists() const {
    if (video_path_.isEmpty()) return false;
    const std::filesystem::path f = funscriptPath();
    std::error_code ec;
    return std::filesystem::exists(f, ec) ||
           std::filesystem::exists(std::filesystem::u8path(samn::CompanionSamnPath(f.u8string())), ec);
}

int GeneratorController::step() const {
    if (!hasVideo()) return 1;
    if (running_) return 4;
    if (!output_path_.isEmpty()) return 5;
    return hasBox() ? 3 : 2;
}

bool GeneratorController::create(bool overwrite) {
    if (running_) return false;
    if (!hasVideo() || !hasBox()) {
        setStatus(hasVideo() ? tr("Drag a tip box on the frame first.") : tr("Choose a video first."));
        return false;
    }
    engine::GenerateRequest req;
    req.video = std::filesystem::path(video_path_.toStdU16String());
    req.box = cv::Rect(box_.x(), box_.y(), box_.width(), box_.height());
    req.camera_compensation = camera_;
    req.scene_cuts = scene_cuts_;
    req.signal.profile = profile_.toStdString();
    req.signal.invert = invert_;
    req.output = funscriptPath();
    req.overwrite = overwrite;
    req.auto_ozone = auto_ozone_;
    const uint64_t id = runner_->Start(std::move(req));
    if (id == 0) return false;
    run_id_ = id;
    running_ = true;
    percent_ = 0;
    progress_text_ = QStringLiteral("0 %");
    output_path_.clear();
    log_.clear();
    run_clock_.start();
    emit runChanged();
    emit progressChanged();
    emit outputChanged();
    emit logChanged();
    emit stepChanged();
    setStatus(tr("Creating the script…"));
    return true;
}

void GeneratorController::cancel() {
    if (!running_) return;
    runner_->Cancel();
    setStatus(tr("Cancel requested…"));
}

void GeneratorController::waitForRun() { runner_->Wait(); }

void GeneratorController::onEvent(const engine::GenerateEvent& e) {
    if (e.run_id != run_id_) return;  // a finished run's late event
    const QString msg = QString::fromStdString(e.message);
    switch (e.kind) {
        case engine::GenerateEventKind::kProgress: {
            percent_ = e.percent;
            if (e.percent < 0) {
                progress_text_ = tr("Running… (could not determine video length)");
            } else {
                progress_text_ = QStringLiteral("%1 %").arg(e.percent);
                // 0.7: the time left once 3 % and 2 s have passed.
                const qint64 elapsed = run_clock_.elapsed();
                if (e.percent >= 3 && elapsed > 2000) {
                    const double left_s = elapsed * (100.0 - e.percent) / e.percent / 1000.0;
                    progress_text_ += left_s >= 90 ? tr("  ·  ~%1 min left").arg(int(std::round(left_s / 60)))
                                                   : tr("  ·  ~%1 s left").arg(int(std::round(left_s)));
                }
            }
            emit progressChanged();
            return;
        }
        case engine::GenerateEventKind::kNote:
            appendLog(msg);
            return;
        case engine::GenerateEventKind::kDone: {
            const std::filesystem::path f = funscriptPath();
            output_path_ = QString::fromStdString(samn::CompanionSamnPath(f.u8string()));
            percent_ = 100;
            progress_text_ = QStringLiteral("100 %");
            appendLog(tr("Wrote %1 (%2 actions)").arg(msg).arg(e.actions));
            setStatus(tr("Done — the script is open below for editing."));
            break;
        }
        case engine::GenerateEventKind::kCanceled:
            appendLog(tr("Canceled."));
            setStatus(tr("Canceled."));
            break;
        case engine::GenerateEventKind::kError:
            appendLog(tr("Failed: %1").arg(msg));
            setStatus(tr("Failed: %1").arg(msg));
            break;
    }
    running_ = false;
    emit runChanged();
    emit progressChanged();
    emit outputChanged();
    emit stepChanged();
    if (e.kind == engine::GenerateEventKind::kDone) emit created(output_path_);
}

void GeneratorController::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged();
}

void GeneratorController::appendLog(const QString& line) {
    log_.append(line);
    emit logChanged();
}

}  // namespace samn::ui
