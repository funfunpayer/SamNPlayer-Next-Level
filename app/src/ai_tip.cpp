#include "ai_tip.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#include "samn/settings.h"

namespace samn::ui {
namespace {

std::vector<std::string> LoadJsonClassNames(const std::filesystem::path& path) {
    QFile f(QString::fromStdU16String(path.u16string()));
    if (!f.open(QIODevice::ReadOnly)) return {};
    std::vector<std::string> names;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value("classes").toArray())
        names.push_back(v.toString().toStdString());
    return names;
}

std::vector<std::string> LoadStudentNames() {
    return LoadJsonClassNames(samn::config_dir() / "models" / "student" / "classes.json");
}

std::vector<std::string> LoadFunGenNames() {
    // Optional override beside the weights (custom FunGen export order).
    auto names = LoadJsonClassNames(samn::config_dir() / "models" / "fungen" / "classes.json");
    if (!names.empty()) return names;
    names = LoadJsonClassNames(samn::config_dir() / "models" / "fungen.classes.json");
    if (!names.empty()) return names;
    return FunGenClassNames();
}

std::filesystem::path FunGenOnnxPath() {
    try {
        const samn::Settings s;
        const auto v = s.get("generator.ai_fungen_path");
        if (v.is_string()) {
            const auto p = std::filesystem::path(v.get<std::string>());
            std::error_code ec;
            if (!p.empty() && std::filesystem::exists(p, ec)) return p;
        }
    } catch (const std::exception&) {
    }
    return samn::config_dir() / "models" / "fungen.onnx";
}

}  // namespace

const std::vector<std::string>& FunGenClassNames() {
    // ack00gar/FunGen config/constants.py CLASS_NAMES_TO_IDS
    static const std::vector<std::string> k{
        "face", "hand", "penis", "glans", "pussy", "butt", "anus", "breast", "navel", "foot", "hips center",
    };
    return k;
}

std::string FunGenToLabel(std::string_view c) {
    if (c == "penis") return "penis";
    if (c == "glans") return "glans";
    if (c == "breast") return "breast";
    if (c == "face") return "mouth";  // FunGen oral / BJ interaction uses face
    if (c == "hand") return "hand";
    if (c == "pussy") return "pussy";
    if (c == "butt") return "butt";
    if (c == "foot") return "foot";
    return {};
}

AiTipReady AiTipModelStatus() {
    if (!samn::vision::OnnxDetector::Available()) return AiTipReady::kNoOnnx;
    if (FindAiTipModel()) return AiTipReady::kReady;
    return AiTipReady::kNoModel;
}

std::optional<AiTipModel> FindAiTipModel() {
    if (!samn::vision::OnnxDetector::Available()) return std::nullopt;
    const auto student = samn::config_dir() / "models" / "student" / "student.onnx";
    const auto fungen = FunGenOnnxPath();
    const auto nudenet = samn::config_dir() / "models" / "nudenet.onnx";
    std::error_code ec;
    if (std::filesystem::exists(student, ec))
        return AiTipModel{student, AiTipKind::kStudent, false, LoadStudentNames()};
    if (std::filesystem::exists(fungen, ec))
        return AiTipModel{fungen, AiTipKind::kFunGen, false, LoadFunGenNames()};
    if (std::filesystem::exists(nudenet, ec)) return AiTipModel{nudenet, AiTipKind::kNudeNet, true, {}};
    return std::nullopt;
}

bool LabelsMatchTip(std::string_view label, std::string_view want) {
    if (label == want) return true;
    return (want == "glans" && label == "penis") || (want == "penis" && label == "glans");
}

std::string LabelForDetection(const samn::vision::Detection& d, const AiTipModel& model) {
    if (d.cls < 0) return {};
    if (model.kind == AiTipKind::kNudeNet || model.nudenet) {
        const auto& nn = samn::vision::NudeNetClasses();
        if (size_t(d.cls) >= nn.size()) return {};
        return samn::vision::NudeNetToLabel(nn[size_t(d.cls)]);
    }
    if (size_t(d.cls) >= model.names.size()) return {};
    const std::string& raw = model.names[size_t(d.cls)];
    if (model.kind == AiTipKind::kFunGen) return FunGenToLabel(raw);
    return raw;
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
