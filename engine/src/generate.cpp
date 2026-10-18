#include "samn/engine/generate.h"

#include <nlohmann/json.hpp>

#include "samn/fileio.h"
#include "samn/samn_file.h"

namespace samn::engine {

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
    if (IsDistanceProfile(req.signal.profile)) {
        // The same recipe as the .samn, through the one serializer.
        const nlohmann::json doc = nlohmann::json::parse(MarshalDocument(samn));
        if (doc.contains("recipe")) meta["device_recipe"] = doc["recipe"];
    }
    return nlohmann::json{{"actions", acts}, {"metadata", meta}}.dump(2);
}

// 0.7 writeCompanionSamn for the single-box path.
Document CompanionSamn(const GenerateRequest& req, const std::vector<Action>& actions) {
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
        d.strength_presets = DefaultStrengthPresets();
        d.active_strength = "normal";
        d.BakeNeoAxes();
    }
    return d;
}

}  // namespace

void Generate(const GenerateRequest& req, uint64_t run_id, const std::function<bool()>& cancel,
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
    TrackOptions to;
    to.start_ms = req.start_ms;
    to.max_frames = req.max_frames;
    to.camera_compensation = req.camera_compensation;
    to.scene_cuts = req.scene_cuts;
    to.axis = req.axis;
    to.cancel = cancel;
    // Progress from the first frame on (GD-21).
    to.on_progress = [&](int done, int total) {
        emit(GenerateEventKind::kProgress, "tracking", total > 0 ? std::min(99, done * 100 / total) : -1);
    };
    std::string error;
    const auto tr = TrackBox(req.video, req.box, to, &error);
    if (!tr) {
        emit(GenerateEventKind::kError, error);
        return;
    }
    if (tr->canceled) {
        emit(GenerateEventKind::kCanceled, "canceled");
        return;
    }
    std::string note;
    const SignalOptions so = ApplyDefaultDetrend(req.signal, req.stroke_hint, &note);
    if (!note.empty()) emit(GenerateEventKind::kNote, note);
    const auto sig = RunSignal(std::vector<double>(tr->times_ms.begin(), tr->times_ms.end()), tr->positions, so, &error);
    if (!sig) {
        emit(GenerateEventKind::kError, error);
        return;
    }
    if (cancel && cancel()) {
        emit(GenerateEventKind::kCanceled, "canceled");
        return;
    }
    try {
        Document samn = CompanionSamn(req, sig->actions);
        WriteFileAtomic(req.output, FunscriptJson(req, sig->actions, *tr, samn));
        SaveDocument(samn_path, samn);
    } catch (const std::exception& e) {
        emit(GenerateEventKind::kError, std::string("cannot write the script: ") + e.what());
        return;
    }
    emit(GenerateEventKind::kDone, req.output.filename().u8string(), 100, sig->actions.size());
}

GenerateRunner::GenerateRunner(GenerateCallback on_event) : on_event_(std::move(on_event)) {}

GenerateRunner::~GenerateRunner() {
    Cancel();
    Wait();
}

uint64_t GenerateRunner::Start(GenerateRequest request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return 0;
    if (worker_.joinable()) worker_.join();  // the previous run has ended
    cancel_ = false;
    running_ = true;
    const uint64_t id = ++next_id_;
    worker_ = std::thread([this, id, req = std::move(request)] {
        Generate(req, id, [this] { return cancel_.load(); }, on_event_);
        running_ = false;
    });
    return id;
}

void GenerateRunner::Cancel() { cancel_ = true; }

void GenerateRunner::Wait() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) worker_.join();
}

}  // namespace samn::engine
