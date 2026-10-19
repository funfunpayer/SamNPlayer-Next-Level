#include "generator_controller.h"

#include "frame_cache.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "samn/engine/generate_queue.h"
#include "samn/engine/tip_find.h"
#include "samn/contact_marks.h"
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
    loadSettings();
}

void GeneratorController::loadSettings() {
    try {
        settings_ = std::make_unique<samn::Settings>();
        auto& s = *settings_;
        const auto str = [&](const char* key, const QString& fallback) {
            const auto v = s.get(key);
            return v.is_string() ? QString::fromStdString(v.get<std::string>()) : fallback;
        };
        const auto flag = [&](const char* key, bool fallback) {
            const auto v = s.get(key);
            return v.is_boolean() ? v.get<bool>() : fallback;
        };
        // Through the setters: an unknown value keeps the default.
        setProfile(str("generator.profile", profile_));
        camera_ = flag("generator.camera_compensation", camera_);
        scene_cuts_ = flag("generator.scene_cuts", scene_cuts_);
        auto_ozone_ = flag("generator.auto_ozone", auto_ozone_);
        contact_vibration_ = flag("generator.contact_vibration", contact_vibration_);
        setTipClass(str("generator.tip_class", tip_class_));
        setContactClass(str("generator.contact_class", contact_class_));
        // 0.7 stored the sensitivity in percent (75).
        const auto span = s.get("generator.contact_vibration_span");
        if (span.is_number()) setContactSpan(span.get<double>() / 100.0);
        setContactCurve(str("generator.contact_vibration_curve", contact_curve_));
    } catch (const std::exception&) {
        // Unreadable settings: the defaults stay.
    }
}

void GeneratorController::saveSetting(const char* key, const nlohmann::json& value) {
    if (!settings_) return;
    try {
        settings_->set(key, value);
    } catch (const std::exception&) {
        // A settings file that cannot be written must not stop creating.
    }
}

// The runner cancels and joins first (GD-25: nothing outlives the app).
GeneratorController::~GeneratorController() {
    cancelFind();
    if (queue_cancel_) *queue_cancel_ = true;
    if (finder_.joinable()) finder_.join();
    if (queue_worker_.joinable()) queue_worker_.join();
    runner_.reset();
}

bool GeneratorController::findTip() {
    if (video_path_.isEmpty() || running_ || finding_ || queue_running_) return false;
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

QString GeneratorController::queueSummary() const {
    int waiting = 0, done = 0, other = 0;
    for (const QVariant& v : queue_) {
        const QString st = v.toMap().value("status").toString();
        if (st == QLatin1String("queued")) ++waiting;
        else if (st == QLatin1String("done")) ++done;
        else if (st != QLatin1String("running")) ++other;
    }
    if (queue_.isEmpty()) return {};
    if (queue_running_) return tr("Queue %1/%2 running").arg(done + other + 1).arg(queue_.size());
    QString s = tr("Queue: %n video(s), %1 waiting", nullptr, int(queue_.size())).arg(waiting);
    if (done) s += tr(", %1 done").arg(done);
    if (other) s += tr(", %1 skipped or failed").arg(other);
    return s;
}

int GeneratorController::addToQueue(const QStringList& pathsOrUrls) {
    if (queue_running_) return 0;
    int added = 0;
    for (const QString& in : pathsOrUrls) {
        const QString path = in.startsWith(QStringLiteral("file:")) ? QUrl(in).toLocalFile() : in;
        if (path.isEmpty()) continue;
        bool known = false;
        for (const QVariant& v : queue_) known |= v.toMap().value("path").toString() == path;
        if (known) continue;
        queue_.append(QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName()}, {"status", "queued"}, {"message", ""}});
        ++added;
    }
    if (added) emit queueChanged();
    return added;
}

void GeneratorController::removeFromQueue(int index) {
    if (queue_running_ || index < 0 || index >= queue_.size()) return;
    queue_.removeAt(index);
    emit queueChanged();
}

void GeneratorController::clearQueue() {
    if (queue_running_ || queue_.isEmpty()) return;
    queue_.clear();
    emit queueChanged();
}

void GeneratorController::setQueueItem(int index, const QString& status, const QString& message) {
    if (index < 0 || index >= queue_.size()) return;
    QVariantMap m = queue_.at(index).toMap();
    m["status"] = status;
    m["message"] = message;
    queue_[index] = m;
    emit queueChanged();
}

bool GeneratorController::runQueue(bool overwrite) {
    if (queue_running_ || running_ || finding_) return false;
    std::vector<std::pair<int, std::filesystem::path>> items;
    for (int i = 0; i < queue_.size(); ++i) {
        const QVariantMap m = queue_.at(i).toMap();
        if (m.value("status").toString() == QLatin1String("queued"))
            items.emplace_back(i, std::filesystem::path(m.value("path").toString().toStdU16String()));
    }
    if (items.empty()) return false;
    if (queue_worker_.joinable()) queue_worker_.join();
    engine::QueueSettings settings;
    settings.signal.profile = profile_.toStdString();
    settings.signal.invert = invert_;
    settings.camera_compensation = camera_;
    settings.scene_cuts = scene_cuts_;
    settings.auto_ozone = auto_ozone_;
    settings.overwrite = overwrite;
    settings.contact_vibration = contact_vibration_;
    settings.contact_vibration_span = contact_span_;
    settings.contact_vibration_curve = contact_curve_.toStdString();
    settings.tip_class = tip_class_.toStdString();
    queue_cancel_ = std::make_shared<std::atomic<bool>>(false);
    queue_running_ = true;
    percent_ = 0;
    progress_text_.clear();
    emit queueChanged();
    emit progressChanged();
    setStatus(tr("Creating the queue…"));
    QPointer<GeneratorController> self(this);
    queue_worker_ = std::thread([self, items, settings, cancel = queue_cancel_] {
        auto post = [self](auto fn) {
            QMetaObject::invokeMethod(qApp, [self, fn] { if (self) fn(self.data()); }, Qt::QueuedConnection);
        };
        const int total = int(items.size());
        for (int n = 0; n < total; ++n) {
            const auto& [index, video] = items[size_t(n)];
            if (cancel->load()) {
                post([index](GeneratorController* g) { g->setQueueItem(index, "canceled", {}); });
                continue;
            }
            post([index](GeneratorController* g) { g->setQueueItem(index, "running", {}); });
            engine::QueueItemOutcome r;
            try {
                r = engine::GenerateQueueItem(video, settings, [cancel] { return cancel->load(); }, [post, n, total](int p) {
                    post([n, total, p](GeneratorController* g) {
                        g->percent_ = (n * 100 + p) / total;
                        g->progress_text_ = tr("Queue %1/%2 · %3 %").arg(n + 1).arg(total).arg(p);
                        emit g->progressChanged();
                    });
                });
            } catch (const std::exception& e) {
                r.status = engine::QueueItemStatus::kFailed;
                r.message = e.what();
            }
            static const char* const names[] = {"done", "skipped", "failed", "canceled"};
            const QString status = QString::fromLatin1(names[int(r.status)]);
            const QString message = QString::fromStdString(r.message);
            post([index, status, message](GeneratorController* g) { g->setQueueItem(index, status, message); });
        }
        post([](GeneratorController* g) { g->finishQueue(); });
    });
    return true;
}

void GeneratorController::finishQueue() {
    queue_running_ = false;
    int done = 0, skipped = 0, failed = 0;
    for (const QVariant& v : queue_) {
        const QString st = v.toMap().value("status").toString();
        if (st == QLatin1String("done")) ++done;
        else if (st == QLatin1String("skipped")) ++skipped;
        else if (st == QLatin1String("failed")) ++failed;
    }
    emit queueChanged();
    setStatus(tr("Queue finished: %1 created, %2 skipped, %3 failed.").arg(done).arg(skipped).arg(failed));
    emit queueFinished(done, skipped, failed);
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
    contact_areas_.clear();  // drawn on the old video
    mark_contact_ = false;
    emit contactChanged();
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
    saveSetting("generator.profile", v.toStdString());
    emit optionsChanged();
}

#define SAMN_SETTER(Name, member, key)         \
    void GeneratorController::Name(bool v) {   \
        if (member == v) return;               \
        member = v;                            \
        if (key) saveSetting(key, v);          \
        emit optionsChanged();                 \
    }
SAMN_SETTER(setInvert, invert_, nullptr)
SAMN_SETTER(setCameraCompensation, camera_, "generator.camera_compensation")
SAMN_SETTER(setSceneCuts, scene_cuts_, "generator.scene_cuts")
SAMN_SETTER(setAutoOZone, auto_ozone_, "generator.auto_ozone")
SAMN_SETTER(setContactVibration, contact_vibration_, "generator.contact_vibration")
#undef SAMN_SETTER

namespace {

QStringList ToQt(const std::vector<std::string>& v) {
    QStringList out;
    for (const std::string& s : v) out << QString::fromStdString(s);
    return out;
}

}  // namespace

QStringList GeneratorController::tipClasses() const { return ToQt(samn::TipClasses()); }
QStringList GeneratorController::contactClasses() const { return ToQt(samn::ContactClasses()); }

void GeneratorController::setTipClass(const QString& c) {
    if (c == tip_class_ || !tipClasses().contains(c)) return;
    tip_class_ = c;
    saveSetting("generator.tip_class", c.toStdString());
    emit optionsChanged();
}

void GeneratorController::setContactClass(const QString& c) {
    if (c == contact_class_ || !contactClasses().contains(c)) return;
    contact_class_ = c;
    saveSetting("generator.contact_class", c.toStdString());
    emit optionsChanged();
}

void GeneratorController::setContactSpan(double span) {
    // 0.7 slider: 40-95 in steps of 5.
    const double v = std::round(std::clamp(span, 0.40, 0.95) * 20.0) / 20.0;
    if (std::abs(v - contact_span_) < 1e-9) return;
    contact_span_ = v;
    saveSetting("generator.contact_vibration_span", int(std::lround(v * 100)));
    emit optionsChanged();
}

void GeneratorController::setContactCurve(const QString& curve) {
    const QString v = QString::fromStdString(samn::NormalizeContactCurve(curve.toStdString()));
    if (v == contact_curve_) return;
    contact_curve_ = v;
    saveSetting("generator.contact_vibration_curve", v.toStdString());
    emit optionsChanged();
}

void GeneratorController::setMarkContact(bool v) {
    if (v == mark_contact_) return;
    mark_contact_ = v;
    emit contactChanged();
}

QVariantList GeneratorController::contactAreas() const {
    QVariantList out;
    for (const samn::ContactMarkBox& b : contact_areas_)
        out << QVariantMap{{"x", qint64(b.x)}, {"y", qint64(b.y)}, {"w", qint64(b.w)}, {"h", qint64(b.h)},
                           {"cls", QString::fromStdString(b.cls)}};
    return out;
}

bool GeneratorController::addContactArea(const QRect& area) {
    // The same rule as the tip box: inside the frame, at least 8 px.
    const QRect r = area.normalized().intersected(QRect(QPoint(0, 0), video_size_));
    mark_contact_ = false;
    if (!hasVideo() || r.width() < 8 || r.height() < 8) {
        emit contactChanged();
        return false;
    }
    contact_areas_.push_back({r.x(), r.y(), r.width(), r.height(), contact_class_.toStdString(), false});
    emit contactChanged();
    return true;
}

void GeneratorController::removeContactArea(int index) {
    if (index < 0 || index >= static_cast<int>(contact_areas_.size())) return;
    contact_areas_.erase(contact_areas_.begin() + index);
    emit contactChanged();
}

void GeneratorController::clearContactAreas() {
    if (contact_areas_.empty() && !mark_contact_) return;
    contact_areas_.clear();
    mark_contact_ = false;
    emit contactChanged();
}

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
    if (running_ || queue_running_) return false;
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
    req.contact_vibration = contact_vibration_;
    req.contact_vibration_span = contact_span_;
    req.contact_vibration_curve = contact_curve_.toStdString();
    req.tip_class = tip_class_.toStdString();
    if (!contact_areas_.empty()) {
        req.contact_area = contact_areas_.front();
        req.extra_areas.assign(contact_areas_.begin() + 1, contact_areas_.end());
    }
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
    if (queue_running_ && queue_cancel_) {
        *queue_cancel_ = true;
        setStatus(tr("Cancel requested…"));
        return;
    }
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
