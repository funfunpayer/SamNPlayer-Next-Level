// Tracking between two keyframes of a label track (D-155 step 3), on a
// generated clip: a disc moving up and down.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <random>
#include <opencv2/imgproc.hpp>

#include "samn/engine/label_tracking.h"
#include "samn/vision/frame_reader.h"
#include "samn/vision/tracker.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using namespace samn::engine::testclips;

namespace {
// The disc's box at ms (WriteMoving: x 160, y = 120 + amp/2 sin(2 pi f t), r 35).
samn::LabelBox DiscAt(int64_t ms, double amp, double freq) {
    const double y = 120 + amp / 2 * std::sin(2 * kPi * freq * double(ms) / 1000.0);
    return {160 - 35, y - 35, 70, 70};
}
}  // namespace

TEST_CASE("tracking between keys follows the disc both ways and blends") {
    const fs::path p = fs::temp_directory_path() / ("samn_between_" + std::to_string(std::random_device{}()) + ".mp4");
    REQUIRE(WriteMoving(p, 3, 60.0, 0.5));
    samn::engine::BetweenKeysOptions o;
    o.step_ms = 200;
    int last = -1;
    o.progress = [&](int v) { last = v; };
    std::string error;
    const auto r = samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), o, &error);
    INFO(error);
    REQUIRE(r);
    CHECK(last == 100);
    REQUIRE(r->forward.size() >= 8);
    REQUIRE(r->forward.size() == r->backward.size());
    const auto keys = samn::BlendTracks(r->forward, r->backward, 0, 2000);
    REQUIRE(keys.size() >= 8);
    for (const auto& k : keys) {
        INFO(k.ms);
        CHECK_FALSE(k.uncertain);
        CHECK(samn::BoxIoU(k.box, DiscAt(k.ms, 60, 0.5)) > 0.6);
    }
    // Errors say why; cancel stops.
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 2000, DiscAt(0, 60, 0.5), 1000, DiscAt(0, 60, 0.5), o, &error));
    samn::engine::BetweenKeysOptions stop;
    stop.cancel = [] { return true; };
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), stop, &error));
    CHECK(error == "canceled");
    samn::engine::BetweenKeysOptions tiny;
    tiny.max_frames = 5;
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), tiny, &error));
    CHECK(error.find("too long") != std::string::npos);
    fs::remove(p);
}

// Review E2: a step of zero (or less) made the sample loop run forever. It is
// refused before the video is even opened.
TEST_CASE("tracking between keys refuses a step of zero or less") {
    for (const int64_t step : {int64_t(0), int64_t(-200)}) {
        samn::engine::BetweenKeysOptions o;
        o.step_ms = step;
        std::string error;
        CHECK_FALSE(samn::engine::TrackBetweenKeys("no-such-clip.mp4", 0, DiscAt(0, 60, 0.5), 2000,
                                                   DiscAt(2000, 60, 0.5), o, &error));
        CHECK(error.find("step") != std::string::npos);
    }
}

namespace {

// The tracking as it was before the bounded window (D-155 step 3): every small
// frame of the stretch in memory, both runs over the vector. The reference the
// streaming version must equal bit for bit.
samn::engine::BetweenKeysResult HoldEverything(const fs::path& video, int64_t from_ms, const samn::LabelBox& from_box,
                                               int64_t to_ms, const samn::LabelBox& to_box, int64_t step_ms,
                                               int max_side) {
    std::string error;
    auto reader = samn::vision::FrameReader::Open(video, &error);
    REQUIRE(reader);
    REQUIRE(reader->SeekMs(from_ms));
    std::vector<cv::Mat> frames;
    std::vector<int64_t> times;
    double scale = 1.0;
    samn::vision::VideoFrame f;
    while (reader->Read(f)) {
        const int64_t ms = f.pts_us / 1000;
        if (ms > to_ms) break;
        if (frames.empty()) scale = std::min(1.0, double(max_side) / std::max(f.bgr.cols, f.bgr.rows));
        cv::Mat small;
        if (scale < 1.0) cv::resize(f.bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
        else small = f.bgr.clone();
        frames.push_back(std::move(small));
        times.push_back(ms);
    }
    std::vector<size_t> samples;
    for (int64_t next = from_ms + step_ms; next < to_ms; next += step_ms) {
        const auto it = std::lower_bound(times.begin(), times.end(), next);
        if (it == times.end()) break;
        const size_t i = size_t(it - times.begin());
        if (samples.empty() || samples.back() != i) samples.push_back(i);
    }
    samn::engine::BetweenKeysResult out;
    for (const bool forward : {true, false}) {
        auto tracker = samn::vision::CreateTracker(samn::vision::TrackerKind::kCsrt, &error);
        REQUIRE(tracker);
        const size_t n = frames.size();
        const size_t start = forward ? 0 : n - 1;
        const samn::LabelBox& b = forward ? from_box : to_box;
        cv::Rect init(int(std::lround(b.x * scale)), int(std::lround(b.y * scale)), int(std::lround(b.w * scale)),
                      int(std::lround(b.h * scale)));
        init &= cv::Rect(0, 0, frames[start].cols, frames[start].rows);
        tracker->Init(frames[start], init);
        std::vector<std::optional<samn::LabelBox>> at(n);
        at[start] = b;
        bool lost = false;
        for (size_t k = 1; k < n; ++k) {
            const size_t i = forward ? k : n - 1 - k;
            if (lost) continue;
            if (auto r = tracker->Update(frames[i]))
                at[i] = samn::LabelBox{r->x / scale, r->y / scale, r->width / scale, r->height / scale};
            else lost = true;
        }
        auto& dst = forward ? out.forward : out.backward;
        for (const size_t i : samples) dst.push_back({times[i], at[i]});
    }
    return out;
}

void CheckSameRun(const std::vector<samn::TrackSample>& a, const std::vector<samn::TrackSample>& b) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].ms == b[i].ms);
        REQUIRE(a[i].box.has_value() == b[i].box.has_value());
        if (!a[i].box) continue;
        CHECK(a[i].box->x == b[i].box->x);  // bit for bit: same frames in, same tracker
        CHECK(a[i].box->y == b[i].box->y);
        CHECK(a[i].box->w == b[i].box->w);
        CHECK(a[i].box->h == b[i].box->h);
    }
}

}  // namespace

// The memory fix: the stretch is read through a bounded window, never held
// whole, and the tracked boxes are exactly those of the hold-everything
// version, for a window larger than the stretch, a few chunks, and one frame.
TEST_CASE("tracking between keys holds a bounded window and gives the same boxes") {
    const fs::path p = TempPath("samn_between_window").replace_extension(".mp4");
    REQUIRE(WriteMoving(p, 4, 60.0, 0.5));  // 100 frames
    const samn::LabelBox a = DiscAt(0, 60, 0.5), b = DiscAt(3000, 60, 0.5);
    const auto ref = HoldEverything(p, 0, a, 3000, b, 200, 640);
    REQUIRE(ref.forward.size() >= 10);
    for (const int window : {1, 7, 24, 500}) {
        INFO("window " << window);
        samn::engine::BetweenKeysOptions o;
        o.window_frames = window;
        o.max_window_frames = window;  // no growth: the bound is the one asked for
        samn::engine::BetweenKeysStats st;
        o.stats = &st;
        std::string error;
        const auto r = samn::engine::TrackBetweenKeys(p, 0, a, 3000, b, o, &error);
        INFO(error);
        REQUIRE(r);
        CheckSameRun(r->forward, ref.forward);
        CheckSameRun(r->backward, ref.backward);
        // Over time: the two chunk buffers plus the full-size read buffers (one
        // for each of the two threads); never the 76 frames of the stretch
        // unless the window is that large.
        const int peak = st.peak_frames_held.load();
        CHECK(peak >= 2);
        CHECK(peak <= 2 * window + 3);
        if (window < 36) CHECK(peak < 76);
        CHECK(st.frames_decoded >= 2 * 75);
        CHECK(st.frames_decoded <= 2 * 76 + 1);
    }
    fs::remove(p);
}

// Both runs lose the disc at a hard cut: gone stays gone, and the boxes (here
// nothing after the cut) equal the reference, with the backward run stopping early.
TEST_CASE("tracking between keys on a cut clip equals the reference") {
    const fs::path p = TempPath("samn_between_cut").replace_extension(".mp4");
    REQUIRE(WriteHardCut(p, 4, 50, 60.0));
    const samn::LabelBox a = DiscAt(0, 60, 0.5);
    for (const int window : {3, 24}) {
        INFO("window " << window);
        const auto ref = HoldEverything(p, 0, a, 3000, a, 200, 640);
        samn::engine::BetweenKeysOptions o;
        o.window_frames = window;
        std::string error;
        const auto r = samn::engine::TrackBetweenKeys(p, 0, a, 3000, a, o, &error);
        INFO(error);
        REQUIRE(r);
        CheckSameRun(r->forward, ref.forward);
        CheckSameRun(r->backward, ref.backward);
    }
    fs::remove(p);
}

// Cancel at the k-th poll, wherever it falls (decode, forward, backward chunk
// edges): always "canceled", never a hang or another error.
TEST_CASE("tracking between keys cancels after k polls") {
    const fs::path p = TempPath("samn_between_cancel").replace_extension(".mp4");
    REQUIRE(WriteMoving(p, 3, 60.0, 0.5));
    for (const int k : {1, 2, 10, 40, 51, 60, 80, 95}) {
        INFO("poll " << k);
        samn::engine::BetweenKeysOptions o;
        o.window_frames = 8;
        int polls = 0;
        o.cancel = [&] { return ++polls >= k; };
        std::string error;
        CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), o, &error));
        CHECK(error == "canceled");
    }
    fs::remove(p);
}

// A key box outside the frame is reported after the checks on the stretch, as
// it was: a too-long stretch says "too long", not the box.
TEST_CASE("tracking between keys reports the stretch before the box") {
    const fs::path p = TempPath("samn_between_box").replace_extension(".mp4");
    REQUIRE(WriteMoving(p, 3, 60.0, 0.5));
    const samn::LabelBox outside{5000, 5000, 70, 70};
    samn::engine::BetweenKeysOptions o;
    std::string error;
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, outside, 2000, DiscAt(2000, 60, 0.5), o, &error));
    CHECK(error.find("outside the frame") != std::string::npos);
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, outside, o, &error));
    CHECK(error.find("outside the frame") != std::string::npos);
    o.max_frames = 5;
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, outside, 2000, DiscAt(2000, 60, 0.5), o, &error));
    CHECK(error.find("too long") != std::string::npos);
    fs::remove(p);
}

namespace {
bool HaveFfmpeg() {
#ifdef _WIN32
    return std::system("ffmpeg -version >nul 2>&1") == 0;
#else
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0;
#endif
}
// Re-encodes `in` with ffmpeg; the filter and flags make it variable frame rate
// or long-GOP. False when ffmpeg fails.
bool Reencode(const fs::path& in, const fs::path& out, const std::string& flags) {
#ifdef _WIN32
    const std::string quiet = " >nul 2>&1";
#else
    const std::string quiet = " >/dev/null 2>&1";
#endif
    const std::string cmd = "ffmpeg -y -i \"" + in.string() + "\" " + flags + " \"" + out.string() + "\"" + quiet;
    return std::system(cmd.c_str()) == 0;
}
// True when ffmpeg can be run. When it cannot: on CI (env CI or
// SAMN_NATIVE_REQUIRED) that fails the case, so a missing program is never a
// silent pass; on Windows the skip is accepted, loudly (the Windows runner has
// no ffmpeg program; the Linux and sanitizer jobs install it); elsewhere it
// is a message, for a developer machine.
bool FfmpegOrFail() {
    if (HaveFfmpeg()) return true;
#ifdef _WIN32
    // On stderr as well: ctest shows a passing test's output only on failure.
    std::fprintf(stderr, "SKIPPED ON WINDOWS (accepted): no ffmpeg program; the VFR, long-GOP and equal-timestamp label tests did NOT run here\n");
    MESSAGE("SKIPPED ON WINDOWS (accepted): the ffmpeg program is not installed; the clips made with it were NOT tested here");
#else
    if (std::getenv("CI") || std::getenv("SAMN_NATIVE_REQUIRED")) {
        FAIL_CHECK("ffmpeg is not installed, but CI or SAMN_NATIVE_REQUIRED is set: these checks must run, not skip");
    } else {
        std::fprintf(stderr, "SKIPPED: no ffmpeg program; the ffmpeg-made label clips were NOT tested\n");
        MESSAGE("SKIPPED: ffmpeg is not installed here; the clips made with it were NOT tested");
    }
#endif
    return false;
}
}  // namespace

// Variable frame rate (phones record it) and a long GOP: a seek lands by the
// nominal rate, after the wanted frame on VFR. The old algorithm worked on
// these; so must the windowed one, with the same boxes. Needs the ffmpeg
// program (synthetic pattern re-encoded; nothing is stored); without it the
// case fails on CI and says so loudly elsewhere (see FfmpegOrFail).
TEST_CASE("tracking between keys on variable-rate and long-GOP clips equals the reference") {
    if (!FfmpegOrFail()) return;
    const fs::path src = TempPath("samn_between_cfr").replace_extension(".mp4");
    REQUIRE(WriteMoving(src, 4, 60.0, 0.5));  // 100 frames
    struct Variant { std::string name; std::string flags; };
    const Variant variants[] = {
        {"vfr irregular drops", "-vf \"select='not(eq(mod(n,5),1)+eq(mod(n,7),3))'\" -fps_mode vfr -c:v mpeg4"},
        {"vfr every third dropped", "-vf \"select='gt(mod(n,3),0)'\" -fps_mode vfr -c:v mpeg4"},
        {"long gop", "-c:v mpeg4 -g 300 -bf 0"},
    };
    for (const auto& v : variants) {
        INFO(v.name);
        const fs::path clip = TempPath("samn_between_var").replace_extension(".mp4");
        if (!Reencode(src, clip, v.flags)) {
            FAIL_CHECK("this ffmpeg could not make the clip " << v.name);
            continue;
        }
        for (const int64_t from : {int64_t(0), int64_t(500)}) {
            const int64_t to = from + 2500;
            const samn::LabelBox a = DiscAt(from, 60, 0.5), b = DiscAt(to, 60, 0.5);
            const auto ref = HoldEverything(clip, from, a, to, b, 200, 640);
            REQUIRE(ref.forward.size() >= 10);
            for (const int window : {1, 3, 24}) {
                INFO("from " << from << " window " << window);
                samn::engine::BetweenKeysOptions o;
                o.window_frames = window;
                std::string error;
                const auto r = samn::engine::TrackBetweenKeys(clip, from, a, to, b, o, &error);
                INFO(error);
                REQUIRE(r);
                CheckSameRun(r->forward, ref.forward);
                CheckSameRun(r->backward, ref.backward);
            }
        }
        fs::remove(clip);
    }
    fs::remove(src);
}

// Frames that share a millisecond (equal timestamps, as some muxers and cuts
// leave them): the seek-and-grab for a chunk starting inside such a run must
// land on the first frame of the run and step over the ones before the wanted
// frame. Before, a window of 1 returned boxes of the wrong frames and a larger
// window stopped with "the video changed between two reads".
TEST_CASE("tracking between keys on a clip with duplicate timestamps equals the reference") {
    if (!FfmpegOrFail()) return;
    const fs::path src = TempPath("samn_between_dup_src").replace_extension(".mp4");
    REQUIRE(WriteMoving(src, 4, 60.0, 0.5));  // 100 frames
    // The encoder refuses equal timestamps (and so does setpts), so the clip is
    // encoded without B-frames and the timestamps are rewritten in the copy
    // (setts), into Matroska, which keeps them.
    const fs::path plain = TempPath("samn_between_dup_plain").replace_extension(".mkv");
    if (!Reencode(src, plain, "-c:v mpeg4 -bf 0")) {
        FAIL_CHECK("this ffmpeg could not make the base clip for the duplicate-timestamp checks");
        return;
    }
    struct Variant { std::string name; std::string flags; };
    const Variant variants[] = {
        // Two frames per timestamp, 80 ms apart.
        {"pairs", "-c copy -bsf:v \"setts=ts='floor(N/2)*0.08/TB'\""},
        // Three frames per timestamp, 120 ms apart.
        {"triples", "-c copy -bsf:v \"setts=ts='floor(N/3)*0.12/TB'\""},
    };
    for (const auto& v : variants) {
        INFO(v.name);
        const fs::path clip = TempPath("samn_between_dup").replace_extension(".mkv");
        if (!Reencode(plain, clip, v.flags)) {
            FAIL_CHECK("this ffmpeg could not make the duplicate-timestamp clip " << v.name);
            continue;
        }
        for (const int64_t from : {int64_t(0), int64_t(500)}) {
            const int64_t to = from + 2500;
            const samn::LabelBox a = DiscAt(from, 60, 0.5), b = DiscAt(to, 60, 0.5);
            const auto ref = HoldEverything(clip, from, a, to, b, 200, 640);
            REQUIRE(ref.forward.size() >= 10);
            for (const int window : {1, 3, 24}) {
                INFO("from " << from << " window " << window);
                samn::engine::BetweenKeysOptions o;
                o.window_frames = window;
                o.max_window_frames = window;
                std::string error;
                const auto r = samn::engine::TrackBetweenKeys(clip, from, a, to, b, o, &error);
                INFO(error);
                REQUIRE(r);
                CheckSameRun(r->forward, ref.forward);
                CheckSameRun(r->backward, ref.backward);
            }
        }
        fs::remove(clip);
    }
    fs::remove(plain);
    fs::remove(src);
}

// Keys inside runs of equal timestamps, other codecs and GOPs (review of the
// first equal-timestamp fix): the frame wanted can be the first of a run while
// the seek lands on its second; intra-only (mjpeg) and short-GOP streams land
// differently from the plain mpeg4 case. For each clip, from a key at a run
// start, between runs and at 0, windows 1, 3, 24 and the adaptive default:
// boxes equal the old algorithm's bit for bit, and the decode stays linear (no
// re-decode from the start for every chunk).
TEST_CASE("tracking between keys with keys inside runs of equal timestamps equals the reference") {
    if (!FfmpegOrFail()) return;
    const fs::path src = TempPath("samn_between_runs_src").replace_extension(".mp4");
    REQUIRE(WriteMoving(src, 6, 60.0, 0.5));  // 150 frames
    struct Variant { std::string name, codec, ts; int64_t offset_ms; };
    const Variant variants[] = {
        {"mjpeg pairs", "-c:v mjpeg -q:v 3", "floor(N/2)*0.08/TB", 0},
        {"mjpeg triples", "-c:v mjpeg -q:v 3", "floor(N/3)*0.12/TB", 0},
        {"mpeg4 gop 7 pairs", "-c:v mpeg4 -bf 0 -g 7", "floor(N/2)*0.08/TB", 0},
        {"mpeg4 gop 7 triples", "-c:v mpeg4 -bf 0 -g 7", "floor(N/3)*0.12/TB", 0},
        {"mpeg4 gop 7 pairs, start at 1.3 s", "-c:v mpeg4 -bf 0 -g 7", "floor(N/2)*0.08/TB+1.3/TB", 1300},
    };
    for (const auto& v : variants) {
        INFO(v.name);
        const fs::path plain = TempPath("samn_between_runs_plain").replace_extension(".mkv");
        const fs::path clip = TempPath("samn_between_runs").replace_extension(".mkv");
        if (!Reencode(src, plain, v.codec) ||
            !Reencode(plain, clip, "-c copy -bsf:v \"setts=ts='" + v.ts + "'\"")) {
            FAIL_CHECK("this ffmpeg could not make the clip " << v.name);
            fs::remove(plain);
            fs::remove(clip);
            continue;
        }
        fs::remove(plain);
        for (const int64_t rel : {int64_t(0), int64_t(480), int64_t(500)}) {
            const int64_t from = v.offset_ms + rel, to = from + 4000;
            const samn::LabelBox a = DiscAt(rel, 60, 0.5), b = DiscAt(rel + 4000, 60, 0.5);
            const auto ref = HoldEverything(clip, from, a, to, b, 200, 640);
            REQUIRE(ref.forward.size() >= 10);
            // The frames of the stretch, as the tool sees them.
            std::string error;
            auto reader = samn::vision::FrameReader::Open(clip, &error);
            REQUIRE(reader);
            REQUIRE(reader->SeekMs(from));
            int n = 0;
            for (samn::vision::VideoFrame f; reader->Read(f) && f.pts_us / 1000 <= to;) ++n;
            for (const int window : {1, 3, 24, 0}) {  // 0: the defaults, with the adaptive window
                INFO("from " << from << " window " << window);
                samn::engine::BetweenKeysOptions o;
                if (window > 0) {
                    o.window_frames = window;
                    o.max_window_frames = window;
                }
                samn::engine::BetweenKeysStats st;
                o.stats = &st;
                error.clear();
                const auto r = samn::engine::TrackBetweenKeys(clip, from, a, to, b, o, &error);
                INFO(error);
                REQUIRE(r);
                CheckSameRun(r->forward, ref.forward);
                CheckSameRun(r->backward, ref.backward);
                // Forward n, backward n, and a few frames of warm-up per chunk;
                // a re-decode from the start for each chunk would be far above.
                const int chunks = (n + std::max(1, window == 0 ? 24 : window) - 1) / std::max(1, window == 0 ? 24 : window);
                CHECK(st.frames_decoded <= 2 * (n + 1) + 8 * chunks + 10);
            }
        }
        fs::remove(clip);
    }
    fs::remove(src);
}
