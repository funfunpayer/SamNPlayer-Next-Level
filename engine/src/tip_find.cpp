#include "samn/engine/tip_find.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

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
    const double fps = reader->fps() > 0 ? reader->fps() : 30.0;
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

    std::vector<std::vector<double>> series(size_t(kGridRows) * kGridCols);
    int frameIdx = 0, analysed = 0;
    while (frameIdx < maxFrames) {
        if (o.cancel && o.cancel()) return fail("canceled");
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
        // 0.7 estimate_camera_motion: the engine's own (no excluded boxes).
        const double camY = CameraMotionY(prevGray, gray, {});
        cv::calcOpticalFlowFarneback(prevGray, gray, flow, 0.5, 3, 15, 3, 5, 1.2, 0);
        for (int r = 0; r < kGridRows; ++r)
            for (int c = 0; c < kGridCols; ++c) {
                const int y0 = int(r * cellH), y1 = int((r + 1) * cellH);
                const int x0 = int(c * cellW), x1 = int((c + 1) * cellW);
                double sum = 0;
                for (int y = y0; y < y1; ++y) {
                    const cv::Vec2f* row = flow.ptr<cv::Vec2f>(y);
                    for (int x = x0; x < x1; ++x) sum += double(row[x][1]);
                }
                const double count = double(y1 - y0) * double(x1 - x0);
                // Camera motion off: what stays is the region's own motion.
                series[size_t(r) * kGridCols + c].push_back(count > 0 ? sum / count - camY : 0);
            }
        std::swap(prevGray, gray);
        ++analysed;
    }
    if (o.progress) o.progress(99);
    if (analysed < 8) return fail("the video is too short or unreadable for the automatic search");

    const double effectiveFps = fps / sampleEvery;
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
    for (const auto& cells : regions)
        res.candidates.push_back({CellsToBox(cells, cellW, cellH, scale, width, height), TipRegionScore(scores, cells)});
    std::stable_sort(res.candidates.begin(), res.candidates.end(),
                     [](const TipCandidate& a, const TipCandidate& b) { return a.score > b.score; });
    // 0.7 max(): the first region with the highest score.
    size_t pick = 0;
    double pickScore = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < regions.size(); ++i) {
        const double s = TipRegionScore(scores, regions[i]);
        if (s > pickScore) {
            pickScore = s;
            pick = i;
        }
    }
    res.box = ShrinkBox(CellsToBox(regions[pick], cellW, cellH, scale, width, height), width, height);
    return res;
}

}  // namespace samn::engine
