#include "samn/engine/compare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "samn/engine/track_box.h"
#include "samn/funscript.h"
#include "samn/sha256.h"
#include "samn/vision/tracker.h"

namespace samn::engine {

namespace {

bool Pinned(const LagCorrelation& c, const CompareSettings& s) {
    return std::abs(c.lag_ms) >= s.max_lag_ms - s.lag_step_ms;
}

std::optional<std::vector<Action>> LoadActions(const std::filesystem::path& p, std::string* error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        *error = "cannot read " + p.filename().u8string();
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        return Parse(ss.str()).actions;
    } catch (const std::exception& e) {
        *error = p.filename().u8string() + ": " + e.what();
        return std::nullopt;
    }
}

std::string Fixed(const std::optional<double>& v, int digits = 3) {
    if (!v) return "–";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", digits, *v);
    return buf;
}

nlohmann::json CorrJson(const LagCorrelation& c) {
    nlohmann::json j{{"r", c.r},
                     {"lag_ms", c.lag_ms},
                     {"orientation", c.orientation},
                     {"n_samples", c.n_samples},
                     {"low_confidence", c.low_confidence},
                     {"aliasing_risk", c.aliasing_risk}};
    if (c.r_zero_lag) j["r_zero_lag"] = *c.r_zero_lag;
    if (c.shape_error) j["shape_error"] = *c.shape_error;
    if (c.dominant_period_ms) j["dominant_period_ms"] = c.dominant_period_ms;
    if (!c.alternate_lags_ms.empty()) j["alternate_lags_ms"] = c.alternate_lags_ms;
    return j;
}

nlohmann::json PairJson(const PairScore& p) {
    nlohmann::json j;
    j["whole"] = p.whole ? CorrJson(*p.whole) : nlohmann::json(nullptr);
    j["boundary_pinned"] = p.boundary_pinned;
    j["windowed_r_median"] = p.windowed_r_median ? nlohmann::json(*p.windowed_r_median) : nlohmann::json(nullptr);
    j["windows_scored"] = p.windows_scored;
    j["windows_total"] = p.windows.size();
    j["windows_low_confidence"] = p.windows_low_confidence;
    j["windows_aliasing"] = p.windows_aliasing;
    j["windows_pinned"] = p.windows_pinned;
    auto& rows = j["windows"] = nlohmann::json::array();
    for (const WindowResult& w : p.windows) {
        nlohmann::json row{{"start_ms", w.start_ms}, {"end_ms", w.end_ms}};
        if (w.correlation)
            row["correlation"] = CorrJson(*w.correlation);
        else
            row["reason"] = w.reason;
        rows.push_back(row);
    }
    return j;
}

}  // namespace

PairScore ScorePair(const std::vector<Action>& ref, const std::vector<Action>& cand, const CompareSettings& s) {
    PairScore p;
    p.whole = BestLagCorrelation(ref, cand, s.max_lag_ms, s.lag_step_ms, s.resample_step_ms);
    p.boundary_pinned = p.whole && Pinned(*p.whole, s);
    p.windows = WindowedBestLagCorrelation(ref, cand, s.window_ms, s.max_lag_ms, s.lag_step_ms, s.resample_step_ms);
    std::vector<double> rs;
    for (const WindowResult& w : p.windows) {
        if (!w.correlation) continue;
        ++p.windows_scored;
        rs.push_back(w.correlation->r);
        p.windows_low_confidence += w.correlation->low_confidence;
        p.windows_aliasing += w.correlation->aliasing_risk;
        p.windows_pinned += Pinned(*w.correlation, s);
    }
    if (!rs.empty()) {
        std::sort(rs.begin(), rs.end());
        const size_t m = rs.size() / 2;
        p.windowed_r_median = rs.size() % 2 ? rs[m] : (rs[m - 1] + rs[m]) / 2;
    }
    return p;
}

ClipReport RunClip(const CompareClip& clip, const CompareSettings& s, const std::function<bool()>& cancel) {
    ClipReport r;
    r.clip = clip;
    r.video_sha256 = Sha256File(clip.video).value_or("");
    std::optional<std::vector<Action>> ref, old;
    if (clip.reference) {
        r.reference_sha256 = Sha256File(*clip.reference).value_or("");
        if (!(ref = LoadActions(*clip.reference, &r.error))) return r;
    }
    if (clip.old_output) {
        r.old_sha256 = Sha256File(*clip.old_output).value_or("");
        if (!(old = LoadActions(*clip.old_output, &r.error))) return r;
    }

    TrackOptions to;
    to.cancel = cancel;
    to.start_ms = clip.start_ms;
    to.end_ms = clip.end_ms;
    to.max_analysis_fps = clip.max_analysis_fps;
    auto tr = TrackBox(clip.video, clip.box, to, &r.error);
    if (!tr) return r;
    if (tr->canceled) {
        r.error = "canceled";
        return r;
    }
    r.frames = tr->stats.total_frames;
    r.lost_frames = tr->stats.lost_frames;
    std::string note;
    const SignalOptions so = ApplyDefaultDetrend(clip.signal, std::nullopt, &note);
    auto sig = RunSignal(std::vector<double>(tr->times_ms.begin(), tr->times_ms.end()), tr->positions, tr->lost, so,
                         &r.error);
    if (!sig) return r;
    r.new_actions = sig->actions;
    r.error.clear();

    if (ref) r.new_vs_reference = ScorePair(*ref, r.new_actions, s);
    if (ref && old) r.old_vs_reference = ScorePair(*ref, *old, s);
    if (old) r.new_vs_old = ScorePair(*old, r.new_actions, s);
    if (r.new_vs_reference && r.old_vs_reference && r.new_vs_reference->whole && r.old_vs_reference->whole)
        r.n_mismatch = r.new_vs_reference->whole->n_samples != r.old_vs_reference->whole->n_samples;
    return r;
}

std::optional<std::vector<CompareClip>> LoadManifest(const std::filesystem::path& path, CompareSettings* settings,
                                                     std::string* error) {
    auto fail = [&](const std::string& why) {
        *error = why;
        return std::optional<std::vector<CompareClip>>();
    };
    std::ifstream in(path);
    if (!in) return fail("cannot read the manifest");
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("clips") || !j["clips"].is_array())
        return fail("the manifest needs a \"clips\" list");
    if (settings) {
        settings->window_ms = j.value("window_ms", settings->window_ms);
        settings->max_lag_ms = j.value("max_lag_ms", settings->max_lag_ms);
    }
    const std::filesystem::path base = path.parent_path();
    auto resolve = [&](const std::string& p) {
        const std::filesystem::path fp = std::filesystem::u8path(p);
        return fp.is_absolute() ? fp : base / fp;
    };
    std::vector<CompareClip> clips;
    for (const auto& c : j["clips"]) {
        if (!c.is_object() || !c.contains("video") || !c["video"].is_string() || !c.contains("box") ||
            !c["box"].is_array() || c["box"].size() != 4)
            return fail("every clip needs \"video\" and \"box\": [x, y, w, h]");
        CompareClip clip;
        clip.video = resolve(c["video"].get<std::string>());
        clip.name = c.value("name", clip.video.stem().u8string());
        const auto& b = c["box"];
        for (const auto& v : b)
            if (!v.is_number_integer()) return fail(clip.name + ": the box needs whole numbers");
        clip.box = cv::Rect(b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>());
        if (clip.box.width <= 0 || clip.box.height <= 0) return fail(clip.name + ": the box is empty");
        clip.signal.profile = c.value("profile", std::string("standard"));
        for (const char* key : {"start_ms", "end_ms", "max_analysis_fps", "max_gap_ms", "outlier_mad_factor"})
            if (c.contains(key) && (!c[key].is_number() || c[key].get<double>() < 0))
                return fail(clip.name + ": \"" + key + "\" must be a number of at least 0");
        clip.start_ms = c.value("start_ms", int64_t{0});
        clip.end_ms = c.value("end_ms", int64_t{0});
        clip.max_analysis_fps = c.value("max_analysis_fps", 0.0);
        clip.signal.max_gap_ms = c.value("max_gap_ms", 0.0);
        clip.signal.outlier_mad_factor = c.value("outlier_mad_factor", 0.0);
        if (c.contains("reference") && c["reference"].is_string()) clip.reference = resolve(c["reference"]);
        if (c.contains("old") && c["old"].is_string()) clip.old_output = resolve(c["old"]);
        clips.push_back(clip);
    }
    return clips;
}

nlohmann::json ReportJson(const std::vector<ClipReport>& reports, const CompareSettings& s) {
    nlohmann::json j;
    j["kind"] = "motion_fidelity_comparison";
    j["engine"] = "samn_engine";
    j["opencv"] = vision::OpenCvVersion();
    j["settings"] = {{"max_lag_ms", s.max_lag_ms},
                     {"lag_step_ms", s.lag_step_ms},
                     {"resample_step_ms", s.resample_step_ms},
                     {"window_ms", s.window_ms}};
    auto& clips = j["clips"] = nlohmann::json::array();
    for (const ClipReport& r : reports) {
        nlohmann::json c{{"name", r.clip.name},
                         {"box", {r.clip.box.x, r.clip.box.y, r.clip.box.width, r.clip.box.height}},
                         {"profile", r.clip.signal.profile},
                         {"video_sha256", r.video_sha256}};
        if (!r.reference_sha256.empty()) c["reference_sha256"] = r.reference_sha256;
        if (!r.old_sha256.empty()) c["old_sha256"] = r.old_sha256;
        if (!r.error.empty()) {
            c["error"] = r.error;
        } else {
            c["frames"] = r.frames;
            c["lost_frames"] = r.lost_frames;
            c["new_actions"] = r.new_actions.size();
            c["n_mismatch"] = r.n_mismatch;
            if (r.new_vs_reference) c["new_vs_reference"] = PairJson(*r.new_vs_reference);
            if (r.old_vs_reference) c["old_vs_reference"] = PairJson(*r.old_vs_reference);
            if (r.new_vs_old) c["new_vs_old"] = PairJson(*r.new_vs_old);
        }
        clips.push_back(c);
    }
    return j;
}

std::string ReportMarkdown(const std::vector<ClipReport>& reports, const CompareSettings& s) {
    std::ostringstream md;
    md << "# Comparison: SamNPlayer 2 engine vs 0.7\n\n";
    md << "Motion Fidelity against the reference (not the Quality Doctor; the two do not predict each other, GD-04). "
       << "Lag window +-" << s.max_lag_ms << " ms, windows of " << s.window_ms / 1000 << " s, " << vision::OpenCvVersion()
       << ".\n\n";
    md << "| clip | profile | pair | r | lag ms | orient. | n | windowed r (median) | windows | flags |\n";
    md << "|---|---|---|---|---|---|---|---|---|---|\n";
    auto row = [&](const ClipReport& r, const char* pair, const std::optional<PairScore>& p) {
        if (!p) return;
        std::string flags;
        auto add = [&](bool on, const std::string& f) {
            if (on) flags += (flags.empty() ? "" : ", ") + f;
        };
        if (p->whole) {
            add(p->whole->low_confidence, "low confidence");
            add(p->whole->aliasing_risk, "aliasing");
        }
        add(p->boundary_pinned, "lag pinned at the edge");
        add(p->windows_aliasing > 0, std::to_string(p->windows_aliasing) + " windows aliasing");
        add(p->windows_pinned > 0, std::to_string(p->windows_pinned) + " windows pinned");
        add(r.n_mismatch && std::string(pair) != "new vs 0.7", "n differs between new and 0.7");
        md << "| " << r.clip.name << " | " << r.clip.signal.profile << " | " << pair << " | "
           << (p->whole ? Fixed(p->whole->r) : "undefined") << " | "
           << (p->whole ? std::to_string(p->whole->lag_ms) : "–") << " | "
           << (p->whole ? p->whole->orientation : "–") << " | " << (p->whole ? std::to_string(p->whole->n_samples) : "–")
           << " | " << Fixed(p->windowed_r_median) << " | " << p->windows_scored << "/" << p->windows.size() << " | "
           << (flags.empty() ? "–" : flags) << " |\n";
    };
    for (const ClipReport& r : reports) {
        if (!r.error.empty()) {
            md << "| " << r.clip.name << " | " << r.clip.signal.profile << " | – | error: " << r.error
               << " | | | | | | |\n";
            continue;
        }
        row(r, "new vs reference", r.new_vs_reference);
        row(r, "0.7 vs reference", r.old_vs_reference);
        row(r, "new vs 0.7", r.new_vs_old);
    }
    md << "\nManifest (content hashes):\n\n";
    for (const ClipReport& r : reports) {
        md << "- " << r.clip.name << ": video `" << r.video_sha256.substr(0, 16) << "`";
        if (!r.reference_sha256.empty()) md << ", reference `" << r.reference_sha256.substr(0, 16) << "`";
        if (!r.old_sha256.empty()) md << ", 0.7 `" << r.old_sha256.substr(0, 16) << "`";
        md << ", box " << r.clip.box.x << "," << r.clip.box.y << "," << r.clip.box.width << "," << r.clip.box.height
           << ", frames " << r.frames << ", lost " << r.lost_frames << "\n";
    }
    return md.str();
}

}  // namespace samn::engine
