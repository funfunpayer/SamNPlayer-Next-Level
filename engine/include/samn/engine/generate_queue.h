#pragma once
// One video of the Generator's queue (0.7 generator.js "Create queue",
// DeepFunGen-style batch): find the tip on its own (FindTip), then create
// the script beside the video with the queue's feel settings (Generate).
// The queue itself, its order and its statuses, lives in the app.
#include <filesystem>
#include <functional>
#include <string>

#include "samn/engine/generate.h"

namespace samn::engine {

// What every video of the queue is created with: GenerateRequest without the
// video, box and output (those come per item).
struct QueueSettings {
    SignalOptions signal;
    bool camera_compensation = true;
    bool scene_cuts = true;
    bool auto_ozone = true;
    bool overwrite = false;  // otherwise a video that has a script is skipped
    // Contact vibration and the tip class; contact areas are marked per
    // video, so the queue has none.
    bool contact_vibration = false;
    double contact_vibration_span = 0;
    std::string contact_vibration_curve;
    std::string tip_class;
};

enum class QueueItemStatus { kDone, kSkipped, kFailed, kCanceled };

struct QueueItemOutcome {
    QueueItemStatus status = QueueItemStatus::kFailed;
    std::string message;           // why skipped or failed; the file name when done
    std::filesystem::path output;  // the .funscript when done
    cv::Rect box;                  // the tip box Find tip chose
};

// Below this best candidate score the "rhythm" is compression noise (a still
// clip scores about 0.002, a clear stroke above 1); such a video fails
// instead of getting a script from noise.
constexpr double kWeakTipScore = 0.05;

// 0.7 scriptPathForVideo: <video name>.funscript beside the video.
std::filesystem::path ScriptPathForVideo(const std::filesystem::path& video);

// percent covers the whole item: Find tip 0..20, creating 20..100.
QueueItemOutcome GenerateQueueItem(const std::filesystem::path& video, const QueueSettings& settings,
                                   const std::function<bool()>& cancel,
                                   const std::function<void(int percent)>& progress);

}  // namespace samn::engine
