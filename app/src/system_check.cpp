#include "system_check.h"

#include <algorithm>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaFormat>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryFile>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#endif

#include "app_log.h"
#include "ai_paths.h"
#include "qt_paths.h"
#include "samn/health.h"
#include "samn/settings.h"
#include "samn/vision/onnx_detector.h"

namespace samn::ui {

namespace {
constexpr qint64 kMiB = 1024 * 1024;
constexpr qint64 kDiskFail = 300 * kMiB;
constexpr qint64 kDiskWarn = 2048 * kMiB;

QString T(const char* s) { return QCoreApplication::translate("SystemCheck", s); }

QString Memory(quint64 mb) {
    if (mb >= 1024) return QStringLiteral("%1 GB").arg(double(mb) / 1024.0, 0, 'f', mb % 1024 == 0 ? 0 : 1);
    return QStringLiteral("%1 MB").arg(mb);
}

QString Describe(const GpuInfo& g) {
    QString s = g.name;
    if (g.software)
        s += QStringLiteral(" · ") + T("software");
    else if (g.integrated)
        s += QStringLiteral(" · ") + T("integrated");
    else
        s += (g.dedicated_mb > 0 ? QStringLiteral(" · ") + Memory(g.dedicated_mb) : QString()) + QStringLiteral(" · ") + T("dedicated");
    return s;
}

CheckItem Make(const char* id, const QString& title, const QString& state, const QString& detail, const QString& fix = {},
               bool important = false) {
    return {QString::fromLatin1(id), title, state, detail, state == QLatin1String("ok") ? QString() : fix, important};
}

samn::health::Gpu ToHealth(const GpuInfo& g) {
    samn::health::Gpu h;
    h.vendor = g.vendor.toStdString();
    h.name = g.name.toStdString();
    h.vram_mb = g.dedicated_mb;
    h.driver = g.driver.toStdString();
    h.kind = g.software ? samn::health::GpuKind::kSoftware
                        : g.integrated ? samn::health::GpuKind::kIntegrated : samn::health::GpuKind::kDiscrete;
    return h;
}
std::vector<samn::health::Gpu> ToHealth(const QList<GpuInfo>& gpus) {
    std::vector<samn::health::Gpu> out;
    for (const GpuInfo& g : gpus) out.push_back(ToHealth(g));
    return out;
}

CheckItem FromCore(const samn::health::Check& k) {
    using samn::health::Status;
    const char* state = k.status == Status::kOk ? "ok" : k.status == Status::kWarn ? "warn" : k.status == Status::kBad ? "fail" : "off";
    return {QString::fromStdString(k.id), QString::fromStdString(k.title), QString::fromLatin1(state), QString::fromStdString(k.detail),
            QString::fromStdString(k.fix), false};
}

// A program's output, or empty when it is missing, fails or takes longer than `ms`.
QString RunProgram(const QString& program, const QStringList& args, int ms) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(ms)) return {};
    if (!p.waitForFinished(ms)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()) : QString();
}

QString OnPath(const char* name) {
    const std::string p = samn::health::FindOnPath(name, qgetenv("PATH").toStdString(), samn::health::CurrentPlatform());
    return p.empty() ? QString() : QString::fromStdString(p);
}

QString FirstOnPath(std::initializer_list<const char*> names) {
    for (const char* n : names) {
        const QString p = OnPath(n);
        if (!p.isEmpty()) return p;
    }
    return {};
}

QString FindFfmpeg() {
    try {
        samn::Settings settings;
        const auto chosen = settings.get("clipprep.ffmpeg", "");
        if (chosen.is_string()) {
            const QString f = QString::fromStdString(chosen.get<std::string>());
            if (!f.isEmpty() && QFileInfo(f).isFile()) return f;
        }
    } catch (const std::exception&) {
    }
    const QString tools = FromFs(samn::config_dir() / "tools");  // where Clip Prep keeps the one it fetched
    for (const char* n : {"ffmpeg", "ffmpeg.exe"})
        if (QFileInfo(tools + QLatin1Char('/') + QLatin1String(n)).isFile()) return tools + QLatin1Char('/') + QLatin1String(n);
    return OnPath("ffmpeg");
}

qint64 TotalRamMb() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    return GlobalMemoryStatusEx(&m) ? qint64(m.ullTotalPhys / (1024 * 1024)) : -1;
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    const auto mb = samn::health::ParseMemTotalMb(f.read(4096).toStdString());
    return mb ? qint64(*mb) : -1;
#else
    return -1;
#endif
}

#ifdef Q_OS_LINUX
QString ReadLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}
#endif
}  // namespace

QString GpuVendorName(quint32 id) {
    switch (id) {
        case 0x10DE: return QStringLiteral("NVIDIA");
        case 0x1002:
        case 0x1022: return QStringLiteral("AMD");
        case 0x8086: return QStringLiteral("Intel");
        case 0x1414: return QStringLiteral("Microsoft");
        default: return QStringLiteral("Other");
    }
}

void ClassifyGpus(QList<GpuInfo>& gpus) {
    for (GpuInfo& g : gpus) {
        const auto kind = samn::health::ClassifyKind(g.vendor.toStdString(), g.dedicated_mb, g.software);
        g.software = kind == samn::health::GpuKind::kSoftware;
        g.integrated = kind == samn::health::GpuKind::kIntegrated;
    }
}

QList<GpuInfo> ListGpus() {
    QList<GpuInfo> out;
#ifdef Q_OS_WIN
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(adapter->GetDesc1(&d))) {
                GpuInfo g;
                g.name = QString::fromWCharArray(d.Description).trimmed();
                g.vendor = GpuVendorName(d.VendorId);
                g.dedicated_mb = quint64(d.DedicatedVideoMemory) / quint64(kMiB);
                g.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                LARGE_INTEGER umd{};  // the user-mode driver version, four 16-bit parts
                if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
                    g.driver = QStringLiteral("%1.%2.%3.%4").arg(HIWORD(umd.HighPart)).arg(LOWORD(umd.HighPart)).arg(HIWORD(umd.LowPart)).arg(LOWORD(umd.LowPart));
                out << g;
            }
            adapter->Release();
        }
        factory->Release();
    }
    if (out.isEmpty()) {  // DXGI gave nothing (remote session, broken driver): ask WMI, which is slower
        const QString text = RunProgram(QStringLiteral("powershell"),
                                        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                                         QStringLiteral("Get-CimInstance Win32_VideoController | Format-List Name,AdapterRAM,DriverVersion,AdapterCompatibility")},
                                        4000);
        for (const samn::health::Gpu& h : samn::health::ParseWindowsVideoControllers(text.toStdString())) {
            GpuInfo g;
            g.name = QString::fromStdString(h.name);
            g.vendor = QString::fromStdString(h.vendor);
            g.dedicated_mb = h.vram_mb;
            g.driver = QString::fromStdString(h.driver);
            out << g;
        }
    }
#elif defined(Q_OS_LINUX)
    const QDir drm(QStringLiteral("/sys/class/drm"));
    static const QRegularExpression card(QStringLiteral("^card\\d+$"));  // not the card0-HDMI-A-1 connectors
    const std::vector<samn::health::PciDisplay> names = samn::health::ParseLspciMm(
        RunProgram(QStringLiteral("lspci"), {QStringLiteral("-mm"), QStringLiteral("-nn"), QStringLiteral("-D")}, 1500).toStdString());
    for (const QString& entry : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!card.match(entry).hasMatch()) continue;
        const QString dev = drm.filePath(entry) + QStringLiteral("/device/");
        bool ok = false;
        const quint32 vendor = ReadLine(dev + QStringLiteral("vendor")).toUInt(&ok, 16);
        if (!ok) continue;
        const quint32 device = ReadLine(dev + QStringLiteral("device")).toUInt(nullptr, 16);
        GpuInfo g;
        g.vendor = GpuVendorName(vendor);
        const QString label = ReadLine(dev + QStringLiteral("label"));
        g.name = !label.isEmpty() ? label
                                  : QStringLiteral("%1 (device 0x%2)").arg(g.vendor == QLatin1String("Other") ? QStringLiteral("Graphics adapter") : g.vendor)
                                        .arg(device, 4, 16, QLatin1Char('0'));
        g.dedicated_mb = ReadLine(dev + QStringLiteral("mem_info_vram_total")).toULongLong() / quint64(kMiB);  // AMD only
        g.driver = QFileInfo(QFileInfo(dev + QStringLiteral("driver")).symLinkTarget()).fileName();  // "nvidia", "i915", "amdgpu"
        // lspci (when installed) knows the marketing name; sysfs only has ids.
        const QString slot = QFileInfo(QDir(dev).canonicalPath()).fileName();  // "0000:01:00.0"
        for (const samn::health::PciDisplay& d : names)
            if (QString::fromStdString(d.slot) == slot && !d.name.empty()) {
                g.name = (g.vendor == QLatin1String("Other") ? QString() : g.vendor + QLatin1Char(' ')) + QString::fromStdString(d.name);
                break;
            }
        out << g;
    }
#endif
    ClassifyGpus(out);
    return out;
}

std::optional<GpuInfo> PickTrainingGpu(const QList<GpuInfo>& gpus) {
    std::optional<GpuInfo> best;
    for (const GpuInfo& g : gpus)
        if (g.vendor == QLatin1String("NVIDIA") && !g.software && (!best || g.dedicated_mb > best->dedicated_mb)) best = g;
    return best;
}

CheckInputs GatherInputs(const QList<GpuInfo>& gpus) {
    CheckInputs in;
    in.gpus = gpus;
    const QString dir = FromFs(samn::config_dir());
    in.config_dir = dir;
    QDir().mkpath(dir);
    {
        QTemporaryFile probe(dir + QStringLiteral("/health-XXXXXX.tmp"));
        in.config_writable = probe.open();
    }
    const QStorageInfo disk(dir);
    in.free_bytes = disk.isValid() ? disk.bytesAvailable() : -1;
    const QList<QMediaFormat::VideoCodec> codecs = QMediaFormat().supportedVideoCodecs(QMediaFormat::Decode);
    in.decodes_h264 = codecs.contains(QMediaFormat::VideoCodec::H264);
    in.decodes_av1 = codecs.contains(QMediaFormat::VideoCodec::AV1);
    in.onnx_available = samn::vision::OnnxDetector::Available();
    // The marker the setup writes when it is done; a folder without it is a setup that stopped half way.
    const QString env = AiDir() + QStringLiteral("/train-env");
    in.train_env = QFileInfo::exists(env + QStringLiteral("/samn-ready.json")) ? TrainEnvState::kReady
                   : QFileInfo::exists(env)                                     ? TrainEnvState::kFailed
                                                                                : TrainEnvState::kNotSetUp;
    try {
        samn::Settings settings;
        const auto path = settings.get("aitrain.nudenet.path", "");
        in.nudenet_present = path.is_string() && QFileInfo::exists(QString::fromStdString(path.get<std::string>()));
    } catch (const std::exception&) {
        in.nudenet_present = false;
    }
    in.ffmpeg_path = FindFfmpeg();
    in.ffprobe_path = OnPath("ffprobe");
    in.python_path = samn::health::CurrentPlatform() == samn::health::Platform::kWindows ? FirstOnPath({"python", "py"})
                                                                                         : FirstOnPath({"python3", "python"});
    in.ram_mb = TotalRamMb();
    return in;
}

QList<CheckItem> RunChecks(const CheckInputs& in) {
    QList<CheckItem> out;
    using samn::health::Platform;
    const std::vector<samn::health::Gpu> gpus = ToHealth(in.gpus);
    const Platform platform = samn::health::CurrentPlatform();

    out << (in.config_writable
                ? Make("settings", T("Settings folder"), "ok", QDir::toNativeSeparators(in.config_dir))
                : Make("settings", T("Settings folder"), "fail", T("Cannot write to %1.").arg(QDir::toNativeSeparators(in.config_dir)),
                       T("Free some space or check the folder's permissions; settings and models cannot be saved."), true));

    if (in.free_bytes < 0)
        out << Make("disk", T("Free disk space"), "warn", T("Could not be read."), T("Make sure the drive of the settings folder has room."));
    else {
        const QString free = Memory(quint64(in.free_bytes / kMiB)) + T(" free");
        if (in.free_bytes < kDiskFail)
            out << Make("disk", T("Free disk space"), "fail", free, T("Free space on the drive of the settings folder; models and training need more."), true);
        else if (in.free_bytes < kDiskWarn)
            out << Make("disk", T("Free disk space"), "warn", free, T("Free some space before downloading models or training (AI Train needs a few GB)."));
        else
            out << Make("disk", T("Free disk space"), "ok", free);
    }

    {
        bool real = false;
        QStringList lines;
        for (const GpuInfo& g : in.gpus) {
            lines << Describe(g);
            real = real || !g.software;
        }
        out << (real ? Make("gpu", T("Graphics adapters"), "ok", lines.join('\n'))
                     : Make("gpu", T("Graphics adapters"), "warn", lines.isEmpty() ? T("No graphics adapter found.") : lines.join('\n'),
                            T("Install the graphics driver of your card; without it video and the interface run slowly.")));
    }

    {
        const auto pick = PickTrainingGpu(in.gpus);
        if (pick) {
            QString detail = T("Training uses %1 (CUDA).").arg(pick->name);
            for (const GpuInfo& g : in.gpus)
                if (g.integrated && !g.software) {
                    detail += QLatin1Char(' ') + T("The integrated graphics (%1) is not used for training.").arg(g.name);
                    break;
                }
            out << Make("ai-gpu", T("AI training device"), "ok", detail);
        } else {
            out << Make("ai-gpu", T("AI training device"), "warn", T("No NVIDIA card: training runs on the processor (slower)."),
                        T("With an NVIDIA card, install its current driver; otherwise training on the processor still works."));
        }
    }

    out << FromCore(samn::health::CheckAiProvider(gpus, platform));

    {
        const QString title = T("Video decoding");
        if (!in.decodes_h264)
            out << Make("video", title, "fail", T("H.264 cannot be decoded."),
                        T("Reinstall the app (its media plugin is missing) or install the system's video codecs."), true);
        else if (!in.decodes_av1)
            out << Make("video", title, "warn", T("H.264 works, AV1 cannot be decoded."),
                        T("Use Convert to H.264 in the Player, or the Windows package that brings dav1d."));
        else
            out << Make("video", title, "ok", T("H.264 and AV1 can be decoded."));
    }

    out << FromCore(samn::health::CheckVideoPath(gpus, in.decodes_h264, in.decodes_av1));

    out << (in.onnx_available ? Make("onnx", T("ONNX Runtime"), "ok", T("Models can run in this build."))
                              : Make("onnx", T("ONNX Runtime"), "warn", T("Not in this build: the detector and the tip confirmation of Find are unavailable."),
                                     T("Use the release package, which brings ONNX Runtime.")));

    switch (in.train_env) {
        case TrainEnvState::kReady: out << Make("ai-env", T("AI training environment"), "ok", T("Ready.")); break;
        case TrainEnvState::kNotSetUp:
            out << Make("ai-env", T("AI training environment"), "warn", T("Not set up."), T("Set it up in AI Train (one click, large download)."));
            break;
        case TrainEnvState::kFailed:
            out << Make("ai-env", T("AI training environment"), "fail", T("The last setup did not finish."),
                        T("Set it up again in AI Train; the Log tab says what went wrong."));
            break;
    }

    out << (in.nudenet_present ? Make("nudenet", T("NudeNet model"), "ok", T("Present."))
                               : Make("nudenet", T("NudeNet model"), "warn", T("Not present (optional)."), T("Fetch it in AI Train if you want it as a teacher.")));

    out << FromCore(samn::health::CheckFfmpeg(in.ffmpeg_path.toStdString(), in.ffprobe_path.toStdString()))
        << FromCore(samn::health::CheckPython(in.python_path.toStdString())) << FromCore(samn::health::CheckRam(in.ram_mb));
    return out;
}

QString Report(const QList<CheckItem>& items) {
    QStringList lines;
    lines << QStringLiteral("SamNPlayer Next %1").arg(QCoreApplication::applicationVersion())
          << QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()) << QString();
    for (const CheckItem& i : items) {
        const char* tag = i.state == QLatin1String("ok")     ? "OK"
                          : i.state == QLatin1String("warn") ? "WARN"
                          : i.state == QLatin1String("off")  ? "OFF"
                                                             : "FAIL";
        lines << QStringLiteral("[%1] %2: %3").arg(QLatin1String(tag), i.title, QString(i.detail).replace('\n', QStringLiteral("; ")));
        if (!i.fix.isEmpty()) lines << QStringLiteral("      %1").arg(i.fix);
    }
    return lines.join('\n') + '\n';
}

SystemCheckController::SystemCheckController(QObject* parent) : QObject(parent) {}

SystemCheckController::~SystemCheckController() {
    if (worker_.joinable()) worker_.join();
}

void SystemCheckController::refresh() {
    if (running_) return;
    running_ = true;
    emit changed();
    if (worker_.joinable()) worker_.join();
    QPointer<SystemCheckController> self(this);
    worker_ = std::thread([self] {
        const QList<GpuInfo> gpus = ListGpus();
        // Back on the GUI thread: the rest touches Qt Multimedia and the settings.
        if (self) QMetaObject::invokeMethod(self, [self, gpus] { if (self) self->finish(GatherInputs(gpus)); }, Qt::QueuedConnection);
    });
}

void SystemCheckController::startupCheck() {
    announce_ = true;
    refresh();
}

void SystemCheckController::setInputs(const CheckInputs& in) { finish(in); }

void SystemCheckController::finish(const CheckInputs& in) {
    checks_ = RunChecks(in);
    items_.clear();
    int problems = 0, warnings = 0;
    important_problem_ = false;
    const CheckItem* first_important = nullptr;
    for (const CheckItem& c : checks_) {
        items_ << QVariantMap{{"id", c.id}, {"title", c.title}, {"state", c.state}, {"detail", c.detail}, {"fix", c.fix}, {"important", c.important}};
        if (c.state == QLatin1String("fail")) ++problems;
        if (c.state == QLatin1String("warn")) ++warnings;
        if (c.state == QLatin1String("fail") && c.important && !first_important) first_important = &c;
    }
    important_problem_ = first_important != nullptr;
    QStringList parts;
    if (problems) parts << (problems == 1 ? tr("1 problem") : tr("%1 problems").arg(problems));
    if (warnings) parts << (warnings == 1 ? tr("1 warning") : tr("%1 warnings").arg(warnings));
    summary_ = parts.isEmpty() ? tr("All checks passed") : parts.join(QStringLiteral(", "));
    running_ = false;
    if (announce_ && first_important)
        AppLog::instance().reportError(tr("System check: %1 — open Settings → System.").arg(first_important->title));
    announce_ = false;
    emit changed();
}

void SystemCheckController::copyReport() {
    if (auto* clip = QGuiApplication::clipboard()) clip->setText(Report(checks_));
}

}  // namespace samn::ui
