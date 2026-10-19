#pragma once
// Scenes of a script: its chapters, each with a Sam Neo 2 feel and a
// suggested feel (0.7 cmd/gui-wails/app_feel.go SuggestChapterScenes /
// SaveChapterScenes, generator/scene_proposals.go). Shown in the Editor
// (D-145). Nothing here applies a suggestion; the user confirms it.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "samn/marks.h"
#include "samn/mapper.h"
#include "samn/samn_file.h"

namespace samn {

// The feels a scene can have; "" is Classic (suction follows the depth).
const std::vector<std::string>& SceneFeelNames();  // "", oral, intercourse, breast, hand

// One proposal of `<clip>.scene.json` (written by 0.7's scene_roles.py).
struct SceneProposal {
    int64_t t_ms = 0;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string scene_type;  // blowjob | handjob | titjob | penetration, "" = unknown
    double confidence = 0;
};

// `<clip>.scene.json` beside the video (0.7 SceneProposalsPathBesideVideo).
std::filesystem::path SceneProposalsPath(const std::filesystem::path& video);

// Version 1 only, a frame size, and a primary box inside the frame per
// proposal (others are dropped); sorted by t_ms. nullopt with *error set when
// the file cannot be read or is not a scene file.
std::optional<std::vector<SceneProposal>> LoadSceneProposals(const std::filesystem::path& path,
                                                             std::string* error = nullptr);

// The contact mark's class as a scene feel: mouth/lips -> oral,
// nipple/breast -> breast, hand -> hand, else "".
std::string ContactClassScene(const std::string& cls);

struct ChapterScene {
    int index = 0;
    std::string name;
    int64_t start_ms = 0, end_ms = 0;
    std::string feel;      // the saved feel ("" = Classic)
    bool has_feel = false;  // a feel is saved for exactly this range
    std::string suggested;  // "" = no suggestion
    std::string source;     // "scene detection" | "contact mark"
    double confidence = 0;  // 0..1
};

// The chapters as scenes (sorted, open ends closed as on the timeline) with
// their saved feel and a suggestion: scene detection first (the feel with the
// most overlap x confidence; confidence = its share of the overlapped time),
// else the contact mark's class for the whole script at 0.5.
std::vector<ChapterScene> ChapterScenes(const std::vector<Chapter>& chapters, int64_t total_ms,
                                        const std::vector<SceneFeelRange>& saved,
                                        const std::vector<SceneProposal>& proposals,
                                        const std::optional<ContactMarks>& marks);

// The ranges to store: one per scene that has a feel (0.7 saveFeels).
std::vector<SceneFeelRange> SceneRangesOf(const std::vector<ChapterScene>& scenes);

}  // namespace samn
