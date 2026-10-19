#include <doctest/doctest.h>

#include <thread>

#include "samn/log_buffer.h"

using namespace samn;

TEST_CASE("log: levels parse and name") {
    CHECK(ParseLogLevel("debug") == LogSeverity::kDebug);
    CHECK(ParseLogLevel(" Warning ") == LogSeverity::kWarn);
    CHECK(ParseLogLevel("ERROR") == LogSeverity::kError);
    CHECK_FALSE(ParseLogLevel("loud"));
    CHECK(std::string(LogLevelName(LogSeverity::kWarn)) == "warn");
}

TEST_CASE("log: the line format of 0.7's Log tab") {
    const std::string line = FormatLogLine({1234, LogSeverity::kWarn, "device lost"});
    // "HH:MM:SS.mmm  WARN   device lost" (local time; the milliseconds are fixed).
    REQUIRE(line.size() > 14);
    CHECK(line.substr(8, 4) == ".234");
    CHECK(line.substr(12) == "  WARN   device lost");
    CHECK(line[2] == ':');
    CHECK(line[5] == ':');
}

TEST_CASE("log: the buffer keeps the newest, filters by level and warnings") {
    LogBuffer b(3);
    CHECK(b.level() == LogSeverity::kInfo);
    CHECK_FALSE(b.Add({1, LogSeverity::kDebug, "hidden at info"}));
    const uint64_t g = b.generation();
    for (int i = 0; i < 5; ++i) CHECK(b.Add({i, i == 3 ? LogSeverity::kError : LogSeverity::kInfo, std::to_string(i)}));
    CHECK(b.generation() > g);
    const auto all = b.Entries();
    REQUIRE(all.size() == 3);  // the oldest went
    CHECK(all.front().text == "2");
    CHECK(all.back().text == "4");
    const auto warn = b.Entries(true);
    REQUIRE(warn.size() == 1);
    CHECK(warn[0].text == "3");
    b.SetLevel(LogSeverity::kDebug);
    CHECK(b.Add({9, LogSeverity::kDebug, "now kept"}));
    b.Clear();
    CHECK(b.Entries().empty());
}

TEST_CASE("log: adding from several threads keeps every entry") {
    LogBuffer b(10000);
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&b, t] {
            for (int i = 0; i < 500; ++i) b.Add({i, LogSeverity::kInfo, std::to_string(t)});
        });
    for (auto& t : ts) t.join();
    CHECK(b.Entries().size() == 2000);
}
