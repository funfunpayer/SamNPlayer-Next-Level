// "Bake offset": bookmarks and chapters move with the script (0.7 shift.go).
#include <doctest/doctest.h>

#include "samn/marks.h"

TEST_CASE("shift: bookmarks and chapters move; before the start they go or are cut") {
    const std::vector<samn::Bookmark> b{{"a", 100}, {"b", 5000}};
    auto [later, d0] = samn::ShiftBookmarks(b, 250);
    CHECK(d0 == 0);
    CHECK(later[0].time_ms == 350);
    auto [earlier, d1] = samn::ShiftBookmarks(b, -200);
    CHECK(d1 == 1);
    REQUIRE(earlier.size() == 1);
    CHECK(earlier[0].time_ms == 4800);

    const std::vector<samn::Chapter> c{{"Intro", 0, 1000}, {"Main", 1000, 9000}, {"Rest", 9000, 0}};
    auto [moved, d2] = samn::ShiftChapters(c, -1500);
    CHECK(d2 == 1);  // the intro lies wholly before 0
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].start_ms == 0);  // cut at 0
    CHECK(moved[0].end_ms == 7500);
    CHECK(moved[1].start_ms == 7500);
    CHECK(moved[1].end_ms == 0);  // open stays open
}
