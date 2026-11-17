// The automatic tip box on POV-like scenes (tip_scenes.h): the finder scores
// a stroke in any direction, offers one box around everything that moves
// together, and the tracker follows the mover from it. The numbers behind
// the thresholds: docs/benchmarks/TIP_AUTO_BOX_2026-11-16.md (samn_tip_bench).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "samn/engine/tip_find.h"
#include "samn/engine/tip_resolve.h"
#include "samn/engine/track_box.h"
#include "tip_scenes.h"

namespace fs = std::filesystem;
using namespace samn::engine;
namespace tc = samn::engine::testclips;

namespace {

struct Clips {
    fs::path dir;
    std::map<std::string, tc::TipSceneInfo> made;
    Clips() : dir(tc::TempPath("samn_tip_auto")) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
    }
    ~Clips() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path Path(const std::string& name) const { return dir / (name + ".mp4"); }
    const tc::TipSceneInfo& Get(const std::string& name) {
        auto it = made.find(name);
        if (it != made.end()) return it->second;
        for (const auto& def : tc::TipScenes())
            if (def.name == name) return made[name] = tc::WriteTipScene(Path(name), def.scene);
        FAIL("unknown scene " << name);
        return made[name];
    }
};

Clips& TheClips() {
    static Clips clips;
    return clips;
}

cv::Rect Frame(const tc::TipSceneInfo& s) { return {0, 0, s.w, s.h}; }

// At least half of the box lies on what the mover covers, and its centre does.
bool OnTheMover(const cv::Rect& box, const tc::TipSceneInfo& s) {
    const cv::Rect swept = s.Swept(0, s.frames) & Frame(s);
    const cv::Point c(box.x + box.width / 2, box.y + box.height / 2);
    return box.area() > 0 && swept.contains(c) && (box & swept).area() * 2 >= box.area();
}

double Pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= double(n);
    mb /= double(n);
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0;
}

struct Followed {
    double r = 0, on_mover = 0, p95 = 0;
    int cuts = 0;
};

// The Create settings (15 fps, camera compensation, cuts, appearance memory).
Followed Follow(const std::string& name, const cv::Rect& box, TrackOptions to = {}) {
    const tc::TipSceneInfo& s = TheClips().Get(name);
    to.max_analysis_fps = 15;
    std::string error;
    const auto tr = TrackBox(TheClips().Path(name), box, to, &error);
    REQUIRE_MESSAGE(tr.has_value(), error);
    std::vector<double> truth, err;
    int on = 0;
    const cv::Point2d off(tr->frame_xs[0] - s.centre[0].x, tr->frame_ys[0] - s.centre[0].y);
    for (size_t i = 0; i < tr->times_ms.size(); ++i) {
        const double t = tr->times_ms[i] / 1000.0;
        const cv::Point2d c = s.centre[size_t(std::clamp(int(std::lround(t * s.fps)), 0, s.frames - 1))];
        truth.push_back(s.stroke(t) * (tr->horizontal ? s.dir_x : s.dir_y));
        err.push_back(std::hypot(tr->frame_xs[i] - off.x - c.x, tr->frame_ys[i] - off.y - c.y));
        if (std::abs(tr->frame_xs[i] - c.x) <= s.mover_w / 2.0 && std::abs(tr->frame_ys[i] - c.y) <= s.mover_h / 2.0) ++on;
    }
    Followed f;
    f.r = std::abs(Pearson(tr->positions, truth));
    f.on_mover = double(on) / double(tr->times_ms.size());
    std::sort(err.begin(), err.end());
    f.p95 = err[size_t(0.95 * double(err.size() - 1))];
    f.cuts = int(tr->scene_cuts.size());
    return f;
}

TipFindResult Find(const std::string& name, TipAxis axis = TipAxis::kPrincipal) {
    TheClips().Get(name);
    TipFindOptions o;
    o.max_seconds = 12;
    o.axis = axis;
    std::string error;
    const auto r = FindTip(TheClips().Path(name), o, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    return *r;
}

}  // namespace

TEST_CASE("tip auto: a sideways or diagonal stroke scores like a vertical one") {
    const double y = Find("pov_y").candidates.front().score;
    const double x = Find("pov_x").candidates.front().score;
    const double d = Find("pov_diag").candidates.front().score;
    MESSAGE("main direction: y " << y << ", x " << x << ", diagonal " << d);
    CHECK(x > 0.6 * y);
    CHECK(x < 1.6 * y);
    CHECK(d > 0.6 * y);
    CHECK(d < 1.6 * y);
    // The vertical-only score (0.7) saw next to nothing of the same X stroke.
    const double y07 = Find("pov_y", TipAxis::kVertical).candidates.front().score;
    const double x07 = Find("pov_x", TipAxis::kVertical).candidates.front().score;
    MESSAGE("vertical only: y " << y07 << ", x " << x07);
    CHECK(x07 < 0.25 * y07);
    // On a vertical stroke both scores see the same motion.
    CHECK(y == doctest::Approx(y07).epsilon(0.15));
}

TEST_CASE("tip auto: the main direction of the stroke is reported") {
    const auto along = [](const TipFindResult& r, double deg) {
        const double pi = 3.14159265358979323846;
        return std::abs(r.axis.x * std::cos(deg * pi / 180) + r.axis.y * std::sin(deg * pi / 180));
    };
    CHECK(along(Find("pov_y"), 90) > 0.95);
    CHECK(along(Find("pov_x"), 0) > 0.95);
    CHECK(along(Find("pov_diag"), 45) > 0.95);
}

TEST_CASE("tip auto: the proposal and the region are on the mover in every scene") {
    for (const auto& def : tc::TipScenes()) {
        const tc::TipSceneInfo& s = TheClips().Get(def.name);
        REQUIRE(s.ok);
        const TipFindResult r = Find(def.name);
        CAPTURE(def.name);
        CAPTURE(r.box);
        CAPTURE(r.region);
        CHECK(OnTheMover(r.box, s));
        CHECK(r.candidates.front().score >= kStrongTipScore);
        REQUIRE(r.region.area() > 0);
        CHECK(OnTheMover(r.region, s));
        // One big box: it holds the compact proposal and most of the mover.
        CHECK((r.region & r.box).area() * 10 >= r.box.area() * 9);
        const cv::Rect mover = s.MoverBox(0) & Frame(s);
        CHECK((r.region & mover).area() * 2 >= mover.area());
    }
}

TEST_CASE("tip auto: a small mover elsewhere is not part of the region") {
    const tc::TipSceneInfo& s = TheClips().Get("pov_distractor");
    const TipFindResult r = Find("pov_distractor");
    CHECK(OnTheMover(r.region, s));
    // The distractor disc lives around (34, 44).
    CHECK(!r.region.contains(cv::Point(34, 44)));
}

TEST_CASE("tip auto: the tracker follows the mover from the region box") {
    for (const char* name : {"pov_y", "pov_x", "pov_diag", "pov_follow_y", "pov_follow_x", "pov_near"}) {
        const TipFindResult r = Find(name);
        REQUIRE(r.region.area() > 0);
        const Followed f = Follow(name, r.region);
        CAPTURE(name);
        MESSAGE(name << ": r " << f.r << ", on the mover " << f.on_mover << ", p95 " << f.p95 << " px");
        CHECK(f.r > 0.9);
        CHECK(f.on_mover > 0.95);
    }
}
