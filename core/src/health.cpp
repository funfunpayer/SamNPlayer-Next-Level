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
        // GB with one decimal, without the overflow of mb * 10 for huge values.
        const std::uint64_t tenth = (mb / 1024) * 10 + ((mb % 1024) * 10 + 512) / 1024;
        return std::to_string(tenth / 10) + (tenth % 10 ? "." + std::to_string(tenth % 10) : std::string()) + " GB";
    }
    return std::to_string(mb) + " MB";
}

std::string Label(const Gpu& g) {
    std::string s = g.name;
    if (g.vram_mb > 0 && g.kind == GpuKind::kDiscrete) s += " (" + Mem(g.vram_mb) + ")";
    return s;
}

// Order that does not depend on the list: more memory first, then the name.
bool Better(const Gpu& g, const Gpu& best) {
    if (g.vram_mb != best.vram_mb) return g.vram_mb > best.vram_mb;
    return g.name < best.name;
}

// The adapter AI should prefer: not software; dedicated before integrated;
// NVIDIA first among dedicated ones, then the most memory, then the name.
const Gpu* PickDedicated(const std::vector<Gpu>& gpus) {
    const Gpu* best = nullptr;
    for (const Gpu& g : gpus) {
        if (g.kind != GpuKind::kDiscrete) continue;
        const bool nv = g.vendor == "NVIDIA", best_nv = best && best->vendor == "NVIDIA";
        if (!best || (nv && !best_nv) || (nv == best_nv && Better(g, *best))) best = &g;
    }
    return best;
}
const Gpu* PickIntegrated(const std::vector<Gpu>& gpus) {
    const Gpu* best = nullptr;
    for (const Gpu& g : gpus)
        if (g.kind == GpuKind::kIntegrated && (!best || Better(g, *best))) best = &g;
    return best;
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
        case 0x1AF4:  // virtio
        case 0x1B36:  // QXL / QEMU
        case 0x15AD:  // VMware
        case 0x80EE:  // VirtualBox
        case 0x1234:  // Bochs / QEMU standard VGA
        case 0x1A03: return "Virtual";  // ASPEED: the server's management controller
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
    for (const char* v : {"vmware", "virtualbox", "innotek", "qemu", "red hat", "virtio", "aspeed", "bochs"})
        if (Contains(t, v)) return "Virtual";
    return "Other";
}

namespace {
// "radeon 780m": three digits and an 'm' that ends the word, without "rx" (an RX 6800M is a card).
bool IsRadeonMobileApuName(const std::string& t) {
    if (!Contains(t, "radeon") || Contains(t, " rx ")) return false;
    for (std::size_t i = 0; i + 3 < t.size(); ++i)
        if (std::isdigit(static_cast<unsigned char>(t[i])) && std::isdigit(static_cast<unsigned char>(t[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(t[i + 2])) && t[i + 3] == 'm' &&
            (i + 4 == t.size() || !std::isalnum(static_cast<unsigned char>(t[i + 4]))) &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(t[i - 1]))))
            return true;
    return false;
}

bool IsAmdApuName(const std::string& name) {
    const std::string t = AsciiLower(name);
    if (Contains(t, "radeon(tm) graphics") || Contains(t, "radeon graphics")) return true;
    if (Contains(t, "vega") && !Contains(t, "rx vega") && !Contains(t, "vega 56") && !Contains(t, "vega 64") && !Contains(t, "frontier") &&
        !Contains(t, "vega ii") && !Contains(t, "radeon vii"))
        return true;
    return IsRadeonMobileApuName(t);
}

// "Arc A770", "Arc(TM) B580", "Arc Pro A40": a card. "Arc Graphics" (Core Ultra) has no model letter: an iGPU.
bool IsIntelArcCardName(const std::string& name) {
    const std::string t = AsciiLower(name);
    const std::size_t arc = t.find("arc");
    if (arc == std::string::npos) return false;
    for (std::size_t i = arc + 3; i + 2 < t.size(); ++i)
        if ((t[i] == 'a' || t[i] == 'b') && (i == 0 || !std::isalnum(static_cast<unsigned char>(t[i - 1]))) &&
            std::isdigit(static_cast<unsigned char>(t[i + 1])) && std::isdigit(static_cast<unsigned char>(t[i + 2])))
            return true;
    return false;
}
}  // namespace

GpuKind ClassifyKind(const std::string& vendor, std::uint64_t vram_mb, bool software, const std::string& name) {
    if (software || vendor == "Microsoft" || vendor == "Virtual") return GpuKind::kSoftware;
    if (vendor == "Intel") return IsIntelArcCardName(name) || vram_mb >= 2048 ? GpuKind::kDiscrete : GpuKind::kIntegrated;
    if (vendor == "AMD") return IsAmdApuName(name) || vram_mb < 1024 ? GpuKind::kIntegrated : GpuKind::kDiscrete;
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

namespace {
std::uint64_t ParseBytes(const std::string& v) {
    if (v.empty() || !std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) return 0;
    try {
        return std::stoull(v);
    } catch (const std::exception&) {
        return 0;
    }
}

Gpu MakeWindowsGpu(const std::string& name, const std::string& compat, std::uint64_t ram_bytes, const std::string& driver) {
    Gpu g;
    g.name = name;
    g.driver = driver;
    g.vendor = VendorFromText(compat.empty() ? name : compat);
    if (g.vendor == "Other") g.vendor = VendorFromText(name);
    g.vram_mb = ram_bytes / (1024 * 1024);
    g.kind = ClassifyKind(g.vendor, g.vram_mb, Contains(AsciiLower(name), "basic render"), name);
    return g;
}
}  // namespace

std::vector<Gpu> ParseWindowsVideoControllers(const std::string& text) {
    std::vector<Gpu> out;
    std::string name, compat, driver;
    std::uint64_t ram_bytes = 0;
    bool have = false;
    auto flush = [&] {
        if (have && !name.empty()) out.push_back(MakeWindowsGpu(name, compat, ram_bytes, driver));
        name.clear();
        compat.clear();
        driver.clear();
        ram_bytes = 0;
        have = false;
    };
    for (const std::string& line : Lines(text)) {
        const std::size_t colon = line.find(':');
        if (Trim(line).empty()) {
            flush();
            continue;
        }
        if (colon == std::string::npos) continue;
        const std::string key = AsciiLower(Trim(line.substr(0, colon)));
        const std::string val = Trim(line.substr(colon + 1));
        if (key == "name") {
            if (have) flush();  // two adapters without a blank line between them
            name = val;
            have = true;
        } else if (key == "adapterram") {
            ram_bytes = ParseBytes(val);
            have = true;
        } else if (key == "driverversion") {
            driver = val;
            have = true;
        } else if (key == "adaptercompatibility") {
            compat = val;
            have = true;
        }
    }
    flush();
    return out;
}

std::vector<Gpu> ParseWindowsVideoControllersCsv(const std::string& text) {
    // One record per line; fields in double quotes, "" inside a field is a quote.
    auto split = [](const std::string& line) {
        std::vector<std::string> f;
        std::string cur;
        bool quoted = false, any = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (quoted) {
                if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') cur += '"', ++i;
                else if (c == '"') quoted = false;
                else cur += c;
            } else if (c == '"') {
                quoted = true;
                any = true;
            } else if (c == ',') {
                f.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (any || !cur.empty() || !f.empty()) f.push_back(cur);
        return f;
    };
    std::vector<Gpu> out;
    std::vector<std::string> header;
    for (const std::string& line : Lines(text)) {
        if (Trim(line).empty()) continue;
        std::vector<std::string> f = split(line);
        if (header.empty()) {
            for (std::string& h : f) header.push_back(AsciiLower(Trim(h)));
            continue;
        }
        std::string name, compat, driver;
        std::uint64_t ram = 0;
        for (std::size_t i = 0; i < f.size() && i < header.size(); ++i) {
            if (header[i] == "name") name = Trim(f[i]);
            else if (header[i] == "adapterram") ram = ParseBytes(Trim(f[i]));
            else if (header[i] == "driverversion") driver = Trim(f[i]);
            else if (header[i] == "adaptercompatibility") compat = Trim(f[i]);
        }
        if (!name.empty()) out.push_back(MakeWindowsGpu(name, compat, ram, driver));
    }
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
            if (!fs::is_regular_file(f, ec) || ec) continue;
            if (platform != Platform::kWindows) {  // a POSIX program needs an execute bit
                const auto perms = fs::status(f, ec).permissions();
                if (ec || (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none) continue;
            }
            return f.u8string();
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
