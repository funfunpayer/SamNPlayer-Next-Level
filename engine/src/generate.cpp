#include "samn/engine/generate.h"

#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "samn/contact_marks.h"
#include "samn/fileio.h"
#include "samn/ozone.h"
#include "samn/samn_file.h"
#include "samn/engine/track_cache.h"

namespace samn::engine {

namespace {
// Motion under this share of the box size is tracker wobble (spec FR-15).
constexpr double kMinTravelShare = 0.02;
}  // namespace

namespace {

// 0.7 writeNativeFunscriptNamed: actions plus the native_pipeline record.
std::string FunscriptJson(const GenerateRequest& req, const std::vector<Action>& actions, const TrackResult& tr,
                          const Document& samn) {
    nlohmann::json acts = nlohmann::json::array();
    for (const Action& a : actions) acts.push_back({{"at", a.at}, {"pos", a.pos}});
    nlohmann::json pipeline{{"tracking", "samn_engine"},
                            {"signal", "signal_chain"},
                            {"backend", "csrt"},
                            {"frames", tr.stats.total_frames},
                            {"lost", tr.stats.lost_frames},
                            {"range_px", tr.horizontal ? tr.stats.horizontal_range : tr.stats.vertical_range},
                            {"valid_frames", tr.stats.valid_frames},
                            {"confidence", tr.stats.confidence}};
    if (!tr.stats.reason.empty()) pipeline["reason"] = tr.stats.reason;
    nlohmann::json meta{{"creator", "SamNPlayer 2 (samn_engine)"},
                        {"duration", actions.empty() ? 0 : actions.back().at},
                        {"native_pipeline", pipeline}};
    const std::string profile = NormalizeProfile(req.signal.profile);
    if (!profile.empty() && profile != "standard") meta["profile"] = profile;
    else if (req.contact_vibration) meta["profile"] = "standard";
    if (IsDistanceProfile(req.signal.profile) || req.contact_vibration || samn.contact_marks) {
        // The same recipe and marks as the .samn, through the one serializer.
        // The tip path stays in the .samn only: it is long, and the Player
        // reads the .samn first.
        const nlohmann::json doc = nlohmann::json::parse(MarshalDocument(samn));
        if ((IsDistanceProfile(req.signal.profile) || req.contact_vibration) && doc.contains("recipe"))
            meta["device_recipe"] = doc["recipe"];
        if (doc.contains("contactMarks")) meta["contact_marks"] = doc["contactMarks"];
    }
    return nlohmann::json{{"actions", acts}, {"metadata", meta}}.dump(2);
}

// Tip (+ optional partner) path for spatial contact (0.7 buildTrajectoryData):
// box centres in frame pixels, one point per kTipPathStepMs, 0.1 px.
constexpr int64_t kTipPathStepMs = 50;

TrajectoryData BuildTrajectory(const TrackResult& tr) {
    TrajectoryData t;
    t.width = tr.size.width;
    t.height = tr.size.height;
    const auto tenth = [](double v) { return std::round(v * 10.0) / 10.0; };
    const bool have_partner =
        !tr.partner_xs.empty() && tr.partner_xs.size() == tr.times_ms.size() &&
        tr.partner_ys.size() == tr.times_ms.size();
    for (size_t i = 0; i < tr.times_ms.size() && i < tr.xs.size() && i < tr.frame_ys.size(); ++i) {
        if (!t.tip.empty() && tr.times_ms[i] - t.tip.back().at_ms < kTipPathStepMs) continue;
        t.tip.push_back({tr.times_ms[i], tenth(tr.xs[i]), tenth(tr.frame_ys[i])});
        if (have_partner) t.partner.push_back({tr.times_ms[i], tenth(tr.partner_xs[i]), tenth(tr.partner_ys[i])});
    }
    return t;
}

// 0.7 writeCompanionSamn for the single-box path.
Document CompanionSamn(const GenerateRequest& req, const std::vector<Action>& actions, const TrackResult& tr) {
    Document d;
    d.version = SamnCurrentVersion;
    d.kind = SamnKind;
    d.creator = "SamNPlayer";
    d.video_path = req.video.u8string();
    d.profile = NormalizeProfile(req.signal.profile);
    d.general = actions;
    d.playback_source = PlaybackRecipe;
    d.duration_ms = actions.empty() ? 0 : actions.back().at;
    if (IsDistanceProfile(req.signal.profile)) {
        d.recipe = RecipeMeta(req.signal.profile);
        if (req.contact_vibration)
            ApplyContactVibration(d.recipe, req.contact_vibration_span, req.contact_vibration_curve);
        d.strength_presets = DefaultStrengthPresets();
        d.active_strength = "normal";
        d.BakeNeoAxes();
    } else if (req.contact_vibration) {
        // Everyday stroke with contact vibration: the same recipe as 0.7.
        DeviceRecipe r;
        r.sync = SyncModeName(SyncMode::Independent);
        r.smoothing = 0.3;
        if (d.profile == ProfileWeich) r = RecipeMeta(ProfileWeich);
        ApplyContactVibration(r, req.contact_vibration_span, req.contact_vibration_curve);
        d.recipe = r;
        if (d.profile.empty()) d.profile = "standard";
    }
    ContactMarkInput marks;
    marks.profile = req.signal.profile;
    marks.tip_class = req.tip_class;
    if (req.box.width > 0 && req.box.height > 0)
        marks.tip = ContactMarkBox{req.box.x, req.box.y, req.box.width, req.box.height, req.tip_class, false};
    // The tip box is always stored (0.7 withTipROI); the contact areas only
    // with contact vibration, which is all they are for (0.7 hid them then).
    if (req.contact_vibration || IsDistanceProfile(req.signal.profile)) {
        marks.primary = req.contact_area;
        marks.extras = req.extra_areas;
    }
    d.contact_marks = BuildContactMarks(marks);
    if (d.contact_marks && HasContactAreas(*d.contact_marks)) d.trajectory = BuildTrajectory(tr);
    return d;
}

}  // namespace

namespace {

void GenerateUnguarded(const GenerateRequest& req, uint64_t run_id, const std::function<bool()>& cancel,
                       const GenerateCallback& on_event) {
    auto emit = [&](GenerateEventKind kind, std::string message, int percent = -1, size_t actions = 0) {
        if (on_event) on_event({run_id, kind, percent, std::move(message), actions});
    };
    std::error_code ec;
    const std::filesystem::path samn_path = std::filesystem::u8path(CompanionSamnPath(req.output.u8string()));
    if (!req.overwrite && (std::filesystem::exists(req.output, ec) || std::filesystem::exists(samn_path, ec))) {
        emit(GenerateEventKind::kError, "the script exists already: " + req.output.filename().u8string());
        return;
    }
    if (req.end_ms > 0 && req.end_ms <= req.start_ms) {
        emit(GenerateEventKind::kError, "the end time must lie after the start time");
        return;
    }
    TrackOptions to;
    to.start_ms = req.start_ms;
    to.end_ms = req.end_ms;
    to.max_frames = req.max_frames;
    to.max_analysis_fps = req.max_analysis_fps;
    to.camera_compensation = req.camera_compensation;
    to.scene_cuts = req.scene_cuts;
    to.axis = req.axis;
    to.mask_rois = req.mask_rois;
    // G2b: gold contact → partner path (fixed box or second CSRT).
    // Tf/Tj: tip↔partner 2D distance drives the stroke (GD-08); everyday
    // contact vibration stays tip-axis.
    if (req.contact_area && req.contact_area->w > 0 && req.contact_area->h > 0) {
        to.partner_box = cv::Rect(static_cast<int>(req.contact_area->x), static_cast<int>(req.contact_area->y),
                                  static_cast<int>(req.contact_area->w), static_cast<int>(req.contact_area->h));
        to.partner_fixed = req.contact_area->fixed;
        to.distance_stroke = IsDistanceProfile(req.signal.profile);
    }
    to.cancel = cancel;
    // Progress from the first frame on (GD-21).
    to.on_progress = [&](int done, int total) {
        emit(GenerateEventKind::kProgress, "tracking", total > 0 ? std::min(99, done * 100 / total) : -1);
    };
    std::string error;
    const std::string cache_key =
        req.track_cache_dir.empty() ? std::string() : TrackCacheKey(req.video, req.box, to);
    std::optional<TrackResult> tr;
    if (!cache_key.empty()) tr = LoadTrackCache(req.track_cache_dir, cache_key);
    if (tr) {
        emit(GenerateEventKind::kNote, "tracking: from the cache");
    } else {
        tr = TrackBox(req.video, req.box, to, &error);
        if (!tr) {
            emit(GenerateEventKind::kError, error);
            return;
        }
        if (tr->canceled) {
            emit(GenerateEventKind::kCanceled, "canceled");
            return;
        }
        if (!cache_key.empty()) {
            // A cache that cannot be written costs speed next time, not this script.
            try {
                SaveTrackCache(req.track_cache_dir, cache_key, *tr);
            } catch (const std::exception& e) {
                emit(GenerateEventKind::kNote, std::string("tracking cache not written: ") + e.what());
            }
        }
    }
    // Spec FR-15 / T5: a box that hardly moves (under 2 % of its size between
    // the 2nd and 98th percentile) is tracker wobble, not a stroke. 0.7's
    // error, not a flat script: a flat script would hide a wrong box (GD-11).
    if (!tr->distance_stroke && tr->positions.size() > 4) {
        const double travel = Percentile(tr->positions, 98) - Percentile(tr->positions, 2);
        const double size = tr->horizontal ? req.box.width : req.box.height;
        if (size > 0 && travel < kMinTravelShare * size) {
            emit(GenerateEventKind::kError,
                 "no discernible motion in the tracked region: check the box size and place (avoid near-full-frame "
                 "boxes); the filters need visible stroke motion");
            return;
        }
    }
    const std::vector<double> times(tr->times_ms.begin(), tr->times_ms.end());
    if (auto warn = AliasingWarning(times, tr->positions)) emit(GenerateEventKind::kNote, *warn);
    std::string note;
    const SignalOptions so = ApplyDefaultDetrend(req.signal, req.stroke_hint, &note);
    if (!note.empty()) emit(GenerateEventKind::kNote, note);
    const auto sig = RunSignal(times, tr->positions, tr->lost, so, &error);
    if (!sig) {
        emit(GenerateEventKind::kError, error);
        return;
    }
    if (sig->outliers > 0 || sig->bridged_gaps > 0 || sig->split_gaps > 0) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "POST: %d outliers, %d gaps bridged, %d gaps left without actions",
                      sig->outliers, sig->bridged_gaps, sig->split_gaps);
        emit(GenerateEventKind::kNote, buf);
    }
    if (cancel && cancel()) {
        emit(GenerateEventKind::kCanceled, "canceled");
        return;
    }
    const std::vector<OMarker> markers = req.auto_ozone ? AutoOZoneMarkers(sig->actions) : std::vector<OMarker>{};
    try {
        Document samn = CompanionSamn(req, sig->actions, *tr);
        samn.o_markers = markers;
        WriteFileAtomic(req.output, FunscriptJson(req, sig->actions, *tr, samn));
        if (!markers.empty()) SaveOMarkers(req.output, markers);
        SaveDocument(samn_path, samn);
    } catch (const std::exception& e) {
        emit(GenerateEventKind::kError, std::string("cannot write the script: ") + e.what());
        return;
    }
    if (!markers.empty()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "O-marker set: %.1f s-%.1f s", markers[0].start_ms / 1000.0,
                      markers[0].end_ms / 1000.0);
        emit(GenerateEventKind::kNote, buf);
    }
    emit(GenerateEventKind::kDone, req.output.filename().u8string(), 100, sig->actions.size());
}

}  // namespace

void Generate(const GenerateRequest& req, uint64_t run_id, const std::function<bool()>& cancel,
              const GenerateCallback& on_event) {
    // Whatever throws below (OpenCV, FFmpeg, the signal chain) ends this run
    // as an error; on the runner's thread it would end the whole app.
    std::string failure;
    try {
        GenerateUnguarded(req, run_id, cancel, on_event);
        return;
    } catch (const std::exception& e) {
        failure = e.what();
    } catch (...) {
        failure = "unknown error";
    }
    if (on_event) on_event({run_id, GenerateEventKind::kError, -1, "generation failed: " + failure, 0});
}

GenerateRunner::GenerateRunner(GenerateCallback on_event) : on_event_(std::move(on_event)) {}

GenerateRunner::~GenerateRunner() {
    Cancel();
    Wait();
}

uint64_t GenerateRunner::Start(GenerateRequest request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return 0;
    if (worker_.joinable()) {
        // A restart from the terminal event runs on the old worker itself:
        // it cannot join itself, so it is parked and joined by a later call.
        if (worker_.get_id() == std::this_thread::get_id())
            retired_.push_back(std::move(worker_));
        else
            worker_.join();  // the previous run has ended
    }
    cancel_ = false;
    running_ = true;
    const uint64_t id = ++next_id_;
    current_id_ = id;
    // The run is over once its terminal event is about to go out, so the
    // handler may Start the next one (the flag is not cleared afterwards:
    // by then it may belong to that next run).
    auto finish = [this, id] {
        std::lock_guard<std::mutex> l(mutex_);
        if (current_id_ == id) running_ = false;
    };
    worker_ = std::thread([this, id, finish, req = std::move(request)] {
        Generate(req, id, [this] { return cancel_.load(); }, [this, finish](const GenerateEvent& e) {
            if (e.kind == GenerateEventKind::kDone || e.kind == GenerateEventKind::kCanceled ||
                e.kind == GenerateEventKind::kError)
                finish();
            if (on_event_) on_event_(e);
        });
        finish();
    });
    return id;
}

void GenerateRunner::Cancel() { cancel_ = true; }

void GenerateRunner::Wait() {
    // Joined outside the lock: the worker's own handler may call Start.
    for (;;) {
        std::vector<std::thread> threads;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& t : retired_) threads.push_back(std::move(t));
            retired_.clear();
            if (worker_.joinable()) threads.push_back(std::move(worker_));
        }
        bool joined = false;
        for (auto& t : threads) {
            if (t.get_id() == std::this_thread::get_id()) {
                std::lock_guard<std::mutex> lock(mutex_);
                retired_.push_back(std::move(t));  // cannot join itself
            } else {
                t.join();
                joined = true;
            }
        }
        if (!joined) return;
    }
}

}  // namespace samn::engine
