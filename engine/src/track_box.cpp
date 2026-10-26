#include "samn/engine/track_box.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "samn/engine/tip_distance.h"
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

    // Optional partner (gold contact): fixed box or a second CSRT. Centres
    // feed .samn trajectory.partner (G2b). With distance_stroke, positions
    // come from TipPartnerDistance (Tf/Tj / GD-08).
    const bool want_partner = opts.partner_box && opts.partner_box->width > 0 && opts.partner_box->height > 0;
    const bool distance_stroke = opts.distance_stroke && want_partner;
    std::unique_ptr<vision::Tracker> partner_tracker;
    cv::Rect partner_last;
    if (want_partner) {
        partner_last = *opts.partner_box;
        if (!opts.partner_fixed) {
            partner_tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, &why);
            if (!partner_tracker) return fail(why);
            partner_tracker->Init(frame.bgr, partner_last);
        }
    }

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
    res.distance_stroke = distance_stroke;
    res.times_ms.push_back(frame.pts_us / 1000);
    res.lost.push_back(0);
    res.xs.push_back(Centre(box).x);
    res.ys.push_back(Centre(box).y);
    if (want_partner) {
        res.partner_xs.push_back(Centre(partner_last).x);
        res.partner_ys.push_back(Centre(partner_last).y);
    }
    std::vector<double> distances;
    double last_good_distance = 0;
    if (distance_stroke) {
        last_good_distance = TipPartnerDistance(box, partner_last);
        distances.push_back(last_good_distance);
    }
    std::vector<double> camera_dy{0.0};
    cv::Rect last = box;
    bool tip_ok = true;  // last frame had a real tip box (found or coast)
    Coast coast;
    coast.ObserveOk(box);
    DispGuard guard;

    const int64_t first_index = frame.index;
    int total = ProgressTotal(reader->frameCount(), first_index, opts.max_frames);
    if (opts.end_ms > 0 && reader->fps() > 0) {
        const int64_t first_ms = frame.pts_us / 1000;
        const int span = static_cast<int>(std::max<int64_t>(0, opts.end_ms - first_ms) * reader->fps() / 1000) + 1;
        total = total > 0 ? std::min(total, span) : span;
    }
    // A skipped frame counts as done, so progress follows the video, not the samples.
    const bool rate_cap = opts.max_analysis_fps > 0;
    // 1 ms of slack: millisecond times of a 60 fps clip are 16/17 ms apart.
    const double min_step_ms = rate_cap ? 1000.0 / opts.max_analysis_fps - 1.0 : 0;
    Progress progress(opts.on_progress, total);
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
        const int64_t frame_ms = frame.pts_us / 1000;
        if (opts.end_ms > 0 && frame_ms > opts.end_ms) break;
        if (rate_cap && frame_ms - res.times_ms.back() < min_step_ms) {
            progress.Report(static_cast<int>(frame.index - first_index));
            continue;
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
                tip_ok = true;
            } else {
                res.xs.push_back(res.xs.back());
                res.ys.push_back(res.ys.back());
                tip_ok = false;
            }
            res.lost.push_back(tip_ok ? 0 : 1);
        } else {
            // CSRT never reports a loss when it is wrong (GD-11): a box that
            // no longer resembles the first crop is marked lost (spec T6).
            // Only the flag changes, so a run without gap handling, which
            // ignores the flags, keeps every position as before.
            const auto likeness = memory ? memory->MatchesOriginal(gray, *found) : std::nullopt;
            res.lost.push_back(likeness && *likeness < AppearanceMemory::kMinScore ? 1 : 0);
            coast.ObserveOk(*found);
            ++valid;
            res.xs.push_back(Centre(*found).x);
            res.ys.push_back(Centre(*found).y);
            last = *found;
            tip_ok = true;
            // Only crops that still resemble the start feed the memory, so
            // slow drift never poisons a later reacquire.
            if (memory && idx % kRememberEveryNFrames == 0)
                if (!likeness || *likeness >= AppearanceMemory::kMinScore) memory->Remember(gray, *found);
        }

        bool partner_ok = want_partner;
        if (want_partner) {
            if (opts.partner_fixed || cut || !partner_tracker) {
                // Fixed (or after a cut): keep the last known centre.
            } else if (auto pb = partner_tracker->Update(frame.bgr)) {
                partner_last = *pb;
                partner_ok = true;
            } else {
                partner_ok = false;  // brief miss: hold last box, gap for distance
            }
            res.partner_xs.push_back(Centre(partner_last).x);
            res.partner_ys.push_back(Centre(partner_last).y);
        }

        if (distance_stroke) {
            // GD-08: tip loss or no usable partner holds the last good gap.
            if (tip_ok && partner_ok) {
                last_good_distance = TipPartnerDistance(last, partner_last);
                distances.push_back(last_good_distance);
            } else {
                distances.push_back(last_good_distance);
            }
        }

        if (opts.camera_compensation) {
            double dy = 0;
            if (!cut) {
                // Tip box plus soft masks (0.7 punch exclude): keep wrong
                // latch regions out of the camera estimate.
                std::vector<cv::Rect> excludes = opts.mask_rois;
                excludes.push_back(last);
                if (want_partner) excludes.push_back(partner_last);
                dy = CameraMotionY(prev_gray, gray, excludes);
                if (dy == 0.0) ++res.stats.camera_frames_lost;
            }
            camera_dy.push_back(cut ? 0.0 : camera_dy.back() + dy);
        } else {
            camera_dy.push_back(0.0);
        }

        if (needs_gray) std::swap(prev_gray, gray);
        res.times_ms.push_back(frame_ms);
        progress.Report(rate_cap ? static_cast<int>(frame.index - first_index) : idx);
        ++idx;
        if (opts.max_frames > 0 && idx >= opts.max_frames) break;
    }
    progress.Report(idx);

    res.frame_ys = res.ys;
    if (opts.camera_compensation) ApplyCameraCompensation(res.ys, camera_dy, res.scene_cuts);

    res.stats.vertical_range = Ptp(res.ys);
    res.stats.horizontal_range = Ptp(res.xs);
    if (distance_stroke) {
        res.horizontal = false;
        res.positions = std::move(distances);
    } else {
        const bool auto_x =
            res.stats.horizontal_range > res.stats.vertical_range * 1.5 && res.stats.horizontal_range > 5;
        res.horizontal = opts.axis == TrackAxis::kX || (opts.axis == TrackAxis::kAuto && auto_x);
        res.positions = res.horizontal ? res.xs : res.ys;
    }

    res.stats.total_frames = idx;
    res.stats.valid_frames = valid;
    res.stats.confidence = double(valid) / idx;
    const double lost = double(res.stats.lost_frames) / idx;
    res.stats.reason = lost > 0.5 ? "tracker_lost_heavy" : lost > 0.15 ? "tracker_lost_elevated" : "";
    return res;
}

}  // namespace samn::engine
