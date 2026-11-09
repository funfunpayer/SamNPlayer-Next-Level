#pragma once
// Hardware and runtime health, as plain data and pure functions (D-167): the
// graphics adapters (integrated and dedicated) classified, which one an AI
// model and video decoding should use, and the "missing parts" checks. Nothing
// here reads the machine: the platform layer (the app) hands in strings and
// numbers, so every rule is testable without hardware. Provider names
// ("CUDA", "DirectML", "OpenVINO", ...) are strings only; this module links
// to no GPU runtime.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace samn::health {

enum class Status { kOk, kWarn, kBad, kOff };  // kOff: optional and not there, not a problem
const char* StatusName(Status s);              // "ok" | "warn" | "bad" | "off"

struct Check {
    std::string id, title;
    Status status = Status::kOk;
    std::string detail;
    std::string fix;  // one line; empty when ok
};

struct HealthReport {
    std::vector<Check> checks;
    int Count(Status s) const;
    Status Worst() const;  // bad > warn > ok; off never counts
    // "All checks passed", "1 problem", "2 warnings" or "1 problem, 2 warnings".
    std::string Summary() const;
    const Check* Find(const std::string& id) const;
};

enum class Platform { kWindows, kLinux, kMac, kOther };
Platform CurrentPlatform();

enum class GpuKind { kIntegrated, kDiscrete, kSoftware };
const char* GpuKindName(GpuKind k);  // "integrated" | "dedicated" | "software"

struct Gpu {
    std::string vendor;  // "NVIDIA", "AMD", "Intel", "Microsoft", "Apple" or "Other"
    std::string name;
    GpuKind kind = GpuKind::kDiscrete;
    std::uint64_t vram_mb = 0;  // dedicated memory; 0 when the platform does not say
    std::string driver;         // driver name or version; empty when unknown
};

// "NVIDIA" for 0x10DE, "AMD" for 0x1002 / 0x1022, "Intel" for 0x8086,
// "Microsoft" for 0x1414, "Apple" for 0x106B, else "Other".
std::string VendorFromPciId(std::uint32_t pci_vendor_id);
// The same from a vendor text ("NVIDIA Corporation", "Advanced Micro Devices,
// Inc. [AMD/ATI]", "Intel(R) ..."); "Other" when none matches.
std::string VendorFromText(const std::string& text);

// Intel is integrated below 2 GB of its own memory (Arc is not), AMD below
// 1 GB, NVIDIA and others never; Microsoft's adapters (Basic Render Driver)
// and `software` are software.
GpuKind ClassifyKind(const std::string& vendor, std::uint64_t vram_mb, bool software = false);

// The adapter an AI model should run on, and through which ONNX Runtime
// provider: the dedicated GPU first (NVIDIA ahead of the rest, then the most
// memory), then the integrated one, then the CPU. A recommendation of what the
// hardware allows; the build still has to ship that provider.
struct AiRecommendation {
    std::string provider;  // "CUDA", "ROCm", "DirectML", "OpenVINO", "CoreML" or "CPU"
    std::string device;    // the adapter's name; empty for the CPU
    bool gpu = false;
    std::string reason;    // one sentence
};
AiRecommendation RecommendAi(const std::vector<Gpu>& gpus, Platform platform);

// Video decoding: a real GPU decodes H.264 / HEVC; AV1 is decoded on the CPU
// with dav1d (the Windows package carries it). `decodes_*` is what the
// media backend says it can decode at all.
struct VideoRecommendation {
    bool gpu_decode = false;
    std::string device;  // the GPU that decodes; empty without one
    std::string h264;    // "GPU (<name>)", "CPU" or "not available"
    std::string av1;     // "CPU (dav1d)" or "not available"
};
VideoRecommendation RecommendVideoDecode(const std::vector<Gpu>& gpus, bool decodes_h264, bool decodes_av1);

// ---- Parsers of platform output (pure string functions) -------------------

// One display controller of `lspci -mm -nn -D` (machine-readable lines such as
// 0000:01:00.0 "VGA compatible controller [0300]" "NVIDIA Corporation [10de]" "GA106 [GeForce RTX 3060] [2503]" -ra1 ...).
// Only display classes 0300, 0302 and 0380 are returned.
struct PciDisplay {
    std::string slot;  // "0000:01:00.0" (or "01:00.0" without -D)
    std::uint32_t vendor_id = 0;
    std::string vendor;  // short name, see VendorFromPciId
    std::string name;    // "GeForce RTX 3060": the bracket name when there is one
};
std::vector<PciDisplay> ParseLspciMm(const std::string& text);

// `Get-CimInstance Win32_VideoController | Format-List Name,AdapterRAM,DriverVersion,AdapterCompatibility`
// ("Key : value" lines, a blank line between adapters). AdapterRAM is a
// 32-bit number: an adapter with 4 GB or more reports 4095 MB, which is still
// enough to tell a card from an integrated one.
std::vector<Gpu> ParseWindowsVideoControllers(const std::string& text);

// /proc/meminfo: MemTotal in MB.
std::optional<std::uint64_t> ParseMemTotalMb(const std::string& meminfo);

// First `name` found in the folders of `path_env` (separated by ';' on
// Windows, ':' elsewhere); "name.exe" is tried on Windows. Empty if none.
std::string FindOnPath(const std::string& name, const std::string& path_env, Platform platform);

// ---- The missing-parts checks ---------------------------------------------

struct PartsInput {
    std::string ffmpeg_path;   // empty: not found
    std::string ffprobe_path;  // empty: not found
    std::string python_path;   // a system Python; empty: none
    std::int64_t ram_mb = -1;  // -1 unknown
    std::vector<Gpu> gpus;
    Platform platform = Platform::kOther;
    bool decodes_h264 = true, decodes_av1 = true;
};

Check CheckFfmpeg(const std::string& ffmpeg_path, const std::string& ffprobe_path);
Check CheckPython(const std::string& python_path);
Check CheckRam(std::int64_t ram_mb);  // bad below 4 GB, warn below 8 GB
Check CheckAiProvider(const std::vector<Gpu>& gpus, Platform platform);
Check CheckVideoPath(const std::vector<Gpu>& gpus, bool decodes_h264, bool decodes_av1);

// The checks this module owns, in the order the System tab shows them:
// ai-provider, video-path, ffmpeg, python, ram.
std::vector<Check> BuildPartsChecks(const PartsInput& in);

}  // namespace samn::health
