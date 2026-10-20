// The ONNX detector (D-155 step 4) on a tiny generated model
// (data/make_tiny_yolo.py): no trained weights and no pictures.
#include <doctest/doctest.h>

#include <vector>

#include <opencv2/imgproc.hpp>

#include "samn/vision/onnx_detector.h"

using samn::vision::Detection;

namespace {
const std::string kTinyModel = std::string(SAMN_VISION_TESTDATA) + "/tiny_yolo.onnx";

// A YOLOv8 head (rows x anchors) filled per anchor.
struct Head {
    int rows, anchors;
    std::vector<float> v;
    Head(int r, int a) : rows(r), anchors(a), v(size_t(r * a), 0.f) {}
    void set(int a, float cx, float cy, float w, float h, int cls, float score) {
        v[size_t(a)] = cx;
        v[size_t(anchors + a)] = cy;
        v[size_t(2 * anchors + a)] = w;
        v[size_t(3 * anchors + a)] = h;
        v[size_t((4 + cls) * anchors + a)] = score;
    }
};
}  // namespace

TEST_CASE("YOLOv8 decoding: threshold, one box per object, frame pixels") {
    Head h(4 + 3, 5);
    h.set(0, 10, 10, 4, 4, 2, 0.8f);   // class 2
    h.set(1, 10.5f, 10, 4, 4, 1, 0.7f);  // the same object, weaker: dropped
    h.set(2, 30, 20, 10, 6, 0, 0.5f);
    h.set(3, 30, 5, 10, 6, 1, 0.1f);    // below the threshold
    h.set(4, 39, 20, 6, 6, 0, 0.9f);    // half outside the frame: clipped
    const auto d = samn::vision::DecodeYoloV8(h.v.data(), h.rows, h.anchors, 2.0, cv::Size(80, 60));
    REQUIRE(d.size() == 3);
    CHECK(d[0].score == doctest::Approx(0.9));
    CHECK(d[0].box.x == doctest::Approx(72));
    CHECK(d[0].box.width == doctest::Approx(8));  // 72..80, the frame's edge
    CHECK(d[1].cls == 2);
    CHECK(d[1].box.x == doctest::Approx(16));
    CHECK(d[1].box.y == doctest::Approx(16));
    CHECK(d[1].box.width == doctest::Approx(8));
    CHECK(d[2].cls == 0);
    CHECK(d[2].box.height == doctest::Approx(12));
    CHECK(samn::vision::DecodeYoloV8(h.v.data(), 4, 5, 1, cv::Size(80, 60)).empty());  // no classes
    CHECK(samn::vision::DecodeYoloV8(nullptr, 7, 5, 1, cv::Size(80, 60)).empty());
}

TEST_CASE("the model's input: padded square, RGB, 0..1") {
    cv::Mat red(32, 64, CV_8UC3, cv::Scalar(0, 0, 255));  // BGR
    double scale = 0;
    const cv::Mat blob = samn::vision::YoloBlob(red, 64, &scale);
    REQUIRE(blob.dims == 4);
    CHECK(blob.size[1] == 3);
    CHECK(blob.size[2] == 64);
    CHECK(scale == doctest::Approx(1.0));
    const float* r = blob.ptr<float>(0, 0);
    const float* b = blob.ptr<float>(0, 2);
    CHECK(r[10 * 64 + 5] == doctest::Approx(1.0));   // red first, inside the frame
    CHECK(r[50 * 64 + 5] == doctest::Approx(0.0));   // the black padding below
    CHECK(b[10 * 64 + 5] == doctest::Approx(0.0));
}

TEST_CASE("NudeNet's classes and what they stand for in the label editor") {
    const auto& c = samn::vision::NudeNetClasses();
    REQUIRE(c.size() == 18);
    CHECK(c[14] == "MALE_GENITALIA_EXPOSED");
    CHECK(samn::vision::NudeNetToLabel("MALE_GENITALIA_EXPOSED") == "penis");
    CHECK(samn::vision::NudeNetToLabel("FACE_FEMALE") == "face");
    CHECK(samn::vision::NudeNetToLabel("FEMALE_BREAST_COVERED").empty());
    CHECK(samn::vision::NudeNetToLabel("FEET_EXPOSED").empty());
}

TEST_CASE("a detector model runs with ONNX Runtime") {
    samn::vision::OnnxDetector det;
    std::string error;
    if (!samn::vision::OnnxDetector::Available()) {
        CHECK_FALSE(det.Load(kTinyModel, &error));
        CHECK(error == "this build has no ONNX Runtime");
        return;
    }
    CHECK_FALSE(samn::vision::OnnxDetector::RuntimeVersion().empty());
    CHECK_FALSE(det.Load(std::string(SAMN_VISION_TESTDATA) + "/missing.onnx", &error));
    CHECK(error == "the model file is missing");
    CHECK_FALSE(det.loaded());
    CHECK(det.Detect(cv::Mat(8, 8, CV_8UC3), 0.25f, &error).empty());
    CHECK(error == "no model loaded");

    REQUIRE_MESSAGE(det.Load(kTinyModel, &error), error);
    CHECK(det.inputSize() == 64);
    CHECK(det.classCount() == 2);

    // Red, 128 x 96: the model sees it at half size; the black padding
    // below makes the red mean 0.75.
    const cv::Mat red(96, 128, CV_8UC3, cv::Scalar(0, 0, 255));
    auto d = det.Detect(red, 0.25f, &error);
    REQUIRE(d.size() == 2);  // anchor 2 is the same object as anchor 1
    CHECK(d[0].cls == 1);
    CHECK(d[0].score == doctest::Approx(0.9));
    CHECK(d[0].box.x == doctest::Approx(80));
    CHECK(d[0].box.y == doctest::Approx(68));
    CHECK(d[0].box.width == doctest::Approx(32));
    CHECK(d[1].cls == 0);
    CHECK(d[1].score == doctest::Approx(0.75).epsilon(0.01));
    CHECK(d[1].box.x == doctest::Approx(24));
    CHECK(d[1].box.height == doctest::Approx(16));

    // Blue has no red: only the fixed box.
    d = det.Detect(cv::Mat(96, 128, CV_8UC3, cv::Scalar(255, 0, 0)));
    REQUIRE(d.size() == 1);
    CHECK(d[0].cls == 1);
    CHECK(det.Detect(cv::Mat(), 0.25f, &error).empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("DETR decoding: sigmoid scores, boxes as fractions, best first") {
    // Two queries, three logits each.
    const float boxes[] = {0.5f, 0.5f, 0.2f, 0.4f,  // centre
                           0.95f, 0.1f, 0.2f, 0.2f};  // over the right edge
    const float logits[] = {-5, 2.0f, -5,   // class 1, 0.88
                            3.0f, -5, -5};  // class 0, 0.95
    const auto d = samn::vision::DecodeDetr(boxes, logits, 2, 3, cv::Size(200, 100), 0.5f);
    REQUIRE(d.size() == 2);
    CHECK(d[0].cls == 0);
    CHECK(d[0].score == doctest::Approx(0.9526).epsilon(0.001));
    CHECK(d[0].box.x == doctest::Approx(170));
    CHECK(d[0].box.width == doctest::Approx(30));  // 170..200, the frame's edge
    CHECK(d[1].cls == 1);
    CHECK(d[1].box.x == doctest::Approx(80));
    CHECK(d[1].box.y == doctest::Approx(30));
    CHECK(d[1].box.height == doctest::Approx(40));
    CHECK(samn::vision::DecodeDetr(boxes, logits, 2, 3, cv::Size(200, 100), 0.9f).size() == 1);
    CHECK(samn::vision::DecodeDetr(nullptr, logits, 2, 3, cv::Size(200, 100)).empty());
}

TEST_CASE("the DETR input: stretched, RGB, ImageNet-normalised") {
    const cv::Mat red(30, 90, CV_8UC3, cv::Scalar(0, 0, 255));
    const cv::Mat blob = samn::vision::DetrBlob(red, 64);
    REQUIRE(blob.dims == 4);
    CHECK(blob.size[2] == 64);
    CHECK(blob.size[3] == 64);
    CHECK(blob.ptr<float>(0, 0)[63 * 64 + 63] == doctest::Approx((1 - 0.485) / 0.229).epsilon(0.001));  // no padding
    CHECK(blob.ptr<float>(0, 2)[0] == doctest::Approx(-0.406 / 0.225).epsilon(0.001));
}

TEST_CASE("an RF-DETR-shaped model runs with ONNX Runtime") {
    if (!samn::vision::OnnxDetector::Available()) return;
    samn::vision::OnnxDetector det;
    std::string error;
    REQUIRE_MESSAGE(det.Load(std::string(SAMN_VISION_TESTDATA) + "/tiny_detr.onnx", &error), error);
    CHECK(det.isDetr());
    CHECK(det.inputSize() == 64);
    CHECK(det.classCount() == 3);
    // Red, 200 x 100: query 1 sees the red (class 0), query 0 is fixed (class 1).
    auto d = det.Detect(cv::Mat(100, 200, CV_8UC3, cv::Scalar(0, 0, 255)), 0.5f, &error);
    REQUIRE(d.size() == 2);
    CHECK(d[0].cls == 1);
    CHECK(d[0].box.x == doctest::Approx(30));  // (0.25 - 0.1) * 200
    CHECK(d[0].box.height == doctest::Approx(40));
    CHECK(d[1].cls == 0);
    CHECK(d[1].score == doctest::Approx(0.905).epsilon(0.01));
    // Blue: only the fixed query.
    d = det.Detect(cv::Mat(100, 200, CV_8UC3, cv::Scalar(255, 0, 0)), 0.5f, &error);
    REQUIRE(d.size() == 1);
    CHECK(d[0].cls == 1);
    CHECK(det.Load(std::string(SAMN_VISION_TESTDATA) + "/tiny_yolo.onnx", &error));  // YOLO still loads
    CHECK_FALSE(det.isDetr());
}
