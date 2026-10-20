#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "samn/vision/frame_reader.h"
#include "samn/video_info.h"

namespace fs = std::filesystem;
using samn::vision::FrameReader;
using samn::vision::VideoFrame;

// A generated test-pattern clip (no real media, GD-22): every frame shows
// its own number as 8 black/white columns, so a test can read back which
// frame the decoder returned.
namespace {
constexpr int kW = 160, kH = 96, kFrames = 60;
constexpr double kFps = 25;

fs::path WritePatternClip() {
    const fs::path p = fs::temp_directory_path() / ("samn_pattern_" + std::to_string(std::random_device{}()) + ".avi");
    cv::VideoWriter w(p.u8string(), cv::CAP_OPENCV_MJPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), kFps,
                      cv::Size(kW, kH));
    REQUIRE(w.isOpened());
    for (int i = 0; i < kFrames; ++i) {
        cv::Mat f(kH, kW, CV_8UC3, cv::Scalar(40, 40, 40));
        for (int bit = 0; bit < 8; ++bit)
            if (i & (1 << bit)) cv::rectangle(f, cv::Rect(bit * 20, 0, 20, kH), cv::Scalar(230, 230, 230), cv::FILLED);
        w.write(f);
    }
    w.release();
    return p;
}

int FrameNumber(const cv::Mat& bgr) {
    int n = 0;
    for (int bit = 0; bit < 8; ++bit)
        if (bgr.at<cv::Vec3b>(kH / 2, bit * 20 + 10)[1] > 128) n |= 1 << bit;
    return n;
}
}  // namespace

TEST_CASE("frame reader: every frame in order with its video time") {
    const fs::path clip = WritePatternClip();
    std::string error;
    auto r = FrameReader::Open(clip, &error);
    REQUIRE_MESSAGE(r, error);
    CHECK(r->fps() == doctest::Approx(kFps));
    CHECK(r->size() == cv::Size(kW, kH));
    CHECK(r->frameCount() == kFrames);
    VideoFrame f;
    int n = 0;
    while (r->Read(f)) {
        CHECK(f.index == n);
        CHECK(FrameNumber(f.bgr) == n);
        CHECK(std::llabs(f.pts_us - static_cast<long long>(n * 40000)) <= 1000);
        ++n;
    }
    CHECK(n == kFrames);
    r.reset();
    fs::remove(clip);
}

TEST_CASE("frame reader: seek is frame-exact, forwards and back") {
    const fs::path clip = WritePatternClip();
    std::string error;
    auto r = FrameReader::Open(clip, &error);
    REQUIRE_MESSAGE(r, error);
    VideoFrame f;
    for (const auto [ms, want] : {std::pair<int64_t, int>{1480, 37}, {200, 5}, {1481, 38}, {0, 0}, {59000, kFrames - 1}}) {
        CAPTURE(ms);
        REQUIRE(r->SeekMs(ms));
        REQUIRE(r->Read(f));
        CHECK(f.index == want);
        CHECK(FrameNumber(f.bgr) == want);
        CHECK(std::llabs(f.pts_us - static_cast<long long>(want * 40000)) <= 1000);
    }
    r.reset();
    fs::remove(clip);
}

TEST_CASE("frame reader: a missing or broken file is an error with the path") {
    std::string error;
    CHECK_FALSE(FrameReader::Open(fs::temp_directory_path() / "samn_no_such_clip.mp4", &error));
    CHECK(error.find("samn_no_such_clip.mp4") != std::string::npos);
    const fs::path junk = fs::temp_directory_path() / ("samn_junk_" + std::to_string(std::random_device{}()) + ".mp4");
    { std::FILE* fp = std::fopen(junk.u8string().c_str(), "wb"); REQUIRE(fp); std::fputs("not a video", fp); std::fclose(fp); }
    error.clear();
    CHECK_FALSE(FrameReader::Open(junk, &error));
    CHECK(!error.empty());
    fs::remove(junk);
}

TEST_CASE("frame reader: the codec as the file says, unknown stays unknown (issue #4)") {
    const fs::path p = WritePatternClip();  // MJPG
    std::string error;
    auto r = FrameReader::Open(p, &error);
    REQUIRE(r);
    CHECK(samn::CodecName(r->fourcc()) == "Motion JPEG");
    r.reset();
    fs::remove(p);
    // A file that is not a video opens to nothing, with a reason; no made-up values.
    const fs::path junk = fs::temp_directory_path() / ("samn_junk_" + std::to_string(std::random_device{}()) + ".mp4");
    {
        std::ofstream(junk, std::ios::binary) << "ftypisom but not really a video";
    }
    CHECK_FALSE(FrameReader::Open(junk, &error));
    CHECK_FALSE(error.empty());
    fs::remove(junk);
}
