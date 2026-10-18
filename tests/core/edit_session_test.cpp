#include <doctest/doctest.h>

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "samn/edit_session.h"
#include "samn/range_edit.h"

using samn::Action;
using samn::DiffAxis;
using samn::EditSession;
using samn::HistoryLimits;

namespace {

std::vector<Action> Saw(size_t n) {
    std::vector<Action> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(Action{static_cast<int64_t>(i) * 100, i % 2 ? 90 : 10});
    return out;
}

bool Equal(const std::vector<Action>& a, const std::vector<Action>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].at != b[i].at || a[i].pos != b[i].pos) return false;
    return true;
}

}  // namespace

TEST_CASE("edit: the patch holds only the changed window") {
    const auto before = Saw(1000);
    auto after = before;
    after[500].pos = 50;
    const auto patch = DiffAxis("stroke", before, after);
    CHECK(patch.first == 500);
    REQUIRE(patch.removed.size() == 1);
    REQUIRE(patch.added.size() == 1);
    CHECK(patch.added[0].pos == 50);

    after.erase(after.begin() + 10, after.begin() + 20);
    const auto cut = DiffAxis("stroke", before, after);
    CHECK(cut.first == 10);
    CHECK(cut.removed.size() == 491);  // 10..500, the edited point included
    CHECK(cut.added.size() == 481);

    const auto none = DiffAxis("stroke", before, before);
    CHECK(none.removed.empty());
    CHECK(none.added.empty());
}

TEST_CASE("edit: move, add, delete undo and redo exactly") {
    EditSession s({{"stroke", Saw(5)}});
    const auto original = s.Axis("stroke");

    REQUIRE(s.MovePoint("stroke", 2, 230, 55, 0));
    CHECK(s.Axis("stroke")[2].at == 230);
    CHECK(s.Axis("stroke")[2].pos == 55);
    const size_t added = s.AddPoint("stroke", 150, 70);
    CHECK(added == 2);
    CHECK(s.Axis("stroke").size() == 6);
    REQUIRE(s.DeletePoint("stroke", 0));
    CHECK(s.UndoCount() == 3);
    CHECK(s.UndoLabel() == "delete point");

    const auto edited = s.Axis("stroke");
    REQUIRE(s.Undo());
    REQUIRE(s.Undo());
    REQUIRE(s.Undo());
    CHECK(Equal(s.Axis("stroke"), original));
    CHECK(!s.Undo());
    REQUIRE(s.Redo());
    REQUIRE(s.Redo());
    REQUIRE(s.Redo());
    CHECK(Equal(s.Axis("stroke"), edited));
    CHECK(!s.Redo());

    // A new edit drops the redo branch.
    s.Undo();
    s.AddPoint("stroke", 1000, 0);
    CHECK(!s.CanRedo());
}

TEST_CASE("edit: 0.7 point rules") {
    EditSession s({{"stroke", {{0, 0}, {100, 100}, {200, 0}}}});
    // Stays between its neighbours, 1 ms apart.
    s.MovePoint("stroke", 1, 500, 50, 0);
    CHECK(s.Axis("stroke")[1].at == 199);
    s.MovePoint("stroke", 1, -40, 50, 0);
    CHECK(s.Axis("stroke")[1].at == 1);
    // Position clamped to 0..100; the last point stays inside the video.
    s.MovePoint("stroke", 2, 9000, 140, 1000);
    CHECK(s.Axis("stroke")[2].at == 1000);
    CHECK(s.Axis("stroke")[2].pos == 100);
    s.MovePoint("stroke", 0, -5, -3, 1000);
    CHECK(s.Axis("stroke")[0].at == 0);
    CHECK(s.Axis("stroke")[0].pos == 0);
    // A point on an existing time takes the new position, no duplicate.
    CHECK(s.AddPoint("stroke", 1000, 30) == 2);
    CHECK(s.Axis("stroke").size() == 3);
    CHECK(s.Axis("stroke")[2].pos == 30);
    // At least 2 points remain.
    CHECK(s.DeletePoint("stroke", 0));
    CHECK(!s.DeletePoint("stroke", 0));
    CHECK(s.Axis("stroke").size() == 2);
    CHECK(!s.DeletePoint("stroke", 7));
    CHECK(!s.MovePoint("stroke", 7, 0, 0, 0));
}

TEST_CASE("edit: a drag is one step; a drag back to the start is none") {
    EditSession s({{"stroke", Saw(10)}});
    s.Begin("move point");
    for (int x = 0; x < 50; ++x) s.MovePoint("stroke", 4, 400 + x, 10 + x, 0);
    CHECK(s.InTransaction());
    CHECK(!s.CanUndo());  // no undo in the middle of a drag
    CHECK(s.Commit());
    CHECK(s.UndoCount() == 1);
    CHECK(s.Axis("stroke")[4].pos == 59);

    s.Begin("move point");
    s.MovePoint("stroke", 4, 420, 20, 0);
    s.MovePoint("stroke", 4, 449, 59, 0);
    CHECK(!s.Commit());
    CHECK(s.UndoCount() == 1);

    s.Begin("add point");
    s.AddPoint("stroke", 450, 5);
    s.Rollback();
    CHECK(s.Axis("stroke").size() == 10);
    CHECK(s.UndoCount() == 1);
}

TEST_CASE("edit: a multi-axis edit is all or nothing") {
    EditSession s({{"stroke", Saw(4)}, {"vibration", Saw(4)}});
    s.Begin("invert all");
    for (const char* axis : {"stroke", "vibration"}) {
        auto inv = s.Axis(axis);
        for (auto& a : inv) a.pos = 100 - a.pos;
        s.Replace(axis, inv, "invert");
    }
    REQUIRE(s.Commit());
    CHECK(s.UndoCount() == 1);
    CHECK(s.Axis("vibration")[0].pos == 90);
    s.Undo();
    CHECK(s.Axis("stroke")[0].pos == 10);
    CHECK(s.Axis("vibration")[0].pos == 10);
}

TEST_CASE("edit: range tools go through the history") {
    EditSession s({{"stroke", Saw(50)}});
    const auto original = s.Axis("stroke");
    s.Replace("stroke", samn::DeleteRange(s.Axis("stroke"), 1000, 2000), "delete range");
    CHECK(s.Axis("stroke").size() == 39);
    s.Replace("stroke", samn::ScaleRangePos(s.Axis("stroke"), 0, 5000, 0.5), "scale range");
    CHECK(s.UndoCount() == 2);
    s.Undo();
    s.Undo();
    CHECK(Equal(s.Axis("stroke"), original));
}

TEST_CASE("edit: the history has a hard limit, oldest first (D-142)") {
    EditSession s({{"stroke", Saw(10)}}, HistoryLimits{5, size_t{1} << 20});
    for (int i = 0; i < 12; ++i) s.MovePoint("stroke", 3, 300, i, 0);
    CHECK(s.UndoCount() == 5);
    while (s.Undo()) {
    }
    CHECK(s.Axis("stroke")[3].pos == 6);  // the 7th edit is the oldest kept

    // Byte limit: one whole-axis step of 100 000 points is about 3.2 MB (16-byte actions, both sides).
    EditSession big({{"stroke", Saw(100000)}}, HistoryLimits{200, size_t{4} << 20});
    for (int i = 0; i < 6; ++i) {
        auto inv = big.Axis("stroke");
        for (auto& a : inv) a.pos = 100 - a.pos;
        big.Replace("stroke", inv, "invert");
    }
    CHECK(big.HistoryBytes() <= (size_t{4} << 20));
    CHECK(big.UndoCount() >= 1);
    CHECK(big.UndoCount() < 6);
}

TEST_CASE("edit: dirty follows the saved state through undo and trimming") {
    EditSession s({{"stroke", Saw(10)}}, HistoryLimits{3, size_t{1} << 20});
    CHECK(!s.dirty());
    s.MovePoint("stroke", 1, 100, 40, 0);
    CHECK(s.dirty());
    s.MarkSaved();
    CHECK(!s.dirty());
    s.Undo();
    CHECK(s.dirty());
    s.Redo();
    CHECK(!s.dirty());

    // Saved at the start, then the start falls out of the history: undoing
    // everything does not reach the saved state, so it stays dirty.
    EditSession t({{"stroke", Saw(10)}}, HistoryLimits{2, size_t{1} << 20});
    t.MarkSaved();
    for (int i = 0; i < 4; ++i) t.MovePoint("stroke", 1, 100, i, 0);
    while (t.Undo()) {
    }
    CHECK(t.dirty());
}

TEST_CASE("edit: random edits always undo back to the start") {
    std::mt19937 rng(7);
    EditSession s({{"stroke", Saw(200)}, {"suction", Saw(30)}}, HistoryLimits{1000, size_t{64} << 20});
    const auto stroke = s.Axis("stroke");
    const auto suction = s.Axis("suction");
    for (int i = 0; i < 400; ++i) {
        const std::string axis = rng() % 3 ? "stroke" : "suction";
        const size_t n = s.Axis(axis).size();
        switch (rng() % 4) {
            case 0: s.MovePoint(axis, rng() % n, rng() % 30000, rng() % 101, 30000); break;
            case 1: s.AddPoint(axis, rng() % 30000, rng() % 101); break;
            case 2: s.DeletePoint(axis, rng() % n); break;
            default: {
                const int64_t a = rng() % 20000;
                s.Replace(axis, samn::ScaleRangePos(s.Axis(axis), a, a + 3000, 0.7), "scale");
            }
        }
    }
    while (s.Undo()) {
    }
    CHECK(Equal(s.Axis("stroke"), stroke));
    CHECK(Equal(s.Axis("suction"), suction));
}

TEST_CASE("edit: one point edit on 100 000 points is cheap (D-083)") {
    EditSession s({{"stroke", Saw(100000)}});
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) s.MovePoint("stroke", 50000, 5000000 + i, i % 101, 0);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    // Each step stores one point, not the axis.
    CHECK(s.HistoryBytes() < 200 * 512);
    MESSAGE("200 single-point steps on 100k points: " << ms << " ms");
}
