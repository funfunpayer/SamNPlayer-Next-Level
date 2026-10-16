#include <doctest/doctest.h>

#include "samn/library.h"

using V = std::vector<std::string>;

TEST_CASE("library: recent list as 0.7 (front, no duplicates, capped)") {
    V l;
    l = samn::PushFront(l, "a", 3);
    l = samn::PushFront(l, "b", 3);
    l = samn::PushFront(l, "a", 3);
    CHECK(l == V{"a", "b"});
    l = samn::PushFront(l, "c", 3);
    l = samn::PushFront(l, "d", 3);
    CHECK(l == V{"d", "c", "a"});
    CHECK(samn::PushFront(l, "", 3) == l);
}

TEST_CASE("library: favorites toggle") {
    bool on = false;
    V f = samn::ToggleFavorite({}, "x", &on);
    CHECK(on);
    CHECK(f == V{"x"});
    f = samn::ToggleFavorite(f, "y", &on);
    CHECK(f == V{"y", "x"});
    f = samn::ToggleFavorite(f, "x", &on);
    CHECK_FALSE(on);
    CHECK(f == V{"y"});
    V many;
    for (int i = 0; i < 60; ++i) many = samn::ToggleFavorite(many, std::to_string(i), &on);
    CHECK(many.size() == samn::kFavoritesMax);
}
