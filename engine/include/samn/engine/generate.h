#pragma once

// One generation run (P3.3): track the box, run the signal chain, write the
// .funscript and its companion .samn as 0.7's native generator did. Runs on
// a worker thread (D-142) with progress, cancel and a run id on every event;
// one run at a time; "canceled" is its own outcome, not an error (GD-25).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

#include "samn/engine/signal_chain.h"
#include "samn/engine/track_box.h"
#include "samn/samn_file.h"

namespace samn::engine {

struct GenerateRequest {
    std::filesystem::path video;
    cv::Rect box;
    int64_t start_ms = 0;
    int64_t end_ms = 0;            // 0 = to the end
    int max_frames = 0;
    double max_analysis_fps = 0;   // 0 = every frame
    // Raw tracks are cached here (spec FR-11); empty = no cache.
    std::filesystem::path track_cache_dir;
    bool camera_compensation = true;
    bool scene_cuts = true;
    TrackAxis axis = TrackAxis::kAuto;
    SignalOptions signal;
    std::optional<StrokeHint> stroke_hint;  // from the stroke preview (P3.6)
    std::filesystem::path output;           // the .funscript; the .samn goes beside it
    bool overwrite = false;                 // overwrite guard: off = never replace a file
    bool auto_ozone = true;                 // primary + up to two secondary O-markers (0.7)
    // Contact vibration and the marked areas (0.7 Options.ContactVibration,
    // RegionClass, ROI2, ExtraTargets); boxes in video pixels. With a contact
    // area the tip's path is stored too, so the Player vibrates when the tip
    // reaches an area.
    bool contact_vibration = false;
    double contact_vibration_span = 0;  // 0 = default
    std::string contact_vibration_curve;
    std::string tip_class;
    std::optional<ContactMarkBox> contact_area;
    std::vector<ContactMarkBox> extra_areas;
    // Ignore regions (0.7 maskRois): black exclusion boxes in video pixels.
    // Passed to TrackBox as camera-motion excludes; they do not drive stroke.
    std::vector<cv::Rect> mask_rois;
};

enum class GenerateEventKind { kProgress, kNote, kDone, kCanceled, kError };

struct GenerateEvent {
    uint64_t run_id = 0;
    GenerateEventKind kind = GenerateEventKind::kProgress;
    int percent = -1;  // 0-100 while tracking; -1 when unknown
    std::string message;
    size_t actions = 0;  // kDone: actions written
};

using GenerateCallback = std::function<void(const GenerateEvent&)>;

// The run itself, on the calling thread. Every event carries run_id.
void Generate(const GenerateRequest& request, uint64_t run_id, const std::function<bool()>& cancel,
              const GenerateCallback& on_event);

// Runs one generation at a time on its own thread. Events arrive on that
// thread; the caller hands them to its UI thread.
class GenerateRunner {
public:
    explicit GenerateRunner(GenerateCallback on_event);
    ~GenerateRunner();  // cancels and waits: nothing outlives the app (GD-25)
    GenerateRunner(const GenerateRunner&) = delete;
    GenerateRunner& operator=(const GenerateRunner&) = delete;

    // The new run's id, or 0 when a run is still going (single instance).
    uint64_t Start(GenerateRequest request);
    void Cancel();
    bool running() const { return running_; }
    // Waits for the current run to end (tests, shutdown).
    void Wait();

private:
    GenerateCallback on_event_;
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    uint64_t next_id_ = 0;
};

}  // namespace samn::engine
