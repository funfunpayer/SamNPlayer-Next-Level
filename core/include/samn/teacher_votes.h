#pragma once
// Votes between teachers (D-155 step 4, Owner 20 Oct): several teachers look
// at the same frame; boxes of one class that overlap are one object. Where
// every teacher that can see the class found it, the proposal is plain;
// where only some did, it is kept but marked uncertain ("Next uncertain"
// jumps there). A class a teacher cannot see at all (NudeNet has no hands)
// is no vote against.

#include <string>
#include <vector>

#include "samn/teacher.h"

namespace samn {

struct TeacherVote {
    std::string teacher;               // e.g. "nudenet", a server model's name
    std::vector<std::string> classes;  // what it can see at all
    std::vector<TeacherBox> boxes;     // what it saw in this frame
};

struct VotedBox {
    TeacherBox box;                     // the group's mean box; uncertain when not all agreed
    std::vector<std::string> teachers;  // who saw it
    int voters = 0;                     // who could have seen it
};

// Groups the boxes by class and overlap (IoU at least min_iou, one box per
// teacher in a group) and averages each group. Sorted by class, then the
// number of teachers that agree (most first).
std::vector<VotedBox> VoteTeacherBoxes(const std::vector<TeacherVote>& votes, double min_iou = 0.3);

// The boxes of a vote, for ApplyTeacherBoxes.
std::vector<TeacherBox> VotedBoxes(const std::vector<VotedBox>& voted);

}  // namespace samn
