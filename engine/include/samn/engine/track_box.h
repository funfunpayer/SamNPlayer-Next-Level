#pragma once

// Single-box tracking (P3.1): port of 0.7's trackcv.TrackROI. CSRT follows
// the box; scene cuts, camera compensation, appearance memory, the jump
// guard and a short velocity coast keep the track honest. The result is the
// raw box-centre track; post-processing (P3.2) turns it into actions.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace samn::engine {

enum class TrackAxis { kAuto, kX, kY };

struct TrackOptions {
    int64_t start_ms = 0;        // video time of the first tracked frame
    int max_frames = 0;          // 0 = to the end
    bool camera_compensation = true;
    bool scene_cuts = true;
    bool appearance_memory = true;
    TrackAxis axis = TrackAxis::kAuto;
    // Soft masks / ignore regions (0.7 maskRois): punched out of the camera-
    // motion estimate together with the tip box, so a wrong latch (knees,
    // background) does not pull the compensation. Video pixels.
    std::vector<cv::Rect> mask_rois;
    // Optional gold contact / partner box (0.7 TrackTwoPoints FixedB /
    // CaptureTrajectory). Centres fill TrackResult::partner_xs / partner_ys
    // for .samn trajectory.partner. Stroke stays tip-axis unless
    // distance_stroke is set (Tf/Tj).
    std::optional<cv::Rect> partner_box;
    bool partner_fixed = false;  // true = keep the drawn box (no second CSRT)
    // When true with partner_box: positions = TipPartnerDistance each frame
    // (GD-08). Tip/partner centres and trajectory still recorded as usual.
    bool distance_stroke = false;
    // Checked every frame; true stops the run (Result::canceled).
    std::function<bool()> cancel;
    // About every 1 % of frames: (done, total); total 0 when unknown.
    std::function<void(int, int)> on_progress;
};

struct TrackStats {
    int lost_frames = 0;         // frames the tracker had no box
    int camera_frames_lost = 0;  // frames without a camera estimate
    int total_frames = 0;
    int valid_frames = 0;        // frames with a real tracker box
    double vertical_range = 0;
    double horizontal_range = 0;
    double confidence = 0;       // valid / total
    std::string reason;          // "", "tracker_lost_elevated", "tracker_lost_heavy"
};

struct TrackResult {
    std::vector<int64_t> times_ms;  // video time per frame
    std::vector<double> positions;  // chosen axis, or tip↔partner distance (pixels)
    std::vector<double> xs, ys;     // box centre per frame (y camera-compensated)
    std::vector<double> frame_ys;   // box centre y in frame pixels, before compensation (tip path)
    // Partner centres in frame pixels (same length as tip when partner_box set).
    std::vector<double> partner_xs, partner_ys;
    bool horizontal = false;        // positions are xs (false when distance_stroke)
    bool distance_stroke = false;   // positions are TipPartnerDistance
    cv::Size size;
    std::vector<int> scene_cuts;    // frame numbers counted from the first tracked frame
    TrackStats stats;
    bool canceled = false;
};

// nullopt with *error set when the video cannot be read.
std::optional<TrackResult> TrackBox(const std::filesystem::path& video, const cv::Rect& box,
                                    const TrackOptions& options, std::string* error);

}  // namespace samn::engine
