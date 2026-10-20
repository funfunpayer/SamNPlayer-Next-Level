#include "samn/vision/onnx_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#ifdef SAMN_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace samn::vision {

cv::Mat YoloBlob(const cv::Mat& bgr, int size, double* scale) {
    const int side = std::max(bgr.cols, bgr.rows);
    cv::Mat square;
    cv::copyMakeBorder(bgr, square, 0, side - bgr.rows, 0, side - bgr.cols, cv::BORDER_CONSTANT, cv::Scalar());
    if (scale) *scale = double(side) / double(size);
    return cv::dnn::blobFromImage(square, 1.0 / 255.0, cv::Size(size, size), cv::Scalar(), true, false, CV_32F);
}

cv::Mat DetrBlob(const cv::Mat& bgr, int size) {
    cv::Mat rgb;
    cv::resize(bgr, rgb, cv::Size(size, size), 0, 0, cv::INTER_LINEAR);
    cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);
    cv::subtract(rgb, cv::Scalar(0.485, 0.456, 0.406), rgb);
    cv::divide(rgb, cv::Scalar(0.229, 0.224, 0.225), rgb);
    return cv::dnn::blobFromImage(rgb);  // HWC to NCHW, no further scaling
}

std::vector<Detection> DecodeDetr(const float* boxes, const float* logits, int queries, int classes, cv::Size frame,
                                  float min_score) {
    std::vector<Detection> out;
    if (!boxes || !logits || queries <= 0 || classes <= 0) return out;
    const auto sigmoid = [](float x) { return 1.0f / (1.0f + std::exp(-x)); };
    const cv::Rect2f bounds(0, 0, float(frame.width), float(frame.height));
    for (int q = 0; q < queries; ++q) {
        int best = 0;
        float logit = logits[q * classes];
        for (int c = 1; c < classes; ++c)
            if (logits[q * classes + c] > logit) {
                logit = logits[q * classes + c];
                best = c;
            }
        const float score = sigmoid(logit);
        if (score < min_score) continue;
        const float* b = boxes + q * 4;
        const float w = b[2] * float(frame.width), h = b[3] * float(frame.height);
        const cv::Rect2f r = cv::Rect2f(b[0] * float(frame.width) - w / 2, b[1] * float(frame.height) - h / 2, w, h) & bounds;
        if (r.width < 1 || r.height < 1) continue;
        out.push_back({best, score, r});
    }
    std::sort(out.begin(), out.end(), [](const Detection& a, const Detection& b) { return a.score > b.score; });
    return out;
}

std::vector<Detection> DecodeYoloV8(const float* data, int rows, int anchors, double scale, cv::Size frame,
                                    float min_score, float nms_iou) {
    const int classes = rows - 4;
    if (!data || classes <= 0 || anchors <= 0) return {};
    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;
    std::vector<int> ids;
    for (int a = 0; a < anchors; ++a) {
        int best = 0;
        float score = data[4 * anchors + a];
        for (int c = 1; c < classes; ++c) {
            const float s = data[(4 + c) * anchors + a];
            if (s > score) {
                score = s;
                best = c;
            }
        }
        if (score < min_score) continue;
        const double cx = data[a], cy = data[anchors + a], w = data[2 * anchors + a], h = data[3 * anchors + a];
        if (w <= 0 || h <= 0) continue;
        boxes.emplace_back((cx - w / 2) * scale, (cy - h / 2) * scale, w * scale, h * scale);
        scores.push_back(score);
        ids.push_back(best);
    }
    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, scores, min_score, nms_iou, keep);
    std::sort(keep.begin(), keep.end(), [&](int x, int y) { return scores[size_t(x)] > scores[size_t(y)]; });
    const cv::Rect2d bounds(0, 0, frame.width, frame.height);
    std::vector<Detection> out;
    for (int k : keep) {
        const cv::Rect2d b = boxes[size_t(k)] & bounds;
        if (b.width < 1 || b.height < 1) continue;
        out.push_back({ids[size_t(k)], scores[size_t(k)], cv::Rect2f(b)});
    }
    return out;
}

#ifdef SAMN_HAVE_ONNXRUNTIME

namespace {
Ort::Env& Environment() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "samn");
    return env;
}
}  // namespace

struct OnnxDetector::Impl {
    std::unique_ptr<Ort::Session> session;
    std::string input_name, output_name, labels_name;  // labels_name: DETR only
    int size = 320;
    int rows = 0;      // YOLOv8: 4 + classes
    int classes = 0;   // DETR: logits per query
    bool detr = false;
};

OnnxDetector::OnnxDetector() : impl_(std::make_unique<Impl>()) {}
OnnxDetector::~OnnxDetector() = default;

bool OnnxDetector::Available() { return true; }

std::string OnnxDetector::RuntimeVersion() { return Ort::GetVersionString(); }

bool OnnxDetector::Load(const std::filesystem::path& model, std::string* error) {
    impl_ = std::make_unique<Impl>();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(model, ec)) {
        if (error) *error = "the model file is missing";
        return false;
    }
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, std::min(4, cv::getNumberOfCPUs() / 2)));
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        auto session = std::make_unique<Ort::Session>(Environment(), model.native().c_str(), options);
        if (session->GetInputCount() != 1 || session->GetOutputCount() < 1) {
            if (error) *error = "not a detector: it needs one picture input";
            return false;
        }
        Ort::AllocatorWithDefaultOptions alloc;
        impl_->input_name = session->GetInputNameAllocated(0, alloc).get();
        impl_->output_name = session->GetOutputNameAllocated(0, alloc).get();
        const auto in = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (in.size() != 4 || (in[1] != 3 && in[1] > 0)) {
            if (error) *error = "not a detector: the input is not a colour picture";
            return false;
        }
        // A fixed side wins; a dynamic one takes NudeNet's 320.
        if (in[2] > 0 && in[3] > 0 && in[2] != in[3]) {
            if (error) *error = "the model wants a picture that is not square";
            return false;
        }
        impl_->size = in[2] > 0 ? int(in[2]) : 320;
        // DETR: "dets" (1 x queries x 4) and "labels" (1 x queries x classes).
        if (session->GetOutputCount() >= 2) {
            std::string dets, labels;
            for (size_t i = 0; i < session->GetOutputCount(); ++i) {
                const std::string name = session->GetOutputNameAllocated(i, alloc).get();
                if (name == "dets") dets = name;
                if (name == "labels") {
                    labels = name;
                    const auto shape = session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
                    if (shape.size() == 3 && shape[2] > 0) impl_->classes = int(shape[2]);
                }
            }
            if (dets.empty() || labels.empty()) {
                if (error) *error = "not a detector this app reads: several outputs, but not dets and labels";
                return false;
            }
            impl_->detr = true;
            impl_->output_name = dets;
            impl_->labels_name = labels;
            impl_->size = in[2] > 0 ? int(in[2]) : 560;
            impl_->session = std::move(session);
            return true;
        }
        const auto out = session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (out.size() != 3 || out[1] == 0 || (out[1] > 0 && out[1] < 5)) {
            if (error) *error = "not a YOLOv8 detector: the output has another shape";
            return false;
        }
        impl_->rows = out[1] > 0 ? int(out[1]) : 0;  // known after the first run otherwise
        impl_->session = std::move(session);
        return true;
    } catch (const Ort::Exception& e) {
        impl_ = std::make_unique<Impl>();
        if (error) *error = e.what();
        return false;
    }
}

bool OnnxDetector::loaded() const { return impl_ && impl_->session; }
int OnnxDetector::inputSize() const { return impl_ ? impl_->size : 0; }
int OnnxDetector::classCount() const {
    if (!impl_) return 0;
    if (impl_->detr) return impl_->classes;
    return impl_->rows > 4 ? impl_->rows - 4 : 0;
}
bool OnnxDetector::isDetr() const { return impl_ && impl_->detr; }

std::vector<Detection> OnnxDetector::Detect(const cv::Mat& bgr, float min_score, std::string* error) const {
    if (!loaded()) {
        if (error) *error = "no model loaded";
        return {};
    }
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        if (error) *error = "the frame is empty or not a colour picture";
        return {};
    }
    double scale = 1;
    cv::Mat blob = impl_->detr ? DetrBlob(bgr, impl_->size) : YoloBlob(bgr, impl_->size, &scale);
    const std::array<int64_t, 4> shape{1, 3, impl_->size, impl_->size};
    try {
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, blob.ptr<float>(), blob.total(), shape.data(),
                                                           shape.size());
        const char* in_names[] = {impl_->input_name.c_str()};
        if (impl_->detr) {
            const char* out_names[] = {impl_->output_name.c_str(), impl_->labels_name.c_str()};
            auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 2);
            const auto d = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            const auto l = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
            if (d.size() != 3 || l.size() != 3 || d[0] != 1 || d[2] != 4 || l[1] != d[1] || l[2] < 1) {
                if (error) *error = "the model answered with another shape";
                return {};
            }
            return DecodeDetr(outputs[0].GetTensorData<float>(), outputs[1].GetTensorData<float>(), int(d[1]), int(l[2]),
                              bgr.size(), min_score);
        }
        const char* out_names[] = {impl_->output_name.c_str()};
        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);
        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto dims = info.GetShape();
        if (dims.size() != 3 || dims[0] != 1 || dims[1] < 5 || dims[2] < 1) {
            if (error) *error = "the model answered with another shape";
            return {};
        }
        return DecodeYoloV8(outputs[0].GetTensorData<float>(), int(dims[1]), int(dims[2]), scale, bgr.size(),
                            min_score);
    } catch (const Ort::Exception& e) {
        if (error) *error = e.what();
        return {};
    }
}

#else  // no ONNX Runtime in this build

struct OnnxDetector::Impl {};
OnnxDetector::OnnxDetector() = default;
OnnxDetector::~OnnxDetector() = default;
bool OnnxDetector::Available() { return false; }
std::string OnnxDetector::RuntimeVersion() { return {}; }
bool OnnxDetector::Load(const std::filesystem::path&, std::string* error) {
    if (error) *error = "this build has no ONNX Runtime";
    return false;
}
bool OnnxDetector::loaded() const { return false; }
int OnnxDetector::inputSize() const { return 0; }
int OnnxDetector::classCount() const { return 0; }
bool OnnxDetector::isDetr() const { return false; }
std::vector<Detection> OnnxDetector::Detect(const cv::Mat&, float, std::string* error) const {
    if (error) *error = "this build has no ONNX Runtime";
    return {};
}

#endif

const std::vector<std::string>& NudeNetClasses() {
    static const std::vector<std::string> k{
        "FEMALE_GENITALIA_COVERED", "FACE_FEMALE",          "BUTTOCKS_EXPOSED",      "FEMALE_BREAST_EXPOSED",
        "FEMALE_GENITALIA_EXPOSED", "MALE_BREAST_EXPOSED",  "ANUS_EXPOSED",          "FEET_EXPOSED",
        "BELLY_COVERED",            "FEET_COVERED",         "ARMPITS_COVERED",       "ARMPITS_EXPOSED",
        "FACE_MALE",                "BELLY_EXPOSED",        "MALE_GENITALIA_EXPOSED", "ANUS_COVERED",
        "FEMALE_BREAST_COVERED",    "BUTTOCKS_COVERED"};
    return k;
}

std::string NudeNetToLabel(const std::string& c) {
    if (c == "MALE_GENITALIA_EXPOSED") return "penis";
    if (c == "FEMALE_GENITALIA_EXPOSED") return "vagina";
    if (c == "FEMALE_BREAST_EXPOSED") return "breast";
    if (c == "FACE_FEMALE" || c == "FACE_MALE") return "face";
    return {};
}

}  // namespace samn::vision
