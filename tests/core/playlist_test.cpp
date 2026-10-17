#include <doctest/doctest.h>

#include <algorithm>

#include "samn/playlist.h"

using samn::Playlist;

TEST_CASE("Playlist: replace drops empty and duplicate paths, clamps start") {
    Playlist p;
    p.Replace({"/a/One.funscript", "", "/a/Two.funscript", "/a/One.funscript"}, 9);
    REQUIRE(p.Items().size() == 2);
    CHECK(p.Items()[0].name == "One");
    CHECK(p.Index() == 1);
    CHECK(p.Visible());
    p.Replace({"/a/x.funscript"});
    CHECK_FALSE(p.Visible());
}

TEST_CASE("Playlist: append seeds the loaded script and refuses duplicates") {
    Playlist p;
    CHECK(p.Append("/b.funscript", "/a.funscript"));
    REQUIRE(p.Items().size() == 2);
    CHECK(p.Items()[0].path == "/a.funscript");
    CHECK(p.Index() == 0);
    CHECK_FALSE(p.Append("/b.funscript"));
    CHECK_FALSE(p.Append(""));
}

TEST_CASE("Playlist: remove keeps the active item where 0.7 does") {
    Playlist p;
    p.Replace({"1", "2", "3", "4"}, 2);
    CHECK_FALSE(p.Remove(0));  // before the active one: index follows
    CHECK(p.Index() == 1);
    CHECK(p.Current()->path == "3");
    CHECK(p.Remove(1));  // the active one: the next takes its place
    CHECK(p.Current()->path == "4");
    CHECK(p.Remove(1));  // active at the end: index steps back
    CHECK(p.Current()->path == "2");
    CHECK(p.Remove(0));
    CHECK(p.Items().empty());
    CHECK(p.Current() == nullptr);
    CHECK_FALSE(p.Remove(0));
}

TEST_CASE("Playlist: next, repeat, auto-advance") {
    std::mt19937 rng(7);
    Playlist p;
    p.Replace({"1", "2", "3"});
    CHECK(p.HasNext());
    CHECK(p.Advance(rng));
    CHECK(p.Advance(rng));
    CHECK(p.Index() == 2);
    CHECK_FALSE(p.HasNext());
    CHECK_FALSE(p.Advance(rng));  // end without Repeat
    p.SetRepeat(true);
    CHECK(p.HasNext());
    CHECK(p.Advance(rng));
    CHECK(p.Index() == 0);
    p.SetAuto(false);
    CHECK_FALSE(p.AdvanceOnFinish(rng));
    p.SetAuto(true);
    CHECK(p.AdvanceOnFinish(rng));
    CHECK(p.Index() == 1);
    CHECK(p.Pick(2));
    CHECK_FALSE(p.Pick(2));
    CHECK_FALSE(p.Pick(9));
}

TEST_CASE("Playlist: shuffle keeps the current item first and all items") {
    std::mt19937 rng(42);
    Playlist p;
    p.Replace({"1", "2", "3", "4", "5", "6"}, 3);
    p.SetShuffle(true, rng);
    CHECK(p.Index() == 0);
    CHECK(p.Current()->path == "4");
    std::vector<std::string> got;
    for (const auto& it : p.Items()) got.push_back(it.path);
    std::sort(got.begin(), got.end());
    CHECK(got == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
    // A repeat cycle with shuffle still holds every item once.
    p.SetRepeat(true);
    for (int i = 0; i < 6; ++i) CHECK(p.Advance(rng));
    CHECK(p.Index() == 0);
    CHECK(p.Items().size() == 6);
}

// 0.7 #pb-prev: the item before, never wrapping (even with Repeat).
TEST_CASE("Playlist: back goes to the item before and stops at the first") {
    Playlist p;
    CHECK_FALSE(p.Back());  // empty
    p.Replace({"/a.funscript"});
    CHECK_FALSE(p.Back());  // one item
    p.Replace({"/a.funscript", "/b.funscript", "/c.funscript"}, 2);
    p.SetRepeat(true);
    CHECK(p.Back());
    CHECK(p.Index() == 1);
    CHECK(p.Back());
    CHECK(p.Index() == 0);
    CHECK_FALSE(p.Back());
    CHECK(p.Index() == 0);
}
