#include "samn/teacher_votes.h"

#include <algorithm>

#include "samn/label_track.h"

namespace samn {

namespace {
struct Group {
    std::string cls;
    std::vector<size_t> teachers;  // indices into votes
    std::vector<LabelBox> boxes;
    LabelBox Mean() const {
        LabelBox m;
        for (const auto& b : boxes) {
            m.x += b.x;
            m.y += b.y;
            m.w += b.w;
            m.h += b.h;
        }
        const double n = double(boxes.size());
        return {m.x / n, m.y / n, m.w / n, m.h / n};
    }
};
}  // namespace

std::vector<VotedBox> VoteTeacherBoxes(const std::vector<TeacherVote>& votes, double min_iou) {
    std::vector<Group> groups;
    for (size_t t = 0; t < votes.size(); ++t) {
        for (const auto& b : votes[t].boxes) {
            Group* best = nullptr;
            double best_iou = min_iou;
            for (auto& g : groups) {
                if (g.cls != b.cls || std::find(g.teachers.begin(), g.teachers.end(), t) != g.teachers.end()) continue;
                const double iou = BoxIoU(g.Mean(), b.box);
                if (iou >= best_iou) {
                    best = &g;
                    best_iou = iou;
                }
            }
            if (!best) {
                groups.push_back({b.cls, {}, {}});
                best = &groups.back();
            }
            best->teachers.push_back(t);
            best->boxes.push_back(b.box);
        }
    }
    std::vector<VotedBox> out;
    for (const auto& g : groups) {
        VotedBox v;
        v.box.cls = g.cls;
        v.box.box = g.Mean();
        for (size_t t = 0; t < votes.size(); ++t) {
            const auto& c = votes[t].classes;
            const bool sees = std::find(c.begin(), c.end(), g.cls) != c.end() ||
                              std::find(g.teachers.begin(), g.teachers.end(), t) != g.teachers.end();
            if (sees) ++v.voters;
        }
        for (size_t t : g.teachers) v.teachers.push_back(votes[t].teacher);
        v.box.uncertain = int(g.teachers.size()) < v.voters;
        out.push_back(std::move(v));
    }
    std::stable_sort(out.begin(), out.end(), [](const VotedBox& a, const VotedBox& b) {
        if (a.box.cls != b.box.cls) return a.box.cls < b.box.cls;
        return a.teachers.size() > b.teachers.size();
    });
    return out;
}

std::vector<TeacherBox> VotedBoxes(const std::vector<VotedBox>& voted) {
    std::vector<TeacherBox> out;
    out.reserve(voted.size());
    for (const auto& v : voted) out.push_back(v.box);
    return out;
}

}  // namespace samn
