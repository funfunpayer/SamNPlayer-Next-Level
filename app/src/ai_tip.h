// Shared student / NudeNet lookup for Generator AI tip proposals and for
// hybrid re-acquire (same documented paths). A hit is a proposal only —
// the tip box is not written until the user Applies (D-057).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "samn/vision/onnx_detector.h"

namespace samn::ui {

struct AiTipModel {
    std::filesystem::path path;
    bool nudenet = false;               // true: NudeNet class map; false: student names
    std::vector<std::string> names;     // student classes.json; unused for NudeNet
};

struct AiTipHit {
    int x = 0, y = 0, w = 0, h = 0;
    float score = 0;
    int cls = -1;
    std::string label;
};

enum class AiTipReady {
    kReady,
    kNoOnnx,   // this build has no ONNX Runtime
    kNoModel,  // neither student.onnx nor nudenet.onnx is on disk
};

// Documented paths (hybrid re-acquire / AI Train):
//   config_dir()/models/student/student.onnx  (preferred)
//   config_dir()/models/nudenet.onnx
AiTipReady AiTipModelStatus();
std::optional<AiTipModel> FindAiTipModel();

bool LabelsMatchTip(std::string_view label, std::string_view want);
std::string LabelForDetection(const samn::vision::Detection& d, const AiTipModel& model);
std::vector<AiTipHit> ListAiHits(const std::vector<samn::vision::Detection>& dets, const AiTipModel& model);
// First hit whose label matches the expected tip class (glans ↔ penis).
// No fallback to another class.
std::optional<AiTipHit> PickExpectedTip(const std::vector<AiTipHit>& hits, std::string_view want);
// Safe contact-class mapping for Shift-click (breast/breasts → breasts,
// nipple/nipples → nipples, mouth → mouth). face / penis / glans / others
// return nullopt — those stay a pending contact candidate until Apply.
std::optional<std::string> ContactClassForLabel(std::string_view label);

}  // namespace samn::ui
