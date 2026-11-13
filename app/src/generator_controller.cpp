#include "generator_controller.h"

#include "app_log.h"
#include "frame_cache.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <optional>
#include <utility>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "qt_paths.h"
#include "samn/vision/canceled.h"
#include "samn/video_info.h"
#include "samn/engine/generate_queue.h"
#include "samn/engine/track_box.h"
#include "samn/engine/tip_find.h"
#include "samn/engine/tip_resolve.h"
#include "samn/contact_marks.h"
#include "samn/contact_preview.h"
#include "samn/ozone.h"
#include "samn/polarity.h"
#include "samn/pov.h"
#include "samn/samn_file.h"
#include "samn/script_quality.h"
#include "samn/settings.h"
#include "samn/vision/frame_reader.h"
#include "samn/vision/onnx_detector.h"
#include "ai_tip.h"

namespace samn::ui {

namespace {

QString Clock(double ms) {
    const int s = int(ms / 1000);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

// The values behind the opt-in switches (spec FR-12, FR-13 defaults).
constexpr double kGapBridgeMs = 500;
constexpr double kOutlierMadFactor = 5;

QVariantMap HitMap(const AiTipHit& h) {
    return QVariantMap{{"x", h.x}, {"y", h.y},     {"w", h.w},     {"h", h.h},
                       {"score", h.score}, {"label", QString::fromStdString(h.label)}, {"cls", h.cls}};
}

AiTipHit HitFromMap(const QVariantMap& m) {
    AiTipHit h;
    h.x = m.value("x").toInt();
    h.y = m.value("y").toInt();
    h.w = m.value("w").toInt();
    h.h = m.value("h").toInt();
    h.score = m.value("score").toFloat();
    h.cls = m.value("cls").toInt();
    h.label = m.value("label").toString().toStdString();
    return h;
}

std::vector<AiTipHit> HitsFromList(const QVariantList& list) {
    std::vector<AiTipHit> hits;
    hits.reserve(size_t(list.size()));
    for (const QVariant& v : list) hits.push_back(HitFromMap(v.toMap()));
    return hits;
}

// Create-only: a strong class match may install the tip box without Apply
// (Suggest tip still needs Apply, D-057). Score gate matches "strong".
constexpr float kStrongAiTipScore = 0.5f;

std::optional<AiTipHit> StrongAiTipSeed(const QImage& frame, const QSize& video, const QString& tipClass, bool fakeReady,
                                        const QVariantList& fakeHits) {
    std::vector<AiTipHit> hits;
    if (fakeReady) {
        hits = HitsFromList(fakeHits);
    } else {
        auto model = FindAiTipModel();
        if (!model || frame.isNull()) return std::nullopt;
        const QImage copy = frame.convertToFormat(QImage::Format_BGR888);
        if (copy.isNull()) return std::nullopt;
        cv::Mat mat(copy.height(), copy.width(), CV_8UC3, const_cast<uchar*>(copy.constBits()),
                    size_t(copy.bytesPerLine()));
        mat = mat.clone();
        const double sx = video.width() > 0 ? double(video.width()) / copy.width() : 1.0;
        const double sy = video.height() > 0 ? double(video.height()) / copy.height() : 1.0;
        auto det = std::make_shared<samn::vision::OnnxDetector>();
        std::string why;
        if (!det->Load(model->path, &why)) return std::nullopt;
        for (AiTipHit h : ListAiHits(det->Detect(mat, 0.25f, &why), *model)) {
            h.x = int(std::lround(h.x * sx));
            h.y = int(std::lround(h.y * sy));
            h.w = int(std::lround(h.w * sx));
            h.h = int(std::lround(h.h * sy));
            hits.push_back(std::move(h));
        }
    }
    auto pick = PickExpectedTip(hits, tipClass.toStdString());
    if (!pick || pick->score < kStrongAiTipScore) return std::nullopt;
    return pick;
}

struct TipSearchFound {
    cv::Rect box;
    double score = 0;
    int64_t start_ms = 0;
    std::vector<samn::engine::TipCandidate> candidates;
    bool from_ai = false;
};

// Hybrid tip proposal: student → YOLO → NudeNet when present.
// Returns empty when ONNX is unavailable or no matching tip-class box.
std::function<std::optional<cv::Rect>(const cv::Mat&)> MakeHybridPropose(const QString& tipClass) {
    auto model = FindAiTipModel();
    if (!model) return {};
    auto det = std::make_shared<samn::vision::OnnxDetector>();
    std::string err;
    if (!det->Load(model->path, &err)) return {};
    const std::string want = tipClass.toStdString();
    return [det, model = *model, want](const cv::Mat& bgr) -> std::optional<cv::Rect> {
        std::string why;
        const auto pick = PickExpectedTip(ListAiHits(det->Detect(bgr, 0.25f, &why), model), want);
        if (!pick) return std::nullopt;
        return cv::Rect(pick->x, pick->y, pick->w, pick->h);
    };
}

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
    refreshAiTipHint();
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
        sliding_ = flag("generator.sliding_dynamics", sliding_);
        adaptive_keyframes_ = flag("generator.adaptive_keyframe", adaptive_keyframes_);
        setMotionAxis(str("generator.axis", motion_axis_));
        if (const auto v = s.get("generator.smooth_window"); v.is_number()) setSmoothWindow(v.get<int>());
        if (const auto v = s.get("generator.min_peak_distance_ms"); v.is_number()) setMinPeakDistanceMs(v.get<int>());
        if (const auto v = s.get("generator.peak_prominence"); v.is_number()) setPeakProminence(v.get<double>());
        if (const auto v = s.get("generator.rdp_tolerance"); v.is_number()) setRdpTolerance(v.get<double>());
        if (const auto v = s.get("generator.max_speed"); v.is_number()) setMaxSpeed(v.get<int>());
        auto_ozone_ = flag("generator.auto_ozone", auto_ozone_);
        auto_tip_ = flag("generator.auto_tip", auto_tip_);
        gap_handling_ = flag("generator.gap_handling", gap_handling_);
        outlier_rejection_ = flag("generator.outlier_rejection", outlier_rejection_);
        track_cache_ = flag("generator.track_cache", track_cache_);
        // FunGen2 has no Advanced tip-loss toggle: when a model is present and
        // the user never set the key, default AI re-acquire on so a painted /
        // Found tip path is helped without opening Advanced.
        if (const auto v = s.get("generator.hybrid_reacquire"); v.is_boolean()) {
            hybrid_reacquire_ = v.get<bool>();
        } else if (aiTipAvailable()) {
            hybrid_reacquire_ = true;
        }
        if (const auto v = s.get("generator.max_analysis_fps"); v.is_number()) setMaxAnalysisFps(v.get<int>());
        if (const auto v = s.get("generator.editor_share"); v.is_number()) setEditorShare(v.get<double>());
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
    // Off is -1: Recommended would otherwise turn it on by itself.
    signal.outlier_mad_factor = outlier_rejection_ ? kOutlierMadFactor : -1;
    // G4a: Sliding on → 3000 ms; off → -1 so profile recipes do not refill it.
    signal.dynamic_range_ms = sliding_ ? 3000 : -1;
    // G4a: Adaptive Keyframes on → error 6 (0.7); off → 0.
    signal.adaptive_error = adaptive_keyframes_ ? 6 : 0;
    // G4b Expert: 0 leaves the profile / PostOptions default (or off for RDP / max speed).
    signal.smooth_window = smooth_window_;
    signal.min_peak_distance_ms = min_peak_distance_ms_;
    signal.peak_prominence = peak_prominence_;
    signal.rdp_tolerance = rdp_tolerance_;
    signal.max_speed = max_speed_;
    // D-160 device fit: a stroker keeps 0.7's 100 ms between actions.
    if (profile_ == QLatin1String(samn::ProfileRecommended) && feelKind() == QLatin1String("stroker"))
        signal.min_action_interval_ms = 100;
    track_cache_dir = track_cache_ ? samn::config_dir() / "cache" / "track" : std::filesystem::path();
}

engine::TrackAxis GeneratorController::trackAxis() const {
    if (motion_axis_ == QLatin1String("x")) return engine::TrackAxis::kX;
    if (motion_axis_ == QLatin1String("y")) return engine::TrackAxis::kY;
    return engine::TrackAxis::kAuto;
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
    cancelTipPreview();
    ++ai_seq_;  // drop a late suggest result
    if (queue_cancel_) *queue_cancel_ = true;
    if (finder_.joinable()) finder_.join();
    if (preview_.joinable()) preview_.join();
    if (ai_worker_.joinable()) ai_worker_.join();
    if (queue_worker_.joinable()) queue_worker_.join();
    runner_.reset();
}

bool GeneratorController::autoFinding() const {
    if (!finding_) return false;
    if (refind_) return pending_auto_;
    // A search for a video opened before is nobody's: it only ends.
    return find_auto_ || find_video_ != video_path_;
}

bool GeneratorController::findTip() {
    if (video_path_.isEmpty() || running_ || queue_running_) return false;
    if (finding_) {
        const bool usable = find_video_ == video_path_ && !(find_cancel_ && *find_cancel_);
        if (usable && !find_auto_) return false;  // the user's own search: one at a time
        // A click while the automatic search runs takes it over when it looks
        // from the shown frame; any other search ends, and a new one starts
        // from the shown frame once it has.
        find_auto_ = pending_auto_ = false;
        if (!usable || find_start_ms_ != int64_t(std::floor(preview_ms_))) {
            refind_ = true;
            if (find_cancel_) *find_cancel_ = true;
        }
        emit findingChanged();
        return true;
    }
    return startTipSearch(false);
}

bool GeneratorController::startTipSearch(bool resolve) {
    if (video_path_.isEmpty() || running_ || queue_running_ || finding_) return false;
    if (finder_.joinable()) finder_.join();
    find_cancel_ = std::make_shared<std::atomic<bool>>(false);
    finding_ = true;
    find_resolve_ = resolve;
    find_auto_ = false;  // openVideo sets it after this returns
    find_video_ = video_path_;
    emit findingChanged();
    if (!resolve) setStatus(tr("Looking for the rhythmic motion…"));
    const quint64 edits = box_edits_;
    find_start_ms_ = int64_t(std::floor(preview_ms_));
    const QImage frameCopy = frame_;
    const QSize videoSize = video_size_;
    const QString tipClass = tip_class_;
    const bool fakeReady = fake_ai_ready_;
    const QVariantList fakeHits = fake_ai_hits_;
    QPointer<GeneratorController> self(this);
    finder_ = std::thread([self, edits, cancel = find_cancel_, forVideo = video_path_,
                           startMs = int64_t(std::floor(preview_ms_)), resolve, frameCopy, videoSize, tipClass,
                           fakeReady, fakeHits, video = std::filesystem::path(video_path_.toStdU16String())] {
        std::string error;
        std::optional<TipSearchFound> r;
        try {
            if (resolve) {
                // Guest Create: short windows, shown frame first (kCreateTipMaxSeconds).
                samn::engine::TipResolveOptions o;
                o.preview_ms = startMs;
                o.cancel = [cancel] { return cancel->load(); };
                if (auto tip = samn::engine::ResolveTipForCreate(video, o, &error)) {
                    r = TipSearchFound{tip->box, tip->score, tip->start_ms, tip->candidates, false};
                } else if (error != samn::kCanceled) {
                    if (auto ai = StrongAiTipSeed(frameCopy, videoSize, tipClass, fakeReady, fakeHits)) {
                        r = TipSearchFound{cv::Rect(ai->x, ai->y, ai->w, ai->h), double(ai->score), startMs, {}, true};
                        error.clear();
                    }
                }
            } else {
                // Find tip / auto-on-open: still from the shown frame, but snappy
                // (12 s) — FunGen2 does not chew 45 s before Create.
                samn::engine::TipFindOptions o;
                o.start_ms = startMs;
                o.max_seconds = 12;
                o.cancel = [cancel] { return cancel->load(); };
                if (auto tip = samn::engine::FindTip(video, o, &error))
                    r = TipSearchFound{tip->box, tip->candidates.empty() ? 0 : tip->candidates.front().score, startMs,
                                       tip->candidates, false};
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, r = std::move(r), error, cancel, forVideo, edits, resolve] {
            if (!self) return;
            self->finding_ = false;
            self->find_resolve_ = false;
            const bool thenCreate = std::exchange(self->create_after_find_, false);
            const bool automatic = std::exchange(self->find_auto_, false) && !thenCreate;
            const bool again = std::exchange(self->refind_, false);
            const bool againAutomatic = std::exchange(self->pending_auto_, false) && !thenCreate;
            emit self->findingChanged();
            // A run or the queue started meanwhile: the box belongs to it.
            if (self->running_ || self->queue_running_) return;
            if (again) {
                // From another frame, or for a video opened meanwhile.
                self->create_after_find_ = thenCreate;
                const bool ok = thenCreate ? self->startTipSearch(true) : self->findTip();
                if (!ok) self->create_after_find_ = false;
                else if (thenCreate) self->setStatus(tr("Finding tip, then creating…"));
                else if (againAutomatic) {
                    self->find_auto_ = true;
                    emit self->findingChanged();
                }
                return;
            }
            if (forVideo != self->video_path_) return;  // another video was opened meanwhile
            // Ended by Cancel, Create or the queue: nothing to set; a stopped
            // search of the user's says so, even if it had just finished.
            if (cancel->load()) {
                if (!automatic) self->setStatus(tr("Find tip: canceled."));
                return;
            }
            if (!r) {
                if (automatic) {
                    self->setStatus(tr("Create finds the tip — no box to paint."));
                    return;
                }
                if (thenCreate && error != samn::kCanceled) {
                    if (!resolve) {
                        self->create_after_find_ = true;
                        self->setStatus(tr("Retrying find…"));
                        if (!self->startTipSearch(true)) {
                            self->create_after_find_ = false;
                            self->setStatus(tr("Could not find the tip — Create again, or Find tip as override."));
                            AppLog::instance().reportError(self->status_);
                        }
                        return;
                    }
                    self->setStatus(tr("Could not find the tip — Create again, or Find tip as override."));
                    AppLog::instance().reportError(self->status_);
                } else
                    self->setStatus(error == samn::kCanceled ? tr("Find tip: canceled.")
                                                            : tr("Find tip: %1").arg(QString::fromStdString(error)));
                return;
            }
            QVariantList list;
            for (const auto& c : r->candidates)
                list.append(QVariantMap{{"x", c.box.x}, {"y", c.box.y}, {"w", c.box.width}, {"h", c.box.height}, {"score", c.score}});
            self->tip_candidates_ = list;
            emit self->tipCandidatesChanged();
            const bool weak = r->from_ai ? false
                                         : (r->candidates.empty() || r->candidates.front().score < samn::engine::kWeakTipScore);
            if (automatic && (weak || self->box_edits_ != edits)) {
                // Unsure, or the user drew a box meanwhile: candidates only (D-159).
                self->setStatus(self->box_edits_ != edits
                                    ? tr("Your box stays. The dashed boxes are what Find tip saw; click one to use it.")
                                    : tr("Not sure where the tip is: Create will search again, or Find tip / drag as override."));
                return;
            }
            if (thenCreate && weak && !resolve) {
                self->create_after_find_ = true;
                self->setStatus(tr("Retrying find…"));
                if (!self->startTipSearch(true)) {
                    self->create_after_find_ = false;
                    self->setStatus(tr("Could not find the tip — Create again, or Find tip as override."));
                    AppLog::instance().reportError(self->status_);
                }
                return;
            }
            // Resolver/FindTip start_ms: track from the winning window, not only the shown frame.
            self->installBox(QRect(r->box.x, r->box.y, r->box.width, r->box.height), double(r->start_ms));
            if (thenCreate) {
                if (weak) {
                    self->setStatus(tr("Could not find the tip — Create again, or Find tip as override."));
                    AppLog::instance().reportError(self->status_);
                } else
                    self->create(self->create_overwrite_);
                return;
            }
            if (r->from_ai)
                self->setStatus(tr("Tip found. The dashed boxes are other candidates; click one to use it."));
            else
                self->setStatus(weak ? tr("Only weak motion found: check the box, or Find tip / drag as override.")
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
    if (queue_running_ || running_) return false;
    std::vector<std::pair<int, std::filesystem::path>> items;
    for (int i = 0; i < queue_.size(); ++i) {
        const QVariantMap m = queue_.at(i).toMap();
        if (m.value("status").toString() == QLatin1String("queued"))
            items.emplace_back(i, std::filesystem::path(m.value("path").toString().toStdU16String()));
    }
    if (items.empty()) return false;
    if (finding_) {
        // The automatic search only competes; the user's own ends first.
        if (!autoFinding() && !*find_cancel_) {
            setStatus(tr("Find tip is still running — stop it or let it finish, then create the queue."));
            return false;
        }
        cancelFind();
    }
    if (queue_worker_.joinable()) queue_worker_.join();
    engine::QueueSettings settings;
    settings.signal.profile = profile_.toStdString();
    settings.signal.invert = invert_;
    settings.axis = trackAxis();
    settings.camera_compensation = camera_;
    settings.scene_cuts = scene_cuts_;
    settings.auto_ozone = auto_ozone_;
    settings.overwrite = overwrite;
    applySwitches(settings.signal, settings.track_cache_dir);
    settings.max_analysis_fps = max_analysis_fps_;
    settings.hybrid_reacquire = hybrid_reacquire_;
    if (hybrid_reacquire_) settings.ai_propose = MakeHybridPropose(tip_class_);
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
    // Videos whose script this queue wrote, also in an earlier run: one script
    // name, one video.
    std::vector<std::filesystem::path> written;
    for (const QVariant& v : queue_) {
        const QVariantMap m = v.toMap();
        if (m.value("status").toString() == QLatin1String("done"))
            written.emplace_back(m.value("path").toString().toStdU16String());
    }
    QPointer<GeneratorController> self(this);
    queue_worker_ = std::thread([self, items, settings, earlier = std::move(written), cancel = queue_cancel_]() mutable {
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
            const std::string clash = engine::ScriptNameCollision(earlier, video);
            if (!clash.empty()) {
                const QString message = QString::fromStdString(clash);
                post([index, message](GeneratorController* g) { g->setQueueItem(index, "failed", message); });
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
            if (r.status == engine::QueueItemStatus::kDone) earlier.push_back(video);  // only a written script blocks
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
    const bool wasAutomatic = autoFinding();
    create_after_find_ = false;
    refind_ = pending_auto_ = false;
    if (find_cancel_) *find_cancel_ = true;
    if (autoFinding() != wasAutomatic) emit findingChanged();
}

void GeneratorController::pickCandidate(int index) {
    if (index < 0 || index >= tip_candidates_.size()) return;
    const QVariantMap c = tip_candidates_.at(index).toMap();
    setBox(QRect(c.value("x").toInt(), c.value("y").toInt(), c.value("w").toInt(), c.value("h").toInt()));
}

bool GeneratorController::aiTipAvailable() const {
    return fake_ai_ready_ || AiTipModelStatus() == AiTipReady::kReady;
}

void GeneratorController::refreshAiTipHint() {
    if (suggesting_ai_tip_) {
        ai_tip_hint_ = tr("Looking only for %1 (strict AI; no class fallback)…").arg(tip_class_);
        return;
    }
    if (hasAiTipProposal()) {
        ai_tip_hint_ = tr("AI proposal ready — Apply to set the tip box. AI never writes the curve.");
        return;
    }
    if (hasAiContactProposal()) {
        const QString label = ai_contact_proposal_.value(QStringLiteral("label")).toString();
        ai_tip_hint_ = tr("AI contact candidate (%1) — Apply contact to add. Does not set the tip.").arg(label);
        return;
    }
    if (!ai_detections_.isEmpty()) {
        ai_tip_hint_ = tr("%n AI box(es). Click = tip proposal (Apply), Shift-click = contact.", nullptr,
                          ai_detections_.size());
        return;
    }
    if (fake_ai_ready_) {
        ai_tip_hint_ = tr("Local AI model ready — Suggest tip does not write the box until Apply.");
        return;
    }
    switch (AiTipModelStatus()) {
        case AiTipReady::kNoOnnx:
            ai_tip_hint_ = tr("This build has no ONNX Runtime — Suggest tip (AI) is off.");
            break;
        case AiTipReady::kNoModel:
            ai_tip_hint_ = tr("No AI model found (models/student/student.onnx, models/yolo.onnx, or models/nudenet.onnx). "
                              "Train the student, choose a YOLO .onnx, or download NudeNet in AI Train.");
            break;
        case AiTipReady::kReady:
            if (const auto m = FindAiTipModel(); m && m->kind == AiTipKind::kYolo)
                ai_tip_hint_ = tr("YOLO ready — Suggest tip does not write the box until Apply.");
            else
                ai_tip_hint_ = tr("Local AI model ready — Suggest tip does not write the box until Apply.");
            break;
    }
}

void GeneratorController::setFakeAiHitsForTest(const QVariantList& hits) {
    fake_ai_ready_ = true;
    fake_ai_hits_ = hits;
    refreshAiTipHint();
    emit aiTipChanged();
}

bool GeneratorController::proposeTipAndSecondFromHits(const std::vector<AiTipHit>& hits) {
    ai_tip_proposal_.clear();
    ai_contact_proposal_.clear();
    const auto tip = PickExpectedTip(hits, tip_class_.toStdString());
    if (!tip) return false;
    const auto contact = PickBestContact(hits, tip_class_.toStdString(), contact_class_.toStdString());
    if (!contact) return false;
    ai_tip_proposal_ = HitMap(*tip);
    QVariantMap pending = HitMap(*contact);
    if (const auto mapped = ContactClassForLabel(contact->label))
        pending.insert(QStringLiteral("cls"), QString::fromStdString(*mapped));
    ai_contact_proposal_ = pending;
    return true;
}

bool GeneratorController::tipAndSecondSeedsAvailable() const {
    const auto hits = HitsFromList(ai_detections_);
    if (!PickExpectedTip(hits, tip_class_.toStdString())) return false;
    return PickBestContact(hits, tip_class_.toStdString(), contact_class_.toStdString()).has_value();
}

void GeneratorController::finishAiSuggest(const QVariantList& hits, const QString& forVideo, quint64 seq, int mode) {
    if (seq != ai_seq_ || forVideo != video_path_) return;
    suggesting_ai_tip_ = false;
    ai_detections_ = hits;
    ai_tip_proposal_.clear();
    ai_contact_proposal_.clear();
    const auto parsed = HitsFromList(hits);
    if (mode == 2) {
        if (proposeTipAndSecondFromHits(parsed))
            setStatus(tr("Tip+2nd ready — Apply tip, then Apply contact. AI never writes the curve."));
        else if (hits.isEmpty())
            setStatus(tr("No AI boxes on this frame."));
        else
            setStatus(tr("Tip+2nd needs tip (%1) and a contact box on this frame.").arg(tip_class_));
    } else if (mode == 1) {
        if (const auto pick = PickExpectedTip(parsed, tip_class_.toStdString())) ai_tip_proposal_ = HitMap(*pick);
        if (hits.isEmpty())
            setStatus(tr("No AI boxes on this frame."));
        else if (hasAiTipProposal())
            setStatus(tr("AI proposal ready — Apply to set the tip box."));
        else
            setStatus(tr("%n AI box(es), none match %1. Click one, then Apply.", nullptr, hits.size()).arg(tip_class_));
    } else {
        if (hits.isEmpty())
            setStatus(tr("No AI boxes on this frame."));
        else
            setStatus(tr("%n AI box(es). Click = tip proposal (Apply), Shift-click = contact.", nullptr, hits.size()));
    }
    refreshAiTipHint();
    emit aiTipChanged();
}

bool GeneratorController::suggestAiTip() { return runAiDetect(1); }

bool GeneratorController::listAiDetections() { return runAiDetect(0); }

bool GeneratorController::suggestTipAndSecond() {
    if (!ai_detections_.isEmpty() && tipAndSecondSeedsAvailable()) {
        if (!proposeTipAndSecondFromHits(HitsFromList(ai_detections_))) return false;
        setStatus(tr("Tip+2nd ready — Apply tip, then Apply contact. AI never writes the curve."));
        refreshAiTipHint();
        emit aiTipChanged();
        return true;
    }
    return runAiDetect(2);
}

bool GeneratorController::runAiDetect(int mode) {
    if (video_path_.isEmpty() || running_ || queue_running_ || suggesting_ai_tip_) return false;
    if (!aiTipAvailable()) {
        refreshAiTipHint();
        setStatus(ai_tip_hint_);
        emit aiTipChanged();
        return false;
    }
    if (frame_.isNull()) {
        setStatus(tr("No frame to search — open a video first."));
        return false;
    }
    const quint64 seq = ++ai_seq_;
    suggesting_ai_tip_ = true;
    refreshAiTipHint();
    emit aiTipChanged();
    setStatus(ai_tip_hint_);
    if (fake_ai_ready_) {
        finishAiSuggest(fake_ai_hits_, video_path_, seq, mode);
        return true;
    }
    if (ai_worker_.joinable()) ai_worker_.join();
    const QImage copy = frame_.convertToFormat(QImage::Format_BGR888);
    const QSize video = video_size_;
    const QString forVideo = video_path_;
    QPointer<GeneratorController> self(this);
    ai_worker_ = std::thread([self, copy, video, forVideo, seq, mode] {
        QVariantList list;
        QString error;
        do {
            auto model = FindAiTipModel();
            if (!model) {
                error = QStringLiteral("no-model");
                break;
            }
            cv::Mat mat(copy.height(), copy.width(), CV_8UC3, const_cast<uchar*>(copy.constBits()),
                        size_t(copy.bytesPerLine()));
            mat = mat.clone();
            const double sx = video.width() > 0 ? double(video.width()) / copy.width() : 1.0;
            const double sy = video.height() > 0 ? double(video.height()) / copy.height() : 1.0;
            auto det = std::make_shared<samn::vision::OnnxDetector>();
            std::string why;
            if (!det->Load(model->path, &why)) {
                error = QString::fromStdString(why);
                break;
            }
            for (AiTipHit h : ListAiHits(det->Detect(mat, 0.25f, &why), *model)) {
                h.x = int(std::lround(h.x * sx));
                h.y = int(std::lround(h.y * sy));
                h.w = int(std::lround(h.w * sx));
                h.h = int(std::lround(h.h * sy));
                list.append(HitMap(h));
            }
        } while (false);
        QMetaObject::invokeMethod(
            qApp,
            [self, list, error, forVideo, seq, mode] {
                if (!self) return;
                if (seq != self->ai_seq_ || forVideo != self->video_path_) {
                    self->suggesting_ai_tip_ = false;
                    emit self->aiTipChanged();
                    return;
                }
                if (!error.isEmpty() && list.isEmpty()) {
                    self->suggesting_ai_tip_ = false;
                    self->setStatus(error == QLatin1String("no-model")
                                        ? tr("No AI model found (models/student/student.onnx, models/yolo.onnx, or models/nudenet.onnx).")
                                        : tr("AI tip failed: %1.").arg(error));
                    self->refreshAiTipHint();
                    emit self->aiTipChanged();
                    return;
                }
                self->finishAiSuggest(list, forVideo, seq, mode);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool GeneratorController::applyAiTip() {
    if (!hasAiTipProposal()) return false;
    const QVariantMap p = ai_tip_proposal_;
    setBox(QRect(p.value("x").toInt(), p.value("y").toInt(), p.value("w").toInt(), p.value("h").toInt()));
    setStatus(tr("Tip box applied from AI proposal."));
    ai_tip_proposal_.clear();
    refreshAiTipHint();
    emit aiTipChanged();
    return true;
}

void GeneratorController::dismissAiTip() {
    if (ai_detections_.isEmpty() && ai_tip_proposal_.isEmpty() && ai_contact_proposal_.isEmpty() &&
        !suggesting_ai_tip_)
        return;
    ++ai_seq_;
    suggesting_ai_tip_ = false;
    ai_detections_.clear();
    ai_tip_proposal_.clear();
    ai_contact_proposal_.clear();
    setStatus(tr("AI tip proposal dismissed."));
    refreshAiTipHint();
    emit aiTipChanged();
}

void GeneratorController::pickAiDetection(int index) {
    if (index < 0 || index >= ai_detections_.size()) return;
    ai_tip_proposal_ = ai_detections_.at(index).toMap();
    setStatus(tr("AI proposal ready — Apply to set the tip box."));
    refreshAiTipHint();
    emit aiTipChanged();
}

bool GeneratorController::addContactFromDetection(const QVariantMap& d, const QString& cls) {
    const QRect box(d.value(QStringLiteral("x")).toInt(), d.value(QStringLiteral("y")).toInt(),
                    d.value(QStringLiteral("w")).toInt(), d.value(QStringLiteral("h")).toInt());
    const bool extra = mark_extra_ && !contact_areas_.empty();
    if (!addContactArea(box)) return false;
    const int at = extra ? int(contact_areas_.size()) - 1 : 0;
    if (!cls.isEmpty()) setAreaClass(at, cls);
    return true;
}

bool GeneratorController::pickAiDetectionAsContact(int index) {
    if (index < 0 || index >= ai_detections_.size()) return false;
    const QVariantMap d = ai_detections_.at(index).toMap();
    const QString label = d.value(QStringLiteral("label")).toString();
    const auto mapped = ContactClassForLabel(label.toStdString());
    const QString cls = mapped ? QString::fromStdString(*mapped) : QString();
    // Safe + Contact vibration on: add now (same as Find-tip Shift-click).
    // Anything else stays a pending candidate — never writes the tip box.
    if (mapped && contact_vibration_) {
        ai_contact_proposal_.clear();
        if (!addContactFromDetection(d, cls)) return false;
        refreshAiTipHint();
        emit aiTipChanged();
        return true;
    }
    QVariantMap pending = d;
    if (!cls.isEmpty()) pending.insert(QStringLiteral("cls"), cls);
    ai_contact_proposal_ = pending;
    setStatus(contact_vibration_
                  ? tr("AI contact candidate (%1) — Apply contact to add. Does not set the tip.").arg(label)
                  : tr("AI contact candidate (%1) — turn on Contact vibration, then Apply contact.").arg(label));
    refreshAiTipHint();
    emit aiTipChanged();
    return false;
}

bool GeneratorController::applyAiContact() {
    if (!hasAiContactProposal()) return false;
    const QVariantMap p = ai_contact_proposal_;
    QString cls = p.value(QStringLiteral("cls")).toString();
    if (cls.isEmpty()) {
        if (const auto mapped = ContactClassForLabel(p.value(QStringLiteral("label")).toString().toStdString()))
            cls = QString::fromStdString(*mapped);
        else
            cls = contact_class_;
    }
    if (!addContactFromDetection(p, cls)) return false;
    ai_contact_proposal_.clear();
    setStatus(tr("Contact area applied from AI proposal."));
    refreshAiTipHint();
    emit aiTipChanged();
    return true;
}

QString GeneratorController::videoName() const { return QFileInfo(video_path_).fileName(); }

bool GeneratorController::openVideo(const QString& pathOrUrl, bool lookForTip) {
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
    box_ms_ = 0;
    cancelTipPreview();
    clearTipPath();
    contact_areas_.clear();  // drawn on the old video
    mask_rois_.clear();
    mark_contact_ = mark_extra_ = mark_ignore_ = false;
    emit contactChanged();
    cancelFind();  // a search on the old video ends; its result is not used
    tip_candidates_.clear();
    emit tipCandidatesChanged();
    ai_detections_.clear();
    ai_tip_proposal_.clear();
    ai_contact_proposal_.clear();
    ++ai_seq_;
    refreshAiTipHint();
    emit aiTipChanged();
    output_path_.clear();
    review_.clear();
    log_.clear();
    percent_ = 0;
    progress_text_.clear();
    emit videoChanged();
    emit boxChanged();
    emit boxLabelChanged();
    emit outputChanged();
    emit logChanged();
    emit progressChanged();
    emit stepChanged();
    // 0.7's poster: 10 % into the clip, at most 30 s in.
    seekPreview(std::min(duration_ms_ * 0.1, 30000.0));
    setStatus(tr("Create finds the tip — no box to paint."));
    if (auto_tip_ && lookForTip && findTip()) {
        if (refind_) pending_auto_ = true;  // starts once the old video's search has ended
        else find_auto_ = true;
    }
    emit findingChanged();
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

void GeneratorController::setBox(const QRect& box) { installBox(box, preview_ms_); }

void GeneratorController::installBox(const QRect& box, double atMs) {
    // Inside the frame; 0.7 treats boxes under 8 px as a click, not a box.
    const QRect r = box.normalized().intersected(QRect(QPoint(0, 0), video_size_));
    const QRect next = r.width() >= 8 && r.height() >= 8 ? r : QRect();
    // The box belongs to the frame it was drawn on: tracking starts there
    // (0.7 startTimeSec), not at 0 where the tip is somewhere else.
    if (next == box_ && box_ms_ == atMs) return;
    box_ = next;
    box_ms_ = atMs;
    ++box_edits_;
    emit boxChanged();
    emit boxLabelChanged();
    emit stepChanged();
    refreshPov();
    // Advanced stays closed for Everyday: when the user (or Create) sets a tip
    // and a local model exists, turn AI re-acquire on unless they saved Off.
    if (!next.isNull()) ensureHybridAssistForTipPath();
    scheduleTipPreview();
}

void GeneratorController::clearBox() { setBox(QRect()); }

QString GeneratorController::boxLabel() const {
    if (!hasBox()) return tr("Create finds the tip — no box to paint.");
    const QString coords = tr("Tip (stroke tracker): x=%1 y=%2 w=%3 h=%4")
                               .arg(box_.x())
                               .arg(box_.y())
                               .arg(box_.width())
                               .arg(box_.height());
    return hasTipPath() ? tr("%1 — follows motion; drag still edits the seed.").arg(coords)
                        : tr("%1 — CSRT follows this box only.").arg(coords);
}

void GeneratorController::setProfile(const QString& p) {
    // Tf/Tj need a contact mark (P3.4); the Generator offers the stroke styles.
    const QString v = p == QLatin1String("weich") || p == QLatin1String("autotune") ||
                              p == QLatin1String(samn::ProfileRecommended)
                          ? p
                          : QStringLiteral("standard");
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
SAMN_SETTER(setSlidingDynamics, sliding_, "generator.sliding_dynamics")
SAMN_SETTER(setAdaptiveKeyframes, adaptive_keyframes_, "generator.adaptive_keyframe")
SAMN_SETTER(setAutoOZone, auto_ozone_, "generator.auto_ozone")
SAMN_SETTER(setAutoTip, auto_tip_, "generator.auto_tip")
SAMN_SETTER(setGapHandling, gap_handling_, "generator.gap_handling")
SAMN_SETTER(setOutlierRejection, outlier_rejection_, "generator.outlier_rejection")
SAMN_SETTER(setTrackCache, track_cache_, "generator.track_cache")
SAMN_SETTER(setHybridReacquire, hybrid_reacquire_, "generator.hybrid_reacquire")
#undef SAMN_SETTER

void GeneratorController::setContactVibration(bool v) {
    if (contact_vibration_ == v) return;
    contact_vibration_ = v;
    saveSetting("generator.contact_vibration", v);
    emit optionsChanged();
    emit povChanged();  // first-person contact suggestion follows the switch
}

void GeneratorController::setMaxAnalysisFps(int v) {
    // 0 = every frame; otherwise 5..60 in steps of 5 (FR-7 analysis rate cap).
    int n = std::clamp(v, 0, 60);
    if (n > 0) n = std::max(5, (n / 5) * 5);
    if (n == max_analysis_fps_) return;
    max_analysis_fps_ = n;
    saveSetting("generator.max_analysis_fps", n);
    emit optionsChanged();
}

void GeneratorController::setEditorShare(double v) {
    const double n = std::clamp(v, 0.18, 0.65);
    if (std::abs(n - editor_share_) < 0.0005) return;
    editor_share_ = n;
    saveSetting("generator.editor_share", n);
    emit optionsChanged();
}

void GeneratorController::setMotionAxis(const QString& axis) {
    const QString v = axis == QLatin1String("x") || axis == QLatin1String("y") ? axis : QStringLiteral("auto");
    if (v == motion_axis_) return;
    motion_axis_ = v;
    saveSetting("generator.axis", v.toStdString());
    emit optionsChanged();
}

void GeneratorController::setSmoothWindow(int v) {
    // Odd window 0..51 (0 = profile default). Even values round down to odd ≥1 when >0.
    int n = std::clamp(v, 0, 51);
    if (n > 0 && n % 2 == 0) --n;
    if (n == smooth_window_) return;
    smooth_window_ = n;
    saveSetting("generator.smooth_window", n);
    emit optionsChanged();
}

void GeneratorController::setMinPeakDistanceMs(int v) {
    // 0..500, step 10 (0 = profile default).
    const int n = std::clamp(v, 0, 500) / 10 * 10;
    if (n == min_peak_distance_ms_) return;
    min_peak_distance_ms_ = n;
    saveSetting("generator.min_peak_distance_ms", n);
    emit optionsChanged();
}

void GeneratorController::setPeakProminence(double v) {
    // 0..1, step 0.05 (0 = profile default).
    double n = std::clamp(v, 0.0, 1.0);
    n = std::round(n / 0.05) * 0.05;
    if (std::abs(n - peak_prominence_) < 1e-9) return;
    peak_prominence_ = n;
    saveSetting("generator.peak_prominence", n);
    emit optionsChanged();
}

void GeneratorController::setRdpTolerance(double v) {
    // 0..20, step 0.5 (0 = off).
    double n = std::clamp(v, 0.0, 20.0);
    n = std::round(n * 2.0) / 2.0;
    if (std::abs(n - rdp_tolerance_) < 1e-9) return;
    rdp_tolerance_ = n;
    saveSetting("generator.rdp_tolerance", n);
    emit optionsChanged();
}

void GeneratorController::setMaxSpeed(int v) {
    // 0..800, step 50 (0 = off).
    const int n = std::clamp(v, 0, 800) / 50 * 50;
    if (n == max_speed_) return;
    max_speed_ = n;
    saveSetting("generator.max_speed", n);
    emit optionsChanged();
}

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

QVariantMap GeneratorController::contactFeelProbe() const {
    const samn::ContactVibrationPreview p =
        samn::PreviewContactVibration(contact_span_, contact_curve_.toStdString());
    QVariantList stroke, vib, peaks;
    stroke.reserve(int(p.stroke.size()));
    vib.reserve(int(p.vib.size()));
    peaks.reserve(int(p.peaks.size()));
    for (const auto& pt : p.stroke) stroke << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    for (const auto& pt : p.vib) vib << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    for (const auto& pt : p.peaks) peaks << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    return {{"activePct", p.active_pct},
            {"peak", p.peak},
            {"hint", QString::fromStdString(p.hint)},
            {"stroke", stroke},
            {"vib", vib},
            {"peaks", peaks}};
}

void GeneratorController::setOptimizeTarget(const QString& target) {
    const QString v = target == QLatin1String("general") || target == QLatin1String("neo2") ||
                             target == QLatin1String("off")
                         ? target
                         : QString();
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

void GeneratorController::ensureHybridAssistForTipPath() {
    if (hybrid_reacquire_ || !aiTipAvailable()) return;
    try {
        if (settings_) {
            const auto v = settings_->get("generator.hybrid_reacquire");
            if (v.is_boolean() && !v.get<bool>()) return;  // user saved Off
        }
    } catch (const std::exception&) {
    }
    hybrid_reacquire_ = true;
    emit optionsChanged();
}

void GeneratorController::setFeelDevice(const QString& d) {
    if (d == feel_device_ || !kFeelDevices.contains(d)) return;
    feel_device_ = d;
    saveSetting("generator.feel.device", d.toStdString());
    // Optimize on Review follows Feel when unset — no Advanced open needed.
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
    // 0.7 sceneFromHints with the primary area's class (no AI scene type yet),
    // but only when a contact area is marked: the class defaults to nipples,
    // and every plain Create would otherwise get the breast feel.
    if (contact_areas_.empty()) return {};
    const QString cls = QString::fromStdString(contact_areas_.front().cls);
    if (cls == QLatin1String("mouth")) return QStringLiteral("oral");
    if (cls == QLatin1String("nipples") || cls == QLatin1String("breasts")) return QStringLiteral("breast");
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

QString GeneratorController::povHint() const {
    const bool has_contact = !contact_areas_.empty();
    const auto advice = samn::AdvisePovContact(
        pov_kind_ == QLatin1String("first_person")
            ? samn::PovKind::FirstPerson
            : (pov_kind_ == QLatin1String("third_person") ? samn::PovKind::ThirdPerson : samn::PovKind::Unknown),
        has_contact);
    if (advice == samn::PovContactAdvice::SuggestContact && contact_vibration_)
        return tr("Close-up — a contact area may help.");
    if (advice == samn::PovContactAdvice::SuggestContact || advice == samn::PovContactAdvice::CloseUp)
        return tr("Close-up.");
    if (advice == samn::PovContactAdvice::WideView) return tr("Wide view.");
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
        // Stay fixed (0.7): keep the magenta box where drawn; off = record
        // its own centre path (track_multi / trajectory.extras).
        const bool stay = extras_stay_fixed_;
        contact_areas_.push_back({r.x(), r.y(), r.width(), r.height(), extra_class_.toStdString(), stay});
        const int n = static_cast<int>(contact_areas_.size()) - 1;
        const QString how = stay ? tr("stay fixed") : tr("follows own path");
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
    emit povChanged();
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
    emit povChanged();
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
    emit povChanged();
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
           std::filesystem::exists(std::filesystem::u8path(samn::CompanionSamnPath(PathUtf8(f))), ec);
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
        // FunGen2: with no tip, Create finds the tip (resolver); a search
        // already looking at this video is waited for, and is the user's
        // from now on. A weak first find retries the resolver.
        create_after_find_ = true;
        create_overwrite_ = overwrite;
        find_auto_ = pending_auto_ = false;
        emit findingChanged();
        if (finding_) {
            const bool usable = find_video_ == video_path_ && !(find_cancel_ && *find_cancel_);
            if (!usable) {
                refind_ = true;
                if (find_cancel_) *find_cancel_ = true;
            }
            setStatus(tr("Finding tip, then creating…"));
            return true;
        }
        if (!startTipSearch(true)) {
            create_after_find_ = false;
            return false;
        }
        setStatus(tr("Finding tip, then creating…"));
        return true;
    }
    cancelTipPreview();  // keep the last preview path; Create will replace it
    engine::GenerateRequest req;
    req.video = std::filesystem::path(video_path_.toStdU16String());
    req.box = cv::Rect(box_.x(), box_.y(), box_.width(), box_.height());
    req.start_ms = int64_t(std::floor(box_ms_));
    req.camera_compensation = camera_;
    req.scene_cuts = scene_cuts_;
    req.axis = trackAxis();
    req.signal.profile = profile_.toStdString();
    req.signal.invert = invert_;
    req.output = funscriptPath();
    req.overwrite = overwrite;
    applySwitches(req.signal, req.track_cache_dir);
    req.max_analysis_fps = max_analysis_fps_;
    req.hybrid_reacquire = hybrid_reacquire_;
    if (hybrid_reacquire_) req.ai_propose = MakeHybridPropose(tip_class_);
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
    // The user's box is used: an automatic search would only compete.
    if (autoFinding()) cancelFind();
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
            output_path_ = QString::fromStdString(samn::CompanionSamnPath(PathUtf8(f)));
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
        loadTipPathFromSamn(output_path_);
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

QVariantMap GeneratorController::tipAt(double scriptMs) const {
    const auto p = samn::PathAt(tipPath_, int64_t(std::llround(scriptMs)));
    if (!p) return {};
    return {{"x", p->x}, {"y", p->y}};
}

void GeneratorController::setTipPathForTest(const QVariantList& points) {
    cancelTipPreview();
    std::vector<samn::TrajectoryPoint> path;
    path.reserve(size_t(points.size()));
    for (const QVariant& v : points) {
        const QVariantMap m = v.toMap();
        path.push_back({int64_t(std::llround(m.value(QStringLiteral("at")).toDouble())),
                        m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()});
    }
    applyTipPath(std::move(path));
}

void GeneratorController::applyTipPath(std::vector<samn::TrajectoryPoint> path) {
    samn::TrajectoryData data;
    data.tip = std::move(path);
    tipPath_ = samn::SortedTipPath(data);
    ++tip_path_revision_;
    emit tipPathChanged();
    emit boxLabelChanged();
    refreshPov();
}

void GeneratorController::clearTipPath() {
    if (tipPath_.empty()) {
        refreshPov();
        return;
    }
    tipPath_.clear();
    ++tip_path_revision_;
    emit tipPathChanged();
    emit boxLabelChanged();
    refreshPov();
}

void GeneratorController::refreshPov() {
    samn::PovTipPath path;
    if (hasTipPath() && hasBox() && video_size_.width() > 0 && video_size_.height() > 0) {
        path.width = video_size_.width();
        path.height = video_size_.height();
        path.box_w = box_.width();
        path.box_h = box_.height();
        path.tip.reserve(tipPath_.size());
        for (const auto& p : tipPath_) path.tip.push_back({p.at_ms, p.x, p.y});
    }
    const auto view = samn::ClassifyTipPov(path);
    pov_kind_ = QString::fromUtf8(samn::PovKindName(view.kind));
    pov_confidence_ = view.confidence;
    emit povChanged();
}

void GeneratorController::loadTipPathFromSamn(const QString& samnPath) {
    if (samnPath.isEmpty()) return;
    try {
        const samn::Document doc = samn::LoadDocument(std::filesystem::path(samnPath.toStdU16String()));
        if (!doc.trajectory || doc.trajectory->tip.empty()) return;
        applyTipPath(samn::SortedTipPath(*doc.trajectory));
    } catch (const std::exception&) {
    }
}

void GeneratorController::cancelTipPreview() {
    ++preview_seq_;
    if (preview_cancel_) *preview_cancel_ = true;
}

void GeneratorController::scheduleTipPreview() {
    cancelTipPreview();
    if (!hasBox() || video_path_.isEmpty()) {
        clearTipPath();
        return;
    }
    if (running_ || queue_running_) return;
    const quint64 seq = preview_seq_;
    QTimer::singleShot(0, this, [this, seq] { startTipPreview(seq); });
}

void GeneratorController::startTipPreview(quint64 seq) {
    if (seq != preview_seq_) return;
    if (!hasBox() || video_path_.isEmpty() || running_ || queue_running_) return;
    if (preview_.joinable()) preview_.join();
    preview_cancel_ = std::make_shared<std::atomic<bool>>(false);
    const QRect box = box_;
    const double at = box_ms_;
    const QString video = video_path_;
    const bool cuts = scene_cuts_;
    const engine::TrackAxis axis = trackAxis();
    QPointer<GeneratorController> self(this);
    preview_ = std::thread([self, seq, box, at, video, cuts, axis, cancel = preview_cancel_] {
        engine::TrackOptions to;
        to.start_ms = int64_t(std::floor(at));
        to.max_analysis_fps = 8;  // lighter than Create; overlay only
        to.max_frames = 200;      // ~25 s overlay seed — guest path stays snappy
        to.camera_compensation = false;
        to.scene_cuts = cuts;
        to.appearance_memory = false;
        to.axis = axis;
        to.cancel = [cancel] { return cancel->load(); };
        std::string error;
        auto tr = engine::TrackBox(std::filesystem::path(video.toStdU16String()),
                                   cv::Rect(box.x(), box.y(), box.width(), box.height()), to, &error);
        QMetaObject::invokeMethod(
            qApp,
            [self, seq, tr = std::move(tr)] {
                if (!self || seq != self->preview_seq_) return;
                if (!tr || tr->canceled || tr->frame_xs.empty()) return;
                const auto data = engine::BuildTrajectory(*tr);
                if (data.tip.empty()) return;
                self->applyTipPath(samn::SortedTipPath(data));
            },
            Qt::QueuedConnection);
    });
}

}  // namespace samn::ui
