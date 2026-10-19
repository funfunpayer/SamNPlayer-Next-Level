#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <fstream>
#include <memory>
#include <vector>

#include "device_controller.h"
#include "samn/settings.h"
#include "samn/training.h"
#include "samn/training_store.h"

namespace samn::ui {

// Training mode for QML (0.7 training.js, training_layout.js, training_device.js,
// app_training*.go): presets and the Custom form, the live session on the shared
// DeviceController, feedback, the session log, history and recap.
//
// The engine (samn::training::Session) is a state machine; this controller ticks
// it from a timer with the real clock and sends its levels to the device. Nothing
// here touches the device unless a session runs.
class TrainingController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(samn::ui::DeviceController* device READ device WRITE setDevice NOTIFY deviceChanged)

    // ---- the form (kept in the settings as training.*) ----
    Q_PROPERTY(bool mock READ mock WRITE setMock NOTIFY formChanged)
    // "" = Custom (the form below), else a built-in or saved program name.
    Q_PROPERTY(QString scriptName READ scriptName WRITE setScriptName NOTIFY formChanged)
    Q_PROPERTY(double tier READ tier WRITE setTier NOTIFY formChanged)  // 0.8 Gentle, 1 Standard, 1.2 Intense
    Q_PROPERTY(bool autoAdjust READ autoAdjust WRITE setAutoAdjust NOTIFY formChanged)
    Q_PROPERTY(QString technique READ technique WRITE setTechnique NOTIFY formChanged)  // stopstart | plateau
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY formChanged)        // vibration | suction | both
    Q_PROPERTY(int cycles READ cycles WRITE setCycles NOTIFY formChanged)
    Q_PROPERTY(int rampUpMs READ rampUpMs WRITE setRampUpMs NOTIFY formChanged)
    Q_PROPERTY(int holdMs READ holdMs WRITE setHoldMs NOTIFY formChanged)
    Q_PROPERTY(int restMs READ restMs WRITE setRestMs NOTIFY formChanged)
    Q_PROPERTY(double peak READ peak WRITE setPeak NOTIFY formChanged)
    Q_PROPERTY(double plateauFraction READ plateauFraction WRITE setPlateauFraction NOTIFY formChanged)
    Q_PROPERTY(double progression READ progression WRITE setProgression NOTIFY formChanged)

    // ---- programs ----
    // Category filter of the built-in list: "" (All) | control | endurance | edge | body.
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY programsChanged)
    // Built-ins of the category and the saved ones: {name, label, text, description, category,
    // categoryLabel, level, custom}. `text` is the label with category and level tags.
    Q_PROPERTY(QVariantList programs READ programs NOTIFY programsChanged)
    Q_PROPERTY(QVariantList myPrograms READ myPrograms NOTIFY programsChanged)
    Q_PROPERTY(QString scriptDescription READ scriptDescription NOTIFY formChanged)
    Q_PROPERTY(bool customScript READ customScript NOTIFY formChanged)

    // ---- the device decides what a training can drive ----
    Q_PROPERTY(bool vibrationAvailable READ vibrationAvailable NOTIFY deviceFollowChanged)
    Q_PROPERTY(bool suctionAvailable READ suctionAvailable NOTIFY deviceFollowChanged)
    Q_PROPERTY(QStringList channelOptions READ channelOptions NOTIFY deviceFollowChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY deviceFollowChanged)
    Q_PROPERTY(QString deviceNote READ deviceNote NOTIFY deviceFollowChanged)

    // ---- the plan ----
    Q_PROPERTY(int planTotalMs READ planTotalMs NOTIFY planChanged)
    // [{name, startMs, endMs, strength}] one per phase (Custom: one per cycle).
    Q_PROPERTY(QVariantList planPhases READ planPhases NOTIFY planChanged)
    Q_PROPERTY(bool planHasJitter READ planHasJitter NOTIFY planChanged)
    Q_PROPERTY(double intensityFactor READ intensityFactor NOTIFY planChanged)
    Q_PROPERTY(QString intensityNote READ intensityNote NOTIFY planChanged)
    Q_PROPERTY(QString historySuggestion READ historySuggestion NOTIFY planChanged)

    // ---- the live session ----
    // "idle" | "connecting" | "running" | "paused"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)  // not idle
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(double vibration READ vibration NOTIFY levelsChanged)
    Q_PROPERTY(double suction READ suction NOTIFY levelsChanged)
    Q_PROPERTY(QString phaseName READ phaseName NOTIFY progressChanged)
    Q_PROPERTY(int phaseIndex READ phaseIndex NOTIFY progressChanged)
    Q_PROPERTY(int phaseCount READ phaseCount NOTIFY progressChanged)
    Q_PROPERTY(QString cycleText READ cycleText NOTIFY progressChanged)
    Q_PROPERTY(QString peakText READ peakText NOTIFY progressChanged)
    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY progressChanged)
    Q_PROPERTY(int remainingSec READ remainingSec NOTIFY progressChanged)
    Q_PROPERTY(int lastFeedback READ lastFeedback NOTIFY feedbackChanged)
    Q_PROPERTY(QString feedbackStatus READ feedbackStatus NOTIFY feedbackChanged)
    Q_PROPERTY(QString feedbackEffect READ feedbackEffect NOTIFY feedbackChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logChanged)

    // ---- history and recap ----
    // Newest first: {label, date, line}.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // {title, summary, hint} of the last session; empty map before the first one.
    Q_PROPERTY(QVariantMap recap READ recap NOTIFY historyChanged)

    // ---- the stage (0.8 training_video.js, kept as training.stage.*) ----
    // "rings" (the live rings), "figure" (the abstract Flow figure) or "video".
    Q_PROPERTY(QString stage READ stage WRITE setStage NOTIFY stageChanged)
    // The film that plays along on the Video stage; the Player's video is untouched.
    Q_PROPERTY(QString videoPath READ videoPath NOTIFY stageChanged)
    Q_PROPERTY(QString videoUrl READ videoUrl NOTIFY stageChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY stageChanged)
    Q_PROPERTY(bool videoLoop READ videoLoop WRITE setVideoLoop NOTIFY stageChanged)
    Q_PROPERTY(bool videoMuted READ videoMuted WRITE setVideoMuted NOTIFY stageChanged)
    // The Player library's recent videos (shared list, newest first).
    Q_PROPERTY(QStringList recentVideos READ recentVideos NOTIFY stageChanged)

public:
    // The engine ticks every kTickMs (a ramp step is 50 ms).
    static constexpr int kTickMs = 20;
    static constexpr int kConnectTimeoutMs = 20000;
    static constexpr qint64 kSafetyLimitMs = 3LL * 3600 * 1000;
    static constexpr int kMaxLog = 500;

    explicit TrainingController(QObject* parent = nullptr);
    ~TrainingController() override;

    samn::ui::DeviceController* device() const { return device_; }
    void setDevice(samn::ui::DeviceController* d);

    // ---- actions ----
    // Connects when needed (Mock, or the saved device kind), then runs. False when blocked.
    Q_INVOKABLE bool start();
    // End the session (Stop).
    Q_INVOKABLE void stop();
    // Safety stop: ends the session and drops queued levels on the device.
    Q_INVOKABLE void emergencyStop();
    // Pause (both channels to 0, the clock stands still) / resume.
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    // Use this film on the Video stage (a path or a file URL); it joins the recent videos.
    Q_INVOKABLE void chooseVideo(const QString& pathOrUrl);
    // End the current cycle now; the session goes on (Esc).
    Q_INVOKABLE void interrupt();
    // Feedback 1-10; 9 and 10 also interrupt the running cycle. Returns false when not running or invalid.
    Q_INVOKABLE bool reportFeedback(int level);

    // ---- programs ----
    Q_INVOKABLE QVariantMap scriptForEditing(const QString& name) const;
    // "" on success, else why not.
    Q_INVOKABLE QString saveScript(const QVariantMap& script);
    Q_INVOKABLE QString deleteScript(const QString& name);
    // {error} or {totalMs, phases} of a script being edited (the editor's "valid yet?" line).
    Q_INVOKABLE QVariantMap previewDraft(const QVariantMap& script) const;

    // ---- history ----
    Q_INVOKABLE void refreshHistory();
    // Writes the CSV; "" on success, else why not.
    Q_INVOKABLE QString exportHistoryCsv(const QString& path) const;
    Q_INVOKABLE QString defaultExportName() const;

    // ---- C++ knobs (tests) ----
    void setTimeScale(double s) { time_scale_ = s; }
    void setSafetyLimitMs(qint64 ms) { safety_limit_ms_ = ms; }
    void setConnectTimeoutMs(int ms) { connect_timeout_ms_ = ms; }

    // ---- state ----
    bool mock() const { return mock_; }
    void setMock(bool on);
    QString scriptName() const { return script_name_; }
    void setScriptName(const QString& name);
    double tier() const { return tier_; }
    void setTier(double t);
    bool autoAdjust() const { return auto_adjust_; }
    void setAutoAdjust(bool on);
    QString technique() const { return QString::fromStdString(samn::training::to_string(opts_.technique)); }
    void setTechnique(const QString& t);
    QString channel() const { return QString::fromStdString(samn::training::to_string(opts_.channel)); }
    void setChannel(const QString& c);
    int cycles() const { return opts_.cycles; }
    void setCycles(int v);
    int rampUpMs() const { return opts_.ramp_up_ms; }
    void setRampUpMs(int v);
    int holdMs() const { return opts_.hold_ms; }
    void setHoldMs(int v);
    int restMs() const { return opts_.rest_ms; }
    void setRestMs(int v);
    double peak() const { return opts_.peak_intensity; }
    void setPeak(double v);
    double plateauFraction() const { return opts_.plateau_fraction; }
    void setPlateauFraction(double v);
    double progression() const { return opts_.progression_per_cycle; }
    void setProgression(double v);

    QString category() const { return category_; }
    void setCategory(const QString& c);
    QVariantList programs() const;
    QVariantList myPrograms() const;
    QString scriptDescription() const;
    bool customScript() const;

    bool vibrationAvailable() const { return vib_available_; }
    bool suctionAvailable() const { return suc_available_; }
    QStringList channelOptions() const;
    bool canStart() const { return !blocked_; }
    QString deviceNote() const { return device_note_; }

    int planTotalMs() const { return plan_.total_ms; }
    QVariantList planPhases() const { return plan_phases_; }
    bool planHasJitter() const { return plan_.has_random_jitter; }
    double intensityFactor() const { return factor_; }
    QString intensityNote() const { return intensity_note_; }
    QString historySuggestion() const { return suggestion_; }

    QString state() const;
    bool active() const { return state_ != State::Idle; }
    bool paused() const { return state_ == State::Paused; }
    double vibration() const { return vib_; }
    double suction() const { return suc_; }
    QString phaseName() const { return phase_name_; }
    int phaseIndex() const { return phase_index_; }
    int phaseCount() const { return phase_count_; }
    QString cycleText() const { return cycle_text_; }
    QString peakText() const { return peak_text_; }
    int elapsedSec() const { return elapsed_sec_; }
    int remainingSec() const;
    int lastFeedback() const { return last_feedback_; }
    QString feedbackStatus() const { return feedback_status_; }
    QString feedbackEffect() const { return feedback_effect_; }
    QStringList logLines() const { return log_; }
    QVariantList history() const { return history_view_; }
    QVariantMap recap() const { return recap_; }

    // For the curve item: the plan and what the session did so far, one sample every
    // sampleMs() of session time.
    const samn::training::Preview& plan() const { return plan_; }
    const std::vector<float>& vibrationSamples() const { return vib_samples_; }
    const std::vector<float>& suctionSamples() const { return suc_samples_; }
    int sampleMs() const { return sample_ms_; }

    QString stage() const { return stage_; }
    void setStage(const QString& stage);
    QString videoPath() const { return video_path_; }
    QString videoUrl() const;
    QString videoName() const;
    bool videoLoop() const { return video_loop_; }
    void setVideoLoop(bool on);
    bool videoMuted() const { return video_muted_; }
    void setVideoMuted(bool on);
    QStringList recentVideos();

signals:
    void deviceChanged();
    void formChanged();
    void programsChanged();
    void deviceFollowChanged();
    void planChanged();
    void stateChanged();
    void levelsChanged();
    void progressChanged();
    void feedbackChanged();
    void logChanged();
    void historyChanged();
    void samplesChanged();
    void stageChanged();
    // Start was refused because the device has neither vibration nor suction.
    void startBlocked();
    // The session ended (naturally, by the user, by an error or the safety limit).
    void sessionFinished();

private:
    enum class State { Idle, Connecting, Running, Paused };
    class Adapter;

    samn::Settings& settings();
    void load();
    void save(const char* key, const nlohmann::json& value);
    void rebuildPrograms();
    void rebuildPlan();
    void updateDeviceFollow();
    std::optional<samn::training::Script> resolveScript() const;
    QString historyKey() const;
    void beginSession();
    void connectThenBegin();
    void onDeviceStatus();
    void failStart(const QString& message);
    void tick();
    void finishSession(const QString& logMessage = QString());
    void setState(State s);
    void log(const QString& line);
    void sampleLevels();
    void updateProgress(bool force);
    void onCycle(const samn::training::CycleResult& r);
    void onScriptCycle(const samn::training::ScriptCycleResult& r);
    void writeLog(const samn::training::LogEntry& e);

    std::unique_ptr<samn::Settings> settings_;
    // stage
    QString stage_ = QStringLiteral("rings"), video_path_;
    bool video_loop_ = true, video_muted_ = true;
    QPointer<DeviceController> device_;
    // form
    bool mock_ = true, auto_adjust_ = true;
    QString script_name_, category_;
    double tier_ = 1.0;
    samn::training::Options opts_;
    // programs
    std::vector<samn::training::Script> builtin_, mine_;
    QVariantList programs_, my_programs_;
    // device follow
    bool vib_available_ = true, suc_available_ = true, blocked_ = false;
    QString device_note_;
    // plan
    samn::training::Preview plan_;
    QVariantList plan_phases_;
    double factor_ = 1.0;
    QString intensity_note_, suggestion_;
    std::vector<samn::training::SessionSummary> history_;
    QVariantList history_view_;
    QVariantMap recap_;
    // session
    State state_ = State::Idle;
    std::unique_ptr<Adapter> adapter_;
    std::unique_ptr<samn::training::Session> session_;
    samn::training::Script run_script_;
    samn::training::Options run_opts_;
    QString run_key_;
    bool run_is_script_ = false;
    bool we_connected_ = false;
    QTimer timer_, connect_timeout_;
    QElapsedTimer clock_, wall_;
    qint64 last_tick_ns_ = 0;
    double time_scale_ = 1.0;
    qint64 safety_limit_ms_ = kSafetyLimitMs;
    int connect_timeout_ms_ = kConnectTimeoutMs;
    std::ofstream log_file_;
    double vib_ = 0, suc_ = 0;
    QString phase_name_, cycle_text_, peak_text_;
    int phase_index_ = 0, phase_count_ = 0, elapsed_sec_ = 0;
    int last_feedback_ = 0;
    QString feedback_status_, feedback_effect_;
    QStringList log_;
    std::vector<float> vib_samples_, suc_samples_;
    int sample_ms_ = 250;
    double next_sample_ms_ = 0;
};

}  // namespace samn::ui
