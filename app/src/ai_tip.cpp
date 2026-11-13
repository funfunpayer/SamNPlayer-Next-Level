#include "ai_tip.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#include "samn/settings.h"

namespace samn::ui {
namespace {

std::vector<std::string> LoadStudentNames() {
    QFile f(QString::fromStdU16String((samn::config_dir() / "models" / "student" / "classes.json").u16string()));
    if (!f.open(QIODevice::ReadOnly)) return {};
    std::vector<std::string> names;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value("classes").toArray())
        names.push_back(v.toString().toStdString());
    return names;
}

}  // namespace

AiTipReady AiTipModelStatus() {
    if (!samn::vision::OnnxDetector::Available()) return AiTipReady::kNoOnnx;
    if (FindAiTipModel()) return AiTipReady::kReady;
    return AiTipReady::kNoModel;
}

std::optional<AiTipModel> FindAiTipModel() {
    if (!samn::vision::OnnxDetector::Available()) return std::nullopt;
    const auto student = samn::config_dir() / "models" / "student" / "student.onnx";
    const auto nudenet = samn::config_dir() / "models" / "nudenet.onnx";
    std::error_code ec;
    if (std::filesystem::exists(student, ec)) return AiTipModel{student, false, LoadStudentNames()};
    if (std::filesystem::exists(nudenet, ec)) return AiTipModel{nudenet, true, {}};
    return std::nullopt;
}

bool LabelsMatchTip(std::string_view label, std::string_view want) {
    if (label == want) return true;
    return (want == "glans" && label == "penis") || (want == "penis" && label == "glans");
}

std::string LabelForDetection(const samn::vision::Detection& d, const AiTipModel& model) {
    if (d.cls < 0) return {};
    if (model.nudenet) {
        const auto& nn = samn::vision::NudeNetClasses();
        if (size_t(d.cls) >= nn.size()) return {};
        return samn::vision::NudeNetToLabel(nn[size_t(d.cls)]);
    }
    if (size_t(d.cls) >= model.names.size()) return {};
    return model.names[size_t(d.cls)];
}

std::vector<AiTipHit> ListAiHits(const std::vector<samn::vision::Detection>& dets, const AiTipModel& model) {
    std::vector<AiTipHit> out;
    out.reserve(dets.size());
    for (const auto& d : dets) {
        const std::string label = LabelForDetection(d, model);
        if (label.empty()) continue;
        const int w = int(std::lround(d.box.width));
        const int h = int(std::lround(d.box.height));
        if (w < 8 || h < 8) continue;
        out.push_back({int(std::lround(d.box.x)), int(std::lround(d.box.y)), w, h, d.score, d.cls, label});
    }
    return out;
}

std::optional<AiTipHit> PickExpectedTip(const std::vector<AiTipHit>& hits, std::string_view want) {
    for (const auto& h : hits) {
        if (LabelsMatchTip(h.label, want)) return h;
    }
    return std::nullopt;
}

std::optional<std::string> ContactClassForLabel(std::string_view label) {
    if (label == "breasts" || label == "breast") return std::string("breasts");
    if (label == "nipples" || label == "nipple") return std::string("nipples");
    if (label == "mouth") return std::string("mouth");
    return std::nullopt;
}

}  // namespace samn::ui
