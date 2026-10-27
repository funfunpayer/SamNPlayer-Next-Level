#include "samn/engine/repair.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <opencv2/imgproc.hpp>

#include "samn/engine/tip_find.h"
#include "samn/vision/frame_reader.h"

namespace samn::engine {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool Accepted(const WindowVerdict& v, double min_appearance) {
    // A retry replaces a failed stretch, so it must pass outright: "still"
    // is not evidence that the new box is on the target.
    if (v.state != WindowState::kPass) return false;
    // Fail closed: it must still look like the original region, by a wider
    // margin than the tracker's own gate (a textured background matched a
    // foreign seed at 0.59 in the tests). An unknown look is not enough.
    return std::isfinite(v.c7) && v.c7 >= min_appearance;
}

// The first run's frame index of a retry's frame, by time (the retry starts
// at a seek, so its frame 0 is some frame of the first run).
int Align(const std::vector<int64_t>& first_times, int64_t t) {
    const auto it = std::lower_bound(first_times.begin(), first_times.end(), t);
    if (it == first_times.end()) return -1;
    int i = static_cast<int>(it - first_times.begin());
    if (i > 0 && std::llabs(first_times[size_t(i - 1)] - t) < std::llabs(*it - t)) --i;
    return std::llabs(first_times[size_t(i)] - t) <= 2 ? i : -1;
}

}  // namespace

std::optional<RepairResult> RepairTrack(const std::filesystem::path& video, const TrackResult& first,
                                        const RepairOptions& o, std::string* error) {
    RepairResult res;
    res.positions = first.positions;
    const int n = static_cast<int>(first.positions.size());

    VerifyOptions vo = o.verify;
    vo.cuts = first.scene_cuts;
    res.verdicts = VerifyWindows(first.positions, first.checks, vo);

    // The original look: the first run's first frame and the user's box.
    cv::Mat seed_gray;
    {
        auto reader = vision::FrameReader::Open(video, error);
        if (!reader) return std::nullopt;
        vision::VideoFrame frame;
        if (!first.times_ms.empty() && first.times_ms.front() > 0) reader->SeekMs(first.times_ms.front());
        if (reader->Read(frame)) cv::cvtColor(frame.bgr, seed_gray, cv::COLOR_BGR2GRAY);
    }

    std::vector<const WindowVerdict*> failed;
    for (const auto& v : res.verdicts)
        if (v.state == WindowState::kFail) failed.push_back(&v);
    res.failed = static_cast<int>(failed.size());

    size_t k = 0;
    while (k < failed.size()) {
        if (o.cancel && o.cancel()) {
            res.canceled = true;
            return res;
        }
        // The run of consecutive failed windows this one starts.
        size_t run_end = k + 1;
        while (run_end < failed.size() && failed[run_end]->first == failed[run_end - 1]->end) ++run_end;
        const int a = failed[k]->first;
        const int b = failed[run_end - 1]->end;

        // Candidates: Find tip on the failed stretch, strongest first.
        std::vector<cv::Rect> boxes;
        TipFindOptions to;
        to.start_ms = first.times_ms[size_t(a)];
        to.max_seconds = o.find_seconds;
        to.cancel = o.cancel;
        std::string why;
        if (auto tip = FindTip(video, to, &why)) {
            boxes.push_back(tip->box);
            for (const auto& c : tip->candidates)
                if (static_cast<int>(boxes.size()) < o.max_candidates && c.box != tip->box) boxes.push_back(c.box);
        }

        // Each candidate tracks the stretch; keep the one whose accepted
        // windows reach furthest from a without a break.
        int best = -1, best_reach = a;
        TrackResult best_run;
        for (size_t c = 0; c < boxes.size(); ++c) {
            TrackOptions t = o.track;
            t.record_checks = true;
            t.appearance_memory = true;  // the look against the original seed is the acceptance test
            // Spliced samples must be on the first run's axis.
            t.axis = first.horizontal ? TrackAxis::kX : TrackAxis::kY;
            t.start_ms = first.times_ms[size_t(a)];
            t.max_frames = b - a;
            t.seed_gray = seed_gray;
            t.seed_box = o.seed_box;
            t.on_progress = nullptr;
            std::string err;
            auto run = TrackBox(video, boxes[c], t, &err);
            if (!run || run->canceled || run->positions.empty()) continue;
            if (Align(first.times_ms, run->times_ms.front()) != a) continue;

            VerifyOptions rv = o.verify;
            rv.skip_frames = 0;
            rv.cuts = run->scene_cuts;
            rv.foreground_ref = 0;
            int reach = a;
            for (const auto& w : VerifyWindows(run->positions, run->checks, rv)) {
                if (a + w.first != reach || !Accepted(w, o.min_appearance)) break;
                reach = a + w.end;
            }
            if (reach > best_reach) {
                best = static_cast<int>(c);
                best_reach = reach;
                best_run = std::move(*run);
            }
        }

        // Splice the verified part; the rest of this window becomes a gap.
        const int first_end = failed[k]->end;
        if (best >= 0) {
            res.run_starts.push_back(a);
            for (int i = a; i < best_reach && i < n; ++i) res.positions[size_t(i)] = best_run.positions[size_t(i - a)];
        }
        // Mark every failed window this attempt covered.
        size_t next = k;
        while (next < run_end && (next == k || failed[next]->first < std::max(best_reach, first_end))) {
            RepairedWindow rw;
            rw.first = failed[next]->first;
            rw.end = failed[next]->end;
            rw.repaired = best >= 0 && rw.end <= best_reach;
            if (rw.repaired) {
                rw.candidate = best;
                rw.box = boxes[size_t(best)];
                ++res.repaired;
            } else {
                for (int i = rw.first; i < rw.end && i < n; ++i) res.positions[size_t(i)] = kNaN;
                ++res.gaps;
            }
            res.windows.push_back(rw);
            ++next;
        }
        k = next;
        if (o.on_progress) o.on_progress(static_cast<int>(k), static_cast<int>(failed.size()));
    }
    return res;
}

}  // namespace samn::engine
