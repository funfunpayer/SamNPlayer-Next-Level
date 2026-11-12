// The 0.7 `.sam` sidecar reader (sam_file.h): synthetic fixtures only.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "comma_locale.h"
#include "samn/sam_file.h"

using samn::ParseSam;

namespace {

std::string frame(long t, double pos, double inten) {
    return "{\"time\":" + std::to_string(t) + ",\"motion\":{\"position\":" + std::to_string(pos) +
           ",\"intensity\":" + std::to_string(inten) + "}}";
}

std::string doc(const std::string& frames, const std::string& extra = "") {
    return "{\"version\":\"0.1\",\"frames\":[" + frames + "]" + extra + "}";
}

}  // namespace

TEST_CASE("sam: a valid file is read with its metadata") {
    const std::string text = R"({
  "version": "0.1",
  "frames": [
    {"time": 0, "motion": {"position": 10, "intensity": 0.25, "confidence": 1}},
    {"time": 500, "motion": {"position": 90, "intensity": 0.8, "velocity": 1.5}}
  ],
  "metadata": {"creator": "SamNPlayer", "source": "funscript-sidecar", "profile": "tj",
               "tracking_gaps": [{"start_ms": 100, "end_ms": 200}],
               "device_recipe": {"sync": "x", "contact_vibration": true}}
})";
    const auto r = ParseSam(text);
    REQUIRE(r.file);
    CHECK(r.error.empty());
    CHECK(r.file->version == "0.1");
    CHECK(r.file->creator == "SamNPlayer");
    CHECK(r.file->source == "funscript-sidecar");
    CHECK(r.file->profile == "tj");
    REQUIRE(r.file->frames.size() == 2);
    CHECK(r.file->frames[1].time_ms == 500);
    CHECK(r.file->frames[1].intensity == doctest::Approx(0.8));
    CHECK(samn::SamHasContactIntensity(*r.file));
    const auto lane = samn::SamIntensityLane(*r.file);
    REQUIRE(lane.size() == 2);
    CHECK(lane[0].at == 0);
    CHECK(lane[0].pos == 25);
    CHECK(lane[1].pos == 80);
}

TEST_CASE("sam: a minimal file takes the defaults") {
    const auto r = ParseSam(R"({"version":"x","frames":[{}]})");
    REQUIRE(r.file);
    REQUIRE(r.file->frames.size() == 1);
    CHECK(r.file->frames[0].time_ms == 0);
    CHECK(r.file->frames[0].intensity == 0);
    CHECK_FALSE(samn::SamHasContactIntensity(*r.file));  // position-only: nothing to import
}

TEST_CASE("sam: unknown fields, nulls and nested extras are skipped") {
    const auto r = ParseSam(R"({"version":"9.9","future":{"a":[1,2,{"b":null}]},
        "frames":[{"time":10,"extra":[[1]],"motion":{"intensity":0.5,"new":{"x":1},"tension":null}}],
        "metadata":{"creator":null,"other":[1,2,3]}})");
    REQUIRE(r.file);
    CHECK(r.file->frames[0].intensity == doctest::Approx(0.5));
    CHECK(r.file->creator.empty());
}

TEST_CASE("sam: frames are sorted, clamped and negative times dropped") {
    const auto r = ParseSam(doc(frame(300, 0, 2.5) + "," + frame(-5, 0, 1) + "," + frame(100, 0, -1)));
    REQUIRE(r.file);
    REQUIRE(r.file->frames.size() == 2);
    CHECK(r.file->dropped == 1);
    CHECK(r.file->frames[0].time_ms == 100);
    CHECK(r.file->frames[0].intensity == 0);
    CHECK(r.file->frames[1].intensity == 1);
}

TEST_CASE("sam: the lane keeps one point per time, the later frame wins") {
    const auto r = ParseSam(doc(frame(100, 0, 0.2) + "," + frame(100, 0, 0.9) + "," + frame(200, 0, 0.5)));
    REQUIRE(r.file);
    const auto lane = samn::SamIntensityLane(*r.file);
    REQUIRE(lane.size() == 2);
    CHECK(lane[0].pos == 90);
}

TEST_CASE("sam: BOM and CRLF are accepted") {
    const std::string text = "\xEF\xBB\xBF{\r\n\"version\":\"0.1\",\r\n\"frames\":[\r\n{\"time\":1,\"motion\":{\"intensity\":1}}\r\n]\r\n}\r\n";
    const auto r = ParseSam(text);
    REQUIRE(r.file);
    CHECK(r.file->frames[0].time_ms == 1);
}

TEST_CASE("sam: numbers do not depend on the locale") {
    samn::testing::CommaLocale loc;
    SAMN_REQUIRE_COMMA_LOCALE(loc);
    const auto r = ParseSam(R"({"version":"0.1","frames":[{"time":1500.0,"motion":{"intensity":0.75}},
        {"time":2e3,"motion":{"intensity":7.5e-1}}]})");
    REQUIRE(r.file);
    CHECK(r.file->frames[0].time_ms == 1500);
    CHECK(r.file->frames[0].intensity == doctest::Approx(0.75));
    CHECK(r.file->frames[1].time_ms == 2000);
    CHECK(r.file->frames[1].intensity == doctest::Approx(0.75));
}

TEST_CASE("sam: a comma as the decimal mark is an error, not a wrong number") {
    const auto r = ParseSam(R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":0,75}}]})");
    CHECK_FALSE(r.file);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("sam: bad documents give an error and no file") {
    const char* bad[] = {
        "",
        "   ",
        "not json",
        "[]",
        "5",
        "{}",
        R"({"frames":[{"time":1}]})",                          // no version
        R"({"version":"","frames":[{"time":1}]})",             // empty version
        R"({"version":"0.1"})",                                // no frames
        R"({"version":"0.1","frames":[]})",                    // empty frames
        R"({"version":"0.1","frames":[{"time":1}]} trailing)",
        R"({"version":"0.1","frames":[{"time":1}])",           // truncated
        R"({"version":"0.1","frames":[{"time":1},{"ti)",       // truncated mid-key
        R"({"version":"0.1","frames":[{"time":NaN}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":NaN}}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":Infinity}}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":1e999}}]})",
        R"({"version":"0.1","frames":[{"time":1e999}]})",
        R"({"version":"0.1","frames":[{"time":"soon"}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":"high"}}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":[1]}]})",
        R"({"version":"0.1","frames":[{"time":1,"motion":7}]})",
        R"({"version":"0.1","frames":[{"time":[1]}]})",
        R"({"version":"0.1","frames":[5]})",
        R"({"version":"0.1","frames":[[1]]})",
        R"({"version":"0.1","frames":{"time":1}})",
        R"({"version":"0.1","frames":"x"})",
        R"({"version":1,"frames":[{"time":1}]})",
        R"({"version":{"a":1},"frames":[{"time":1}]})",
        R"({"version":"0.1","frames":[{"time":1}],"metadata":[1]})",
        R"({"version":"0.1","frames":[{"time":1}],"metadata":{"creator":5}})",
        R"({"version":"0.1","frames":[{"time":1}],"metadata":"x"})",
    };
    for (const char* b : bad) {
        INFO("input: " << b);
        const auto r = ParseSam(b);
        CHECK_FALSE(r.file);
        CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("sam: binary and invalid UTF-8 never crash") {
    std::string bin;
    for (int i = 0; i < 4096; ++i) bin.push_back(static_cast<char>((i * 131 + 7) & 0xFF));
    CHECK_FALSE(ParseSam(bin).file);
    std::string nul = std::string("{\"version\":\"0.1\",\"frames\":[{\"time\":1}]}") + std::string(1, '\0');
    CHECK_FALSE(ParseSam(nul).file);
    CHECK_FALSE(ParseSam(std::string("{\"version\":\"\xFF\xFE\",\"frames\":[{\"time\":1}]}")).file);
    CHECK_FALSE(ParseSam("\xEF\xBB").file);
}

TEST_CASE("sam: deep nesting is refused, not recursed") {
    std::string deep(100000, '[');
    CHECK_FALSE(ParseSam(deep).file);
    std::string nested = R"({"version":"0.1","x":)" + std::string(1000, '[') + std::string(1000, ']') +
                         R"(,"frames":[{"time":1}]})";
    const auto r = ParseSam(nested);
    CHECK_FALSE(r.file);
    CHECK(r.error.find("deeply") != std::string::npos);
}

TEST_CASE("sam: size and frame limits") {
    std::string big(samn::kSamMaxBytes + 1, ' ');
    const auto r = ParseSam(big);
    CHECK_FALSE(r.file);
    CHECK(r.error.find("too large") != std::string::npos);

    // One more frame than the limit: refused while streaming, not stored.
    std::string frames;
    frames.reserve(samn::kSamMaxFrames * 3 + 16);
    for (size_t i = 0; i <= samn::kSamMaxFrames; ++i) frames += "{},";
    frames.pop_back();
    const auto many = ParseSam(doc(frames));
    CHECK_FALSE(many.file);
    CHECK(many.error.find("too many") != std::string::npos);
}

TEST_CASE("sam: a large valid file is read") {
    std::string frames;
    for (int i = 0; i < 100000; ++i) {
        if (i) frames += ",";
        frames += R"({"time":)" + std::to_string(i * 20) + R"(,"motion":{"intensity":0.5}})";
    }
    const auto r = ParseSam(doc(frames));
    REQUIRE(r.file);
    CHECK(r.file->frames.size() == 100000);
}

TEST_CASE("sam: sidecar path is the script's stem plus .sam") {
    CHECK(samn::SamSidecarPath("/v/clip.funscript") == "/v/clip.sam");
    CHECK(samn::SamSidecarPath("/v/clip.samn") == "/v/clip.sam");
    CHECK(samn::SamSidecarPath("/v.d/clip") == "/v.d/clip.sam");
    CHECK(samn::SamSidecarPath("/v/a.b.funscript") == "/v/a.b.sam");
    CHECK(samn::SamSidecarPath("").empty());
}

TEST_CASE("sam: LoadSam reads a file and reports a missing one") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "samn_sam_file_test";
    fs::create_directories(dir);
    const fs::path p = dir / "t.sam";
    {
        std::ofstream(p, std::ios::binary) << doc(frame(5, 0, 0.5));
    }
    const auto ok = samn::LoadSam(p);
    REQUIRE(ok.file);
    CHECK(ok.file->frames[0].time_ms == 5);
    const auto missing = samn::LoadSam(dir / "nope.sam");
    CHECK_FALSE(missing.file);
    CHECK_FALSE(missing.error.empty());
    fs::remove_all(dir);
}
