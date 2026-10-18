// samn_compare: the comparison controller on the command line (P3.8).
//   samn_compare <manifest.json> <out-folder>
// Writes report.json and report.md (numbers only, no media) into the folder.

#include <filesystem>
#include <iostream>

#include "samn/engine/compare.h"
#include "samn/fileio.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: samn_compare <manifest.json> <out-folder>\n";
        return 2;
    }
    using namespace samn::engine;
    CompareSettings settings;
    std::string error;
    const auto clips = LoadManifest(std::filesystem::u8path(argv[1]), &settings, &error);
    if (!clips) {
        std::cerr << "samn_compare: " << error << "\n";
        return 2;
    }
    std::vector<ClipReport> reports;
    int failed = 0;
    for (const CompareClip& c : *clips) {
        std::cerr << "clip " << c.name << " ...\n";
        reports.push_back(RunClip(c, settings));
        if (!reports.back().error.empty()) {
            ++failed;
            std::cerr << "  error: " << reports.back().error << "\n";
        }
    }
    const std::filesystem::path out = std::filesystem::u8path(argv[2]);
    std::filesystem::create_directories(out);
    samn::WriteFileAtomic(out / "report.json", ReportJson(reports, settings).dump(2));
    const std::string md = ReportMarkdown(reports, settings);
    samn::WriteFileAtomic(out / "report.md", md);
    std::cout << md;
    return failed ? 1 : 0;
}
