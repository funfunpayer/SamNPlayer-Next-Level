// Parity tool (P3.1, not a test): writes the synthetic clips into a folder
// and the C++ track of each clip and option set as CSV, for comparison with
// 0.7's Go TrackROI on the same files (docs/benchmarks/P3_1_TRACK_PARITY.md).
//   engine_parity_dump <folder>

#include <fstream>
#include <iostream>

#include "samn/engine/track_box.h"
#include "synthetic_clips.h"

using namespace samn::engine;
namespace clips = samn::engine::testclips;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: engine_parity_dump <folder>\n";
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    const bool ok = clips::WriteMoving(dir / "moving.mp4", 8, 80, 1.0) &&
                    clips::WriteMoving(dir / "long.mp4", 300, 80, 0.7) &&
                    clips::WritePanning(dir / "panning.mp4", 10, 80) &&
                    clips::WriteHardCut(dir / "cut.mp4", 8, 100, 80);
    if (!ok) return 1;
    struct Set {
        const char* name;
        bool cam, cut, mem;
    };
    const Set sets[] = {{"plain", false, false, false}, {"cam", true, false, false},
                        {"cutmem", false, true, true},   {"all", true, true, true}};
    for (const char* clip : {"moving", "long", "panning", "cut"}) {
        for (const Set& s : sets) {
            TrackOptions o;
            o.camera_compensation = s.cam;
            o.scene_cuts = s.cut;
            o.appearance_memory = s.mem;
            o.axis = TrackAxis::kY;
            std::string error;
            auto r = TrackBox(dir / (std::string(clip) + ".mp4"), cv::Rect(100, 40, 120, 160), o, &error);
            if (!r) {
                std::cerr << clip << ": " << error << "\n";
                return 1;
            }
            std::ofstream out(dir / (std::string(clip) + "_" + s.name + "_cpp.csv"));
            out.precision(17);
            for (size_t i = 0; i < r->positions.size(); ++i) out << r->xs[i] << ',' << r->ys[i] << '\n';
            std::cout << clip << ' ' << s.name << " frames=" << r->stats.total_frames
                      << " lost=" << r->stats.lost_frames << " cuts=" << r->scene_cuts.size() << "\n";
        }
    }
}
