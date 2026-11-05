// Locale-free numbers (number.h) and the places that used strtod, stod and
// "%.3f": under a comma-decimal locale "1.6" read as 1 and 62.5 was written
// as "62,5".
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "comma_locale.h"
#include "samn/clip_prep.h"
#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/number.h"
#include "samn/samn_file.h"

using samn::testing::CommaLocale;

TEST_CASE("number: ParseDoublePrefix follows strtod for decimal numbers") {
    auto p = samn::ParseDoublePrefix("  -12.5e1xyz");
    CHECK(p.ok);
    CHECK(p.value == -125.0);
    CHECK(p.used == 9);
    CHECK(samn::StrToDouble("+.5") == 0.5);
    CHECK(samn::StrToDouble("3.") == 3.0);
    CHECK(samn::StrToDouble("7") == 7.0);
    CHECK_FALSE(samn::ParseDoublePrefix("").ok);
    CHECK_FALSE(samn::ParseDoublePrefix("abc").ok);
    CHECK_FALSE(samn::ParseDoublePrefix("inf").ok);
    CHECK_FALSE(samn::ParseDoublePrefix("nan").ok);
    CHECK_FALSE(samn::ParseDoublePrefix("+-1").ok);
    CHECK_FALSE(samn::ParseDoublePrefix("-").ok);
    CHECK(samn::StrToDouble("1,6") == 1.0);  // a comma is never a decimal point here

    p = samn::ParseDoublePrefix("1e999");
    CHECK(p.ok);
    CHECK(p.out_of_range);
    CHECK(std::isinf(p.value));
    p = samn::ParseDoublePrefix("-1e999");
    CHECK(p.value == -std::numeric_limits<double>::infinity());
    p = samn::ParseDoublePrefix("1e-999");
    CHECK(p.out_of_range);
    CHECK(p.value == 0.0);
    p = samn::ParseDoublePrefix("0.000001e-999");
    CHECK(p.value == 0.0);
    p = samn::ParseDoublePrefix("123456e+305");
    CHECK(std::isinf(p.value));
}

TEST_CASE("number: FormatFixed writes a point") {
    CHECK(samn::FormatFixed(62.5, 3) == "62.500");
    CHECK(samn::FormatFixed(1.0 / 3.0, 2) == "0.33");
    CHECK(samn::FormatFixed(-0.25, 1) == "-0.2");
}

TEST_CASE("locale: a .samn number reads the same under a comma-decimal locale") {
    CommaLocale loc;
    SAMN_REQUIRE_COMMA_LOCALE(loc);
    // The C library really is in comma mode.
    CHECK(std::strtod("1,6", nullptr) == 1.6);
    const samn::Document d = samn::ParseDocument(R"({"general":[{"at":0,"pos":1.6}]})");
    REQUIRE(d.general.size() == 1);
    CHECK(d.general[0].pos == 2);  // 1.6 rounds to 2; "1" came out of the comma locale
}

TEST_CASE("locale: clip times and seconds keep the point under a comma-decimal locale") {
    CommaLocale loc;
    SAMN_REQUIRE_COMMA_LOCALE(loc);
    REQUIRE(samn::ParseClipTime("1:02.5"));
    CHECK(*samn::ParseClipTime("1:02.5") == doctest::Approx(62.5));
    CHECK(samn::FormatClipSeconds(62.5) == "62.5");
    CHECK(samn::FormatClipSeconds(1.25) == "1.25");
}

TEST_CASE("locale: funscript numbers and times under a comma-decimal locale") {
    CommaLocale loc;
    SAMN_REQUIRE_COMMA_LOCALE(loc);
    const auto s = samn::Parse(
        R"({"actions":[{"at":"1000.4","pos":"49.6"},{"at":2000,"pos":10}],"metadata":{"duration":"12.5"}})");
    REQUIRE(s.actions.size() == 2);
    CHECK(s.actions[0].at == 1000);
    CHECK(s.actions[0].pos == 50);
    CHECK(s.duration_meta == 12500);
    CHECK(samn::FlexTimeMs(nlohmann::json("1:02.5")) == 62500);
}
