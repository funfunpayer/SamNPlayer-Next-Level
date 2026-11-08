#include "samn/health.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "samn/text.h"

namespace samn::health {

namespace {
bool Contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Mem(std::uint64_t mb) {
    if (mb >= 1024) {
        const std::uint64_t tenth = (mb * 10 + 512) / 1024;  // GB with one decimal
        return std::to_string(tenth / 10) + (tenth % 10 ? "." + std::to_string(tenth % 10) : std::string()) + " GB";
    }
    return std::to_string(mb) + " MB";
}

std::string Label(const Gpu& g) {
    std::string s = g.name;
    if (g.vram_mb > 0 && g.kind == GpuKind::kDiscrete) s += " (" + Mem(g.vram_mb) + ")";
    return s;
}

// The adapter AI should prefer: not software; dedicated before integrated;
// NVIDIA first among dedicated ones, then the most memory; first in the list on a tie.
const Gpu* PickDedicated(const std::vector<Gpu>& gpus) {
    const Gpu* best = nullptr;
    for (const Gpu& g : gpus) {
        if (g.kind != GpuKind::kDiscrete) continue;
        const bool nv = g.vendor == "NVIDIA", best_nv = best && best->vendor == "NVIDIA";
        if (!best || (nv && !best_nv) || (nv == best_nv && g.vram_mb > best->vram_mb)) best = &g;
    }
    return best;
}
const Gpu* PickIntegrated(const std::vector<Gpu>& gpus) {
    for (const Gpu& g : gpus)
        if (g.kind == GpuKind::kIntegrated) return &g;
    return nullptr;
}

Check Make(const char* id, const char* title, Status st, std::string detail, std::string fix = {}) {
    Check c;
    c.id = id;
    c.title = title;
    c.status = st;
    c.detail = std::move(detail);
    c.fix = st == Status::kOk ? std::string() : std::move(fix);
    return c;
}

// The text between the last '[' ... ']' pair that is not a 4-digit hex id.
bool IsHexId(const std::string& s) {
    return s.size() == 4 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// "GA106 [GeForce RTX 3060] [2503]" -> name "GeForce RTX 3060", id 0x2503.
// "NVIDIA Corporation [10de]" -> name "NVIDIA Corporation", id 0x10de.
std::string SplitBracketId(const std::string& field, std::uint32_t* id) {
    std::string s = Trim(field);
    const std::size_t open = s.rfind('[');
    if (open != std::string::npos && s.size() > open + 1 && s.back() == ']' && IsHexId(s.substr(open + 1, s.size() - open - 2))) {
        if (id) *id = static_cast<std::uint32_t>(std::stoul(s.substr(open + 1, 4), nullptr, 16));
        s = Trim(s.substr(0, open));
    }
    return s;
}

std::string BracketName(const std::string& s) {
    const std::size_t open = s.find('[');
    const std::size_t close = open == std::string::npos ? open : s.find(']', open);
    if (close != std::string::npos && close > open + 1) return Trim(s.substr(open + 1, close - open - 1));
    return s;
}

std::vector<std::string> QuotedFields(const std::string& line, std::string* head) {
    std::vector<std::string> out;
    const std::size_t first = line.find('"');
    *head = Trim(line.substr(0, first));
    std::size_t i = first;
    while (i != std::string::npos && i < line.size()) {
        const std::size_t close = line.find('"', i + 1);
        if (close == std::string::npos) break;
        out.push_back(line.substr(i + 1, close - i - 1));
        i = line.find('"', close + 1);
    }
    return out;
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(i, e - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(std::move(l));
        i = e + 1;
    }
    return out;
}
}  // namespace

const char* StatusName(Status s) {
    switch (s) {
        case Status::kOk: return "ok";
        case Status::kWarn: return "warn";
        case Status::kBad: return "bad";
        case Status::kOff: return "off";
    }
    return "ok";
}

int HealthReport::Count(Status s) const {
    return static_cast<int>(std::count_if(checks.begin(), checks.end(), [s](const Check& c) { return c.status == s; }));
}

Status HealthReport::Worst() const {
    if (Count(Status::kBad)) return Status::kBad;
    if (Count(Status::kWarn)) return Status::kWarn;
    return Status::kOk;
}

std::string HealthReport::Summary() const {
    const int bad = Count(Status::kBad), warn = Count(Status::kWarn);
    std::string s;
    if (bad) s = bad == 1 ? "1 problem" : std::to_string(bad) + " problems";
    if (warn) s += (s.empty() ? "" : ", ") + (warn == 1 ? std::string("1 warning") : std::to_string(warn) + " warnings");
    return s.empty() ? "All checks passed" : s;
}

const Check* HealthReport::Find(const std::string& id) const {
    for (const Check& c : checks)
        if (c.id == id) return &c;
    return nullptr;
}

Platform CurrentPlatform() {
#if defined(_WIN32)
    return Platform::kWindows;
#elif defined(__linux__)
    return Platform::kLinux;
#elif defined(__APPLE__)
    return Platform::kMac;
#else
    return Platform::kOther;
#endif
}

const char* GpuKindName(GpuKind k) {
    switch (k) {
        case GpuKind::kIntegrated: return "integrated";
        case GpuKind::kDiscrete: return "dedicated";
        case GpuKind::kSoftware: return "software";
    }
    return "dedicated";
}

std::string VendorFromPciId(std::uint32_t id) {
    switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002:
        case 0x1022: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x106B: return "Apple";
        default: return "Other";
    }
}

std::string VendorFromText(const std::string& text) {
    const std::string t = AsciiLower(text);
    if (Contains(t, "nvidia")) return "NVIDIA";
    if (Contains(t, "advanced micro") || Contains(t, "amd") || Contains(t, "ati tech")) return "AMD";
    if (Contains(t, "intel")) return "Intel";
    if (Contains(t, "microsoft")) return "Microsoft";
    if (Contains(t, "apple")) return "Apple";
    return "Other";
}

GpuKind ClassifyKind(const std::string& vendor, std::uint64_t vram_mb, bool software) {
    if (software || vendor == "Microsoft") return GpuKind::kSoftware;
    if (vendor == "Intel") return vram_mb < 2048 ? GpuKind::kIntegrated : GpuKind::kDiscrete;
    if (vendor == "AMD") return vram_mb < 1024 ? GpuKind::kIntegrated : GpuKind::kDiscrete;
    if (vendor == "Apple") return GpuKind::kIntegrated;
    return GpuKind::kDiscrete;
}

AiRecommendation RecommendAi(const std::vector<Gpu>& gpus, Platform platform) {
    AiRecommendation r;
    r.provider = "CPU";
    if (platform == Platform::kMac) {
        if (const Gpu* g = PickIntegrated(gpus)) {
            r.provider = "CoreML";
            r.device = g->name;
            r.gpu = true;
            r.reason = "The Apple GPU runs models through CoreML.";
            return r;
        }
        r.reason = "No GPU provider is known for this Mac; models run on the CPU.";
        return r;
    }
    const bool win = platform == Platform::kWindows;
    if (const Gpu* g = PickDedicated(gpus)) {
        std::string p;
        if (g->vendor == "NVIDIA") p = "CUDA";
        else if (g->vendor == "AMD") p = win ? "DirectML" : "ROCm";
        else if (g->vendor == "Intel") p = win ? "DirectML" : "OpenVINO";
        else if (win) p = "DirectML";
        if (!p.empty()) {
            r.provider = p;
            r.device = g->name;
            r.gpu = true;
            r.reason = "The dedicated GPU " + Label(*g) + " is the fastest choice (" + p + ").";
            if (const Gpu* ig = PickIntegrated(gpus)) r.reason += " The integrated " + ig->name + " is left to the desktop.";
            return r;
        }
    }
    if (const Gpu* g = PickIntegrated(gpus)) {
        std::string p;
        if (g->vendor == "Intel") p = "OpenVINO";
        else if (win) p = "DirectML";
        if (!p.empty()) {
            r.provider = p;
            r.device = g->name;
            r.gpu = true;
            r.reason = "No dedicated GPU: the integrated " + g->name + " can run models (" + p + ").";
            return r;
        }
    }
    const bool any_real = std::any_of(gpus.begin(), gpus.end(), [](const Gpu& g) { return g.kind != GpuKind::kSoftware; });
    r.reason = any_real ? "No GPU this build can use for models; they run on the CPU." : "No usable GPU found; models run on the CPU.";
    return r;
}

VideoRecommendation RecommendVideoDecode(const std::vector<Gpu>& gpus, bool decodes_h264, bool decodes_av1) {
    VideoRecommendation v;
    const Gpu* gpu = PickDedicated(gpus);
    if (!gpu) gpu = PickIntegrated(gpus);
    if (gpu) {
        v.gpu_decode = true;
        v.device = gpu->name;
    }
    v.h264 = !decodes_h264 ? "not available" : v.gpu_decode ? "GPU (" + v.device + ")" : "CPU";
    v.av1 = decodes_av1 ? "CPU (dav1d)" : "not available";
    return v;
}

std::vector<PciDisplay> ParseLspciMm(const std::string& text) {
    std::vector<PciDisplay> out;
    for (const std::string& line : Lines(text)) {
        std::string slot;
        const std::vector<std::string> f = QuotedFields(line, &slot);
        if (f.size() < 3 || slot.empty()) continue;
        std::uint32_t cls = 0;
        SplitBracketId(f[0], &cls);
        if (cls != 0x0300 && cls != 0x0302 && cls != 0x0380) continue;
        PciDisplay d;
        d.slot = slot;
        const std::string vendor_text = SplitBracketId(f[1], &d.vendor_id);
        d.vendor = d.vendor_id ? VendorFromPciId(d.vendor_id) : VendorFromText(vendor_text);
        if (d.vendor == "Other") d.vendor = VendorFromText(vendor_text);
        d.name = BracketName(SplitBracketId(f[2], nullptr));
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<Gpu> ParseWindowsVideoControllers(const std::string& text) {
    std::vector<Gpu> out;
    Gpu cur;
    bool have = false;
    std::string compat;
    std::uint64_t ram_bytes = 0;
    auto flush = [&] {
        if (have && !cur.name.empty()) {
            cur.vendor = VendorFromText(compat.empty() ? cur.name : compat);
            if (cur.vendor == "Other") cur.vendor = VendorFromText(cur.name);
            cur.vram_mb = ram_bytes / (1024 * 1024);
            cur.kind = ClassifyKind(cur.vendor, cur.vram_mb, Contains(AsciiLower(cur.name), "basic render"));
            out.push_back(cur);
        }
        cur = Gpu{};
        compat.clear();
        ram_bytes = 0;
        have = false;
    };
    for (const std::string& line : Lines(text)) {
        const std::size_t colon = line.find(':');
        if (Trim(line).empty() || colon == std::string::npos) {
            if (Trim(line).empty()) flush();
            continue;
        }
        const std::string key = AsciiLower(Trim(line.substr(0, colon)));
        const std::string val = Trim(line.substr(colon + 1));
        if (key == "name") {
            if (have) flush();  // two adapters without a blank line between them
            cur.name = val;
            have = true;
        } else if (key == "adapterram") {
            try {
                ram_bytes = val.empty() ? 0 : std::stoull(val);
            } catch (const std::exception&) {
                ram_bytes = 0;
            }
            have = true;
        } else if (key == "driverversion") {
            cur.driver = val;
            have = true;
        } else if (key == "adaptercompatibility") {
            compat = val;
            have = true;
        }
    }
    flush();
    return out;
}

std::optional<std::uint64_t> ParseMemTotalMb(const std::string& meminfo) {
    for (const std::string& line : Lines(meminfo)) {
        if (line.rfind("MemTotal:", 0) != 0) continue;
        std::size_t i = 9;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        std::size_t e = i;
        while (e < line.size() && std::isdigit(static_cast<unsigned char>(line[e]))) ++e;
        if (e == i) return std::nullopt;
        try {
            return std::stoull(line.substr(i, e - i)) / 1024;  // kB -> MB
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string FindOnPath(const std::string& name, const std::string& path_env, Platform platform) {
    namespace fs = std::filesystem;
    const char sep = platform == Platform::kWindows ? ';' : ':';
    std::size_t i = 0;
    while (i <= path_env.size()) {
        std::size_t e = path_env.find(sep, i);
        if (e == std::string::npos) e = path_env.size();
        std::string dir = path_env.substr(i, e - i);
        i = e + 1;
        if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') dir = dir.substr(1, dir.size() - 2);
        if (dir.empty()) continue;
        const fs::path d = fs::u8path(dir);
        const char* tries[] = {"", ".exe"};
        for (const char* ext : tries) {
            if (*ext && platform != Platform::kWindows) break;
            std::error_code ec;
            const fs::path f = d / fs::u8path(name + ext);
            if (fs::is_regular_file(f, ec) && !ec) return f.u8string();
        }
    }
    return {};
}

Check CheckFfmpeg(const std::string& ffmpeg_path, const std::string& ffprobe_path) {
    const std::string probe = ffprobe_path.empty() ? "ffprobe not found (not needed)" : "ffprobe: " + ffprobe_path;
    if (!ffmpeg_path.empty()) return Make("ffmpeg", "ffmpeg", Status::kOk, ffmpeg_path + "\n" + probe);
    return Make("ffmpeg", "ffmpeg", Status::kOff, "Not found. Clip Prep, Convert to H.264 and some exports need it.\n" + probe,
                "Open Clip Prep and let it fetch ffmpeg once, or put ffmpeg on the PATH.");
}

Check CheckPython(const std::string& python_path) {
    if (!python_path.empty()) return Make("python", "Python", Status::kOk, python_path + " (AI Train brings its own Python and does not use it)");
    return Make("python", "Python", Status::kOff, "No system Python found. AI Train fetches its own Python, so this is fine.");
}

Check CheckRam(std::int64_t ram_mb) {
    if (ram_mb < 0) return Make("ram", "Memory (RAM)", Status::kWarn, "Could not be read.", "Nothing to do; the app works as usual.");
    const std::string d = Mem(static_cast<std::uint64_t>(ram_mb)) + " installed";
    if (ram_mb < 4096) return Make("ram", "Memory (RAM)", Status::kBad, d, "Close other programs; AI Train and Generate need 8 GB or more.");
    if (ram_mb < 8192) return Make("ram", "Memory (RAM)", Status::kWarn, d, "Close other programs before AI Train or a long Generate run.");
    return Make("ram", "Memory (RAM)", Status::kOk, d);
}

Check CheckAiProvider(const std::vector<Gpu>& gpus, Platform platform) {
    const AiRecommendation r = RecommendAi(gpus, platform);
    const char* title = "AI models (ONNX Runtime provider)";
    if (r.gpu) return Make("ai-provider", title, Status::kOk, r.provider + " on " + r.device + ". " + r.reason);
    return Make("ai-provider", title, Status::kWarn, "CPU. " + r.reason,
                "Install the current driver of your graphics card; the CPU path works, only slower.");
}

Check CheckVideoPath(const std::vector<Gpu>& gpus, bool decodes_h264, bool decodes_av1) {
    const VideoRecommendation v = RecommendVideoDecode(gpus, decodes_h264, decodes_av1);
    const char* title = "Video decode path";
    const std::string d = "H.264 / HEVC: " + v.h264 + "\nAV1: " + v.av1;
    // No H.264 at all is the "Video decoding" row's problem; this row does not report it twice.
    if (!decodes_h264) return Make("video-path", title, Status::kOff, d + "\nSee \"Video decoding\".", "");
    if (!v.gpu_decode) return Make("video-path", title, Status::kWarn, d, "Install the graphics driver; without it 4K video may stutter.");
    return Make("video-path", title, Status::kOk, d);
}

std::vector<Check> BuildPartsChecks(const PartsInput& in) {
    std::vector<Check> out;
    out.push_back(CheckAiProvider(in.gpus, in.platform));
    out.push_back(CheckVideoPath(in.gpus, in.decodes_h264, in.decodes_av1));
    out.push_back(CheckFfmpeg(in.ffmpeg_path, in.ffprobe_path));
    out.push_back(CheckPython(in.python_path));
    out.push_back(CheckRam(in.ram_mb));
    return out;
}

}  // namespace samn::health
