#include "generator_controller.h"

#include "app_log.h"
#include "frame_cache.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <utility>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "samn/video_info.h"
#include "samn/engine/generate_queue.h"
#include "samn/engine/tip_find.h"
#include "samn/contact_marks.h"
#include "samn/ozone.h"
#include "samn/polarity.h"
#include "samn/samn_file.h"
#include "samn/script_quality.h"
#include "samn/settings.h"
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

// The values behind the opt-in switches (spec FR-12, FR-13 defaults).
constexpr double kGapBridgeMs = 500;
constexpr double kOutlierMadFactor = 5;

}  // namespace

GeneratorController::GeneratorController(QObject* parent) : QObject(parent) {
    // The auto scene follows the marked contact area.
    connect(this, &GeneratorController::contactChanged, this, &GeneratorController::feelChanged);
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
        gap_handling_ = flag("generator.gap_handling", gap_handling_);
        outlier_rejection_ = flag("generator.outlier_rejection", outlier_rejection_);
        track_cache_ = flag("generator.track_cache", track_cache_);
        contact_vibration_ = flag("generator.contact_vibration", contact_vibration_);
        setTipClass(str("generator.tip_class", tip_class_));
        setContactClass(str("generator.contact_class", contact_class_));
        setExtraClass(str("generator.extra_class", extra_class_));
        partner_fixed_ = flag("generator.partner_fixed", partner_fixed_);
        extras_stay_fixed_ = flag("generator.extras_stay_fixed", extras_stay_fixed_);
        // 0.7 stored the sensitivity in percent (75).
        const auto span = s.get("generator.contact_vibration_span");
        if (span.is_number()) setContactSpan(span.get<double>() / 100.0);
        setContactCurve(str("generator.contact_vibration_curve", contact_curve_));
        setOptimizeTarget(str("generator.optimize_target", optimize_target_));
        pos_overlay_ = flag("generator.pos_overlay", pos_overlay_);
        setFeelDevice(str("generator.feel.device", feel_device_));
        setSceneFeel(str("generator.feel.scene", scene_feel_));
        setVibSource(str("generator.feel.vib_source", vib_source_));
        if (const auto b = s.get("generator.feel.balance"); b.is_number()) setFeelBalance(b.get<int>());
        vib_follow_ = flag("generator.feel.follow", vib_follow_);
        show_all_feel_ = flag("generator.feel.show_all", show_all_feel_);
    } catch (const std::exception&) {
        // Unreadable settings: the defaults stay.
    }
}

void GeneratorController::applySwitches(engine::SignalOptions& signal,
                                        std::filesystem::path& track_cache_dir) const {
    signal.max_gap_ms = gap_handling_ ? kGapBridgeMs : 0;
    signal.outlier_mad_factor = outlier_rejection_ ? kOutlierMadFactor : 0;
    track_cache_dir = track_cache_ ? samn::config_dir() / "cache" / "track" : std::filesystem::path();
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
            const bool thenCreate = std::exchange(self->create_after_find_, false);
            if (forVideo != self->video_path_) return;  // another video was opened meanwhile
            if (!r) {
                if (thenCreate && error != "canceled") {
                    self->setStatus(tr("Find tip failed: %1.").arg(QString::fromStdString(error)));
                    AppLog::instance().reportError(self->status_);  // 0.7 uiError
                }
                else
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
            if (thenCreate) {
                // Create goes on only with a real find; a weak one waits for the user.
                if (weak) {
                    self->setStatus(tr("Find tip failed: only weak motion found — check the box, or drag it by hand, then Create."));
                    AppLog::instance().reportError(self->status_);
                }
                else self->create(self->create_overwrite_);
                return;
            }
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
    applySwitches(settings.signal, settings.track_cache_dir);
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
    create_after_find_ = false;
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
        AppLog::instance().reportError(status_);
        return false;
    }
    video_path_ = path;
    video_size_ = QSize(reader->size().width, reader->size().height);
    duration_ms_ = reader->frameCount() > 0 ? reader->frameCount() * 1000.0 / reader->fps() : 0;
    // An unknown codec says so (issue #4); it is never guessed.
    const std::string codec = samn::CodecName(reader->fourcc());
    video_info_ = QStringLiteral("%1 · %2×%3 · %4 fps · %5")
                      .arg(Clock(duration_ms_))
                      .arg(video_size_.width())
                      .arg(video_size_.height())
                      .arg(std::round(reader->fps() * 100) / 100)
                      .arg(codec.empty() ? tr("codec unknown") : QString::fromStdString(codec));
    box_ = QRect();
    contact_areas_.clear();  // drawn on the old video
    mask_rois_.clear();
    mark_contact_ = mark_extra_ = mark_ignore_ = false;
    emit contactChanged();
    cancelFind();  // a search on the old video ends; its result is not used
    tip_candidates_.clear();
    emit tipCandidatesChanged();
    output_path_.clear();
    review_.clear();
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
SAMN_SETTER(setGapHandling, gap_handling_, "generator.gap_handling")
SAMN_SETTER(setOutlierRejection, outlier_rejection_, "generator.outlier_rejection")
SAMN_SETTER(setTrackCache, track_cache_, "generator.track_cache")
SAMN_SETTER(setContactVibration, contact_vibration_, "generator.contact_vibration")
#undef SAMN_SETTER

void GeneratorController::setPartnerFixed(bool v) {
    if (v == partner_fixed_) return;
    partner_fixed_ = v;
    saveSetting("generator.partner_fixed", v);
    if (!contact_areas_.empty()) {
        contact_areas_.front().fixed = v;
        emit contactChanged();
    }
    emit optionsChanged();
}

void GeneratorController::setExtrasStayFixed(bool v) {
    if (v == extras_stay_fixed_) return;
    extras_stay_fixed_ = v;
    saveSetting("generator.extras_stay_fixed", v);
    for (size_t i = 1; i < contact_areas_.size(); ++i) contact_areas_[i].fixed = v;
    if (contact_areas_.size() > 1) emit contactChanged();
    emit optionsChanged();
}

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
    emit feelChanged();
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

void GeneratorController::setOptimizeTarget(const QString& target) {
    const QString v = target == QLatin1String("general") || target == QLatin1String("neo2") ? target : QString();
    if (v == optimize_target_) return;
    optimize_target_ = v;
    saveSetting("generator.optimize_target", v.toStdString());
    emit optionsChanged();
}

void GeneratorController::setPosOverlay(bool on) {
    if (on == pos_overlay_) return;
    pos_overlay_ = on;
    saveSetting("generator.pos_overlay", on);
    emit optionsChanged();
}

void GeneratorController::setExtraClass(const QString& c) {
    if (c == extra_class_ || (!c.isEmpty() && !contactClasses().contains(c))) return;
    extra_class_ = c;
    saveSetting("generator.extra_class", c.toStdString());
    emit optionsChanged();
}

namespace {
const QStringList kFeelDevices{"auto", "neo2", "vibrator", "stroker"};
const QStringList kSceneFeels{"auto", "", "oral", "intercourse", "breast", "hand"};
const QStringList kVibSources{"", "sound+contact", "sound"};
QString SceneTitle(const QString& s) {
    if (s == QLatin1String("oral")) return QObject::tr("Oral");
    if (s == QLatin1String("intercourse")) return QObject::tr("Intercourse");
    if (s == QLatin1String("breast")) return QObject::tr("Breast");
    if (s == QLatin1String("hand")) return QObject::tr("Hand");
    return QObject::tr("Classic");
}
}  // namespace

void GeneratorController::setFeelDevice(const QString& d) {
    if (d == feel_device_ || !kFeelDevices.contains(d)) return;
    feel_device_ = d;
    saveSetting("generator.feel.device", d.toStdString());
    emit feelChanged();
}

void GeneratorController::setConnectedKind(const QString& k) {
    const QString v = k == QLatin1String("neo2") || k == QLatin1String("vibrator") || k == QLatin1String("stroker") ? k : QString();
    if (v == connected_kind_) return;
    connected_kind_ = v;
    emit feelChanged();
}

QString GeneratorController::feelKind() const {
    // 0.7: auto follows the connected device, a Sam Neo 2 without one.
    if (feel_device_ != QLatin1String("auto")) return feel_device_;
    return connected_kind_.isEmpty() ? QStringLiteral("neo2") : connected_kind_;
}

void GeneratorController::setShowAllFeel(bool on) {
    if (on == show_all_feel_) return;
    show_all_feel_ = on;
    saveSetting("generator.feel.show_all", on);
    emit feelChanged();
}

void GeneratorController::setSceneFeel(const QString& sc) {
    if (sc == scene_feel_ || !kSceneFeels.contains(sc)) return;
    scene_feel_ = sc;
    saveSetting("generator.feel.scene", sc.toStdString());
    emit feelChanged();
}

QString GeneratorController::autoScene() const {
    // 0.7 sceneFromHints with the contact mark's class (no AI scene type yet),
    // but only when a contact area is marked: the class defaults to nipples,
    // and every plain Create would otherwise get the breast feel.
    if (contact_areas_.empty()) return {};
    if (contact_class_ == QLatin1String("mouth")) return QStringLiteral("oral");
    if (contact_class_ == QLatin1String("nipples") || contact_class_ == QLatin1String("breasts")) return QStringLiteral("breast");
    return {};
}

void GeneratorController::setFeelBalance(int b) {
    // 0.7 slider: -100..100 in steps of 10.
    const int v = int(std::lround(std::clamp(b, -100, 100) / 10.0)) * 10;
    if (v == feel_balance_) return;
    feel_balance_ = v;
    saveSetting("generator.feel.balance", v);
    emit feelChanged();
}

void GeneratorController::setVibFollow(bool on) {
    if (on == vib_follow_) return;
    vib_follow_ = on;
    saveSetting("generator.feel.follow", on);
    emit feelChanged();
}

void GeneratorController::setVibSource(const QString& src) {
    if (src == vib_source_ || !kVibSources.contains(src)) return;
    vib_source_ = src;
    saveSetting("generator.feel.vib_source", src.toStdString());
    emit feelChanged();
}

QString GeneratorController::feelSummary() const {
    // 0.7 "Sam Neo 2 feel · Oral · mix suction 30% · vibration follows suction".
    const QString scene = scene_feel_ == QLatin1String("auto") ? autoScene() : scene_feel_;
    QStringList parts{SceneTitle(scene)};
    if (feel_balance_ != 0)
        parts << tr("mix %1 %2%").arg(feel_balance_ > 0 ? tr("suction") : tr("vibration")).arg(std::abs(feel_balance_));
    if (vib_follow_) parts << tr("vibration follows suction");
    return parts.join(QStringLiteral(" · "));
}

QString GeneratorController::feelHint() const {
    if (show_all_feel_) return {};
    const QString k = feelKind();
    if (k == QLatin1String("vibrator"))
        return tr("Vibrator: contact vibration, or the video's sound (Vibration below). Suction feel hidden.");
    if (k == QLatin1String("stroker"))
        return tr("Stroker: the stroke is all it needs — vibration and suction settings hidden.");
    return {};
}

bool GeneratorController::applyFeel(const QString& samnPath) {
    if (feelKind() != QLatin1String("neo2") || samnPath.isEmpty()) return false;
    const QString scene = scene_feel_ == QLatin1String("auto") ? autoScene() : scene_feel_;
    const double balance = feel_balance_ / 100.0;
    // Classic, centre mix and follow off leave the script exactly as before (0.7).
    if (scene.isEmpty() && !vib_follow_ && feel_balance_ == 0) return false;
    try {
        const std::filesystem::path samn(samnPath.toStdU16String());
        samn::Document doc = samn::LoadDocument(samn);
        doc.ApplyFeelRecipe(scene.toStdString(), vib_follow_, balance);
        if (samn::NormalizePlaybackSource(doc.playback_source) == samn::PlaybackAxes) doc.BakeNeoAxes();
        samn::SaveDocument(samn, doc);
        // The new .funscript plays the same feel (0.7 SaveFeelSettings).
        doc.ExportFunscript(funscriptPath());
    } catch (const std::exception& e) {
        appendLog(tr("Feel not saved: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    appendLog(tr("Sam Neo 2 feel: %1").arg(feelSummary()));
    return true;
}

void GeneratorController::setMarkContact(bool v) {
    if (v == mark_contact_) return;
    mark_contact_ = v;
    if (v) {
        mark_extra_ = mark_ignore_ = false;
        setStatus(tr("Contact area: drag on preview (gold)."));
    }
    emit contactChanged();
}

void GeneratorController::setMarkExtra(bool v) {
    if (v == mark_extra_) return;
    mark_extra_ = v;
    if (v) {
        mark_contact_ = mark_ignore_ = false;
        setStatus(tr("Another contact area: drag on preview (magenta)."));
    }
    emit contactChanged();
}

void GeneratorController::setMarkIgnore(bool v) {
    if (v == mark_ignore_) return;
    mark_ignore_ = v;
    if (v) {
        mark_contact_ = mark_extra_ = false;
        setStatus(tr("Ignore region: drag on preview (black)."));
    }
    emit contactChanged();
}

QVariantList GeneratorController::contactAreas() const {
    QVariantList out;
    for (const samn::ContactMarkBox& b : contact_areas_)
        out << QVariantMap{{"x", qint64(b.x)}, {"y", qint64(b.y)}, {"w", qint64(b.w)}, {"h", qint64(b.h)},
                           {"cls", QString::fromStdString(b.cls)}, {"fixed", b.fixed}};
    return out;
}

QVariantList GeneratorController::maskRois() const {
    QVariantList out;
    for (const QRect& r : mask_rois_)
        out << QVariantMap{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
    return out;
}

bool GeneratorController::markTip(const QRect& box) {
    setBox(box);
    if (!hasBox()) return false;
    // 0.7 generator.js, after the tip box is painted.
    setStatus(contact_vibration_
                  ? tr("Snack 1 done — tip box is the stroke tracker (CSRT). Next: Mark contact (gold), or skip to "
                       "Create.")
                  : tr("Tip box set — CSRT follows this look; Find tip again after a big camera-angle change."));
    return true;
}

bool GeneratorController::addContactArea(const QRect& area) {
    // The same rule as the tip box: inside the frame, at least 8 px.
    const QRect r = area.normalized().intersected(QRect(QPoint(0, 0), video_size_));
    // The first area is the primary whatever was asked: the engine reads it so.
    const bool extra = mark_extra_ && !contact_areas_.empty();
    mark_contact_ = mark_extra_ = mark_ignore_ = false;
    if (!hasVideo() || r.width() < 8 || r.height() < 8) {
        emit contactChanged();
        return false;
    }
    if (extra) {
        // Stay fixed (0.7): keep the magenta box where drawn; off = follow
        // partner path once extras multi-path records it.
        const bool stay = extras_stay_fixed_;
        contact_areas_.push_back({r.x(), r.y(), r.width(), r.height(), extra_class_.toStdString(), stay});
        const int n = static_cast<int>(contact_areas_.size()) - 1;
        const QString how = stay ? tr("stay fixed") : tr("follows partner path");
        setStatus(extra_class_.isEmpty() ? tr("Extra contact #%1 added, %2.").arg(n).arg(how)
                                         : tr("Extra contact #%1 (%2) added, %3.").arg(n).arg(extra_class_).arg(how));
    } else {
        // 0.7 has one primary (gold); drawing it again moves it.
        // Partner fixed (pixels): on = keep the gold box; off = track it.
        const samn::ContactMarkBox b{r.x(), r.y(), r.width(), r.height(), contact_class_.toStdString(),
                                     partner_fixed_};
        if (contact_areas_.empty())
            contact_areas_.push_back(b);
        else
            contact_areas_.front() = b;
        setStatus(contact_vibration_
                      ? tr("Snack 2 done — partner touch (gold). Optional: + Another, or continue to Feel → Create.")
                      : tr("Optional contact zone set."));
    }
    emit contactChanged();
    return true;
}

bool GeneratorController::addMaskRoi(const QRect& area) {
    const QRect r = area.normalized().intersected(QRect(QPoint(0, 0), video_size_));
    mark_ignore_ = mark_contact_ = mark_extra_ = false;
    if (!hasVideo() || r.width() < 8 || r.height() < 8) {
        emit contactChanged();
        return false;
    }
    mask_rois_.push_back(r);
    // 0.7: "Ignore region (black) #N added…" (1-based).
    setStatus(tr("Ignore region (black) #%1 added.").arg(static_cast<int>(mask_rois_.size())));
    emit contactChanged();
    return true;
}

void GeneratorController::removeMaskRoi(int index) {
    if (index < 0 || index >= static_cast<int>(mask_rois_.size())) return;
    mask_rois_.erase(mask_rois_.begin() + index);
    emit contactChanged();
}

void GeneratorController::clearMaskRois() {
    if (mask_rois_.empty() && !mark_ignore_) return;
    mask_rois_.clear();
    mark_ignore_ = false;
    emit contactChanged();
}

void GeneratorController::removeContactArea(int index) {
    if (index < 0 || index >= static_cast<int>(contact_areas_.size())) return;
    contact_areas_.erase(contact_areas_.begin() + index);
    // An extra moved up to primary takes a class (the primary has one).
    if (index == 0 && !contact_areas_.empty() && contact_areas_.front().cls.empty())
        contact_areas_.front().cls = contact_class_.toStdString();
    emit contactChanged();
}

void GeneratorController::setAreaClass(int index, const QString& cls) {
    if (index < 0 || index >= static_cast<int>(contact_areas_.size())) return;
    const bool primary = index == 0;
    if (primary ? !contactClasses().contains(cls) : (!cls.isEmpty() && !contactClasses().contains(cls))) return;
    if (contact_areas_[index].cls == cls.toStdString()) return;
    contact_areas_[index].cls = cls.toStdString();
    setStatus(primary ? tr("Contact type → %1").arg(cls)
                      : tr("Extra contact class → %1").arg(cls.isEmpty() ? tr("any") : cls));
    emit contactChanged();
}

void GeneratorController::clearContactAreas() {
    if (contact_areas_.empty() && !mark_contact_ && !mark_extra_) return;
    contact_areas_.clear();
    mark_contact_ = mark_extra_ = false;
    emit contactChanged();
}

void GeneratorController::clearExtras() {
    mark_extra_ = mark_ignore_ = false;
    const bool had_extras = contact_areas_.size() > 1;
    const bool had_masks = !mask_rois_.empty();
    if (had_extras) contact_areas_.resize(1);
    mask_rois_.clear();
    setStatus(had_masks && !had_extras ? tr("Soft masks cleared.")
              : had_masks              ? tr("Extra contact areas and soft masks cleared.")
                                       : tr("Extra contact areas cleared."));
    emit contactChanged();
}

void GeneratorController::deleteMark(int index) {
    if (index == -1) {
        if (!hasBox()) return;
        clearBox();
        setStatus(tr("Tip mark cleared — Find tip area or paint again."));
    } else if (index >= kMaskSelectBase) {
        const int mi = index - kMaskSelectBase;
        if (mi < 0 || mi >= static_cast<int>(mask_rois_.size())) return;
        removeMaskRoi(mi);
        setStatus(tr("Ignore region removed."));
    } else if (index >= 0 && index < static_cast<int>(contact_areas_.size())) {
        removeContactArea(index);
        setStatus(index == 0 ? tr("Contact mark cleared.") : tr("Extra contact removed."));
    }
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
    if (!hasVideo()) {
        setStatus(tr("Choose a video first."));
        return false;
    }
    if (!hasBox()) {
        // 0.7: with no tip, Create finds the tip first, then creates.
        if (!finding_ && !findTip()) return false;
        create_after_find_ = true;
        create_overwrite_ = overwrite;
        setStatus(tr("Finding tip, then creating…"));
        return true;
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
    applySwitches(req.signal, req.track_cache_dir);
    req.auto_ozone = auto_ozone_;
    req.contact_vibration = contact_vibration_;
    req.contact_vibration_span = contact_span_;
    req.contact_vibration_curve = contact_curve_.toStdString();
    req.tip_class = tip_class_.toStdString();
    if (!contact_areas_.empty()) {
        req.contact_area = contact_areas_.front();
        req.extra_areas.assign(contact_areas_.begin() + 1, contact_areas_.end());
    }
    for (const QRect& r : mask_rois_) req.mask_rois.emplace_back(r.x(), r.y(), r.width(), r.height());
    const uint64_t id = runner_->Start(std::move(req));
    if (id == 0) return false;
    run_id_ = id;
    running_ = true;
    percent_ = 0;
    progress_text_ = QStringLiteral("0 %");
    output_path_.clear();
    review_.clear();
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
    if (finding_) {
        cancelFind();
        setStatus(tr("Tip find canceled."));
        return;
    }
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
            buildReview(f);
            {
                QString done = tr("Done — Emotion Script ready in Play.");
                if (review_.value("oStartS").toInt() >= 0)
                    done += tr(" — O-marker set: %1s–%2s").arg(review_.value("oStartS").toInt()).arg(review_.value("oEndS").toInt());
                setStatus(done + tr(". It is open below for editing."));
            }
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
    if (e.kind == engine::GenerateEventKind::kDone) {
        applyFeel(output_path_);  // 0.7: the feel goes in right after Create
        emit created(output_path_);
    }
}

// 0.7's quality box (Quality Doctor on the actions, the tracker's loss) and
// postgen.js review (direction, O-zone) for the script just written.
void GeneratorController::buildReview(const std::filesystem::path& funscript) {
    review_.clear();
    std::vector<samn::Action> actions;
    QString reason;
    int lost = 0;
    try {
        actions = samn::LoadFunscript(funscript).actions;
    } catch (const std::exception&) {
        return;
    }
    // The engine's tracking record (metadata.native_pipeline).
    QFile f(QString::fromStdU16String(funscript.u16string()));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject pipe = QJsonDocument::fromJson(f.readAll()).object().value("metadata").toObject().value("native_pipeline").toObject();
        reason = pipe.value("reason").toString();
        lost = pipe.value("lost").toInt();
    }
    const auto q = samn::EvaluateScriptQuality(actions);
    QStringList warnings;
    for (const auto& w : q.warnings) warnings << QString::fromStdString(w);
    review_["quality"] = int(std::lround(q.score * 100));
    review_["qualityPassed"] = q.passed;
    review_["qualityWarnings"] = warnings;
    review_["trackingReason"] = reason;
    review_["lostFrames"] = lost;
    int o_start = -1, o_end = -1;
    try {
        const auto markers = samn::LoadOMarkers(funscript);
        if (!markers.empty()) {
            o_start = int(std::lround(markers.front().start_ms / 1000.0));
            o_end = int(std::lround(markers.front().end_ms / 1000.0));
        }
    } catch (const std::exception&) {
    }
    review_["oStartS"] = o_start;
    review_["oEndS"] = o_end;
    const auto pol = samn::SuggestPolarity(actions);
    review_["direction"] = QString::fromStdString(pol.reason);
    review_["suggestInvert"] = pol.suggest_invert;
    const auto oz = samn::SuggestOZone(actions);
    review_["ozone"] = oz.ok ? tr("O-zone suggested: %1s–%2s").arg(std::lround(oz.start_ms / 1000.0)).arg(std::lround(oz.end_ms / 1000.0))
                             : tr("O-zone: %1").arg(QString::fromStdString(oz.reason));
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
