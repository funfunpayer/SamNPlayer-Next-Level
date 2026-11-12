#include "samn/pov.h"

#include <algorithm>
#include <cmath>
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

}  // namespace samn
