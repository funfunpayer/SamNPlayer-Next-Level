#include "samn/label_track.h"

#include <algorithm>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "samn/fileio.h"

namespace samn {

namespace {
constexpr const char* kKind = "samnplayer.labels";
constexpr int kVersion = 1;

LabelBox Lerp(const LabelBox& a, const LabelBox& b, double f) {
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.w + (b.w - a.w) * f, a.h + (b.h - a.h) * f};
}
}  // namespace

std::string LabelClassOf(const std::string& label) {
    const auto sp = label.find_last_of(' ');
    if (sp == std::string::npos || sp + 1 >= label.size()) return label;
    for (size_t i = sp + 1; i < label.size(); ++i)
        if (label[i] < '0' || label[i] > '9') return label;
    return label.substr(0, sp);
}

LabelTrack::LabelTrack(std::string label, std::string cls)
    : label_(std::move(label)), cls_(cls.empty() ? LabelClassOf(label_) : std::move(cls)) {}

void LabelTrack::SetKey(const LabelKey& key, int64_t tolerance_ms) {
    for (auto& k : keys_)
        if (std::llabs(k.ms - key.ms) <= tolerance_ms) {
            k = key;
            return;
        }
    const auto it = std::lower_bound(keys_.begin(), keys_.end(), key.ms,
                                     [](const LabelKey& k, int64_t ms) { return k.ms < ms; });
    keys_.insert(it, key);
}

bool LabelTrack::RemoveKey(int64_t ms, int64_t tolerance_ms) {
    auto best = keys_.end();
    int64_t dist = tolerance_ms + 1;
    for (auto it = keys_.begin(); it != keys_.end(); ++it)
        if (std::llabs(it->ms - ms) < dist) {
            dist = std::llabs(it->ms - ms);
            best = it;
        }
    if (best == keys_.end()) return false;
    keys_.erase(best);
    return true;
}

int LabelTrack::Confirm(int64_t from_ms, int64_t to_ms) {
    if (to_ms < from_ms) std::swap(from_ms, to_ms);
    int n = 0;
    for (auto& k : keys_)
        if (k.ms >= from_ms && k.ms <= to_ms && !k.confirmed) {
            k.confirmed = true;
            ++n;
        }
    return n;
}

const LabelKey* LabelTrack::KeyAt(int64_t ms, int64_t tolerance_ms) const {
    const LabelKey* best = nullptr;
    int64_t dist = tolerance_ms + 1;
    for (const auto& k : keys_)
        if (std::llabs(k.ms - ms) < dist) {
            dist = std::llabs(k.ms - ms);
            best = &k;
        }
    return best;
}

LabelState LabelTrack::At(int64_t ms) const {
    LabelState s;
    if (keys_.empty() || ms < keys_.front().ms) return s;
    // The last key at or before ms.
    const auto next = std::upper_bound(keys_.begin(), keys_.end(), ms,
                                       [](int64_t t, const LabelKey& k) { return t < k.ms; });
    const LabelKey& prev = *(next - 1);
    const bool on_key = prev.ms == ms;
    if (prev.absent) {
        s.present = false;
        s.covered = prev.covered;
        s.reviewed = prev.confirmed && (on_key || next == keys_.end() || next->confirmed);
        return s;
    }
    s.present = true;
    if (next == keys_.end() || next->absent || on_key) {
        // Exactly on a key, or held until an absent key or the end.
        s.box = prev.box;
        s.reviewed = prev.confirmed && (on_key || next == keys_.end() || next->confirmed);
        return s;
    }
    const double f = double(ms - prev.ms) / double(next->ms - prev.ms);
    s.box = Lerp(prev.box, next->box, f);
    s.reviewed = prev.confirmed && next->confirmed;
    return s;
}

LabelTrack* LabelProject::Track(const std::string& label) {
    for (auto& t : tracks)
        if (t.label() == label) return &t;
    return nullptr;
}

LabelTrack& LabelProject::AddTrack(const std::string& label) {
    if (auto* t = Track(label)) return *t;
    tracks.emplace_back(label);
    return tracks.back();
}

LabelTrack& LabelProject::AddObject(const std::string& cls) {
    if (!Track(cls)) {
        tracks.emplace_back(cls, cls);
        return tracks.back();
    }
    for (int n = 2;; ++n) {
        const std::string name = cls + " " + std::to_string(n);
        if (!Track(name)) {
            tracks.emplace_back(name, cls);
            return tracks.back();
        }
    }
}

std::vector<std::string> LabelProject::Classes() const {
    std::vector<std::string> out;
    for (const auto& t : tracks)
        if (std::find(out.begin(), out.end(), t.cls()) == out.end()) out.push_back(t.cls());
    return out;
}

int LabelProject::ObjectCount(const std::string& cls) const {
    return int(std::count_if(tracks.begin(), tracks.end(), [&](const LabelTrack& t) { return t.cls() == cls; }));
}

const std::vector<std::string>& PhaseNames() {
    static const std::vector<std::string> kNames{"intro", "empty", "oral", "hand", "breast", "intercourse", "climax",
                                                 "cool-down"};
    return kNames;
}

void LabelProject::SetPhase(int64_t start_ms, int64_t end_ms, const std::string& name, bool confirmed,
                            const std::string& source) {
    if (end_ms <= start_ms) return;
    std::vector<LabelPhase> out;
    for (const auto& p : phases) {
        if (p.end_ms <= start_ms || p.start_ms >= end_ms) {
            out.push_back(p);
            continue;
        }
        if (p.start_ms < start_ms) {
            LabelPhase left = p;
            left.end_ms = start_ms;
            out.push_back(left);
        }
        if (p.end_ms > end_ms) {
            LabelPhase right = p;
            right.start_ms = end_ms;
            out.push_back(right);
        }
    }
    if (!name.empty()) out.push_back({start_ms, end_ms, name, confirmed, source});
    std::sort(out.begin(), out.end(), [](const LabelPhase& a, const LabelPhase& b) { return a.start_ms < b.start_ms; });
    phases.clear();
    for (const auto& p : out) {
        if (!phases.empty() && phases.back().end_ms == p.start_ms && phases.back().name == p.name &&
            phases.back().confirmed == p.confirmed)
            phases.back().end_ms = p.end_ms;
        else
            phases.push_back(p);
    }
}

const LabelPhase* LabelProject::PhaseAt(int64_t ms) const {
    for (const auto& p : phases)
        if (ms >= p.start_ms && ms < p.end_ms) return &p;
    return nullptr;
}

double BoxIoU(const LabelBox& a, const LabelBox& b) {
    const double x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    const double inter = std::max(0.0, x1 - x0) * std::max(0.0, y1 - y0);
    const double uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0 ? inter / uni : 0.0;
}

std::vector<LabelKey> BlendTracks(const std::vector<TrackSample>& forward, const std::vector<TrackSample>& backward,
                                  int64_t from_ms, int64_t to_ms, double uncertain_iou) {
    std::vector<LabelKey> out;
    const double span = double(std::max<int64_t>(1, to_ms - from_ms));
    for (const auto& f : forward) {
        if (f.ms <= from_ms || f.ms >= to_ms) continue;
        // The backward sample closest in time.
        const TrackSample* b = nullptr;
        for (const auto& s : backward)
            if (!b || std::llabs(s.ms - f.ms) < std::llabs(b->ms - f.ms)) b = &s;
        const std::optional<LabelBox> fb = f.box;
        const std::optional<LabelBox> bb = b ? b->box : std::nullopt;
        LabelKey k;
        k.ms = f.ms;
        k.confirmed = false;
        k.source = "tracker";
        if (!fb && !bb) {
            k.absent = true;
            k.uncertain = true;
        } else if (!fb || !bb) {
            k.box = fb ? *fb : *bb;
            k.uncertain = true;
        } else {
            const double w = std::clamp(double(f.ms - from_ms) / span, 0.0, 1.0);  // weight of the backward run
            k.box = {fb->x + (bb->x - fb->x) * w, fb->y + (bb->y - fb->y) * w, fb->w + (bb->w - fb->w) * w,
                     fb->h + (bb->h - fb->h) * w};
            k.uncertain = BoxIoU(*fb, *bb) < uncertain_iou;
        }
        out.push_back(std::move(k));
    }
    return out;
}

int FillBetweenKeys(LabelTrack& t, int64_t from_ms, int64_t to_ms, const std::vector<LabelKey>& keys) {
    std::vector<int64_t> drop;
    for (const auto& k : t.keys())
        if (k.ms > from_ms && k.ms < to_ms && !k.confirmed) drop.push_back(k.ms);
    for (const int64_t ms : drop) t.RemoveKey(ms);
    int added = 0;
    for (const auto& k : keys) {
        if (k.ms <= from_ms || k.ms >= to_ms) continue;
        if (const LabelKey* there = t.KeyAt(k.ms); there && there->confirmed) continue;
        t.SetKey(k);
        ++added;
    }
    return added;
}

std::optional<int64_t> NextUncertain(const LabelProject& p, int64_t after_ms, const std::string& label) {
    std::optional<int64_t> best;
    for (const auto& t : p.tracks) {
        if (!label.empty() && t.label() != label) continue;
        for (const auto& k : t.keys())
            if (k.uncertain && !k.confirmed && k.ms > after_ms && (!best || k.ms < *best)) best = k.ms;
    }
    return best;
}

bool IsExcludeClass(const std::string& cls) { return cls == kExcludeClass; }

std::vector<LabelBox> ExcludeZonesAt(const LabelProject& p, int64_t ms) {
    std::vector<LabelBox> out;
    for (const auto& t : p.tracks) {
        if (!IsExcludeClass(t.cls())) continue;
        const LabelState s = t.At(ms);
        if (s.present) out.push_back(s.box);
    }
    return out;
}

std::filesystem::path LabelProjectPath(const std::filesystem::path& clip) {
    std::filesystem::path p = clip;
    p.replace_extension(".labels.json");
    return p;
}

std::string SerializeLabelProject(const LabelProject& p) {
    nlohmann::json j;
    j["kind"] = kKind;
    j["version"] = kVersion;
    j["video"] = p.video;
    j["durationMs"] = p.duration_ms;
    j["tracks"] = nlohmann::json::array();
    for (const auto& t : p.tracks) {
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& k : t.keys()) {
            nlohmann::json o{{"ms", k.ms}, {"confirmed", k.confirmed}, {"source", k.source}};
            if (k.absent) o["absent"] = true;
            if (k.absent && k.covered) o["covered"] = true;
            else o["box"] = {k.box.x, k.box.y, k.box.w, k.box.h};
            if (k.uncertain) o["uncertain"] = true;
            keys.push_back(std::move(o));
        }
        j["tracks"].push_back({{"label", t.label()}, {"class", t.cls()}, {"keys", std::move(keys)}});
    }
    if (!p.phases.empty()) {
        j["phases"] = nlohmann::json::array();
        for (const auto& ph : p.phases)
            j["phases"].push_back({{"startMs", ph.start_ms},
                                   {"endMs", ph.end_ms},
                                   {"phase", ph.name},
                                   {"confirmed", ph.confirmed},
                                   {"source", ph.source}});
    }
    return j.dump(2);
}

LabelProject ParseLabelProject(const std::string& json) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("labels: not JSON: ") + e.what());
    }
    if (!j.is_object() || j.value("kind", "") != kKind) throw std::runtime_error("labels: not a SamNPlayer labels file");
    LabelProject p;
    p.video = j.value("video", "");
    p.duration_ms = j.value("durationMs", int64_t(0));
    for (const auto& t : j.value("tracks", nlohmann::json::array())) {
        if (!t.is_object()) continue;
        const std::string label = t.value("label", "");
        LabelTrack* existing = p.Track(label);
        if (!existing) {
            p.tracks.emplace_back(label, t.value("class", ""));
            existing = &p.tracks.back();
        }
        LabelTrack& track = *existing;
        for (const auto& o : t.value("keys", nlohmann::json::array())) {
            if (!o.is_object() || !o.contains("ms")) continue;
            LabelKey k;
            k.ms = o.value("ms", int64_t(0));
            k.confirmed = o.value("confirmed", false);
            k.source = o.value("source", "user");
            k.absent = o.value("absent", false);
            k.covered = k.absent && o.value("covered", false);
            k.uncertain = o.value("uncertain", false);
            if (!k.absent) {
                const auto b = o.value("box", nlohmann::json::array());
                if (!b.is_array() || b.size() != 4) continue;
                k.box = {b[0].get<double>(), b[1].get<double>(), b[2].get<double>(), b[3].get<double>()};
            }
            track.SetKey(k);
        }
    }
    for (const auto& o : j.value("phases", nlohmann::json::array())) {
        if (!o.is_object()) continue;
        p.SetPhase(o.value("startMs", int64_t(0)), o.value("endMs", int64_t(0)), o.value("phase", ""),
                   o.value("confirmed", true), o.value("source", "user"));
    }
    return p;
}

void SaveLabelProject(const std::filesystem::path& path, const LabelProject& p) {
    WriteFileAtomic(path, SerializeLabelProject(p));
}

LabelProject LoadLabelProject(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("labels: cannot read " + path.string());
    std::ostringstream data;
    data << in.rdbuf();
    return ParseLabelProject(data.str());
}

std::vector<LabelSample> ExportSamples(const LabelProject& p, const std::vector<std::string>& classes,
                                       int64_t every_ms) {
    std::vector<LabelSample> out;
    if (every_ms <= 0 || p.duration_ms < 0) return out;  // duration 0: a still image, one sample
    for (int64_t ms = 0; ms <= p.duration_ms; ms += every_ms) {
        LabelSample s;
        s.ms = ms;
        bool reviewed = false, covered = false;
        for (const auto& t : p.tracks) {
            const auto it = std::find(classes.begin(), classes.end(), t.cls());
            if (it == classes.end()) continue;
            const LabelState st = t.At(ms);
            if (st.covered) covered = true;
            if (!st.reviewed) continue;
            reviewed = true;
            if (st.present && st.box.w > 0 && st.box.h > 0)
                s.items.push_back({int(it - classes.begin()), st.box});
        }
        if (reviewed && !covered) out.push_back(std::move(s));
    }
    return out;
}

std::string YoloLines(const LabelSample& s, double frame_w, double frame_h) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(6);
    for (const auto& it : s.items) {
        const double x0 = std::clamp(it.box.x, 0.0, frame_w), y0 = std::clamp(it.box.y, 0.0, frame_h);
        const double x1 = std::clamp(it.box.x + it.box.w, 0.0, frame_w), y1 = std::clamp(it.box.y + it.box.h, 0.0, frame_h);
        if (x1 <= x0 || y1 <= y0) continue;
        o << it.class_id << ' ' << (x0 + x1) / 2 / frame_w << ' ' << (y0 + y1) / 2 / frame_h << ' ' << (x1 - x0) / frame_w
          << ' ' << (y1 - y0) / frame_h << '\n';
    }
    return o.str();
}

}  // namespace samn
