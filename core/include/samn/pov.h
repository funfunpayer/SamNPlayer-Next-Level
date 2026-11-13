#pragma once
// Camera point of view from a 3×3 occupancy grid per step: unknown,
// first_person, or third_person. Fail closed (zero, NaN, conflict, a run
// shorter than min_run_ms). Core only: no OpenCV, no UI, no file write.
//
// Row-major grid:
//   0 1 2   top  L C R
//   3 4 5   mid  L C R
//   6 7 8   bot  L C R
// scale is total occupancy, split is 2*min(L,R)/total (L/R bimodality),
// cy is the occupancy centroid (0 = top row, 2 = bottom).
//
// Slice 2 (POV-Contact): ClassifyTipPov rasterizes the seed tip box along
// the tracked centres into those grids. One blob, constant box size. It is
// not a second ROI (D-055) and cannot see two-person large+split. Full-frame
// occupancy still wants TrackBox per-frame box w/h or a 3×3 foreground
// (MeasurePovSignals; see track_box.h).

#include <array>
#include <cstdint>
#include <vector>

namespace samn {

enum class PovKind { Unknown, FirstPerson, ThirdPerson };

// "unknown" | "first_person" | "third_person". An out-of-range value is
// "unknown" (fail closed).
const char* PovKindName(PovKind kind);

struct PovSignals {
    int64_t step_ms = 500;
    std::vector<std::array<double, 9>> grid;
};

struct PovSegment {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    PovKind kind = PovKind::Unknown;
    double confidence = 0;  // 0..1; a ranking value, not a probability
};

struct PovOptions {
    // Fitted on the synthetic 3×3 grids in tests/core/pov_test.cpp (slice 1:
    // no real frames yet). A first-person body fills the centre and lower
    // third (~3 units of occupancy); a distant third-person figure is ~0.2;
    // two side-by-side figures that fill the frame (~5) stay unknown.
    int64_t min_run_ms = 2000;  // a single 500 ms step is flicker
    double scale_gate = 2.0;    // total occupancy; below is "small"
    double split_gate = 0.55;   // 2*min(L,R)/total; first-person spill is ~0.4
    double cy_gate = 1.15;      // occupancy centroid; first person sits lower
};

// Empty grid → empty list. step_ms <= 0 is treated as 500. The caller's
// options are not modified.
std::vector<PovSegment> ClassifyPov(const PovSignals& sig, const PovOptions& opt = {});

// Seed tip box (frame pixels) plus tracked centres. One rectangle, no
// partner/extra path: D-055 stays closed.
struct PovTipSample {
    int64_t at_ms = 0;
    double x = 0;
    double y = 0;
};

struct PovTipPath {
    int64_t width = 0;
    int64_t height = 0;
    double box_w = 0;
    double box_h = 0;
    std::vector<PovTipSample> tip;
};

// Splat the seed box, centred on each tip sample, onto a 3×3 occupancy
// grid every step_ms (default 500; <= 0 → 500). Empty / non-positive size
// / empty path → empty signals (fail closed). Cheap: no video.
PovSignals PovSignalsFromTip(const PovTipPath& path, int64_t step_ms = 500);

// Kind covering at_ms (half-open [start, end); the last end is included).
// No covering segment → Unknown.
PovKind PovAt(const std::vector<PovSegment>& segs, int64_t at_ms);

// Longest first/third run (confidence breaks a tie). All-unknown / empty →
// Unknown. confidence of that run, or 0.
PovKind DominantPov(const std::vector<PovSegment>& segs);
double DominantPovConfidence(const std::vector<PovSegment>& segs);

struct PovClassification {
    PovKind kind = PovKind::Unknown;
    double confidence = 0;
    std::vector<PovSegment> segments;
};

// ClassifyPov(PovSignalsFromTip(path)). kind is DominantPov.
PovClassification ClassifyTipPov(const PovTipPath& path, const PovOptions& opt = {});

// Feel/contact overlay. Never a box: SuggestContact is copy only.
enum class PovContactAdvice { None, SuggestContact, CloseUp, WideView };
PovContactAdvice AdvisePovContact(PovKind kind, bool has_contact_area);

}  // namespace samn
