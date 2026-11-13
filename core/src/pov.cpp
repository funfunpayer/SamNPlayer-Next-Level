#include "samn/pov.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace samn {
namespace {

double Clamp01(double x) {
    if (!std::isfinite(x) || x < 0) return 0;
    if (x > 1) return 1;
    return x;
}

// How far v sits above the gate, as a share of the gate (0 at the gate, 1
// at twice the gate). A non-positive gate treats any positive v as 1.
double Over(double v, double gate) {
    if (gate <= 0) return v > 0 ? 1.0 : 0.0;
    return Clamp01((v - gate) / gate);
}

double Under(double v, double gate) {
    if (gate <= 0) return 1.0;
    return Clamp01((gate - v) / gate);
}

struct Label {
    PovKind kind = PovKind::Unknown;
    double conf = 0;
};

// One grid: non-finite → unknown. Negatives count as 0. All-zero → unknown.
Label ClassifyStep(const std::array<double, 9>& g, const PovOptions& o) {
    double cell[9];
    double total = 0;
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(g[static_cast<size_t>(i)])) return {PovKind::Unknown, 0};
        cell[i] = g[static_cast<size_t>(i)] > 0 ? g[static_cast<size_t>(i)] : 0;
        total += cell[i];
    }
    if (total <= 0) return {PovKind::Unknown, 0};

    const double left = cell[0] + cell[3] + cell[6];
    const double right = cell[2] + cell[5] + cell[8];
    const double mid = cell[3] + cell[4] + cell[5];
    const double bot = cell[6] + cell[7] + cell[8];
    const double scale = total;
    const double split = 2.0 * std::min(left, right) / total;
    const double cy = (mid + 2.0 * bot) / total;

    const bool large = scale >= o.scale_gate;
    const bool splity = split >= o.split_gate;
    const bool lower = cy >= o.cy_gate;

    // Large and split: a filled frame that is also L/R bimodal. Ambiguous
    // (two people filling the frame, a close-up pair) → unknown.
    if (large && splity) {
        return {PovKind::Unknown, Clamp01(0.5 * (Over(scale, o.scale_gate) + Over(split, o.split_gate)))};
    }
    // Large centre+lower, low split → first person (body fills the lower
    // middle of the frame, not two side-by-side figures).
    if (large && lower && !splity) {
        const double c = (Over(scale, o.scale_gate) + Over(cy, o.cy_gate) + Under(split, o.split_gate)) / 3.0;
        return {PovKind::FirstPerson, Clamp01(c)};
    }
    // Small, or L/R bimodal that did not fill the frame → third person.
    if (!large) {
        const double c = splity ? std::max(Under(scale, o.scale_gate), Over(split, o.split_gate))
                                : Under(scale, o.scale_gate);
        return {PovKind::ThirdPerson, Clamp01(c)};
    }
    // Large, not lower, not split: a close-up that is not a body-down view.
    return {PovKind::Unknown, 0};
}

void MergeAdjacent(std::vector<PovSegment>& segs) {
    if (segs.size() < 2) return;
    std::vector<PovSegment> out;
    out.reserve(segs.size());
    for (const auto& seg : segs) {
        if (!out.empty() && out.back().kind == seg.kind) {
            const double d0 = static_cast<double>(out.back().end_ms - out.back().start_ms);
            const double d1 = static_cast<double>(seg.end_ms - seg.start_ms);
            const double den = d0 + d1;
            out.back().confidence = den > 0 ? (out.back().confidence * d0 + seg.confidence * d1) / den : 0;
            out.back().end_ms = seg.end_ms;
        } else {
            out.push_back(seg);
        }
    }
    segs = std::move(out);
}

}  // namespace

const char* PovKindName(PovKind kind) {
    switch (kind) {
    case PovKind::Unknown: return "unknown";
    case PovKind::FirstPerson: return "first_person";
    case PovKind::ThirdPerson: return "third_person";
    }
    return "unknown";
}

std::vector<PovSegment> ClassifyPov(const PovSignals& signals, const PovOptions& opt) {
    if (signals.grid.empty()) return {};
    const int64_t step = signals.step_ms > 0 ? signals.step_ms : 500;

    std::vector<Label> labels;
    labels.reserve(signals.grid.size());
    for (const auto& g : signals.grid) labels.push_back(ClassifyStep(g, opt));

    std::vector<PovSegment> segs;
    const size_t n = labels.size();
    size_t i = 0;
    while (i < n) {
        size_t j = i + 1;
        while (j < n && labels[j].kind == labels[i].kind) ++j;
        double sum = 0;
        for (size_t k = i; k < j; ++k) sum += labels[k].conf;
        PovSegment seg;
        seg.start_ms = static_cast<int64_t>(i) * step;
        seg.end_ms = static_cast<int64_t>(j) * step;
        seg.kind = labels[i].kind;
        seg.confidence = sum / static_cast<double>(j - i);
        segs.push_back(seg);
        i = j;
    }

    // A first/third run shorter than min_run_ms is flicker → unknown.
    for (auto& seg : segs) {
        if (seg.kind != PovKind::Unknown && (seg.end_ms - seg.start_ms) < opt.min_run_ms) {
            seg.kind = PovKind::Unknown;
        }
    }
    MergeAdjacent(segs);
    return segs;
}

namespace {

double OverlapArea(double ax0, double ay0, double ax1, double ay1, double bx0, double by0, double bx1, double by1) {
    const double x = std::min(ax1, bx1) - std::max(ax0, bx0);
    const double y = std::min(ay1, by1) - std::max(ay0, by0);
    if (!(x > 0) || !(y > 0) || !std::isfinite(x) || !std::isfinite(y)) return 0;
    return x * y;
}

const PovTipSample* NearestTip(const std::vector<PovTipSample>& tip, int64_t at_ms, int64_t max_gap) {
    if (tip.empty()) return nullptr;
    const auto it = std::lower_bound(tip.begin(), tip.end(), at_ms,
                                     [](const PovTipSample& p, int64_t v) { return p.at_ms < v; });
    const PovTipSample* best = nullptr;
    if (it == tip.end())
        best = &tip.back();
    else if (it == tip.begin())
        best = &*it;
    else {
        const auto prev = std::prev(it);
        best = (at_ms - prev->at_ms) <= (it->at_ms - at_ms) ? &*prev : &*it;
    }
    if (std::llabs(best->at_ms - at_ms) > max_gap) return nullptr;
    return best;
}

}  // namespace

PovSignals PovSignalsFromTip(const PovTipPath& path, int64_t step_ms) {
    PovSignals out;
    if (path.width <= 0 || path.height <= 0 || !(path.box_w > 0) || !(path.box_h > 0) || path.tip.empty())
        return out;
    if (!std::isfinite(path.box_w) || !std::isfinite(path.box_h)) return out;
    const double fw = static_cast<double>(path.width);
    const double fh = static_cast<double>(path.height);
    if (!std::isfinite(fw) || !std::isfinite(fh)) return out;

    std::vector<PovTipSample> tip = path.tip;
    std::stable_sort(tip.begin(), tip.end(),
                     [](const PovTipSample& a, const PovTipSample& b) { return a.at_ms < b.at_ms; });

    const int64_t step = step_ms > 0 ? step_ms : 500;
    const int64_t last = tip.back().at_ms;
    if (last < 0) return out;
    const size_t n = static_cast<size_t>(last / step) + 1;
    out.step_ms = step;
    out.grid.resize(n);

    const double cell_w = fw / 3.0;
    const double cell_h = fh / 3.0;
    const double cell_a = cell_w * cell_h;
    if (!(cell_a > 0)) return {};

    for (size_t i = 0; i < n; ++i) {
        const int64_t t = static_cast<int64_t>(i) * step;
        const PovTipSample* s = NearestTip(tip, t, step);
        std::array<double, 9> g{};
        if (s && std::isfinite(s->x) && std::isfinite(s->y)) {
            const double x0 = std::clamp(s->x - path.box_w * 0.5, 0.0, fw);
            const double y0 = std::clamp(s->y - path.box_h * 0.5, 0.0, fh);
            const double x1 = std::clamp(s->x + path.box_w * 0.5, 0.0, fw);
            const double y1 = std::clamp(s->y + path.box_h * 0.5, 0.0, fh);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    const double cx0 = static_cast<double>(c) * cell_w;
                    const double cy0 = static_cast<double>(r) * cell_h;
                    const double a = OverlapArea(x0, y0, x1, y1, cx0, cy0, cx0 + cell_w, cy0 + cell_h);
                    g[static_cast<size_t>(r * 3 + c)] = a / cell_a;
                }
            }
        }
        out.grid[i] = g;
    }
    return out;
}

PovKind PovAt(const std::vector<PovSegment>& segs, int64_t at_ms) {
    for (const auto& s : segs) {
        if (at_ms >= s.start_ms && at_ms < s.end_ms) return s.kind;
    }
    if (!segs.empty() && at_ms == segs.back().end_ms) return segs.back().kind;
    return PovKind::Unknown;
}

PovKind DominantPov(const std::vector<PovSegment>& segs) {
    PovKind best = PovKind::Unknown;
    int64_t best_dur = 0;
    double best_conf = -1;
    for (const auto& s : segs) {
        if (s.kind == PovKind::Unknown) continue;
        const int64_t d = s.end_ms - s.start_ms;
        if (d > best_dur || (d == best_dur && s.confidence > best_conf)) {
            best = s.kind;
            best_dur = d;
            best_conf = s.confidence;
        }
    }
    return best;
}

double DominantPovConfidence(const std::vector<PovSegment>& segs) {
    const PovKind k = DominantPov(segs);
    if (k == PovKind::Unknown) return 0;
    int64_t best_dur = 0;
    double best_conf = 0;
    for (const auto& s : segs) {
        if (s.kind != k) continue;
        const int64_t d = s.end_ms - s.start_ms;
        if (d > best_dur || (d == best_dur && s.confidence > best_conf)) {
            best_dur = d;
            best_conf = s.confidence;
        }
    }
    return Clamp01(best_conf);
}

PovClassification ClassifyTipPov(const PovTipPath& path, const PovOptions& opt) {
    PovClassification out;
    out.segments = ClassifyPov(PovSignalsFromTip(path), opt);
    out.kind = DominantPov(out.segments);
    out.confidence = DominantPovConfidence(out.segments);
    return out;
}

PovContactAdvice AdvisePovContact(PovKind kind, bool has_contact_area) {
    if (kind == PovKind::FirstPerson) return has_contact_area ? PovContactAdvice::CloseUp : PovContactAdvice::SuggestContact;
    if (kind == PovKind::ThirdPerson) return PovContactAdvice::WideView;
    return PovContactAdvice::None;
}

}  // namespace samn
