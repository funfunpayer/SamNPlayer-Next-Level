#include "samn/engine/tip_find.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include "samn/vision/canceled.h"
#include "samn/vision/frame_reader.h"
#include "track_parts.h"

namespace samn::engine {
namespace {

constexpr int kGridCols = 12;
constexpr int kGridRows = 8;

double TipRegionScore(const cv::Mat& scores, const std::vector<std::pair<int, int>>& cells) {
    if (cells.empty()) return 0;
    double peak = -std::numeric_limits<double>::infinity();
    int rmin = cells[0].first, rmax = rmin, cmin = cells[0].second, cmax = cmin;
    for (const auto& [r, c] : cells) {
        peak = std::max(peak, scores.at<double>(r, c));
        rmin = std::min(rmin, r);
        rmax = std::max(rmax, r);
        cmin = std::min(cmin, c);
        cmax = std::max(cmax, c);
    }
    const double coverage = double((rmax - rmin + 1) * (cmax - cmin + 1)) / std::max(1, scores.rows * scores.cols);
    // Full weight up to ~12 % of the frame, then a gentle decay (0.7).
    return peak * (1.0 / (1.0 + std::max(0.0, coverage - 0.12) * 4.0));
}

cv::Rect CellsToBox(const std::vector<std::pair<int, int>>& cells, double cellW, double cellH, double scale,
                    int width, int height, int minSide = 24) {
    int r0 = cells[0].first, r1 = r0, c0 = cells[0].second, c1 = c0;
    for (const auto& [r, c] : cells) {
        r0 = std::min(r0, r);
        r1 = std::max(r1, r);
        c0 = std::min(c0, c);
        c1 = std::max(c1, c);
    }
    ++r1;
    ++c1;
    int x = int(c0 * cellW / scale);
    int y = int(r0 * cellH / scale);
    const int w = std::max(minSide, int((c1 - c0) * cellW / scale));
    const int h = std::max(minSide, int((r1 - r0) * cellH / scale));
    x = std::max(0, std::min(x, width - w));
    y = std::max(0, std::min(y, height - h));
    return {x, y, w, h};
}

// A slight inset: coarse cells track steadier with less empty rim (0.7).
cv::Rect ShrinkBox(cv::Rect b, int width, int height, double padFrac = 0.1, int minSide = 16) {
    const int padX = int(b.width * padFrac), padY = int(b.height * padFrac);
    int x0 = b.x + padX;
    const int x1 = b.x + b.width - padX;
    int y0 = b.y + padY;
    const int y1 = b.y + b.height - padY;
    const int w = std::max(minSide, x1 - x0), h = std::max(minSide, y1 - y0);
    x0 = std::max(0, std::min(x0, width - w));
    y0 = std::max(0, std::min(y0, height - h));
    return {x0, y0, w, h};
}

// The main direction of a cell's motion: the principal axis of its flow
// vectors over time (unit; y >= 0, and x > 0 for a level axis, so the same
// motion always gives the same sign).
cv::Point2d MainDirection(const std::vector<double>& xs, const std::vector<double>& ys) {
    const size_t n = std::min(xs.size(), ys.size());
    if (n == 0) return {0, 1};
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) {
        mx += xs[i];
        my += ys[i];
    }
    mx /= double(n);
    my /= double(n);
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (xs[i] - mx) * (xs[i] - mx);
        syy += (ys[i] - my) * (ys[i] - my);
        sxy += (xs[i] - mx) * (ys[i] - my);
    }
    if (sxx + syy <= 0) return {0, 1};
    const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
    cv::Point2d d(std::cos(theta), std::sin(theta));
    if (d.y < -1e-9 || (std::abs(d.y) <= 1e-9 && d.x < 0)) d = -d;
    return d;
}

// Linear percentile of v (p in 0..1); v is sorted in place.
double Percentile(std::vector<double>& v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double at = p * double(v.size() - 1);
    const size_t lo = size_t(at);
    const size_t hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (v[hi] - v[lo]) * (at - double(lo));
}

// The mover around a peak cell, as a box at the search's first frame ("one
// big tip over everything that moves together").
//
// activity is the flow left after the camera's motion, summed per pixel
// over the search; a pixel the mover covers all the time has about the peak
// cell's level, one it only reaches at the end of a stroke a share of it.
// Everything connected to the peak cell above kActivityShare of that level
// is what the mover sweeps. The stroke itself (the peak cell's motion summed
// up along its main direction) is then taken off again: the sweep reaches
// about a quarter of the stroke beyond the mover on either side, and the
// mover starts where the stroke's first sample stands against its middle.
constexpr double kActivityShare = 0.3;
constexpr double kSweepBeyondMover = 0.25;  // of the stroke (peak to peak), each side
constexpr double kMaxRegionShare = 0.6;     // more of the picture than this is the camera's rest
constexpr int kMinRegionSide = 16;          // small px

cv::Rect MoverBox(const cv::Mat& activity, const cv::Rect& peakCell, const cv::Point2d& axis,
                  const std::vector<double>& stroke) {
    const double level = cv::mean(activity(peakCell))[0];
    if (!(level > 0)) return {};
    cv::Mat mask = activity >= kActivityShare * level;
    // Thin bridges to something else that moves are cut.
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, {5, 5}));
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    // The component that holds most of the peak cell.
    std::vector<int> inCell(size_t(std::max(n, 1)), 0);
    for (int y = peakCell.y; y < peakCell.y + peakCell.height; ++y)
        for (int x = peakCell.x; x < peakCell.x + peakCell.width; ++x) ++inCell[size_t(labels.at<int>(y, x))];
    int label = 0;
    for (int i = 1; i < n; ++i)
        if (inCell[size_t(i)] > (label ? inCell[size_t(label)] : 0)) label = i;
    if (label == 0) return {};
    const cv::Rect sweep(stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP),
                         stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT));
    if (double(sweep.area()) > kMaxRegionShare * double(activity.total())) return {};

    // The stroke: the peak cell's position over the search, the first sample
    // at 0, a steady drift taken off (a line through the first and last).
    std::vector<double> pos(stroke.size());
    double sum = 0;
    for (size_t i = 0; i < stroke.size(); ++i) pos[i] = (sum += stroke[i]);
    if (pos.size() > 1) {
        const double slope = pos.back() / double(pos.size() - 1);
        for (size_t i = 0; i < pos.size(); ++i) pos[i] -= slope * double(i);
    }
    std::vector<double> sorted = pos;
    const double lo = Percentile(sorted, 0.05), hi = Percentile(sorted, 0.95);
    const double swing = hi - lo;         // peak to peak
    const double first = -(lo + hi) / 2;  // the first frame against the stroke's middle

    const double cutX = 2 * kSweepBeyondMover * swing * std::abs(axis.x);
    const double cutY = 2 * kSweepBeyondMover * swing * std::abs(axis.y);
    const double w = std::max(double(kMinRegionSide), sweep.width - cutX);
    const double h = std::max(double(kMinRegionSide), sweep.height - cutY);
    const double cx = sweep.x + sweep.width / 2.0 + first * axis.x;
    const double cy = sweep.y + sweep.height / 2.0 + first * axis.y;
    cv::Rect box(int(std::lround(cx - w / 2)), int(std::lround(cy - h / 2)), int(std::lround(w)), int(std::lround(h)));
    return box & cv::Rect(0, 0, activity.cols, activity.rows);
}

}  // namespace

double PeriodicityScore(const std::vector<double>& signal, double fps) {
    const size_t n = signal.size();
    if (n < 8) return 0;
    double mean = 0;
    for (double v : signal) mean += v;
    mean /= double(n);
    std::vector<double> sig(n);
    bool allZero = true;
    for (size_t i = 0; i < n; ++i) {
        sig[i] = signal[i] - mean;
        // numpy.allclose(sig, 0): |x| <= atol (1e-8)
        if (std::abs(sig[i]) > 1e-8) allZero = false;
    }
    if (allZero) return 0;
    // |rfft| for k = 0..n/2 at k * fps / n Hz.
    cv::Mat in(1, int(n), CV_64F, sig.data()), out;
    cv::dft(in, out, cv::DFT_COMPLEX_OUTPUT);
    double total = 0, peak = 0;
    bool any = false;
    for (size_t k = 0; k <= n / 2; ++k) {
        const double f = double(k) * fps / double(n);
        if (f < 0.1 || f > 4.0) continue;
        const cv::Vec2d z = out.at<cv::Vec2d>(0, int(k));
        const double mag = std::hypot(z[0], z[1]);
        total += mag;
        peak = std::max(peak, mag);
        any = true;
    }
    if (!any || total <= 0) return 0;
    double amplitude = 0;
    for (double v : sig) amplitude += std::abs(v);
    amplitude /= double(n);
    return peak / total * amplitude;
}

std::vector<std::vector<std::pair<int, int>>> PeakRegions(const cv::Mat& scores, double decay, int maxRegions) {
    const int rows = scores.rows, cols = scores.cols;
    const int maxCells = std::max(4, rows * cols / 8);
    const int maxRowSpan = std::max(2, rows / 3), maxColSpan = std::max(2, cols / 3);
    std::vector<uchar> available(size_t(rows) * cols, 1);
    auto avail = [&](int r, int c) -> uchar& { return available[size_t(r) * cols + c]; };
    std::vector<std::vector<std::pair<int, int>>> regions;
    for (int n = 0; n < maxRegions; ++n) {
        // argmax over the available cells, first one wins on ties (numpy).
        int r0 = -1, c0 = -1;
        double peak = -std::numeric_limits<double>::infinity();
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                if (avail(r, c) && scores.at<double>(r, c) > peak) {
                    peak = scores.at<double>(r, c);
                    r0 = r;
                    c0 = c;
                }
        if (r0 < 0 || !std::isfinite(peak) || peak <= 0) break;
        const double core = peak * decay;
        // Python heapq of (-score, r, c): the best score first, then the
        // smaller row, then the smaller column.
        using Item = std::tuple<double, int, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        heap.emplace(-peak, r0, c0);
        std::vector<std::pair<int, int>> cells;
        int rmin = r0, rmax = r0, cmin = c0, cmax = c0;
        while (!heap.empty() && int(cells.size()) < maxCells) {
            const auto [neg, r, c] = heap.top();
            heap.pop();
            if (!avail(r, c)) continue;
            const int nrmin = std::min(rmin, r), nrmax = std::max(rmax, r);
            const int ncmin = std::min(cmin, c), ncmax = std::max(cmax, c);
            if (nrmax - nrmin + 1 > maxRowSpan || ncmax - ncmin + 1 > maxColSpan) continue;
            rmin = nrmin;
            rmax = nrmax;
            cmin = ncmin;
            cmax = ncmax;
            avail(r, c) = 0;
            cells.emplace_back(r, c);
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    const int nr = r + dr, nc = c + dc;
                    if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && avail(nr, nc) &&
                        scores.at<double>(nr, nc) >= core)
                        heap.emplace(-scores.at<double>(nr, nc), nr, nc);
                }
        }
        if (!cells.empty()) regions.push_back(std::move(cells));
    }
    return regions;
}

std::optional<TipFindResult> FindTip(const std::filesystem::path& video, const TipFindOptions& o, std::string* error) {
    auto fail = [&](const char* text) {
        if (error) *error = text;
        return std::nullopt;
    };
    std::string openError;
    auto reader = vision::FrameReader::Open(video, &openError);
    if (!reader) {
        if (error) *error = openError;
        return std::nullopt;
    }
    const double fps = reader->fps();  // Open rejects a video without a frame rate
    const int width = reader->size().width, height = reader->size().height;
    if (o.start_ms > 0 && !reader->SeekMs(o.start_ms)) return fail("the video cannot seek");
    const int sampleEvery = std::max(1, o.sample_every);
    const int maxFrames = std::max(1, int(o.max_seconds * fps));

    const double scale = width > 320 ? 320.0 / width : 1.0;
    const int smallW = int(width * scale), smallH = int(height * scale);
    const double cellW = double(smallW) / kGridCols, cellH = double(smallH) / kGridRows;

    vision::VideoFrame f;
    if (!reader->Read(f)) return fail("the first frame cannot be read");
    cv::Mat small, prevGray, gray, flow;
    cv::resize(f.bgr, small, cv::Size(smallW, smallH));
    cv::cvtColor(small, prevGray, cv::COLOR_BGR2GRAY);

    const size_t nCells = size_t(kGridRows) * kGridCols;
    const bool principal = o.axis == TipAxis::kPrincipal;
    // Per cell and analysed frame pair: the cell's mean flow minus the
    // camera's motion there. series is what gets scored: the vertical rest
    // (0.7), or the rest along the cell's own main direction of motion.
    std::vector<std::vector<double>> series(nCells), flowX(principal ? nCells : 0), flowY(principal ? nCells : 0);
    cv::Mat activity;
    if (principal) activity = cv::Mat::zeros(smallH, smallW, CV_32F);
    int frameIdx = 0, analysed = 0;
    while (frameIdx < maxFrames) {
        if (o.cancel && o.cancel()) return fail(kCanceled);
        if (o.progress) o.progress(std::min(99, frameIdx * 100 / maxFrames));
        bool ok = true;
        for (int i = 0; i < sampleEvery; ++i) {
            ok = reader->Read(f);
            ++frameIdx;
            if (!ok) break;
        }
        if (!ok) break;
        cv::resize(f.bgr, small, cv::Size(smallW, smallH));
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
        // 0.7 estimate_camera_motion: the engine's own (no excluded boxes),
        // with 0.7's 3 px RANSAC distance. 0.7 took its y shift only; the
        // main-direction score takes the camera's motion at each cell (a
        // pan moves every cell alike, a zoom moves the rim more).
        const CameraMotion cam = EstimateCameraMotion(prevGray, gray, {}, CameraMotion::kRansacPx07);
        const double camY = cam.ok ? cam.m(1, 2) : 0.0;
        const bool camUsable = cam.ok && cam.Plausible();
        cv::calcOpticalFlowFarneback(prevGray, gray, flow, 0.5, 3, 15, 3, 5, 1.2, 0);
        if (principal)
            for (int y = 0; y < smallH; ++y) {
                const cv::Vec2f* row = flow.ptr<cv::Vec2f>(y);
                float* out = activity.ptr<float>(y);
                for (int x = 0; x < smallW; ++x) {
                    double dx = row[x][0], dy = row[x][1];
                    if (camUsable) {
                        dx -= (cam.m(0, 0) - 1) * x + cam.m(0, 1) * y + cam.m(0, 2);
                        dy -= cam.m(1, 0) * x + (cam.m(1, 1) - 1) * y + cam.m(1, 2);
                    }
                    out[x] += float(std::hypot(dx, dy));
                }
            }
        for (int r = 0; r < kGridRows; ++r)
            for (int c = 0; c < kGridCols; ++c) {
                const int y0 = int(r * cellH), y1 = int((r + 1) * cellH);
                const int x0 = int(c * cellW), x1 = int((c + 1) * cellW);
                double sumX = 0, sumY = 0;
                for (int y = y0; y < y1; ++y) {
                    const cv::Vec2f* row = flow.ptr<cv::Vec2f>(y);
                    for (int x = x0; x < x1; ++x) {
                        sumX += double(row[x][0]);
                        sumY += double(row[x][1]);
                    }
                }
                const double count = double(y1 - y0) * double(x1 - x0);
                const size_t cell = size_t(r) * kGridCols + c;
                if (!principal) {
                    // Camera motion off: what stays is the region's own motion.
                    series[cell].push_back(count > 0 ? sumY / count - camY : 0);
                    continue;
                }
                cv::Point2d shift(0, 0);
                if (camUsable) shift = cam.Shift({(x0 + x1) / 2.0, (y0 + y1) / 2.0});
                flowX[cell].push_back(count > 0 ? sumX / count - shift.x : 0);
                flowY[cell].push_back(count > 0 ? sumY / count - shift.y : 0);
            }
        std::swap(prevGray, gray);
        ++analysed;
    }
    if (o.progress) o.progress(99);
    if (analysed < 8) return fail("the video is too short or unreadable for the automatic search");

    const double effectiveFps = fps / sampleEvery;
    std::vector<cv::Point2d> axes(nCells, cv::Point2d(0, 1));
    if (principal)
        for (size_t cell = 0; cell < nCells; ++cell) {
            axes[cell] = MainDirection(flowX[cell], flowY[cell]);
            series[cell].resize(flowX[cell].size());
            for (size_t i = 0; i < series[cell].size(); ++i)
                series[cell][i] = flowX[cell][i] * axes[cell].x + flowY[cell][i] * axes[cell].y;
        }
    cv::Mat scores(kGridRows, kGridCols, CV_64F);
    double best = 0;
    for (int r = 0; r < kGridRows; ++r)
        for (int c = 0; c < kGridCols; ++c) {
            scores.at<double>(r, c) = PeriodicityScore(series[size_t(r) * kGridCols + c], effectiveFps);
            best = std::max(best, scores.at<double>(r, c));
        }
    if (best <= 0) return fail("no rhythmic motion found - please mark the tip by hand");
    const auto regions = PeakRegions(scores);
    if (regions.empty()) return fail("no moving regions found - please mark the tip by hand");

    TipFindResult res;
    size_t winner = 0;
    double winnerScore = -1;
    for (size_t i = 0; i < regions.size(); ++i) {
        const double score = TipRegionScore(scores, regions[i]);
        res.candidates.push_back({CellsToBox(regions[i], cellW, cellH, scale, width, height), score});
        if (score > winnerScore) {  // the first with the highest score, as the stable sort below
            winnerScore = score;
            winner = i;
        }
    }
    std::stable_sort(res.candidates.begin(), res.candidates.end(),
                     [](const TipCandidate& a, const TipCandidate& b) { return a.score > b.score; });
    // 0.7 max(): the first region with the highest score, which the stable
    // sort above has put in front.
    res.box = ShrinkBox(res.candidates.front().box, width, height);
    if (principal) {
        const auto [pr, pc] = regions[winner].front();  // the region's peak cell
        res.axis = axes[size_t(pr) * kGridCols + pc];
        const cv::Rect peakCell(int(pc * cellW), int(pr * cellH), std::max(1, int((pc + 1) * cellW) - int(pc * cellW)),
                                std::max(1, int((pr + 1) * cellH) - int(pr * cellH)));
        const cv::Rect mover = MoverBox(activity, peakCell, res.axis, series[size_t(pr) * kGridCols + pc]);
        if (mover.width >= kMinRegionSide && mover.height >= kMinRegionSide) {
            cv::Rect box(int(std::lround(mover.x / scale)), int(std::lround(mover.y / scale)),
                         int(std::lround(mover.width / scale)), int(std::lround(mover.height / scale)));
            res.region = box & cv::Rect(0, 0, width, height);
        }
    }
    return res;
}

}  // namespace samn::engine
