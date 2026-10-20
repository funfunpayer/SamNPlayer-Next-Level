// A YOLOv8-style object detector in ONNX format, run with ONNX Runtime
// (D-155 step 4): NudeNet as a local teacher, later our own student. The
// picture goes in as NudeNet expects it (padded to a square at the right and
// bottom, RGB, 0..1); the boxes come back in frame pixels. Without ONNX
// Runtime in the build (SAMN_WITH_ONNXRUNTIME off) Load() says so.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace samn::vision {

struct Detection {
    int cls = -1;      // index into the model's classes
    float score = 0;   // 0..1
    cv::Rect2f box;    // frame pixels
};

// The model's input: pad right and bottom to a square (black), resize to
// size × size, RGB, scaled to 0..1, NCHW. *scale turns model pixels back
// into frame pixels.
cv::Mat YoloBlob(const cv::Mat& bgr, int size, double* scale);

// Decodes a YOLOv8 head of `rows` = 4 + classes rows by `anchors` columns
// (cx, cy, w, h in model pixels, then one score per class): the boxes whose
// best class reaches min_score, one per object (class-agnostic NMS at
// nms_iou), in frame pixels and clipped to the frame, best first.
std::vector<Detection> DecodeYoloV8(const float* data, int rows, int anchors, double scale, cv::Size frame,
                                    float min_score = 0.25f, float nms_iou = 0.45f);

class OnnxDetector {
public:
    OnnxDetector();
    ~OnnxDetector();
    OnnxDetector(const OnnxDetector&) = delete;
    OnnxDetector& operator=(const OnnxDetector&) = delete;

    // Whether this build carries ONNX Runtime at all.
    static bool Available();
    // The ONNX Runtime version, empty without it.
    static std::string RuntimeVersion();

    // Loads a model with one picture input and one YOLOv8 output. False with
    // *error set for a missing or unreadable file or another kind of model.
    bool Load(const std::filesystem::path& model, std::string* error);
    bool loaded() const;
    int inputSize() const;   // the square side the model sees
    int classCount() const;  // the output's rows minus the four box rows

    // The boxes in a BGR frame. Empty with *error set when it fails.
    std::vector<Detection> Detect(const cv::Mat& bgr, float min_score = 0.25f, std::string* error = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// NudeNet 3.4's 18 classes, in the order of its output rows.
const std::vector<std::string>& NudeNetClasses();
// The label class a NudeNet class stands for in the label editor ("" when
// there is none: covered parts, feet, armpits, belly).
std::string NudeNetToLabel(const std::string& nudenet_class);

}  // namespace samn::vision
