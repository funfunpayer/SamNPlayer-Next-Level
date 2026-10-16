#pragma once

// Geometry of the script curve as SamNPlayer 0.7 draws it (playback.js
// smoothCurvePath / drawSpeedWave / interpPosAt), without any drawing
// library, so the app and the tests share it.
//
// Improvements over 0.7: the visible part of a script is found by binary
// search (0.7 walked every point on every frame), and PositionAt is
// O(log n) instead of a linear scan.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// A point in curve space: time in ms, position 0-100.
struct CurvePoint {
    double at = 0;
    double pos = 0;
    bool operator==(const CurvePoint& o) const { return at == o.at && pos == o.pos; }
};

// One cubic Bezier piece from p1 to p2 with control points c1, c2.
struct CubicSegment {
    CurvePoint p1, c1, c2, p2;
};

// Catmull-Rom spline through actions[first..last] as cubic Bezier pieces
// (tension 1/6, as 0.7). Control positions are clamped to 0-100, so the
// line never overshoots past a position the script could hold. The points
// themselves stay exact. The neighbours outside [first, last] are used for
// the tangents when they exist, so a visible window joins its neighbours
// smoothly. Fewer than two points give no segments.
std::vector<CubicSegment> CatmullRomBezier(const std::vector<Action>& actions, size_t first, size_t last);

// Index range [first, last] of the actions that touch [t0, t1], widened by
// one point on each side so the line runs to the edges. Empty script:
// {1, 0} (first > last).
std::pair<size_t, size_t> VisibleRange(const std::vector<Action>& actions, int64_t t0, int64_t t1);

// Linear position at a time (as the device sees it). Before the first point:
// the first position; after the last: the last. Empty: nullopt.
std::optional<double> PositionAt(const std::vector<Action>& actions, int64_t ms);

// Positions per second at which the tempo wave is full (0.7: 600 u/s).
// Speed highlights (0.7 funscript/speed_highlight.go, OFS "max speed"):
// intensity = 500 x |dpos| / dt_ms; a step above maxIntensity is "too fast".
constexpr double kSpeedHighlightDefault = 400.0;
struct SpeedSegment {
    int64_t from_ms = 0, to_ms = 0;
    double intensity = 0;  // rounded to 0.1
};
// maxIntensity <= 0 uses kSpeedHighlightDefault. Steps with dt <= 0 are skipped.
std::vector<SpeedSegment> SpeedHighlights(const std::vector<Action>& actions, double maxIntensity);

constexpr double kSpeedWaveFull = 600.0;
// Average gap (px) below which single strokes are no longer readable while
// playing (0.7 DOT_MIN_GAP) and while editing.
constexpr double kDotMinGapPx = 14.0;
constexpr double kEditMinGapPx = 4.0;

// Number of actions with a time inside [t0, t1].
size_t CountInWindow(const std::vector<Action>& actions, int64_t t0, int64_t t1);

// True when the visible strokes are too dense to read at this width.
bool IsDense(size_t visible, double widthPx, double minGapPx);

// Width of a tempo bin: it must hold a whole stroke (up and down).
double SpeedBinPx(size_t visible, double widthPx);

// The calm tempo wave for a dense stroke curve: stroke speed per bin of
// binPx pixels, 0-600 u/s mapped to 0-100, smoothed twice with a [1 2 1]/4
// filter. Returned as {x centre in px, value 0-100}, sorted by x.
std::vector<CurvePoint> SpeedWave(const std::vector<Action>& actions, double t0, double t1, double widthPx,
                                  double binPx);

// --- View window (0.7 zoom: viewStartMs / viewEndMs) -----------------------

struct ViewWindow {
    int64_t start_ms = 0;
    int64_t end_ms = 1;
    bool operator==(const ViewWindow& o) const { return start_ms == o.start_ms && end_ms == o.end_ms; }
};

// The narrowest window a zoom reaches.
constexpr int64_t kMinViewSpanMs = 250;
// Mouse wheel and button steps.
constexpr double kWheelZoomFactor = 1.25;
constexpr double kButtonZoomFactor = 1.6;

// The whole timeline: [0, max(1, total)].
ViewWindow FullView(int64_t totalMs);
// Inside [0, total], at least 1 ms wide; an empty window becomes the full one.
ViewWindow ClampView(ViewWindow v, int64_t totalMs);
bool IsZoomed(ViewWindow v, int64_t totalMs);
// Zoom by factor (> 1 = in) around anchorMs, which keeps its place on
// screen. Without an anchor: the window's centre. Reaching the full span
// resets to the full view.
ViewWindow ZoomView(ViewWindow v, int64_t totalMs, double factor, std::optional<double> anchorMs = std::nullopt);
// A range with a margin of 8 % (at least 200 ms) on each side.
ViewWindow ZoomToRange(int64_t startMs, int64_t endMs, int64_t totalMs);

// --- Time ruler and labels ---------------------------------------------------

// Ruler tick distance: the first of 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15,
// 30, 60 min whose ticks are at least minPx (0.7: 72 px) apart.
int64_t RulerStepMs(double spanMs, double widthPx, double minPx = 72.0);
// Ruler label: "mm:ss", or "h:mm:ss" from one hour (rounded to seconds).
std::string FormatRulerTime(int64_t ms);
// Tooltip time: "12.3s" below a minute, else "m:ss.t". (0.7 could print
// "1:01.10" for 61.96 s; here the tenths carry over: "1:02.0".)
std::string FormatTooltipTime(int64_t ms);
// A drag on the heatmap marks a range only when it spans more than this;
// a shorter one is a click that seeks (0.7).
constexpr int64_t kMarkDragMinMs = 150;

}  // namespace samn
