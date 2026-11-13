// Shared student / YOLO / NudeNet lookup for Generator AI tip proposals and
// for hybrid re-acquire (same documented paths). A hit is a proposal only —
// the tip box is not written until the user Applies (D-057).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "samn/vision/onnx_detector.h"

namespace samn::ui {

enum class AiTipKind {
    kStudent,  // RF-DETR student.onnx + classes.json
    kYolo,     // user YOLO .onnx (e.g. FunGen Discord weights) + optional classes.json
    kNudeNet,  // NudeNet teacher export
};

struct AiTipModel {
    std::filesystem::path path;
    AiTipKind kind = AiTipKind::kStudent;
    bool nudenet = false;               // kind == kNudeNet (kept for call sites)
    std::vector<std::string> names;     // student / YOLO class names; unused for NudeNet
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
    kNoModel,  // no student / YOLO / NudeNet model on disk
};

// Documented paths (priority: student → YOLO → NudeNet):
//   config_dir()/models/student/student.onnx
//   config_dir()/models/yolo.onnx  (+ optional models/yolo/classes.json)
//   config_dir()/models/nudenet.onnx
// Optional override: settings key generator.ai_yolo_path (absolute file).
AiTipReady AiTipModelStatus();
std::optional<AiTipModel> FindAiTipModel();

// Default YOLO body-part class order (FunGen-compatible CLASS_NAMES_TO_IDS).
const std::vector<std::string>& YoloBodyClassNames();
// Map a YOLO body class name onto our tip / contact labels. Empty when unused
// (anus, navel, hips center, …). face → mouth for oral contact.
std::string MapYoloBodyLabel(std::string_view yolo_class);

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
