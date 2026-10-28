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
#include "samn/settings.h"
#include "samn/vision/onnx_detector.h"
#include "train_controller.h"

namespace samn::ui {

namespace {
constexpr qint64 kMiB = 1024 * 1024;
constexpr qint64 kDiskFail = 300 * kMiB;
constexpr qint64 kDiskWarn = 2048 * kMiB;

QString FromFs(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
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
        if (g.vendor == QLatin1String("Microsoft")) g.software = true;
        if (g.vendor == QLatin1String("Intel"))
            g.integrated = g.dedicated_mb < 2048;
        else if (g.vendor == QLatin1String("AMD"))
            g.integrated = g.dedicated_mb < 1024;
        else
            g.integrated = false;
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
                out << g;
            }
            adapter->Release();
        }
        factory->Release();
    }
#elif defined(Q_OS_LINUX)
    const QDir drm(QStringLiteral("/sys/class/drm"));
    static const QRegularExpression card(QStringLiteral("^card\\d+$"));  // not the card0-HDMI-A-1 connectors
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
    const QString env = TrainController::aiDir() + QStringLiteral("/train-env");
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
    return in;
}

QList<CheckItem> RunChecks(const CheckInputs& in) {
    QList<CheckItem> out;

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
    return out;
}

QString Report(const QList<CheckItem>& items) {
    QStringList lines;
    lines << QStringLiteral("SamNPlayer Next %1").arg(QCoreApplication::applicationVersion())
          << QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()) << QString();
    for (const CheckItem& i : items) {
        const char* tag = i.state == QLatin1String("ok") ? "OK" : i.state == QLatin1String("warn") ? "WARN" : "FAIL";
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
