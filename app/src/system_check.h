#pragma once
// The health check (D-162): which graphics adapters the PC has (an Intel
// iGPU next to an NVIDIA card is the common case), which of them trains the
// AI, and whether the things the app needs are there. Shown in Settings →
// System; at start only an important failure raises a notice.

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include <thread>

namespace samn::ui {

struct GpuInfo {
    QString name;
    QString vendor;  // "NVIDIA", "AMD", "Intel", "Microsoft" or "Other"
    quint64 dedicated_mb = 0;
    bool integrated = false;
    bool software = false;  // Microsoft's Basic Render Driver and the like
};

// "NVIDIA" for 0x10DE, "AMD" for 0x1002 / 0x1022, "Intel" for 0x8086,
// "Microsoft" for 0x1414, else "Other".
QString GpuVendorName(quint32 pci_vendor_id);

// Every graphics adapter of this PC (DXGI on Windows, /sys/class/drm on
// Linux, empty elsewhere), classified. May take a moment: not for the GUI thread.
QList<GpuInfo> ListGpus();

// Sets integrated and software: Intel is integrated unless it has 2 GB or more
// of its own (Arc), AMD below 1 GB, NVIDIA never; Microsoft adapters are software.
void ClassifyGpus(QList<GpuInfo>& gpus);

// The card that trains: the NVIDIA adapter (CUDA sees nothing else) with the
// most dedicated memory, whatever the order of the list; none without one.
std::optional<GpuInfo> PickTrainingGpu(const QList<GpuInfo>& gpus);

struct CheckItem {
    QString id, title;
    QString state;  // "ok" | "warn" | "fail"
    QString detail, fix;
    bool important = false;  // a failure of it raises the start notice
};

enum class TrainEnvState { kNotSetUp, kReady, kFailed };

// Everything RunChecks looks at, so tests can hand in any PC.
struct CheckInputs {
    QList<GpuInfo> gpus;
    QString config_dir;
    qint64 free_bytes = -1;  // in the settings folder; -1 unknown
    bool config_writable = true;
    bool decodes_h264 = true, decodes_av1 = true;
    bool onnx_available = true;
    TrainEnvState train_env = TrainEnvState::kReady;
    bool nudenet_present = true;
};

// The inputs of this PC, with the adapters given (ListGpus is slow, so the
// caller decides on which thread it runs).
CheckInputs GatherInputs(const QList<GpuInfo>& gpus);

// The checks in the order the tab shows them: settings, disk, gpu, ai-gpu,
// video, onnx, ai-env, nudenet.
QList<CheckItem> RunChecks(const CheckInputs& in);

// Plain text for "Copy report": version, system, one line per check.
QString Report(const QList<CheckItem>& items);

class SystemCheckController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // [{id, title, state, detail, fix, important}], empty until the first run.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool importantProblem READ importantProblem NOTIFY changed)
    // "All checks passed", "1 problem", "2 warnings" or "1 problem, 2 warnings".
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit SystemCheckController(QObject* parent = nullptr);
    ~SystemCheckController() override;

    QVariantList items() const { return items_; }
    bool running() const { return running_; }
    bool importantProblem() const { return important_problem_; }
    QString summary() const { return summary_; }

    // Runs the checks (the adapters off the GUI thread); changed() when done.
    Q_INVOKABLE void refresh();
    // Once at start: like refresh(), then one error notice when an important
    // check failed.
    Q_INVOKABLE void startupCheck();
    Q_INVOKABLE void copyReport();
    // Tests: the checks of a PC of their own, finished at once.
    void setInputs(const CheckInputs& in);

    QString report() const { return Report(checks_); }

signals:
    void changed();

private:
    void finish(const CheckInputs& in);

    QList<CheckItem> checks_;
    QVariantList items_;
    QString summary_;
    bool running_ = false, important_problem_ = false, announce_ = false;
    std::thread worker_;
};

}  // namespace samn::ui
