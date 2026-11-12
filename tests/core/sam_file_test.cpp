// The 0.7 `.sam` sidecar reader (sam_file.h): synthetic fixtures only.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
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

// Fuzz-style: seeded byte flips, cuts and splices of a valid file. Whatever the
// input, the parser returns (no crash, no hang) and a result is well formed.
TEST_CASE("sam: mutated and random input never crashes and stays well formed") {
    const std::string seed = R"({"version":"0.1","frames":[{"time":0,"motion":{"position":10,"intensity":0.25}},
        {"time":500,"motion":{"position":90,"intensity":0.8,"confidence":1}}],
        "metadata":{"creator":"c","tracking_gaps":[{"start_ms":1,"end_ms":2}],"device_recipe":{"sync":"x"}}})";
    uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    const auto check = [](const std::string& in) {
        const auto r = ParseSam(in);
        if (r.file) {
            CHECK_FALSE(r.file->frames.empty());
            CHECK_FALSE(r.file->version.empty());
            int64_t prev = -1;
            for (const auto& f : r.file->frames) {
                CHECK(f.time_ms >= 0);
                CHECK(f.time_ms >= prev);
                CHECK(f.intensity >= 0.0);
                CHECK(f.intensity <= 1.0);
                CHECK(std::isfinite(f.intensity));
                prev = f.time_ms;
            }
        } else {
            CHECK_FALSE(r.error.empty());
        }
    };
    for (int i = 0; i < 4000; ++i) {
        std::string m = seed;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits && !m.empty(); ++e) {
            const size_t at = static_cast<size_t>(next() % m.size());
            switch (next() % 4) {
                case 0: m[at] = static_cast<char>(next() & 0xFF); break;
                case 1: m.erase(at, 1 + static_cast<size_t>(next() % 8)); break;
                case 2: m.insert(at, 1, "{}[]\",:-.e0123456789nul"[next() % 24]); break;
                default: m.resize(at); break;
            }
        }
        check(m);
    }
    for (int i = 0; i < 500; ++i) {
        std::string junk(static_cast<size_t>(next() % 300), '\0');
        for (auto& c : junk) c = static_cast<char>(next() & 0xFF);
        check(junk);
    }
}

TEST_CASE("sam: an error is short and never quotes the file") {
    const std::string pre = R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":)";
    const std::string digits(5u << 20, '7');
    const std::string longText(5u << 20, 'x');
    const std::string inputs[] = {
        pre + digits + "x}}]}",                                             // digit run then junk
        pre + digits + ".",                                                 // digit run, truncated
        R"({"version":")" + longText,                                       // unterminated string
        R"({"version":"0.1","frames":[{"time":1}],")" + longText + "\":",  // bad / unfinished key
        R"({"version":")" + longText + "\xFF\"}",                           // invalid UTF-8 after a long string
        R"({"version":"0.1","frames":[{"time":")" + longText + "\"}]}",     // wrong type with a long value
    };
    for (const auto& in : inputs) {
        const auto r = ParseSam(in);
        CHECK_FALSE(r.file);
        CHECK_FALSE(r.error.empty());
        CHECK(r.error.size() <= samn::kSamMaxErrorChars);
        CHECK(r.error.find("json.exception") == std::string::npos);
        CHECK(r.error.find("xxxx") == std::string::npos);
        CHECK(r.error.find("7777") == std::string::npos);
    }
}

TEST_CASE("sam: a long version string is cut") {
    const auto r = ParseSam(R"({"version":")" + std::string(4u << 20, 'v') + R"(","frames":[{"time":1}]})");
    REQUIRE(r.file);
    CHECK(r.file->version.size() == 256);
}

TEST_CASE("sam: float times are rounded, huge ones refused, underflow is zero") {
    auto t = [](const std::string& v) {
        return ParseSam(R"({"version":"0.1","frames":[{"time":)" + v + "}]}");
    };
    REQUIRE(t("1500.6").file);
    CHECK(t("1500.6").file->frames[0].time_ms == 1501);  // rounded, not truncated
    CHECK(t("1500.4").file->frames[0].time_ms == 1500);
    CHECK(t("8.9e15").file);
    CHECK_FALSE(t("9e15").file);
    CHECK_FALSE(t("-9e15").file);
    CHECK_FALSE(t("1e308").file);
    const auto tiny = ParseSam(R"({"version":"0.1","frames":[{"time":1,"motion":{"intensity":1e-400}}]})");
    REQUIRE(tiny.file);
    CHECK(tiny.file->frames[0].intensity == 0);
    CHECK_FALSE(ParseSam(R"({"version":"0.1","frames":[{"time":1e999}]})").file);
}

TEST_CASE("sam: intensity under half a percent passes the 0.7 gate but gives an empty lane") {
    const auto r = ParseSam(doc(frame(0, 0, 0.003) + "," + frame(100, 0, 0.004)));
    REQUIRE(r.file);
    CHECK(samn::SamHasContactIntensity(*r.file));  // 0.7's > 0.001
    for (const auto& a : samn::SamIntensityLane(*r.file)) CHECK(a.pos == 0);
}

TEST_CASE("sam: the cached load follows the file") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "samn_sam_cache_test";
    fs::create_directories(dir);
    const fs::path p = dir / "c.sam";
    {
        std::ofstream(p, std::ios::binary) << doc(frame(5, 0, 0.5));
    }
    const auto a = samn::LoadSamCached(p);
    REQUIRE(a.file);
    CHECK(a.file->frames[0].time_ms == 5);
    const auto b = samn::LoadSamCached(p);  // from the cache
    REQUIRE(b.file);
    CHECK(b.file->frames[0].time_ms == 5);
    {
        std::ofstream(p, std::ios::binary) << doc(frame(7, 0, 0.5) + "," + frame(9, 0, 0.5));
    }
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(5));
    const auto c = samn::LoadSamCached(p);
    REQUIRE(c.file);
    CHECK(c.file->frames.size() == 2);
    fs::remove(p);
    CHECK_FALSE(samn::LoadSamCached(p).file);  // gone: an error, not the stale entry
    fs::remove_all(dir);
}
