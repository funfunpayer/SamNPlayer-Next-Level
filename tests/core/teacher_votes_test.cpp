// Votes between teachers (D-155 step 4): agreement is plain, a box only some
// teachers saw is kept but uncertain, a class a teacher cannot see is no
// vote against.
#include <doctest/doctest.h>

#include "samn/teacher_votes.h"

using samn::LabelBox;
using samn::TeacherBox;
using samn::TeacherVote;

namespace {
const std::vector<std::string> kNudeNet{"penis", "vagina", "breast", "face"};
const std::vector<std::string> kServer{"glans", "penis", "mouth", "hand", "breast", "face"};
}  // namespace

TEST_CASE("teachers that agree give one plain box, the mean of theirs") {
    const std::vector<TeacherVote> votes{
        {"nudenet", kNudeNet, {{"penis", {100, 100, 40, 80}}}},
        {"vision-a", kServer, {{"penis", {110, 104, 40, 80}}}},
    };
    const auto v = samn::VoteTeacherBoxes(votes);
    REQUIRE(v.size() == 1);
    CHECK(v[0].box.cls == "penis");
    CHECK_FALSE(v[0].box.uncertain);
    CHECK(v[0].voters == 2);
    CHECK(v[0].teachers == std::vector<std::string>{"nudenet", "vision-a"});
    CHECK(v[0].box.box.x == doctest::Approx(105));
    CHECK(v[0].box.box.y == doctest::Approx(102));
}

TEST_CASE("a box only one teacher saw is kept, uncertain; one it cannot see is not") {
    const std::vector<TeacherVote> votes{
        {"nudenet", kNudeNet, {{"face", {10, 10, 30, 30}}}},
        {"vision-a", kServer,
         {{"face", {300, 10, 30, 30}},  // elsewhere: another face
          {"hand", {200, 200, 20, 20}}}},
    };
    const auto v = samn::VoteTeacherBoxes(votes);
    REQUIRE(v.size() == 3);
    // Two faces, each seen by one of two teachers that can see faces.
    CHECK(v[0].box.cls == "face");
    CHECK(v[0].box.uncertain);
    CHECK(v[1].box.cls == "face");
    CHECK(v[1].box.uncertain);
    // NudeNet has no hands: the hand is plain.
    CHECK(v[2].box.cls == "hand");
    CHECK(v[2].voters == 1);
    CHECK_FALSE(v[2].box.uncertain);
}

TEST_CASE("one teacher's two boxes stay two objects; a lone teacher is never uncertain") {
    const std::vector<TeacherVote> one{{"nudenet", kNudeNet, {{"breast", {0, 0, 50, 50}}, {"breast", {10, 0, 50, 50}}}}};
    const auto v = samn::VoteTeacherBoxes(one);
    REQUIRE(v.size() == 2);
    CHECK_FALSE(v[0].box.uncertain);
    CHECK_FALSE(v[1].box.uncertain);
    CHECK(samn::VoteTeacherBoxes({}).empty());
    CHECK(samn::VotedBoxes(v).size() == 2);
}

TEST_CASE("an uncertain vote becomes an uncertain key") {
    samn::LabelProject p;
    p.duration_ms = 10000;
    TeacherBox b{"face", {10, 10, 30, 30}};
    b.uncertain = true;
    REQUIRE(samn::ApplyTeacherBoxes(p, 1000, {b}, "teachers") == 1);
    const auto* k = p.tracks.at(0).KeyAt(1000, 0);
    REQUIRE(k);
    CHECK(k->uncertain);
    CHECK_FALSE(k->confirmed);
    CHECK(samn::NextUncertain(p, 0).has_value());
}
