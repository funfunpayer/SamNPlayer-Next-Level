#include <doctest/doctest.h>

#include "samn/update_check.h"

using namespace samn;

TEST_CASE("update: only plain version tags are releases") {
    CHECK(ParseReleaseTag("v2.1.3") == std::array<int, 3>{2, 1, 3});
    CHECK(ParseReleaseTag(" 0.7.0 ") == std::array<int, 3>{0, 7, 0});
    CHECK_FALSE(ParseReleaseTag("patch-v2.1.3-p1"));
    CHECK_FALSE(ParseReleaseTag("v2.1.3-rc1"));
    CHECK_FALSE(ParseReleaseTag("v2.1"));
    CHECK_FALSE(ParseReleaseTag("v2.1.3.4"));
    CHECK_FALSE(ParseReleaseTag("v2..3"));
    CHECK_FALSE(ParseReleaseTag(""));
    CHECK_FALSE(ParseReleaseTag("v99999999.0.0"));
}

TEST_CASE("update: newer compares numbers, a dev build is older than its release") {
    CHECK(IsNewerRelease("2.0.0", "v2.0.1"));
    CHECK(IsNewerRelease("2.0.9", "v2.0.10"));  // numbers, not text
    CHECK(IsNewerRelease("2.9.0", "v3.0.0"));
    CHECK_FALSE(IsNewerRelease("2.0.1", "v2.0.1"));
    CHECK_FALSE(IsNewerRelease("2.1.0", "v2.0.5"));
    CHECK(IsNewerRelease("2.0.0-dev", "v2.0.0"));
    CHECK_FALSE(IsNewerRelease("2.0.1-dev", "v2.0.0"));
    CHECK(IsNewerRelease("", "v0.0.1"));         // unknown build: offer it
    CHECK_FALSE(IsNewerRelease("2.0.0", "nonsense"));
}

TEST_CASE("update: the notes preview of 0.7") {
    CHECK(ReleaseNotesPreview("") == "");
    CHECK(ReleaseNotesPreview("<!-- hidden -->\n## What's new\n\n* Faster *start*\n> Quote\n`code`\nfifth") ==
          "What's new \xC2\xB7 Faster *start* \xC2\xB7 Quote \xC2\xB7 code`");
    const std::string long_line(300, 'a');
    const std::string cut = ReleaseNotesPreview(long_line);
    CHECK(cut.size() == 219 + 3);  // 219 letters and "…"
    CHECK(cut.substr(219) == "\xE2\x80\xA6");
    // Never cut inside a multi-byte character.
    std::string umlauts;
    for (int i = 0; i < 230; ++i) umlauts += "\xC3\xA4";
    CHECK(ReleaseNotesPreview(umlauts, 10) == std::string(umlauts.substr(0, 18)) + "\xE2\x80\xA6");
}

TEST_CASE("update: the latest-release answer") {
    const auto r = ParseLatestRelease(
        R"({"tag_name":"v2.1.0","html_url":"https://github.com/funfunpayer/SamNPlayer-Next-Level/releases/tag/v2.1.0","body":"Notes","draft":false,"prerelease":false})");
    REQUIRE(r);
    CHECK(r->tag == "v2.1.0");
    CHECK(r->page_url == "https://github.com/funfunpayer/SamNPlayer-Next-Level/releases/tag/v2.1.0");
    CHECK(r->notes == "Notes");
    // A page elsewhere is replaced by the project's releases page.
    const auto other = ParseLatestRelease(R"({"tag_name":"v2.1.0","html_url":"https://example.com/x"})");
    REQUIRE(other);
    CHECK(other->page_url == ReleasesPageUrl());
    const auto tricky = ParseLatestRelease(
        R"({"tag_name":"v2.1.0","html_url":"https://github.com/funfunpayer/SamNPlayer-Next-Level/releases/../../evil?x"})");
    REQUIRE(tricky);
    CHECK(tricky->page_url == ReleasesPageUrl());
    CHECK_FALSE(ParseLatestRelease(R"({"tag_name":"patch-v2.1.0-p1"})"));
    CHECK_FALSE(ParseLatestRelease(R"({"tag_name":"v2.1.0","prerelease":true})"));
    CHECK_FALSE(ParseLatestRelease(R"({"tag_name":"v2.1.0","draft":true})"));
    CHECK_FALSE(ParseLatestRelease("not json"));
    CHECK_FALSE(ParseLatestRelease("[]"));
    CHECK(LatestReleaseApiUrl() == "https://api.github.com/repos/funfunpayer/SamNPlayer-Next-Level/releases/latest");
}
