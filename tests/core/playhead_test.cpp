#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "samn/playhead.h"

using samn::PlayheadExtrapolator;

namespace {

// A media player that reports its true position every `period` ms (every
// fourth report comes 3 ms late, the positions are exact). Checks
// the estimate every millisecond in [from, to) against the true position.
struct Stream {
    PlayheadExtrapolator& head;
    double rate;
    int64_t start_pos;  // video ms at wall 0
    double True(int64_t now) const { return double(start_pos) + double(now) * rate; }
    // Reports at period steps from `from` (inclusive) to `to`; returns the
    // largest error of the estimate between them (after the first report).
    double Play(int64_t from, int64_t to, int64_t period) {
        double worst = 0;
        int64_t next = from;
        int k = 0;
        bool reported = false;
        for (int64_t now = from; now < to; ++now) {
            if (now >= next) {
                head.Report(int64_t(std::llround(True(now))), now);
                reported = true;
                ++k;
                next = from + k * period + (k % 4 == 0 ? 3 : 0);
            }
            if (reported) worst = std::max(worst, std::abs(double(head.Estimate(now)) - True(now)));
        }
        return worst;
    }
};

}  // namespace

TEST_CASE("playhead: steady 100 ms reports at rate 1 stay within 2 ms of the true position") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    Stream s{h, 1.0, 1000};
    CHECK(s.Play(0, 3000, 100) <= 2.0);
    CHECK(h.CapMs() == 120);  // 100 ms median x 1.2
}

TEST_CASE("playhead: steady 100 ms reports at rate 2 stay within 2 ms of the true position") {
    PlayheadExtrapolator h;
    h.SetRate(2.0, 0);
    h.SetPlaying(true);
    Stream s{h, 2.0, 500};
    CHECK(s.Play(0, 3000, 100) <= 2.0);
}

TEST_CASE("playhead: 50 ms reports (Qt 6.7) at rate 0.5 stay within 2 ms; the cap follows the interval") {
    PlayheadExtrapolator h;
    h.SetRate(0.5, 0);
    h.SetPlaying(true);
    Stream s{h, 0.5, 0};
    CHECK(s.Play(0, 2000, 50) <= 2.0);
    CHECK(h.CapMs() == 60);
}

TEST_CASE("playhead: the cap is the median interval x 1.2 within 20-150 ms, 120 before it is known") {
    PlayheadExtrapolator h;
    CHECK(h.CapMs() == 120);
    h.SetPlaying(true);
    h.Report(0, 0);
    CHECK(h.CapMs() == 120);  // one report: no interval yet
    for (int i = 1; i <= 9; ++i) h.Report(i * 10, i * 10);
    CHECK(h.CapMs() == 20);  // 10 ms x 1.2 = 12 -> the minimum
    for (int i = 1; i <= 9; ++i) h.Report(90 + i * 200, 90 + i * 200);
    CHECK(h.CapMs() == 150);  // 200 ms x 1.2 = 240 -> the maximum
    h.Clear();
    CHECK(h.CapMs() == 120);
    CHECK(h.Estimate(5000) == 0);
}

TEST_CASE("playhead: no advance before the first report after play") {
    PlayheadExtrapolator h;
    h.Report(2000, 0);  // paused here
    CHECK(h.Estimate(50) == 2000);
    h.SetPlaying(true);
    CHECK(h.Estimate(180) == 2000);
    CHECK(!h.Advancing(180));
    h.Report(2080, 180);
    CHECK(h.Advancing(200));
    CHECK(h.Estimate(200) == 2100);
}

TEST_CASE("playhead: pause and stop freeze it at the last report") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 5; ++i) h.Report(i * 100, i * 100);
    CHECK(h.Estimate(550) == 550);
    h.SetPlaying(false);
    CHECK(!h.Advancing(560));
    CHECK(h.Estimate(560) == 500);
    CHECK(h.Estimate(5000) == 500);
    h.Report(520, 600);  // the player's last word on pause
    CHECK(h.Estimate(5000) == 520);
    // A seek while paused: the estimate is the new position, still standing.
    h.Report(9000, 700);
    CHECK(h.Estimate(800) == 9000);
    // Stop (back to 0).
    h.Report(0, 900);
    CHECK(h.Estimate(1000) == 0);
}

TEST_CASE("playhead: a stall (no report for 300 ms) stops at the cap") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 5; ++i) h.Report(1000 + i * 100, i * 100);
    REQUIRE(h.CapMs() == 120);
    CHECK(h.Estimate(560) == 1560);
    CHECK(h.Advancing(620));
    CHECK(h.Estimate(620) == 1620);
    CHECK(!h.Advancing(621));
    CHECK(h.Estimate(621) == 1620);
    CHECK(h.Estimate(800) == 1620);  // 300 ms without a report: never further than the cap
    // At rate 2 the cap is 120 ms of wall time, 240 ms of video.
    h.SetRate(2.0, 800);
    h.Report(1700, 800);
    CHECK(h.Estimate(1100) == 1700 + 240);
    // The media clock comes back at 1800: 140 ms behind the estimate is
    // within cap x rate (240 ms), so the estimate holds (no step back) until
    // the video has caught up ...
    h.Report(1800, 1100);
    CHECK(h.Estimate(1100) == 1940);
    CHECK(h.Estimate(1150) == 1940);
    // ... and then follows the reports again.
    h.Report(1950, 1150);
    CHECK(h.Estimate(1160) == 1970);
}

TEST_CASE("playhead: a report a little behind never moves the estimate backwards") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 4; ++i) h.Report(i * 100, i * 100);
    CHECK(h.Estimate(490) == 490);
    // The report says 460 at 490 (30 ms behind): the estimate holds at 490 ...
    h.Report(460, 490);
    int64_t last = h.Estimate(490);
    CHECK(last == 490);
    for (int64_t now = 491; now < 600; ++now) {
        const int64_t e = h.Estimate(now);
        CHECK(e >= last);  // never backwards
        last = e;
    }
    CHECK(h.Estimate(519) == 490);  // ... until the reported position catches up (460 + 30)
    CHECK(h.Estimate(530) == 500);  // then it follows the report
    // An early report (ahead of the estimate): the estimate jumps forward to it.
    h.Report(620, 590);
    CHECK(h.Estimate(590) == 620);
}

TEST_CASE("playhead: a report far behind or far ahead is a seek") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 4; ++i) h.Report(5000 + i * 100, i * 100);
    // 150 ms behind is beyond cap x rate (120 ms): taken as it is.
    h.Report(5250, 400);
    CHECK(h.Estimate(400) == 5250);
    // A seek back (the loop jump to the marked range's start).
    h.Report(1000, 450);
    CHECK(h.Estimate(450) == 1000);
    CHECK(h.Estimate(500) == 1050);
    // A seek ahead.
    h.Report(30000, 520);
    CHECK(h.Estimate(560) == 30040);
    // The seek reports came between the timer's reports: they are not
    // report intervals (the cap stays at 100 ms x 1.2).
    CHECK(h.CapMs() == 120);
}

TEST_CASE("playhead: a loop jump repeats cleanly") {
    // Loop over 1000-1500: the player jumps back when a report reaches 1500.
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    int64_t pos = 1000, now = 0;
    int jumps = 0;
    for (int i = 0; i < 40; ++i, now += 100) {
        h.Report(pos, now);
        if (pos >= 1500) {
            h.Seek(now);  // the player is about to jump
            CHECK(h.Estimate(now + 50) == pos);
            pos = 1000;
            h.Report(pos, now + 1);
            CHECK(h.Estimate(now + 1) == 1000);
            ++jumps;
        }
        CHECK(h.Estimate(now + 60) <= pos + 60);
        pos += 100;
    }
    CHECK(jumps >= 5);
    CHECK(h.CapMs() == 120);
}

TEST_CASE("playhead: a rate change holds until the next report, then moves at the new rate") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 3; ++i) h.Report(i * 100, i * 100);
    CHECK(h.Estimate(340) == 340);
    h.SetRate(2.0, 340);
    CHECK(h.rate() == 2.0);
    CHECK(!h.Advancing(360));
    CHECK(h.Estimate(390) == 340);
    h.Report(400, 400);
    CHECK(h.Estimate(450) == 500);
    // A rate of 0 or below (or not a number) is ignored.
    h.SetRate(0, 450);
    h.SetRate(-1, 450);
    h.SetRate(std::nan(""), 450);
    CHECK(h.rate() == 2.0);
    CHECK(h.Estimate(460) == 520);
}

TEST_CASE("playhead: Hold freezes the estimate without going back") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 3; ++i) h.Report(i * 100, i * 100);
    h.Hold(370);
    CHECK(h.Estimate(370) == 370);
    CHECK(h.Estimate(450) == 370);
    CHECK(!h.Advancing(380));
    // The next report is 30 ms behind the frozen estimate: no step back.
    h.Report(340, 380);
    CHECK(h.Estimate(380) == 370);
    CHECK(h.Estimate(420) == 380);
}

TEST_CASE("playhead: noisy reports never move the estimate backwards and it stays near the truth") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    int64_t last = -1;
    double worst = 0;
    for (int64_t now = 0, k = 0; now < 5000; ++now) {
        if (now == k * 100) {
            const int64_t noise = ((k * 37) % 21) - 10;  // -10 .. +10 ms
            h.Report(now + noise, now);
            ++k;
        }
        const int64_t e = h.Estimate(now);
        if (last >= 0) CHECK(e >= last);
        last = e;
        if (now >= 100) worst = std::max(worst, std::abs(double(e - now)));
    }
    CHECK(worst <= 30);
}

TEST_CASE("playhead: a report up to cap x rate behind holds the estimate, so no move is sent again") {
    // A tick at 1056 sent the move to the action after 1050; the media clock
    // then reports 1040 at 1100 (60 ms behind the estimate, no jump).
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 10; ++i) h.Report(i * 100, i * 100);
    CHECK(h.Estimate(1056) == 1056);
    h.Report(1040, 1100);
    CHECK(h.Estimate(1100) == 1100);  // not back to 1040 (the action at 1050 stays passed)
    CHECK(h.Estimate(1159) == 1100);  // held until the video catches up (1040 + 60)
    CHECK(h.Estimate(1170) == 1110);
    // At rate 4 the window is 4 x 120 ms: 60 ms behind holds as well.
    PlayheadExtrapolator r;
    r.SetRate(4.0, 0);
    r.SetPlaying(true);
    for (int i = 0; i <= 10; ++i) r.Report(i * 400, i * 100);
    CHECK(r.Estimate(1100) == 4400);
    r.Report(4340, 1100);
    CHECK(r.Estimate(1100) == 4400);
}

TEST_CASE("playhead: further behind than max(cap x rate, 40 ms) is a seek") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 10; ++i) h.Report(i * 100, i * 100);
    REQUIRE(h.CapMs() == 120);
    h.Report(979, 1100);  // 121 ms behind
    CHECK(h.Estimate(1100) == 979);
    // 10 ms reports: the cap is 20 ms, the window still 40 ms.
    PlayheadExtrapolator f;
    f.SetPlaying(true);
    for (int i = 0; i <= 20; ++i) f.Report(i * 10, i * 10);
    REQUIRE(f.CapMs() == 20);
    f.Report(170, 210);  // 40 ms behind: held
    CHECK(f.Estimate(210) == 210);
    f.Report(100, 230);  // 110 ms behind: a seek
    CHECK(f.Estimate(230) == 100);
}

TEST_CASE("playhead: Seek() takes the next report as it is, also a little behind") {
    PlayheadExtrapolator h;
    h.SetPlaying(true);
    for (int i = 0; i <= 4; ++i) h.Report(i * 100, i * 100);
    h.Seek(450);  // the page seeks 30 ms back (to 420)
    CHECK(!h.Advancing(455));
    CHECK(h.Estimate(455) == 450);
    h.Report(420, 460);
    CHECK(h.Estimate(460) == 420);
    CHECK(h.Estimate(500) == 460);
    // Only the next report: the one after it holds a step back again.
    h.Report(480, 540);
    CHECK(h.Estimate(560) == 500);
    h.Report(470, 560);
    CHECK(h.Estimate(560) == 500);
    CHECK(h.CapMs() == 120);  // the seek report gave no interval
}

TEST_CASE("playhead: never more than max(cap x rate, 40 ms) ahead of the latest report") {
    for (const double rate : {0.5, 1.0, 2.0}) {
        PlayheadExtrapolator h;
        h.SetRate(rate, 0);
        h.SetPlaying(true);
        double video = 0;
        int64_t next = 0, last = -1;
        for (int64_t now = 0, k = 0; now < 20000; ++now) {
            video += rate;
            bool reported = false;
            if (now == next) {
                // Hiccups: every 7th report 60 ms behind in position, every
                // 11th after the media clock lost 250 ms.
                if (k % 11 == 5) video -= 250 * rate;
                const int64_t pos = int64_t(video) - (k % 7 == 3 ? 60 : 0);
                const int64_t est = h.Estimate(now);
                h.Report(pos, now);
                const int64_t window = std::max<int64_t>(40, std::llround(double(h.CapMs()) * rate));
                // Backwards only for a report further behind than the window.
                if (h.Estimate(now) < est) CHECK(est - pos > window);
                reported = true;
                ++k;
                next = now + 100 + (k % 3);
            }
            const int64_t e = h.Estimate(now);
            CHECK(e - h.last_report_ms() <= std::max<int64_t>(40, std::llround(double(h.CapMs()) * rate)));
            if (last >= 0 && !reported) CHECK(e >= last);
            last = e;
        }
    }
}
