#pragma once
// Label tracks for AI Train (D-155 §2.2): each label (glans, mouth, hand_1 …)
// is a track through the clip, not a box on one picture. Keys hold a box or
// "absent" at a time; between two box keys the box is interpolated; after an
// absent key the label is gone until the next box key. A key is confirmed when
// the user set or accepted it; teacher and tracker keys start unconfirmed.
// The project lives beside the clip as <clip>.labels.json.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace samn {

// Video pixels.
struct LabelBox {
    double x = 0, y = 0, w = 0, h = 0;
};

struct LabelKey {
    int64_t ms = 0;
    bool absent = false;  // the label is not visible from here on
    // With absent: it is there but covered (by a hand, a body, the edge …);
    // those frames teach nothing, so they are left out of the samples.
    bool covered = false;
    LabelBox box;         // ignored when absent
    bool confirmed = false;
    std::string source = "user";  // "user", "tracker", "teacher:<name>"
};

// What a track says at one time.
struct LabelState {
    bool present = false;
    bool covered = false;  // absent because something covers it
    LabelBox box;
    // Between two confirmed keys (or on one): the user has checked this stretch.
    bool reviewed = false;
};

// A track is one object of a class: "hand" and "hand 2" are two hands; the
// model learns the class, the number only tells the objects apart.
class LabelTrack {
public:
    explicit LabelTrack(std::string label = {}, std::string cls = {});
    const std::string& label() const { return label_; }
    const std::string& cls() const { return cls_; }
    const std::vector<LabelKey>& keys() const { return keys_; }

    // Adds or replaces the key at the same time (within tolerance_ms).
    void SetKey(const LabelKey& key, int64_t tolerance_ms = 0);
    // Removes the key nearest to ms within tolerance_ms; false when none.
    bool RemoveKey(int64_t ms, int64_t tolerance_ms = 0);
    // Marks every key in [from_ms, to_ms] confirmed; returns how many changed.
    int Confirm(int64_t from_ms, int64_t to_ms);
    // The key at ms (within tolerance), if any.
    const LabelKey* KeyAt(int64_t ms, int64_t tolerance_ms = 0) const;

    LabelState At(int64_t ms) const;

private:
    std::string label_;
    std::string cls_;
    std::vector<LabelKey> keys_;  // sorted by ms, unique ms
};

struct LabelProject {
    std::string video;  // file name of the clip, relative to the project file
    int64_t duration_ms = 0;
    std::vector<LabelTrack> tracks;

    LabelTrack* Track(const std::string& label);
    LabelTrack& AddTrack(const std::string& label);  // returns the existing one when present
    // A new object of the class: "hand", then "hand 2", "hand 3" (the lowest free number).
    LabelTrack& AddObject(const std::string& cls);
    // The classes in the order they first appear.
    std::vector<std::string> Classes() const;
    int ObjectCount(const std::string& cls) const;
};

// "hand 2" → "hand"; "hand" → "hand".
std::string LabelClassOf(const std::string& label);

// <clip>.labels.json beside the clip.
std::filesystem::path LabelProjectPath(const std::filesystem::path& clip);
std::string SerializeLabelProject(const LabelProject& p);
// Throws std::runtime_error on invalid JSON or a wrong kind.
LabelProject ParseLabelProject(const std::string& json);
void SaveLabelProject(const std::filesystem::path& path, const LabelProject& p);
LabelProject LoadLabelProject(const std::filesystem::path& path);

// One training sample: a frame and the boxes of every label reviewed there
// (an empty box list is a negative sample: reviewed, nothing visible).
struct LabelSample {
    int64_t ms = 0;
    struct Item {
        int class_id = 0;
        LabelBox box;
    };
    std::vector<Item> items;
};

// Every every_ms of the clip, the frames where at least one track is reviewed
// and none is covered there (a covered object would teach "nothing here");
// class ids are the indices of the tracks' classes in `classes` (tracks of
// other classes are skipped).
std::vector<LabelSample> ExportSamples(const LabelProject& p, const std::vector<std::string>& classes,
                                       int64_t every_ms);

// "class cx cy w h" lines, normalised to the frame size, boxes clipped to it.
std::string YoloLines(const LabelSample& s, double frame_w, double frame_h);

}  // namespace samn
