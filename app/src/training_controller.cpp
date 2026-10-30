#include "training_controller.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "device_controller.h"
#include "samn/library.h"
#include "samn/phase_rules.h"

namespace samn::ui {

namespace trn = samn::training;

namespace {

// Friendly program labels (0.7 PRESET_LABELS; the branch adds the level programs).
QString presetLabel(const QString& name) {
    static const QHash<QString, QString> labels = {
        {"stop-start", "Stop-Start (classic)"},
        {"plateau", "Plateau / Edging (classic)"},
        {"vibration-wave-suction-focus", "Vibration wave \xE2\x86\x92 Suction focus"},
        {"tissue-massage", "Tissue massage (suction)"},
        {"vibration-massage", "Vibration massage"},
        {"variable", "Variable / randomized wave"},
        {"pulse-rhythm", "Pulse rhythm"},
        {"endurance-ladder", "Endurance ladder"},
        {"edge-ladder", "Edge ladder"},
        {"breathing-pace", "Breathing pace"},
        {"pelvic-floor", "Pelvic floor cues"},
    };
    const auto it = labels.constFind(name);
    if (it != labels.constEnd()) return *it;
    QString s = name;
    s.replace('-', ' ');
    return s;
}

QString categoryLabel(const QString& c) {
    if (c == "control") return "Control";
    if (c == "endurance") return "Endurance";
    if (c == "edge") return "Edge Play";
    if (c == "body") return "Body";
    return {};
}

QString pct(double v) { return QString::number(int(std::lround(v * 100))) + "%"; }

QString signedPct(double factor) {
    const int p = int(std::lround((factor - 1) * 100));
    return (p >= 0 ? "+" : "") + QString::number(p) + "%";
}

// Go's Duration notation for the safety-limit message: 3h0m0s, 1m30s, 10s, 500ms.
QString durationText(qint64 ms) {
    if (ms < 1000) return QString::number(ms) + "ms";
    const qint64 s = ms / 1000, h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0) return QString("%1h%2m%3s").arg(h).arg(m).arg(sec);
    if (m > 0) return QString("%1m%2s").arg(m).arg(sec);
    return QString("%1s").arg(sec);
}

QString friendlyTechnique(const QString& technique, const QString& channel) {
    if (channel == "script") {
        QString s = technique;
        s.replace('-', ' ');
        return s;
    }
    const QString t = technique == "plateau" ? "Plateau" : technique == "stopstart" ? "Stop-Start" : technique;
    const QString c = channel == "vibration" ? "Vibration" : channel == "suction" ? "Suction" : channel == "both" ? "Both" : channel;
    return t + "/" + c;
}

QVariantMap toMap(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
}

trn::Script scriptFromMap(const QVariantMap& m) {
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact);
    return nlohmann::json::parse(bytes.toStdString()).get<trn::Script>();
}

}  // namespace

// The engine's device: the shared DeviceController, only the channels it has.
class TrainingController::Adapter : public trn::Device {
public:
    explicit Adapter(TrainingController& c) : c_(c) {}
    void set_vibration(double v) override {
        if (c_.device_ && c_.vib_available_) c_.device_->setVibration(v);
    }
    void set_suction(double v) override {
        if (c_.device_ && c_.suc_available_) c_.device_->setSuction(v);
    }
    void stop() override {
        if (c_.device_ && c_.device_->connected()) c_.device_->stop();
    }

private:
    TrainingController& c_;
};

TrainingController::TrainingController(QObject* parent) : QObject(parent), adapter_(new Adapter(*this)) {
    opts_.technique = trn::Technique::StopStart;
    opts_.channel = trn::Channel::Vibration;
    opts_.cycles = 5;
    opts_.ramp_up_ms = 8000;
    opts_.hold_ms = 3000;
    opts_.rest_ms = 10000;
    opts_.peak_intensity = 0.8;
    opts_.plateau_fraction = 0.7;
    opts_.progression_per_cycle = 0.15;
    load();
    timer_.setInterval(kTickMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &TrainingController::tick);
    connect_timeout_.setSingleShot(true);
    connect(&connect_timeout_, &QTimer::timeout, this, [this] {
        if (state_ != State::Connecting) return;
        if (device_) device_->disconnectDevice();
        failStart(QStringLiteral("connection timed out"));
    });
    builtin_ = trn::builtin_scripts();
    rebuildPrograms();
    refreshHistory();  // also builds the plan
}

TrainingController::~TrainingController() {
    if (state_ != State::Idle) finishSession();
}

samn::Settings& TrainingController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

void TrainingController::save(const char* key, const nlohmann::json& value) {
    try {
        settings().set(key, value);
    } catch (const std::exception&) {
        // A settings file that cannot be written must not stop a training.
    }
}

void TrainingController::load() {
    auto& s = settings();
    mock_ = s.get("training.mock", true).get<bool>();
    opts_.technique = trn::technique_from(s.get("training.technique", "stopstart").get<std::string>());
    opts_.channel = trn::channel_from(s.get("training.channel", "vibration").get<std::string>());
    const auto num = [&](const char* key, double def, double lo, double hi) {
        const auto v = s.get(key, def);
        return v.is_number() ? std::clamp(v.get<double>(), lo, hi) : def;
    };
    opts_.cycles = int(num("training.cycles", 5, 1, 20));
    opts_.ramp_up_ms = int(num("training.ramp_up_ms", 8000, 0, 30000));
    opts_.hold_ms = int(num("training.hold_ms", 3000, 0, 15000));
    opts_.rest_ms = int(num("training.rest_ms", 10000, 0, 60000));
    opts_.peak_intensity = num("training.peak_intensity", 0.8, 0, 1);
    opts_.plateau_fraction = num("training.plateau_fraction", 0.7, 0, 1);
    opts_.progression_per_cycle = num("training.progression_per_cycle", 0.15, 0, 1);
    const auto text = [&](const char* key) {
        const auto v = s.get(key, "");
        return v.is_string() ? QString::fromStdString(v.get<std::string>()) : QString();
    };
    const QString stage = text("training.stage.mode");
    if (stage == "figure" || stage == "video") stage_ = stage;
    video_path_ = text("training.stage.video");
    const auto flag = [&](const char* key, bool def) {
        const auto v = s.get(key, def);
        return v.is_boolean() ? v.get<bool>() : def;
    };
    video_loop_ = flag("training.stage.loop", true);
    video_muted_ = flag("training.stage.muted", true);
    const QString climax = text("training.stage.climax_action");
    if (climax == "none" || climax == "pause" || climax == "cooldown") climax_action_ = climax;
    loadVideoPhases();
}

// ---------------------------------------------------------------- stage

void TrainingController::setStage(const QString& stage) {
    if (stage == stage_ || (stage != "rings" && stage != "figure" && stage != "video")) return;
    stage_ = stage;
    save("training.stage.mode", stage_.toStdString());
    emit stageChanged();
}

QString TrainingController::videoUrl() const {
    return video_path_.isEmpty() ? QString() : QUrl::fromLocalFile(video_path_).toString();
}

QString TrainingController::videoName() const { return QFileInfo(video_path_).fileName(); }

void TrainingController::setVideoLoop(bool on) {
    if (on == video_loop_) return;
    video_loop_ = on;
    save("training.stage.loop", on);
    emit stageChanged();
}

void TrainingController::setVideoMuted(bool on) {
    if (on == video_muted_) return;
    video_muted_ = on;
    save("training.stage.muted", on);
    emit stageChanged();
}

void TrainingController::loadVideoPhases() {
    video_phases_.clear();
    last_climax_ = -1;
    if (video_path_.isEmpty()) return;
    const auto file = samn::LabelProjectPath(std::filesystem::path(video_path_.toStdU16String()));
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return;
    try {
        for (const auto& p : samn::LoadLabelProject(file).phases)
            if (p.confirmed) video_phases_.push_back(p);
    } catch (const std::exception&) {
        video_phases_.clear();  // a broken labels file: the film plays as it is
    }
}

QVariantList TrainingController::videoPhases() const {
    QVariantList out;
    for (const auto& p : video_phases_)
        out << QVariantMap{{"startMs", double(p.start_ms)},
                           {"endMs", double(p.end_ms)},
                           {"phase", QString::fromStdString(p.name)}};
    return out;
}

void TrainingController::setClimaxAction(const QString& action) {
    if (action == climax_action_ || (action != "none" && action != "pause" && action != "cooldown")) return;
    climax_action_ = action;
    save("training.stage.climax_action", action.toStdString());
    emit stageChanged();
}

double TrainingController::videoPosition(double ms) {
    if (video_phases_.empty() || ms < 0) return -1;
    const auto step = samn::StepVideoPhases(video_phases_, int64_t(std::llround(ms)), last_climax_);
    if (step.climax >= 0) {
        last_climax_ = step.climax;
        if (state_ == State::Running) {
            if (climax_action_ == "pause") {
                log("The film's climax: paused (Play goes on).");
                pause();
            } else if (climax_action_ == "cooldown") {
                log("The film's climax: cool-down.");
                interrupt();
            }
        }
    } else if (step.seek_to < 0) {
        // Out of every climax again: the next one counts (a looped film).
        bool inside = false;
        for (const auto& p : video_phases_)
            if (p.name == "climax" && ms >= double(p.start_ms) && ms < double(p.end_ms)) inside = true;
        if (!inside) last_climax_ = -1;
    }
    return step.seek_to >= 0 ? double(step.seek_to) : -1;
}

QStringList TrainingController::recentVideos() {
    QStringList out;
    try {
        const auto v = settings().get(samn::kRecentVideosKey, nlohmann::json::array());
        if (v.is_array())
            for (const auto& e : v)
                if (e.is_string() && !e.get<std::string>().empty()) out << QString::fromStdString(e.get<std::string>());
    } catch (const std::exception&) {
    }
    return out;
}

void TrainingController::chooseVideo(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    const QString path = QFileInfo(url.isLocalFile() ? url.toLocalFile() : pathOrUrl).absoluteFilePath();
    if (pathOrUrl.isEmpty() || !QFileInfo(path).isFile()) return;
    video_path_ = path;
    save("training.stage.video", path.toStdString());
    loadVideoPhases();
    std::vector<std::string> recent;
    for (const auto& p : recentVideos()) recent.push_back(p.toStdString());
    save(samn::kRecentVideosKey, samn::PushFront(std::move(recent), path.toStdString(), samn::kRecentMax));
    if (stage_ != "video") {
        stage_ = QStringLiteral("video");
        save("training.stage.mode", "video");
    }
    emit stageChanged();
}

// ---------------------------------------------------------------- device

void TrainingController::setDevice(DeviceController* d) {
    if (d == device_) return;
    if (device_) disconnect(device_, nullptr, this, nullptr);
    device_ = d;
    if (device_) {
        connect(device_, &DeviceController::statusChanged, this, [this] {
            updateDeviceFollow();
            onDeviceStatus();
        });
    }
    updateDeviceFollow();
    emit deviceChanged();
}

// Training follows the device: only the channels it has (Mock and "nothing known yet" keep both).
void TrainingController::updateDeviceFollow() {
    const bool known = !mock_ && device_ && device_->connected() && !device_->capabilities().isEmpty();
    const bool vib = !known || device_->capVibration();
    const bool suc = !known || device_->capSuction();
    const bool blocked = known && !vib && !suc;
    QString note;
    if (known) {
        const QString name = device_->name().isEmpty() ? QStringLiteral("This device") : device_->name();
        if (blocked)
            note = name + " has no vibration or suction motor \xE2\x80\x94 Training needs one of them (e.g. Sam Neo 2). "
                          "Mock tests without a device.";
        else if (!suc)
            note = name + " has no suction \xE2\x80\x94 Training drives vibration.";
        else if (!vib)
            note = name + " has no vibration \xE2\x80\x94 Training drives suction.";
    }
    const bool changed = vib != vib_available_ || suc != suc_available_ || blocked != blocked_ || note != device_note_;
    vib_available_ = vib;
    suc_available_ = suc;
    blocked_ = blocked;
    device_note_ = note;
    // The Channel select follows: a device with one motor leaves only that channel.
    const bool only_vib = vib && !suc, only_suc = suc && !vib;
    if ((only_vib && opts_.channel != trn::Channel::Vibration) || (only_suc && opts_.channel != trn::Channel::Suction)) {
        opts_.channel = only_vib ? trn::Channel::Vibration : trn::Channel::Suction;
        save("training.channel", trn::to_string(opts_.channel));
        emit formChanged();
        rebuildPlan();
    }
    if (changed) emit deviceFollowChanged();
}

QStringList TrainingController::channelOptions() const {
    QStringList o;
    if (vib_available_) o << "vibration";
    if (suc_available_) o << "suction";
    if (vib_available_ && suc_available_) o << "both";
    return o;
}

// ---------------------------------------------------------------- form

#define SAMN_SET(type, field, expr, key, json)         \
    do {                                               \
        const type nv = (expr);                        \
        if (nv == field) return;                       \
        field = nv;                                    \
        save(key, json);                               \
        emit formChanged();                            \
        rebuildPlan();                                 \
    } while (0)

void TrainingController::setMock(bool on) {
    if (on == mock_) return;
    mock_ = on;
    save("training.mock", on);
    emit formChanged();
    updateDeviceFollow();
}

void TrainingController::setScriptName(const QString& name) {
    if (name == script_name_) return;
    script_name_ = name;
    emit formChanged();
    rebuildPlan();
}

void TrainingController::setTier(double t) {
    if (t == tier_ || !(t > 0)) return;
    tier_ = t;
    emit formChanged();
    rebuildPlan();
}

void TrainingController::setAutoAdjust(bool on) {
    if (on == auto_adjust_) return;
    auto_adjust_ = on;
    emit formChanged();
    rebuildPlan();
}

void TrainingController::setTechnique(const QString& t) {
    const trn::Technique nv = trn::technique_from(t.toStdString());
    if (nv == opts_.technique) return;
    opts_.technique = nv;
    save("training.technique", trn::to_string(nv));
    emit formChanged();
    rebuildPlan();
}

void TrainingController::setChannel(const QString& c) {
    if (!channelOptions().contains(c)) return;
    const trn::Channel nv = trn::channel_from(c.toStdString());
    if (nv == opts_.channel) return;
    opts_.channel = nv;
    save("training.channel", trn::to_string(nv));
    emit formChanged();
    rebuildPlan();
}

void TrainingController::setCycles(int v) { SAMN_SET(int, opts_.cycles, std::clamp(v, 1, 20), "training.cycles", nv); }
void TrainingController::setRampUpMs(int v) { SAMN_SET(int, opts_.ramp_up_ms, std::clamp(v, 0, 30000), "training.ramp_up_ms", nv); }
void TrainingController::setHoldMs(int v) { SAMN_SET(int, opts_.hold_ms, std::clamp(v, 0, 15000), "training.hold_ms", nv); }
void TrainingController::setRestMs(int v) { SAMN_SET(int, opts_.rest_ms, std::clamp(v, 0, 60000), "training.rest_ms", nv); }
void TrainingController::setPeak(double v) { SAMN_SET(double, opts_.peak_intensity, std::clamp(v, 0.0, 1.0), "training.peak_intensity", nv); }
void TrainingController::setPlateauFraction(double v) { SAMN_SET(double, opts_.plateau_fraction, std::clamp(v, 0.0, 1.0), "training.plateau_fraction", nv); }
void TrainingController::setProgression(double v) { SAMN_SET(double, opts_.progression_per_cycle, std::clamp(v, 0.0, 1.0), "training.progression_per_cycle", nv); }

#undef SAMN_SET

// ---------------------------------------------------------------- programs

void TrainingController::setCategory(const QString& c) {
    if (c == category_) return;
    category_ = c;
    rebuildPrograms();
}

void TrainingController::rebuildPrograms() {
    mine_ = trn::list_custom_scripts(trn::scripts_dir());
    const auto entry = [](const trn::Script& s, bool custom) {
        const QString name = QString::fromStdString(s.name);
        const QString cat = QString::fromStdString(s.category);
        QString label = custom ? "\xE2\x98\x85 " + name : presetLabel(name);
        QStringList tags;
        if (!categoryLabel(cat).isEmpty()) tags << categoryLabel(cat);
        if (s.level > 0) tags << "L" + QString::number(s.level);
        return QVariantMap{{"name", name},
                           {"label", label},
                           {"text", tags.isEmpty() ? label : label + " \xC2\xB7 " + tags.join(" \xC2\xB7 ")},
                           {"description", QString::fromStdString(s.description)},
                           {"category", cat},
                           {"categoryLabel", categoryLabel(cat)},
                           {"level", s.level},
                           {"custom", custom}};
    };
    programs_.clear();
    for (const auto& s : builtin_)
        if (category_.isEmpty() || QString::fromStdString(s.category) == category_) programs_ << entry(s, false);
    my_programs_.clear();
    for (const auto& s : mine_) my_programs_ << entry(s, true);
    emit programsChanged();
}

QVariantList TrainingController::programs() const { return programs_; }
QVariantList TrainingController::myPrograms() const { return my_programs_; }

QString TrainingController::scriptDescription() const {
    if (script_name_.isEmpty()) return {};
    const auto s = trn::find_script(script_name_.toStdString(), trn::scripts_dir());
    return s ? QString::fromStdString(s->description) : QString();
}

bool TrainingController::customScript() const {
    return !script_name_.isEmpty() && !trn::builtin_script(script_name_.toStdString());
}

QVariantMap TrainingController::scriptForEditing(const QString& name) const {
    if (trn::builtin_script(name.toStdString())) return {};
    for (const auto& s : mine_)
        if (QString::fromStdString(s.name) == name) return toMap(nlohmann::json(s));
    return {};
}

QString TrainingController::saveScript(const QVariantMap& script) {
    trn::Script s;
    try {
        s = scriptFromMap(script);
    } catch (const std::exception& e) {
        return QString("not a valid script: ") + e.what();
    }
    const std::string err = trn::save_custom_script(s, trn::scripts_dir());
    if (!err.empty()) return QString::fromStdString(err);
    rebuildPrograms();
    if (script_name_ == QString::fromStdString(s.name)) rebuildPlan();  // an edit of the selected program
    return {};
}

QString TrainingController::deleteScript(const QString& name) {
    const std::string err = trn::delete_custom_script(name.toStdString(), trn::scripts_dir());
    if (!err.empty()) return QString::fromStdString(err);
    rebuildPrograms();
    if (script_name_ == name) setScriptName({});
    return {};
}

QVariantMap TrainingController::previewDraft(const QVariantMap& script) const {
    trn::Script s;
    try {
        s = scriptFromMap(script);
    } catch (const std::exception& e) {
        return {{"error", QString("not a valid script: ") + e.what()}};
    }
    if (const std::string err = trn::validate_script(s); !err.empty()) return {{"error", QString::fromStdString(err)}};
    const trn::Preview p = trn::build_preview(s);
    return {{"totalMs", p.total_ms}, {"phases", int(s.phases.size())}};
}

// ---------------------------------------------------------------- plan

QString TrainingController::historyKey() const {
    return script_name_.isEmpty() ? technique() : script_name_;
}

std::optional<trn::Script> TrainingController::resolveScript() const {
    auto s = trn::find_script(script_name_.toStdString(), trn::scripts_dir());
    if (!s) return std::nullopt;
    return trn::scale_script(*s, factor_);
}

void TrainingController::rebuildPlan() {
    const bool script = !script_name_.isEmpty();
    factor_ = trn::combined_intensity_factor(script, tier_, auto_adjust_, history_, historyKey().toStdString());
    const auto adj = trn::history_adjustment(history_, script ? script_name_.toStdString() : technique().toStdString());
    suggestion_ = QString::fromStdString(adj.message);
    intensity_note_.clear();
    if (script) {
        QStringList parts;
        if (tier_ != 1) parts << QString("%1 tier \xC3\x97%2").arg(tier_ > 1 ? "Intense" : "Gentle").arg(QString::number(tier_, 'g', 3));
        if (auto_adjust_ && adj.factor != 1) parts << QString("history nudge \xC3\x97%1").arg(QString::number(adj.factor, 'g', 3));
        if (!parts.isEmpty())
            intensity_note_ = QString("Applied intensity: \xC3\x97%1 (%2)").arg(factor_, 0, 'f', 2).arg(parts.join(", "));
    }
    if (script) {
        const auto s = resolveScript();
        plan_ = s ? trn::build_preview(*s) : trn::Preview{};
    } else {
        plan_ = trn::build_preview(opts_);
    }
    plan_phases_.clear();
    for (size_t i = 0; i < plan_.markers.size(); ++i) {
        const int from = plan_.markers[i].at_ms;
        const int to = i + 1 < plan_.markers.size() ? plan_.markers[i + 1].at_ms : plan_.total_ms;
        plan_phases_ << QVariantMap{{"name", QString::fromStdString(plan_.markers[i].name)},
                                    {"startMs", from},
                                    {"endMs", to},
                                    {"strength", trn::phase_strength(plan_, from, to)}};
    }
    if (state_ == State::Idle) {
        phase_name_ = plan_.markers.empty() ? QString() : QString::fromStdString(plan_.markers[0].name);
        phase_index_ = 0;
        phase_count_ = int(plan_.markers.size());
        elapsed_sec_ = 0;
        emit progressChanged();
        emit samplesChanged();  // the curve item draws the new plan
    }
    emit planChanged();
}

// ---------------------------------------------------------------- session

QString TrainingController::state() const {
    switch (state_) {
        case State::Connecting: return "connecting";
        case State::Running: return "running";
        case State::Paused: return "paused";
        default: return "idle";
    }
}

int TrainingController::remainingSec() const {
    return std::max(0, int(std::lround(plan_.total_ms / 1000.0)) - elapsed_sec_);
}

void TrainingController::setState(State s) {
    if (s == state_) return;
    state_ = s;
    emit stateChanged();
}

void TrainingController::log(const QString& line) {
    log_ << line;
    if (log_.size() > kMaxLog) log_.removeFirst();
    emit logChanged();
}

bool TrainingController::start() {
    if (state_ != State::Idle) return false;
    updateDeviceFollow();
    if (blocked_) {
        emit startBlocked();
        return false;
    }
    log_.clear();
    last_feedback_ = 0;
    feedback_status_.clear();
    feedback_effect_.clear();
    emit logChanged();
    emit feedbackChanged();
    if (!device_) {
        log("ERROR: no device available");
        return false;
    }
    run_is_script_ = !script_name_.isEmpty();
    if (run_is_script_) {
        const auto s = resolveScript();
        if (!s) {
            log("ERROR: unknown training script \"" + script_name_ + "\"");
            return false;
        }
        run_script_ = *s;
        run_key_ = script_name_;
        if (factor_ != 1) log(QString("Profile intensity adjusted: \xC3\x97%1").arg(factor_, 0, 'f', 2));
    } else {
        run_opts_ = opts_;
        run_key_ = technique();
        if (const std::string e = trn::validate_options(run_opts_); !e.empty()) {
            log("ERROR: " + QString::fromStdString(e));
            return false;
        }
    }
    if (device_->connected()) {
        log("Using existing connection from Tools \xE2\x86\x92 Device.");
        beginSession();
        return true;
    }
    we_connected_ = true;
    setState(State::Connecting);
    log("Connecting...");
    if (!device_->connecting()) {
        const QString kind = mock_ ? "mock" : (device_->savedKind().isEmpty() ? QString("ble") : device_->savedKind());
        device_->connectDevice(kind, mock_ ? QString() : device_->savedTarget(kind));
    }
    connect_timeout_.start(connect_timeout_ms_);
    return true;
}

void TrainingController::onDeviceStatus() {
    if (state_ != State::Connecting || !device_) return;
    if (device_->connected()) {
        connect_timeout_.stop();
        if (blocked_) {
            failStart(device_note_);
            if (device_) device_->disconnectDevice();
            return;
        }
        beginSession();
    } else if (!device_->connecting()) {
        connect_timeout_.stop();
        failStart(device_->lastError().isEmpty() ? QStringLiteral("connection failed") : device_->lastError());
    }
}

void TrainingController::failStart(const QString& message) {
    connect_timeout_.stop();
    log("ERROR: " + message);
    we_connected_ = false;
    setState(State::Idle);
    log("Training finished.");
    emit sessionFinished();
}

void TrainingController::beginSession() {
    std::string err;
    session_ = run_is_script_ ? trn::Session::script(run_script_, adapter_.get(), &err)
                              : trn::Session::custom(run_opts_, adapter_.get(), &err);
    if (!session_) {
        failStart(QString::fromStdString(err));
        return;
    }
    session_->on_levels = [this](double v, double s) {
        const bool moved = std::abs(v - vib_) >= 0.005 || std::abs(s - suc_) >= 0.005 || ((v == 0) != (vib_ == 0)) ||
                           ((s == 0) != (suc_ == 0));
        if (!moved) return;
        vib_ = v;
        suc_ = s;
        emit levelsChanged();
    };
    session_->on_cycle = [this](const trn::CycleResult& r) { onCycle(r); };
    session_->on_script_cycle = [this](const trn::ScriptCycleResult& r) { onScriptCycle(r); };

    std::error_code ec;
    std::filesystem::create_directories(trn::sessions_dir(), ec);
    log_file_.close();
    log_file_.clear();
    log_file_.open(trn::sessions_dir() / trn::session_log_name(run_key_.toStdString(), trn::file_stamp_now()));
    if (!log_file_) log("Session log could not be created.");

    vib_samples_.clear();
    suc_samples_.clear();
    sample_ms_ = std::clamp(plan_.total_ms / 1000, 100, 2000);
    next_sample_ms_ = 0;
    vib_ = suc_ = 0;
    device_->setSessionActiveFor(this, true);
    setState(State::Running);
    log("Training starting...");
    clock_.start();
    wall_.start();
    last_tick_ns_ = 0;
    session_->advance(0);  // the first levels go out now
    sampleLevels();
    updateProgress(true);
    timer_.start();
}

void TrainingController::tick() {
    if (!session_) return;
    if (wall_.elapsed() >= safety_limit_ms_) {
        finishSession("Session safety limit (" + durationText(safety_limit_ms_) + ") reached \xE2\x80\x94 ended automatically.");
        return;
    }
    const qint64 ns = wall_.nsecsElapsed();
    const double dt = double(ns - last_tick_ns_) / 1e6 * time_scale_;
    last_tick_ns_ = ns;
    session_->advance(dt);
    sampleLevels();
    updateProgress(false);
    if (session_->finished()) finishSession();
}

void TrainingController::sampleLevels() {
    while (session_ && session_->elapsed_ms() >= next_sample_ms_) {
        vib_samples_.push_back(float(session_->vibration()));
        suc_samples_.push_back(float(session_->suction()));
        next_sample_ms_ += sample_ms_;
        emit samplesChanged();
    }
}

void TrainingController::updateProgress(bool force) {
    if (!session_) return;
    const auto p = session_->position();
    const int sec = int(session_->elapsed_ms() / 1000);
    QString cycle, peak;
    if (session_->is_script()) {
        cycle = QString("%1 (%2/%3), phase %4/%5").arg(QString::fromStdString(p.phase_name)).arg(p.repeat_index + 1)
                    .arg(p.repeats_total).arg(p.phase_index + 1).arg(p.phases_total);
        QStringList peaks;
        if (p.vibration_peak > 0) peaks << "Vibration " + pct(p.vibration_peak);
        if (p.suction_peak > 0) peaks << "Suction " + pct(p.suction_peak);
        peak = peaks.isEmpty() ? QString("-") : peaks.join(", ");
    } else {
        cycle = QString("%1 / %2").arg(p.repeat_index + 1).arg(p.repeats_total);
        peak = pct(std::max(p.vibration_peak, p.suction_peak));
    }
    const QString name = QString::fromStdString(p.phase_name);
    const int idx = session_->is_script() ? p.phase_index : p.repeat_index;
    const int count = session_->is_script() ? p.phases_total : p.repeats_total;
    if (!force && sec == elapsed_sec_ && name == phase_name_ && cycle == cycle_text_ && peak == peak_text_) return;
    elapsed_sec_ = sec;
    phase_name_ = name;
    phase_index_ = idx;
    phase_count_ = count;
    cycle_text_ = cycle;
    peak_text_ = peak;
    emit progressChanged();
}

void TrainingController::onCycle(const trn::CycleResult& r) {
    QString line = QString("Cycle %1/%2: peak %3, hold %4ms").arg(r.cycle_index + 1).arg(run_opts_.cycles)
                       .arg(pct(r.peak_intensity)).arg(r.hold_ms);
    if (r.arousal_before) line += QString(", adjusted after feedback %1").arg(r.arousal_before);
    if (r.reached_peak_after_ms) line += QString(", reached after %1s").arg(r.reached_peak_after_ms / 1000.0, 0, 'f', 1);
    if (r.stopped_by_user) line += " \xE2\x80\x94 stopped by you";
    log(line);
    writeLog(trn::log_entry(run_opts_, r, trn::timestamp_now()));
}

void TrainingController::onScriptCycle(const trn::ScriptCycleResult& r) {
    QStringList peaks;
    if (r.vibration_peak > 0) peaks << "Vibration " + pct(r.vibration_peak);
    if (r.suction_peak > 0) peaks << "Suction " + pct(r.suction_peak);
    log(QString("%1 %2/%3: %4, rest %5ms%6").arg(QString::fromStdString(r.phase_name)).arg(r.repeat_index + 1)
            .arg(r.repeats_total).arg(peaks.isEmpty() ? QString("(no channel)") : peaks.join(", ")).arg(r.rest_ms)
            .arg(r.stopped_by_user ? " \xE2\x80\x94 interrupted on request" : ""));
    feedback_effect_ = r.arousal_before
        ? QString("Feedback %1 \xE2\x86\x92 peak %2, rest %3 \xE2\x80\x94 applied to both channels.").arg(r.arousal_before)
              .arg(signedPct(r.peak_factor_applied)).arg(signedPct(r.rest_factor_applied))
        : QString();
    emit feedbackChanged();
    writeLog(trn::log_entry(run_key_.toStdString(), r, trn::timestamp_now()));
}

void TrainingController::writeLog(const trn::LogEntry& e) {
    if (!log_file_) return;
    log_file_ << trn::to_json(e).dump() << '\n';
    log_file_.flush();
}

void TrainingController::finishSession(const QString& message) {
    timer_.stop();
    connect_timeout_.stop();
    if (session_) {
        if (!session_->finished()) session_->abort();
        session_.reset();
    }
    log_file_.close();
    if (device_) device_->setSessionActiveFor(this, false);
    if (we_connected_ && device_) device_->disconnectDevice();  // a connection that was already there stays
    we_connected_ = false;
    vib_ = suc_ = 0;
    emit levelsChanged();
    if (!message.isEmpty()) log(message);
    log("Training finished.");
    setState(State::Idle);
    refreshHistory();
    emit sessionFinished();
}

void TrainingController::stop() {
    if (state_ == State::Idle) return;
    if (state_ == State::Connecting) {
        if (device_) device_->disconnectDevice();
        failStart(QStringLiteral("cancelled"));
        return;
    }
    finishSession();
}

void TrainingController::emergencyStop() {
    if (state_ == State::Idle) return;
    if (device_) device_->emergencyStop();
    stop();
}

void TrainingController::pause() {
    if (state_ != State::Running || !session_) return;
    session_->pause();
    setState(State::Paused);
    log("Paused.");
}

void TrainingController::resume() {
    if (state_ != State::Paused || !session_) return;
    last_tick_ns_ = wall_.nsecsElapsed();  // the paused time is not a step of the session
    session_->resume();
    setState(State::Running);
    log("Resumed.");
}

void TrainingController::togglePause() {
    if (state_ == State::Paused) resume();
    else pause();
}

void TrainingController::interrupt() {
    if (!session_) return;
    session_->stop_cycle();
    log("Cycle interrupted \xE2\x80\x94 pause running, then continues.");
}

bool TrainingController::reportFeedback(int level) {
    if (!session_) {
        feedback_status_ = "Not applied: no training is running";
        emit feedbackChanged();
        return false;
    }
    if (level < 1 || level > 10) {
        feedback_status_ = QString("Not applied: value must be between 1 and 10 (got %1)").arg(level);
        emit feedbackChanged();
        return false;
    }
    session_->report_arousal(level);
    // 9 and 10 clearly mean "too much now": the running cycle ends too, not only the next one.
    if (level >= 9) session_->stop_cycle();
    last_feedback_ = level;
    feedback_status_ = QString("%1 reported \xE2\x80\x94 affects the next cycle.").arg(level);
    emit feedbackChanged();
    return true;
}

// ---------------------------------------------------------------- history

void TrainingController::refreshHistory() {
    history_ = trn::training_history(trn::sessions_dir());
    history_view_.clear();
    for (const auto& s : history_) {
        const QDateTime dt = QDateTime::fromString(QString::fromStdString(s.started_at), Qt::ISODate).toLocalTime();
        const QString date = dt.isValid() ? QLocale::system().toString(dt, QLocale::ShortFormat) : QString::fromStdString(s.started_at);
        QString line = QString("%1: %2 cycles, avg peak %3").arg(friendlyTechnique(QString::fromStdString(s.technique), QString::fromStdString(s.channel)))
                           .arg(s.cycles_completed).arg(pct(s.mean_peak_intensity));
        if (s.cycles_stopped_early > 0) line += QString(", %1\xC3\x97 interrupted").arg(s.cycles_stopped_early);
        if (s.arousal_reports_count > 0) line += QString(", avg feedback %1").arg(s.mean_arousal_reported, 0, 'f', 1);
        history_view_ << QVariantMap{{"date", date}, {"line", line}, {"text", date + " \xE2\x80\x94 " + line}};
    }
    recap_.clear();
    if (!history_.empty()) {
        const auto& s = history_.front();
        QString summary = QString("%1 cycles \xC2\xB7 avg peak %2").arg(s.cycles_completed).arg(pct(s.mean_peak_intensity));
        if (s.cycles_stopped_early > 0) summary += QString(" \xC2\xB7 %1\xC3\x97 interrupted").arg(s.cycles_stopped_early);
        if (s.arousal_reports_count > 0) summary += QString(" \xC2\xB7 avg feedback %1").arg(s.mean_arousal_reported, 0, 'f', 1);
        QString title = friendlyTechnique(QString::fromStdString(s.technique), QString::fromStdString(s.channel));
        if (s.channel != "script") title = title.section('/', 0, 0);
        recap_ = QVariantMap{{"title", title},
                             {"summary", summary},
                             {"hint", "Logged under Recent sessions. Export CSV from History when you want a file."}};
    }
    emit historyChanged();
    rebuildPlan();  // the history nudge reads it
}

QString TrainingController::exportHistoryCsv(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return f.errorString();
    f.write(QByteArray::fromStdString(trn::history_csv(history_)));
    return {};
}

QString TrainingController::defaultExportName() const {
    return "training-history-" + QDate::currentDate().toString("yyyyMMdd") + ".csv";
}

}  // namespace samn::ui
