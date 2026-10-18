#include "samn/engine/beatbar_video.h"

#include <algorithm>
#include <cmath>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "samn/vision/frame_reader.h"

namespace samn::engine {
namespace {

struct Canceled {};

// Frames from [startSec, startSec + durSec) (durSec 0 = to the end) as
// 320 x 192 grey, rows [y0, y1); vertical turns the picture a quarter
// clockwise first (a side strip becomes a bottom or top one). Resampled to
// 30 fps like ffmpeg's fps filter: each output tick takes the input frame
// nearest in time. Returns false when the video cannot be read. (Either
// quarter turn would do: it only has to lay the strip flat; FindBeatBar
// takes bars at the top as well as at the bottom.)
bool StreamGray(const std::filesystem::path& video, bool vertical, double startSec, double durSec, int y0, int y1,
                const BeatBarOptions& o, const std::function<void(const GrayFrame&, double sec)>& fn,
                std::string* error) {
    auto reader = vision::FrameReader::Open(video, error);
    if (!reader) return false;
    if (startSec > 0 && !reader->SeekMs(int64_t(std::llround(startSec * 1000)))) {
        if (error) *error = "the video cannot seek";
        return false;
    }
    const double tick = 1.0 / kBeatBarFps;
    const double endSec = durSec > 0 ? startSec + durSec : 1e18;
    double next = startSec;  // time of the next output frame
    GrayFrame prev{kBeatBarW, y1 - y0, {}};
    double prevSec = -1;
    cv::Mat turned, gray, small;
    auto emitUntil = [&](double limit) {
        while (next < limit && next < endSec) {
            fn(prev, next);
            next += tick;
        }
    };
    vision::VideoFrame f;
    while (reader->Read(f)) {
        if (o.cancel && o.cancel()) throw Canceled{};
        const double sec = double(f.pts_us) / 1e6;
        if (prevSec >= 0) emitUntil((prevSec + sec) / 2);  // nearest frame wins
        if (next >= endSec) break;
        const cv::Mat* src = &f.bgr;
        if (vertical) {
            cv::rotate(f.bgr, turned, cv::ROTATE_90_CLOCKWISE);
            src = &turned;
        }
        cv::cvtColor(*src, gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, small, cv::Size(kBeatBarW, kBeatBarH), 0, 0, cv::INTER_AREA);
        prev.pix.assign(small.ptr<uint8_t>(y0), small.ptr<uint8_t>(y0) + size_t(y1 - y0) * kBeatBarW);
        prevSec = sec;
        if (next < sec - tick) next = sec;  // a window that starts before the first frame
    }
    if (prevSec >= 0) emitUntil(prevSec + tick / 2 + 1e-9);
    return true;
}

double DurationSec(const std::filesystem::path& video) {
    std::string ignored;
    auto reader = vision::FrameReader::Open(video, &ignored);
    if (!reader || reader->fps() <= 0) return 0;
    return double(reader->frameCount()) / reader->fps();
}

}  // namespace

std::optional<BeatBarResult> DetectBeatBarBeats(const std::filesystem::path& video, const BeatBarOptions& o,
                                                std::string* error) {
    BeatBarResult res;
    const double duration = DurationSec(video);
    auto progress = [&](int p) {
        if (o.progress) o.progress(std::clamp(p, 0, 99));
    };
    try {
        // The bar may start after an intro: try the start, then 30 % and 60 %.
        std::vector<double> starts{0};
        if (duration > 3 * kBeatBarFindSec) {
            starts.push_back(duration * 0.3);
            starts.push_back(duration * 0.6);
        }
        // Most bars run along the bottom or top; some down a side edge.
        bool searched = false;
        for (const bool vertical : {false, true}) {
            for (const double ss : starts) {
                std::vector<GrayFrame> frames;
                if (!StreamGray(video, vertical, ss, kBeatBarFindSec, 0, kBeatBarH, o,
                                [&](const GrayFrame& g, double) { frames.push_back(g); }, error))
                    return std::nullopt;
                searched = true;
                if (const auto bar = FindBeatBar(frames)) {
                    res.found = true;
                    res.bar = *bar;
                    res.vertical = vertical;
                    break;
                }
            }
            if (res.found) break;
        }
        progress(10);
        if (!searched || !res.found) return res;
        BeatTimer timer(res.bar, kBeatBarFps);
        if (!StreamGray(video, res.vertical, 0, 0, res.bar.y0, res.bar.y1, o,
                        [&](const GrayFrame& g, double sec) {
                            timer.AddStrip(g);
                            if (duration > 0) progress(10 + int(sec / duration * 89));
                        },
                        error))
            return std::nullopt;
        res.beats_ms = timer.Beats();
        res.bpm = MedianBpm(res.beats_ms);
    } catch (const Canceled&) {
        if (error) *error = "canceled";
        return std::nullopt;
    }
    return res;
}

}  // namespace samn::engine
