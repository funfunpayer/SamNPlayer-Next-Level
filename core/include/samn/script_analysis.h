#pragma once
// What a script is made of: motion-state shares and one plain-English line.
// Port of SamNPlayer 0.7 AnalyzeScript / summarizeStates (app_analysis.go)
// and the chapter labels in playback.js (CHAPTER_LABELS, formatMs).
#include <map>
#include <string>
#include <vector>

#include "samn/funscript.h"
#include "samn/motion_chapters.h"
#include "samn/motion_classify.h"

namespace samn {

struct ScriptAnalysis {
    std::vector<MotionSegment> segments;
    std::map<std::string, double> share_by_state;  // fraction of total_ms, 0..1
    double total_ms = 0;
    std::string summary;
    // "no script loaded (or too few points)" when there are fewer than 4
    // actions. Empty on success.
    std::string error;
};

// Actions in time order (stable when two times match). Classify uses
// StaticSpeed 4, ChangeRatio 0.25, MinSegmentMs 250 and SmoothRadius 2.
// Fewer than 4 actions sets error and leaves the rest empty.
ScriptAnalysis AnalyzeScriptActions(const std::vector<Action>& actions);

// One line for the Playback analysis box. Shares under 2% are left out;
// at most four parts, largest first. Equal shares are ordered by state
// name so the line does not depend on map order (0.7's map walk is random
// when two shares match).
std::string SummarizeMotionStates(const std::map<std::string, double>& shares, double total_ms);

// 0.7 CHAPTER_LABELS. An unknown kind is returned as itself (a stored
// chapter name, or "chapter" when that name was empty).
std::string MotionChapterLabel(const std::string& kind);

// 0.7 formatMs: round to whole seconds, then m:ss. Minutes are not wrapped
// at 60. A negative time is shown as 0:00.
std::string FormatAnalysisClock(double ms);

}  // namespace samn
