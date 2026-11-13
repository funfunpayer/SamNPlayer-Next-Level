#include "samn/engine/tip_resolve.h"

#include <algorithm>
#include <cmath>

#include "samn/vision/canceled.h"
#include "samn/vision/frame_reader.h"

namespace samn::engine {

std::vector<int64_t> TipResolveStartMs(int64_t duration_ms, int64_t preview_ms) {
    std::vector<int64_t> out;
    auto add = [&](int64_t t) {
        if (t < 0) return;
        if (duration_ms > 0 && t >= duration_ms) return;
        if (std::find(out.begin(), out.end(), t) != out.end()) return;
        out.push_back(t);
    };
    // Shown frame first: openVideo already seeks ~10 %, and the user often
    // scrubs to motion — FunGen2-like snappy Create.
    if (preview_ms >= 0) add(preview_ms);
    add(0);
    if (duration_ms > 0) {
        add(int64_t(duration_ms * 0.25));
        add(int64_t(duration_ms * 0.50));
    }
    return out;
}

std::optional<int> BestUsableTipIndex(const std::vector<TipCandidate>& candidates, int max_n) {
    const int n = std::min(max_n, static_cast<int>(candidates.size()));
    int best = -1;
    double best_score = -1;
    for (int i = 0; i < n; ++i) {
        const double s = candidates[size_t(i)].score;
        if (s < kWeakTipScore) continue;
        if (best < 0 || s > best_score) {
            best_score = s;
            best = i;
        }
    }
    if (best < 0) return std::nullopt;
    return best;
}

std::optional<TipResolveResult> ResolveTipForCreate(const std::filesystem::path& video, const TipResolveOptions& o,
                                                    std::string* error) {
    auto fail = [&](const char* text) {
        if (error) *error = text;
        return std::nullopt;
    };
    std::string open_error;
    auto reader = vision::FrameReader::Open(video, &open_error);
    if (!reader) {
        if (error) *error = open_error;
        return std::nullopt;
    }
    int64_t duration_ms = 0;
    if (reader->frameCount() > 0 && reader->fps() > 0)
        duration_ms = int64_t(reader->frameCount() * 1000.0 / reader->fps());
    reader.reset();

    const std::vector<int64_t> starts = TipResolveStartMs(duration_ms, o.preview_ms);
    const int nwin = std::max(1, static_cast<int>(starts.size()));
    const int max_n = o.max_candidates > 0 ? o.max_candidates : kTipResolveMaxCandidates;
    const int64_t window_ms = int64_t(std::max(0.0, o.max_seconds) * 1000.0);
    int64_t covered_to = 0;
    std::optional<TipResolveResult> best;

    for (int wi = 0; wi < static_cast<int>(starts.size()); ++wi) {
        if (o.cancel && o.cancel()) return fail(kCanceled);
        if (starts[size_t(wi)] < covered_to) continue;
        TipFindOptions to;
        to.start_ms = starts[size_t(wi)];
        to.max_seconds = o.max_seconds;
        to.sample_every = o.sample_every > 0 ? o.sample_every : 2;
        to.cancel = o.cancel;
        to.progress = [&](int p) {
            if (o.progress) o.progress(std::min(99, (wi * 100 + std::clamp(p, 0, 99)) / nwin));
        };
        std::string why;
        const auto tip = FindTip(video, to, &why);
        if (!tip) {
            if (why == kCanceled) return fail(kCanceled);
            continue;
        }
        covered_to = std::max(covered_to, starts[size_t(wi)] + window_ms);
        const auto idx = BestUsableTipIndex(tip->candidates, max_n);
        if (!idx) continue;

        TipResolveResult res;
        const int i = *idx;
        res.box = i == 0 ? tip->box : tip->candidates[size_t(i)].box;
        res.score = tip->candidates[size_t(i)].score;
        res.start_ms = starts[size_t(wi)];
        res.candidate_index = i;
        res.reason = "window " + std::to_string(res.start_ms) + " ms, candidate " + std::to_string(i + 1);
        res.candidates = tip->candidates;

        if (!best || res.score > best->score) best = res;
        if (res.score >= kStrongTipScore) {
            if (o.progress) o.progress(99);
            return best;
        }
    }
    if (best) {
        if (o.progress) o.progress(99);
        return best;
    }
    return fail("only weak motion found");
}

}  // namespace samn::engine
