#include "samn/engine/track_box.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "samn/vision/frame_reader.h"
#include "samn/vision/tracker.h"
#include "track_parts.h"

namespace samn::engine {

namespace {

constexpr int kRememberEveryNFrames = 25;

double Ptp(const std::vector<double>& v) {
    if (v.empty()) return 0;
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return *hi - *lo;
}

cv::Point2d Centre(const cv::Rect& r) { return {r.x + r.width / 2.0, r.y + r.height / 2.0}; }

// 0.7 progressReporter: about 100 updates per run.
class Progress {
public:
    Progress(const std::function<void(int, int)>& fn, int total) : fn_(fn), total_(std::max(0, total)) {}
    void Report(int done) {
        if (!fn_) return;
        if (done < next_ && (total_ <= 0 || done < total_)) return;
        fn_(done, total_);
        next_ = done + (total_ > 0 ? std::max(1, total_ / 100) : 10);
    }

private:
    const std::function<void(int, int)>& fn_;
    int total_;
    int next_ = 0;
};

}  // namespace

int ProgressTotal(int64_t frame_count, int64_t first_index, int max_frames) {
    const bool known = frame_count > 0;
    int64_t total = known ? std::max<int64_t>(0, frame_count - first_index) : 0;
    if (max_frames > 0 && (!known || max_frames < total)) total = max_frames;
    return static_cast<int>(total);
}

std::optional<TrackResult> TrackBox(const std::filesystem::path& video, const cv::Rect& box,
                                    const TrackOptions& opts, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return std::optional<TrackResult>();
    };
    auto reader = vision::FrameReader::Open(video, error);
    if (!reader) return std::nullopt;
    if (opts.start_ms > 0 && !reader->SeekMs(opts.start_ms)) return fail("video cannot seek");

    vision::VideoFrame frame;
    if (!reader->Read(frame)) return fail("first frame cannot be read");
    std::string why;
    std::unique_ptr<vision::Tracker> tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, &why);
    if (!tracker) return fail(why);
    tracker->Init(frame.bgr, box);

    const bool needs_gray = opts.camera_compensation || opts.scene_cuts || opts.appearance_memory;
    cv::Mat prev_gray, gray;
    if (needs_gray) cv::cvtColor(frame.bgr, prev_gray, cv::COLOR_BGR2GRAY);

    std::optional<AppearanceMemory> memory;
    if (opts.appearance_memory) {
        memory.emplace();
        memory->Remember(prev_gray, box);  // the user-confirmed start region
    }

    TrackResult res;
    res.size = reader->size();
    res.times_ms.push_back(frame.pts_us / 1000);
    res.xs.push_back(Centre(box).x);
    res.ys.push_back(Centre(box).y);
    std::vector<double> camera_dy{0.0};
    cv::Rect last = box;
    Coast coast;
    coast.ObserveOk(box);
    DispGuard guard;

    Progress progress(opts.on_progress, ProgressTotal(reader->frameCount(), frame.index, opts.max_frames));
    progress.Report(0);

    // A fresh CSRT on the found box, as 0.7 did after every reacquire.
    auto reanchor = [&](const cv::Rect& found) {
        tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, &why);
        if (tracker) tracker->Init(frame.bgr, found);
        return tracker != nullptr;
    };

    int idx = 1, valid = 1;
    while (reader->Read(frame)) {
        if (opts.cancel && opts.cancel()) {
            TrackResult canceled;
            canceled.canceled = true;
            return canceled;
        }
        if (needs_gray) cv::cvtColor(frame.bgr, gray, cv::COLOR_BGR2GRAY);
        const bool cut = opts.scene_cuts && IsSceneCut(prev_gray, gray);

        std::optional<cv::Rect> found;
        if (cut) {
            res.scene_cuts.push_back(idx);
            // No re-init on the old box after a hard cut: a tip invented on
            // the old pixel latches onto the background.
            if (memory)
                if (auto r = memory->Reacquire(gray); r && reanchor(*r)) found = r;
        } else {
            found = tracker->Update(frame.bgr);
            if (found) {
                // Integer centres as 0.7 (x + w/2 in ints) for the jump test.
                const double dist = std::hypot(double(found->x + found->width / 2 - last.x - last.width / 2),
                                               double(found->y + found->height / 2 - last.y - last.height / 2));
                if (guard.Implausible(dist))
                    found.reset();
                else
                    guard.Accept(dist);
            }
            if (!found && memory)
                if (auto r = memory->Reacquire(gray); r && reanchor(*r)) found = r;
        }
        if (!tracker) return fail(why);

        if (!found) {
            ++res.stats.lost_frames;
            if (auto c = coast.OnLost()) {
                res.xs.push_back(Centre(*c).x);
                res.ys.push_back(Centre(*c).y);
                last = *c;
            } else {
                res.xs.push_back(res.xs.back());
                res.ys.push_back(res.ys.back());
            }
        } else {
            coast.ObserveOk(*found);
            ++valid;
            res.xs.push_back(Centre(*found).x);
            res.ys.push_back(Centre(*found).y);
            last = *found;
            // Only crops that still resemble the start feed the memory, so
            // slow drift never poisons a later reacquire.
            if (memory && idx % kRememberEveryNFrames == 0) {
                const auto score = memory->MatchesOriginal(gray, *found);
                if (!score || *score >= AppearanceMemory::kMinScore) memory->Remember(gray, *found);
            }
        }

        if (opts.camera_compensation) {
            double dy = 0;
            if (!cut) {
                // Tip box plus soft masks (0.7 punch exclude): keep wrong
                // latch regions out of the camera estimate.
                std::vector<cv::Rect> excludes = opts.mask_rois;
                excludes.push_back(last);
                dy = CameraMotionY(prev_gray, gray, excludes);
                if (dy == 0.0) ++res.stats.camera_frames_lost;
            }
            camera_dy.push_back(cut ? 0.0 : camera_dy.back() + dy);
        } else {
            camera_dy.push_back(0.0);
        }

        if (needs_gray) std::swap(prev_gray, gray);
        res.times_ms.push_back(frame.pts_us / 1000);
        progress.Report(idx);
        ++idx;
        if (opts.max_frames > 0 && idx >= opts.max_frames) break;
    }
    progress.Report(idx);

    res.frame_ys = res.ys;
    if (opts.camera_compensation) ApplyCameraCompensation(res.ys, camera_dy, res.scene_cuts);

    res.stats.vertical_range = Ptp(res.ys);
    res.stats.horizontal_range = Ptp(res.xs);
    const bool auto_x = res.stats.horizontal_range > res.stats.vertical_range * 1.5 && res.stats.horizontal_range > 5;
    res.horizontal = opts.axis == TrackAxis::kX || (opts.axis == TrackAxis::kAuto && auto_x);
    res.positions = res.horizontal ? res.xs : res.ys;

    res.stats.total_frames = idx;
    res.stats.valid_frames = valid;
    res.stats.confidence = double(valid) / idx;
    const double lost = double(res.stats.lost_frames) / idx;
    res.stats.reason = lost > 0.5 ? "tracker_lost_heavy" : lost > 0.15 ? "tracker_lost_elevated" : "";
    return res;
}

}  // namespace samn::engine
