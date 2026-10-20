// Clip prep (0.7's Clip-Prep, D-155 step 6): times as typed, ffmpeg's
// arguments with 0.7's settings, the suggested name, marks.json batches and
// the script cut to the same window.
#include <doctest/doctest.h>

#include <algorithm>

#include "samn/clip_prep.h"

using samn::Action;

TEST_CASE("times are seconds or MM:SS or HH:MM:SS") {
    CHECK(*samn::ParseClipTime("85") == doctest::Approx(85));
    CHECK(*samn::ParseClipTime(" 85.5 ") == doctest::Approx(85.5));
    CHECK(*samn::ParseClipTime("1:25") == doctest::Approx(85));
    CHECK(*samn::ParseClipTime("01:02:03.25") == doctest::Approx(3723.25));
    CHECK(*samn::ParseClipTime("90:00") == doctest::Approx(5400));  // the first part may be large
    for (const char* bad : {"", " ", "abc", "-5", "1:60", "1.5:20", "1:2:3:4", "1..2", "1:", ":5", "1e3"})
        CHECK_MESSAGE(!samn::ParseClipTime(bad), bad);
}

TEST_CASE("seconds for ffmpeg are short and exact") {
    CHECK(samn::FormatClipSeconds(85) == "85");
    CHECK(samn::FormatClipSeconds(85.5) == "85.5");
    CHECK(samn::FormatClipSeconds(1.25) == "1.25");
    CHECK(samn::FormatClipSeconds(0.1234) == "0.123");
    CHECK(samn::FormatClipSeconds(-3) == "0");
}

TEST_CASE("presets give 0.7's widths") {
    CHECK(samn::ClipPrepMaxWidth("720p") == 1280);
    CHECK(samn::ClipPrepMaxWidth("960w") == 960);
    CHECK(samn::ClipPrepMaxWidth("1080p") == 1920);
    CHECK(samn::ClipPrepMaxWidth("what") == 1280);
}

TEST_CASE("the cut uses 0.7's settings and reports progress") {
    samn::ClipPrepOptions o;
    o.input = "/v/long movie.mp4";
    o.output = "/v/long movie_1-25-2-05.mp4";
    o.start_s = 85;
    o.end_s = 125.5;
    const auto a = samn::ClipPrepArgs(o);
    REQUIRE(a);
    const auto after = [&](const std::string& flag) {
        const auto it = std::find(a->begin(), a->end(), flag);
        return it == a->end() || it + 1 == a->end() ? std::string("<none>") : *(it + 1);
    };
    CHECK(after("-ss") == "85");
    CHECK(after("-t") == "40.5");
    CHECK(after("-i") == "/v/long movie.mp4");
    CHECK(after("-vf") == "scale='min(1280,iw)':-2:flags=lanczos");
    CHECK(after("-c:v") == "libx264");
    CHECK(after("-preset") == "veryfast");
    CHECK(after("-crf") == "20");
    CHECK(after("-pix_fmt") == "yuv420p");
    CHECK(after("-c:a") == "aac");
    CHECK(after("-b:a") == "128k");
    CHECK(after("-movflags") == "+faststart");
    CHECK(after("-progress") == "pipe:1");
    CHECK(a->back() == o.output);
    // -ss before -i: a fast seek.
    CHECK(std::find(a->begin(), a->end(), "-ss") < std::find(a->begin(), a->end(), "-i"));

    o.no_audio = true;
    o.max_width = 960;
    const auto quiet = samn::ClipPrepArgs(o);
    REQUIRE(quiet);
    CHECK(std::find(quiet->begin(), quiet->end(), "-an") != quiet->end());
    CHECK(std::find(quiet->begin(), quiet->end(), "aac") == quiet->end());
    CHECK(std::find(quiet->begin(), quiet->end(), "scale='min(960,iw)':-2:flags=lanczos") != quiet->end());
}

TEST_CASE("a bad cut says why") {
    const auto why = [](samn::ClipPrepOptions o) {
        std::string e;
        CHECK_FALSE(samn::ClipPrepArgs(o, &e));
        return e;
    };
    samn::ClipPrepOptions o{"/a.mp4", "/b.mp4", 10, 20};
    CHECK(samn::ClipPrepArgs(o));
    auto x = o;
    x.input.clear();
    CHECK(why(x) == "no video to cut");
    x = o;
    x.output = " ";
    CHECK(why(x) == "no file to write");
    x = o;
    x.output = x.input;
    CHECK(why(x) == "the cut cannot replace its own video");
    x = o;
    x.end_s = 10;
    CHECK(why(x) == "the end must be after the start");
    x = o;
    x.start_s = -1;
    CHECK(why(x) == "the start is before the video");
}

TEST_CASE("the suggested name is 0.7's") {
    CHECK(samn::SuggestClipName("movie", "1:25", "2:05") == "movie_1-25-2-05.mp4");
    CHECK(samn::SuggestClipName("movie", "85", "125.5") == "movie_85-125.5.mp4");
    CHECK(samn::SuggestClipName("", "", "x/y") == "clip_t-___.mp4");
}

TEST_CASE("a marks file lists cuts in either form") {
    const auto m = samn::ParseClipMarks(R"({"source":"long.mp4","max_width":960,"clips":[
        {"name":"intro","start":"0:10","end":"0:40"},
        {"name":"mid","start_sec":100,"duration_sec":30},
        {"start":5,"end":6.5}]})");
    REQUIRE(m);
    CHECK(m->source == "long.mp4");
    CHECK(m->max_width == 960);
    REQUIRE(m->clips.size() == 3);
    CHECK(m->clips[0].name == "intro");
    CHECK(m->clips[0].start_s == doctest::Approx(10));
    CHECK(m->clips[0].end_s == doctest::Approx(40));
    CHECK(m->clips[1].end_s == doctest::Approx(130));
    CHECK(m->clips[2].name == "clip_3");

    std::string e;
    CHECK_FALSE(samn::ParseClipMarks("nope", &e));
    CHECK(e.find("no JSON") != std::string::npos);
    CHECK_FALSE(samn::ParseClipMarks(R"({"clips":[]})", &e));
    CHECK(e.find("no clips") != std::string::npos);
    CHECK_FALSE(samn::ParseClipMarks(R"({"clips":[{"name":"x","start":"0:50","end":"0:40"}]})", &e));
    CHECK(e.find("clip 1 (x)") != std::string::npos);
}

TEST_CASE("the script is cut to the same window") {
    const std::vector<Action> s{{0, 0}, {1000, 100}, {2000, 0}, {3000, 100}, {4000, 0}};
    const auto cut = samn::CutScriptWindow(s, 1500, 3000);
    REQUIRE(cut.size() == 3);
    CHECK(cut[0].at == 0);
    CHECK(cut[0].pos == 50);  // the stroke crosses the start halfway
    CHECK(cut[1].at == 500);
    CHECK(cut[1].pos == 0);
    CHECK(cut[2].at == 1500);  // an action on the edge stays once
    CHECK(cut[2].pos == 100);

    CHECK(samn::CutScriptWindow(s, 3000, 3000).empty());
    CHECK(samn::CutScriptWindow({}, 0, 1000).empty());
    // A window past the script keeps nothing.
    CHECK(samn::CutScriptWindow(s, 5000, 6000).empty());
}
