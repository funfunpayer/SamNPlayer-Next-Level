// Picture signals for the phase rules (D-157), on a generated clip.
#include <doctest/doctest.h>

#include <filesystem>
#include <random>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "samn/vision/picture_signals.h"

namespace fs = std::filesystem;

TEST_CASE("picture signals: black is dark, a still picture is calm, a moving one is not") {
    const fs::path p = fs::temp_directory_path() / ("samn_signals_" + std::to_string(std::random_device{}()) + ".avi");
    constexpr double kFps = 20;
    {
        cv::VideoWriter w(p.u8string(), cv::CAP_OPENCV_MJPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), kFps,
                          cv::Size(128, 72));
        REQUIRE(w.isOpened());
        for (int i = 0; i < 60; ++i) {  // 0..3 s: black, 3..6 s: still grey, 6..9 s: a moving square
            cv::Mat f(72, 128, CV_8UC3, cv::Scalar(0, 0, 0));
            if (i >= 20) f.setTo(cv::Scalar(120, 120, 120));
            if (i >= 40) cv::rectangle(f, cv::Rect((i - 40) * 5, 10, 30, 30), cv::Scalar(250, 250, 250), cv::FILLED);
            w.write(f);
        }
    }
    samn::vision::PictureSignalOptions o;
    o.step_ms = 250;
    int last = -1;
    o.progress = [&](int v) { last = v; };
    std::string error;
    const auto s = samn::vision::MeasurePictureSignals(p, o, &error);
    samn::vision::PictureSignalOptions stop;
    stop.cancel = [] { return true; };
    std::string why;
    CHECK(samn::vision::MeasurePictureSignals(p, stop, &why).luma.empty());
    CHECK(why == "canceled");
    fs::remove(p);
    INFO(error);
    REQUIRE(s.luma.size() >= 10);
    CHECK(s.luma.size() == s.motion.size());
    CHECK(last == 100);
    const size_t n = s.luma.size();
    CHECK(s.luma[1] < 0.05);                // black
    CHECK(s.luma[n / 2] > 0.4);             // grey
    CHECK(s.motion[n / 2 - 1] < 0.01);      // still
    CHECK(s.motion[n - 2] > 0.01);          // moving
    // A broken path fails with a reason.
    CHECK(samn::vision::MeasurePictureSignals("/nonexistent/clip.mp4", {}, &error).luma.empty());
    CHECK_FALSE(error.empty());
}
