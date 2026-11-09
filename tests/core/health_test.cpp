#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "samn/health.h"

using namespace samn::health;

namespace {
Gpu G(const char* vendor, const char* name, std::uint64_t mb) {
    Gpu g;
    g.vendor = vendor;
    g.name = name;
    g.vram_mb = mb;
    g.kind = ClassifyKind(vendor, mb);
    return g;
}
}  // namespace

TEST_CASE("health: vendor ids and texts") {
    CHECK(VendorFromPciId(0x10DE) == "NVIDIA");
    CHECK(VendorFromPciId(0x1002) == "AMD");
    CHECK(VendorFromPciId(0x1022) == "AMD");
    CHECK(VendorFromPciId(0x8086) == "Intel");
    CHECK(VendorFromPciId(0x1414) == "Microsoft");
    CHECK(VendorFromPciId(0x1234) == "Other");
    CHECK(VendorFromText("NVIDIA Corporation") == "NVIDIA");
    CHECK(VendorFromText("Advanced Micro Devices, Inc. [AMD/ATI]") == "AMD");
    CHECK(VendorFromText("Intel(R) Corporation") == "Intel");
    CHECK(VendorFromText("Contoso Graphics") == "Other");
}

TEST_CASE("health: integrated or dedicated") {
    CHECK(ClassifyKind("Intel", 128) == GpuKind::kIntegrated);
    CHECK(ClassifyKind("Intel", 1024) == GpuKind::kIntegrated);
    CHECK(ClassifyKind("Intel", 8192) == GpuKind::kDiscrete);  // Arc
    CHECK(ClassifyKind("AMD", 512) == GpuKind::kIntegrated);
    CHECK(ClassifyKind("AMD", 8192) == GpuKind::kDiscrete);
    CHECK(ClassifyKind("NVIDIA", 0) == GpuKind::kDiscrete);
    CHECK(ClassifyKind("Microsoft", 0) == GpuKind::kSoftware);
    CHECK(ClassifyKind("Other", 0, true) == GpuKind::kSoftware);
}

TEST_CASE("health: AI provider, dedicated first, then iGPU, then CPU") {
    const Gpu igpu = G("Intel", "Intel UHD Graphics 770", 128);
    const Gpu rtx = G("NVIDIA", "GeForce RTX 3060", 12288);
    const Gpu rx = G("AMD", "Radeon RX 6700 XT", 12288);

    // The iGPU listed first never wins over the card.
    auto r = RecommendAi({igpu, rtx}, Platform::kWindows);
    CHECK(r.provider == "CUDA");
    CHECK(r.device == "GeForce RTX 3060");
    CHECK(r.gpu);

    CHECK(RecommendAi({rx, rtx}, Platform::kLinux).device == "GeForce RTX 3060");  // NVIDIA ahead of a bigger-memory rival
    CHECK(RecommendAi({rx}, Platform::kWindows).provider == "DirectML");
    CHECK(RecommendAi({rx}, Platform::kLinux).provider == "ROCm");
    CHECK(RecommendAi({G("Intel", "Intel Arc A770", 16384)}, Platform::kLinux).provider == "OpenVINO");
    CHECK(RecommendAi({G("Intel", "Intel Arc A770", 16384)}, Platform::kWindows).provider == "DirectML");

    // Two dedicated cards of one vendor: the bigger one.
    CHECK(RecommendAi({G("NVIDIA", "Small", 4096), G("NVIDIA", "Big", 24576)}, Platform::kLinux).device == "Big");

    r = RecommendAi({igpu}, Platform::kLinux);
    CHECK(r.provider == "OpenVINO");
    CHECK(r.device == "Intel UHD Graphics 770");
    CHECK(RecommendAi({G("AMD", "Radeon Graphics", 512)}, Platform::kWindows).provider == "DirectML");
    CHECK(RecommendAi({G("AMD", "Radeon Graphics", 512)}, Platform::kLinux).provider == "CPU");

    r = RecommendAi({G("Microsoft", "Microsoft Basic Render Driver", 0)}, Platform::kWindows);
    CHECK(r.provider == "CPU");
    CHECK(!r.gpu);
    CHECK(r.device.empty());
    CHECK(RecommendAi({}, Platform::kWindows).provider == "CPU");
    CHECK(RecommendAi({}, Platform::kMac).provider == "CPU");
    CHECK(RecommendAi({G("Apple", "Apple M2", 0)}, Platform::kMac).provider == "CoreML");
}

TEST_CASE("health: video decode path") {
    const auto with = RecommendVideoDecode({G("Intel", "UHD", 128), G("NVIDIA", "RTX", 8192)}, true, true);
    CHECK(with.gpu_decode);
    CHECK(with.device == "RTX");
    CHECK(with.h264 == "GPU (RTX)");
    CHECK(with.av1 == "CPU (dav1d)");
    const auto igpu = RecommendVideoDecode({G("Intel", "UHD", 128)}, true, true);
    CHECK(igpu.device == "UHD");
    const auto none = RecommendVideoDecode({}, true, false);
    CHECK(!none.gpu_decode);
    CHECK(none.h264 == "CPU");
    CHECK(none.av1 == "not available");
    CHECK(RecommendVideoDecode({}, false, false).h264 == "not available");
    CHECK(!RecommendVideoDecode({G("Microsoft", "Basic", 0)}, true, true).gpu_decode);
}

TEST_CASE("health: lspci -mm -nn -D parser on a synthetic listing") {
    const std::string text =
        "0000:00:00.0 \"Host bridge [0600]\" \"Intel Corporation [8086]\" \"Device [1234]\" -r01 \"Contoso [1111]\" \"Board [2222]\"\n"
        "0000:00:02.0 \"VGA compatible controller [0300]\" \"Intel Corporation [8086]\" \"AlderLake-S GT1 [UHD Graphics 770] [4680]\" -r0c \"Contoso [1111]\" \"Board [3333]\"\n"
        "0000:01:00.0 \"VGA compatible controller [0300]\" \"NVIDIA Corporation [10de]\" \"GA106 [GeForce RTX 3060] [2503]\" -ra1 \"Contoso [1111]\" \"Card [4444]\"\r\n"
        "0000:01:00.1 \"Audio device [0403]\" \"NVIDIA Corporation [10de]\" \"GA106 High Definition Audio Controller [228e]\" -ra1 \"Contoso [1111]\" \"Card [4444]\"\n"
        "0000:03:00.0 \"3D controller [0302]\" \"Advanced Micro Devices, Inc. [AMD/ATI] [1002]\" \"Compute Part [9999]\" \"Contoso [1111]\" \"Card [5555]\"\n"
        "garbage line\n"
        "\n";
    const auto d = ParseLspciMm(text);
    REQUIRE(d.size() == 3);
    CHECK(d[0].slot == "0000:00:02.0");
    CHECK(d[0].vendor == "Intel");
    CHECK(d[0].name == "UHD Graphics 770");
    CHECK(d[1].slot == "0000:01:00.0");
    CHECK(d[1].vendor_id == 0x10DE);
    CHECK(d[1].name == "GeForce RTX 3060");
    CHECK(d[2].vendor == "AMD");
    CHECK(d[2].name == "Compute Part");
    CHECK(ParseLspciMm("").empty());
}

TEST_CASE("health: Win32_VideoController list parser on a synthetic listing") {
    const std::string text =
        "\r\nName                 : Intel(R) UHD Graphics 770\r\nAdapterRAM           : 1073741824\r\nDriverVersion        : 31.0.101.4502\r\nAdapterCompatibility : Intel Corporation\r\n\r\n"
        "Name                 : NVIDIA GeForce RTX 3060\r\nAdapterRAM           : 4293918720\r\nDriverVersion        : 31.0.15.3623\r\nAdapterCompatibility : NVIDIA\r\n\r\n"
        "Name                 : Microsoft Basic Render Driver\r\nAdapterRAM           :\r\nDriverVersion        : 10.0.19041.1\r\nAdapterCompatibility : Microsoft\r\n";
    const auto g = ParseWindowsVideoControllers(text);
    REQUIRE(g.size() == 3);
    CHECK(g[0].vendor == "Intel");
    CHECK(g[0].kind == GpuKind::kIntegrated);
    CHECK(g[0].vram_mb == 1024);
    CHECK(g[0].driver == "31.0.101.4502");
    CHECK(g[1].vendor == "NVIDIA");
    CHECK(g[1].kind == GpuKind::kDiscrete);
    CHECK(g[1].vram_mb == 4095);  // the 32-bit cap
    CHECK(g[2].kind == GpuKind::kSoftware);
    CHECK(g[2].vram_mb == 0);
    CHECK(ParseWindowsVideoControllers("").empty());
    CHECK(ParseWindowsVideoControllers("nothing useful here").empty());
}

TEST_CASE("health: MemTotal") {
    CHECK(ParseMemTotalMb("MemTotal:       16318412 kB\nMemFree:  1 kB\n") == 15935);
    CHECK(!ParseMemTotalMb("MemFree: 12 kB\n"));
    CHECK(!ParseMemTotalMb("MemTotal: lots\n"));
    CHECK(!ParseMemTotalMb(""));
}

TEST_CASE("health: FindOnPath") {
    namespace fs = std::filesystem;
    // The fixture folder is reached through a relative PATH entry from inside the temp folder: an
    // absolute Windows temp path ("C:\\...") has a drive colon that the ':' separator of the
    // Linux case would split, so the test must not depend on the host's path style.
    const fs::path cwd = fs::current_path();
    const fs::path base = fs::temp_directory_path();
    const std::string rel = "samn_health_path_test";
    fs::create_directories(base / rel);
    { std::ofstream(base / rel / "samn-tool") << "x"; }
    { std::ofstream(base / rel / "samn-win.exe") << "x"; }
    fs::current_path(base);

    const std::string env_posix = "nonexistent-samn-dir:" + rel + ":";
    CHECK(FindOnPath("samn-tool", env_posix, Platform::kLinux) == (fs::path(rel) / "samn-tool").u8string());
    CHECK(FindOnPath("samn-missing", env_posix, Platform::kLinux).empty());
    CHECK(FindOnPath("samn-win", env_posix, Platform::kLinux).empty());  // .exe only on Windows
    // Windows separator ';', a quoted entry, and a drive-letter entry that is not there.
    const std::string env_win = "C:\\samn-nonexistent\\tools;\"" + rel + "\"";
    CHECK(FindOnPath("samn-win", env_win, Platform::kWindows) == (fs::path(rel) / "samn-win.exe").u8string());
    CHECK(FindOnPath("samn-tool", env_win, Platform::kWindows) == (fs::path(rel) / "samn-tool").u8string());  // an exact name is tried first
    CHECK(FindOnPath("samn-missing", "C:\\samn-nonexistent\\tools", Platform::kWindows).empty());
    CHECK(FindOnPath("samn-tool", "", Platform::kLinux).empty());

    fs::current_path(cwd);
    fs::remove_all(base / rel);
}

TEST_CASE("health: parts checks and the report summary") {
    CHECK(CheckFfmpeg("/x/ffmpeg", "").status == Status::kOk);
    const Check missing = CheckFfmpeg("", "");
    CHECK(missing.status == Status::kOff);
    CHECK(!missing.fix.empty());
    CHECK(CheckFfmpeg("/x/ffmpeg", "/x/ffprobe").fix.empty());
    CHECK(CheckPython("").status == Status::kOff);
    CHECK(CheckPython("/usr/bin/python3").status == Status::kOk);
    CHECK(CheckRam(2048).status == Status::kBad);
    CHECK(CheckRam(6000).status == Status::kWarn);
    CHECK(CheckRam(16384).status == Status::kOk);
    CHECK(CheckRam(16384).detail == "16 GB installed");
    CHECK(CheckRam(12288 + 512).detail == "12.5 GB installed");
    CHECK(CheckRam(-1).status == Status::kWarn);
    CHECK(CheckVideoPath({}, true, true).status == Status::kWarn);
    CHECK(CheckVideoPath({G("NVIDIA", "RTX", 8192)}, true, true).status == Status::kOk);
    CHECK(CheckVideoPath({G("NVIDIA", "RTX", 8192)}, false, true).status == Status::kOff);  // reported by the app's "Video decoding" row
    CHECK(CheckAiProvider({}, Platform::kLinux).status == Status::kWarn);
    CHECK(CheckAiProvider({G("NVIDIA", "RTX", 8192)}, Platform::kLinux).status == Status::kOk);

    PartsInput in;
    in.gpus = {G("NVIDIA", "RTX", 8192)};
    in.ram_mb = 16384;
    const auto checks = BuildPartsChecks(in);
    REQUIRE(checks.size() == 5);
    CHECK(checks[0].id == "ai-provider");
    CHECK(checks[4].id == "ram");

    HealthReport rep;
    CHECK(rep.Summary() == "All checks passed");
    rep.checks = checks;  // ffmpeg and python are off: no warning
    CHECK(rep.Worst() == Status::kOk);
    CHECK(rep.Summary() == "All checks passed");
    rep.checks.push_back(CheckRam(6000));
    CHECK(rep.Summary() == "1 warning");
    rep.checks.push_back(CheckRam(1000));
    rep.checks.push_back(CheckRam(1000));
    CHECK(rep.Summary() == "2 problems, 1 warning");
    CHECK(rep.Worst() == Status::kBad);
    CHECK(rep.Find("ffmpeg"));
    CHECK(!rep.Find("nothing"));
    CHECK(std::string(StatusName(Status::kOff)) == "off");
}
