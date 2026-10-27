#include "samn/engine/track_box.h"

#include <algorithm>
#include <cmath>

#include <limits>

#include <opencv2/imgproc.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/video/tracking.hpp>

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

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double MedianOf(std::vector<float>& v) {
    if (v.empty()) return 0.0;
    const auto mid = v.begin() + static_cast<long>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

// SC-2: the checker signals of the self-check (docs/P3_SELF_CHECK_ENGINE.md
// §5) on a half-size copy of each frame. Only observes; never changes the
// box, so positions stay identical with recording on or off.
class CheckRecorder {
public:
    explicit CheckRecorder(cv::Size size) : size_(size), bg_(cv::createBackgroundSubtractorMOG2(500, 16, false)) {}

    // box: the box written for this frame; ok: the tracker (or a verified
    // reacquire) returned it; inliers / appearance: NaN when unknown.
    void Frame(const cv::Mat& bgr, const cv::Rect& box, bool ok, double inliers, double appearance) {
        cv::Mat small, gray, fg;
        cv::resize(bgr, small, cv::Size(), kScale, kScale, cv::INTER_AREA);
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
        bg_->apply(small, fg);

        const cv::Rect frame_rect(0, 0, gray.cols, gray.rows);
        const cv::Rect sb = cv::Rect(int(box.x * kScale), int(box.y * kScale), std::max(1, int(box.width * kScale)),
                                     std::max(1, int(box.height * kScale))) &
                            frame_rect;
        const bool box_in_picture = sb.area() > 0;

        // Flow on the half-size frame: inside the box against the rest
        // (camera removed) for c1, and the share of pixels that move against
        // the frame's median for the scene motion. MOG2 is not used for the
        // scene: it learns a regular stroke into its background model within
        // a few cycles, so a steady scene would read as "nothing moves".
        double fx = 0, fy = 0, scene = kNaN;
        if (!prev_.empty()) {
            cv::Mat flow;
            cv::calcOpticalFlowFarneback(prev_, gray, flow, 0.5, 3, 15, 3, 5, 1.2, 0);
            std::vector<float> all_x, all_y, in_x, in_y, out_x, out_y;
            all_x.reserve(flow.total());
            all_y.reserve(flow.total());
            for (int r = 0; r < flow.rows; ++r) {
                const auto* row = flow.ptr<cv::Point2f>(r);
                for (int col = 0; col < flow.cols; ++col) {
                    all_x.push_back(row[col].x);
                    all_y.push_back(row[col].y);
                    if (!box_in_picture) continue;
                    const bool inside = sb.contains(cv::Point(col, r));
                    (inside ? in_x : out_x).push_back(row[col].x);
                    (inside ? in_y : out_y).push_back(row[col].y);
                }
            }
            if (box_in_picture && !out_x.empty()) {
                fx = (MedianOf(in_x) - MedianOf(out_x)) / kScale;
                fy = (MedianOf(in_y) - MedianOf(out_y)) / kScale;
            }
            std::vector<float> cx = all_x, cy = all_y;
            const double mx = MedianOf(cx), my = MedianOf(cy);
            size_t moving = 0;
            for (size_t k = 0; k < all_x.size(); ++k) {
                const double dx = all_x[k] - mx, dy = all_y[k] - my;
                moving += dx * dx + dy * dy > kMovingPx * kMovingPx ? 1 : 0;
            }
            scene = static_cast<double>(moving) / static_cast<double>(all_x.size());
        }
        prev_ = gray;

        checks_.box_flow.push_back(fy);
        flow_x_.push_back(fx);
        checks_.foreground.push_back(box_in_picture ? cv::countNonZero(fg(sb)) / double(sb.area()) : 0.0);
        checks_.scene_motion.push_back(scene);
        checks_.tracker_ok.push_back(ok ? 1 : 0);
        checks_.camera_inliers.push_back(inliers);
        checks_.appearance.push_back(appearance);
        checks_.box_outside.push_back(BoxOutsideShare(box.x, box.y, box.width, box.height, size_.width, size_.height));
    }

    FrameChecks Finish(bool horizontal, bool distance_stroke, bool camera_compensation, bool appearance_memory) {
        if (horizontal) checks_.box_flow.swap(flow_x_);
        checks_.box_flow_cross = std::move(flow_x_);  // after a swap this holds the vertical flow
        if (distance_stroke) {  // a distance has no single axis to compare
            checks_.box_flow.clear();
            checks_.box_flow_cross.clear();
        }
        if (!camera_compensation) checks_.camera_inliers.clear();
        if (!appearance_memory) checks_.appearance.clear();
        return std::move(checks_);
    }

private:
    static constexpr double kScale = 0.5;
    static constexpr double kMovingPx = 0.5;  // half-size pixels per frame (1 px at full size)
    cv::Size size_;
    cv::Ptr<cv::BackgroundSubtractorMOG2> bg_;
    cv::Mat prev_;
    FrameChecks checks_;
    std::vector<double> flow_x_;
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
        if (!opts.seed_gray.empty() && opts.seed_box.area() > 0 && opts.seed_gray.size() == prev_gray.size())
            memory->Remember(opts.seed_gray, opts.seed_box);  // the original look stays template 0
        memory->Remember(prev_gray, box);  // the user-confirmed start region
    }

    TrackResult res;
    res.size = reader->size();
    res.distance_stroke = distance_stroke;
    res.times_ms.push_back(frame.pts_us / 1000);
    res.xs.push_back(Centre(box).x);
    res.ys.push_back(Centre(box).y);
    if (want_partner) {
        res.partner_xs.push_back(Centre(partner_last).x);
        res.partner_ys.push_back(Centre(partner_last).y);
    }
    std::optional<AdaptiveSceneCut> adaptive_cut;
    if (opts.scene_cuts && opts.adaptive_cuts)
        adaptive_cut.emplace(std::max(10, static_cast<int>(std::lround(2.0 * (reader->fps() > 0 ? reader->fps() : 30.0)))));
    std::optional<CheckRecorder> recorder;
    if (opts.record_checks) {
        recorder.emplace(res.size);
        recorder->Frame(frame.bgr, box, true, kNaN, kNaN);
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
        const bool cut = opts.scene_cuts && (adaptive_cut ? adaptive_cut->Test(prev_gray, gray) : IsSceneCut(prev_gray, gray));

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
        } else {
            coast.ObserveOk(*found);
            ++valid;
            res.xs.push_back(Centre(*found).x);
            res.ys.push_back(Centre(*found).y);
            last = *found;
            tip_ok = true;
            // Only crops that still resemble the start feed the memory, so
            // slow drift never poisons a later reacquire.
            if (memory && idx % kRememberEveryNFrames == 0) {
                const auto score = memory->MatchesOriginal(gray, *found);
                if (!score || *score >= AppearanceMemory::kMinScore) memory->Remember(gray, *found);
            }
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

        double inliers = kNaN;
        if (opts.camera_compensation) {
            double dy = 0;
            if (!cut) {
                // Tip box plus soft masks (0.7 punch exclude): keep wrong
                // latch regions out of the camera estimate.
                std::vector<cv::Rect> excludes = opts.mask_rois;
                excludes.push_back(last);
                if (want_partner) excludes.push_back(partner_last);
                dy = CameraMotionY(prev_gray, gray, excludes, recorder ? &inliers : nullptr);
                if (dy == 0.0) ++res.stats.camera_frames_lost;
            }
            camera_dy.push_back(cut ? 0.0 : camera_dy.back() + dy);
        } else {
            camera_dy.push_back(0.0);
        }

        if (recorder) {
            double appearance = kNaN;
            if (memory)
                if (const auto score = memory->MatchesOriginal(gray, last)) appearance = *score;
            recorder->Frame(frame.bgr, last, found.has_value(), inliers, appearance);
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
    if (distance_stroke) {
        res.horizontal = false;
        res.positions = std::move(distances);
    } else {
        const bool auto_x =
            res.stats.horizontal_range > res.stats.vertical_range * 1.5 && res.stats.horizontal_range > 5;
        res.horizontal = opts.axis == TrackAxis::kX || (opts.axis == TrackAxis::kAuto && auto_x);
        res.positions = res.horizontal ? res.xs : res.ys;
    }

    if (recorder)
        res.checks = recorder->Finish(res.horizontal, distance_stroke, opts.camera_compensation, opts.appearance_memory);

    res.stats.total_frames = idx;
    res.stats.valid_frames = valid;
    res.stats.confidence = double(valid) / idx;
    const double lost = double(res.stats.lost_frames) / idx;
    res.stats.reason = lost > 0.5 ? "tracker_lost_heavy" : lost > 0.15 ? "tracker_lost_elevated" : "";
    return res;
}

}  // namespace samn::engine
