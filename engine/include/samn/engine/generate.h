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

#include <opencv2/core.hpp>

#include "samn/engine/signal_chain.h"
#include "samn/engine/track_box.h"

namespace samn::engine {

struct GenerateRequest {
    std::filesystem::path video;
    cv::Rect box;
    int64_t start_ms = 0;
    int max_frames = 0;
    bool camera_compensation = true;
    bool scene_cuts = true;
    TrackAxis axis = TrackAxis::kAuto;
    SignalOptions signal;
    std::optional<StrokeHint> stroke_hint;  // from the stroke preview (P3.6)
    std::filesystem::path output;           // the .funscript; the .samn goes beside it
    bool overwrite = false;                 // overwrite guard: off = never replace a file
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
