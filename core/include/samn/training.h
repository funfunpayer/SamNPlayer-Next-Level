#pragma once
// Training engine: stand-alone up/down cycles for stamina and control practice
// (port of 0.7 player/training.go plus the "level programs, categories and body
// cues" of the training-levels-recap branch). No Qt, no threads.
//
// The 0.7 engine ran goroutines against the wall clock. This one is a state
// machine that the caller drives with advance(dt_ms): the same writes in the
// same order (50 ms ramp steps, interruptible holds and rests), but the clock
// is the caller's, so the Qt app ticks it from a timer and the tests run a
// whole session in microseconds.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace samn::training {

enum class Technique { StopStart, Plateau };
enum class Channel { Vibration, Suction, Both };

// "stopstart" | "plateau", "vibration" | "suction" | "both" (the 0.7 names).
std::string to_string(Technique t);
std::string to_string(Channel c);
Technique technique_from(const std::string& s);  // unknown -> StopStart
Channel channel_from(const std::string& s);      // unknown -> Vibration

// A single-curve session (the Custom form).
struct Options {
    Technique technique = Technique::StopStart;
    Channel channel = Channel::Vibration;
    int cycles = 0;
    int ramp_up_ms = 0, hold_ms = 0, rest_ms = 0;
    double peak_intensity = 0;
    double plateau_fraction = 0;       // Plateau only: floor as a share of the peak
    double progression_per_cycle = 0;  // the last cycle is this much stronger / longer
};

// One channel's level across one repeat of a phase.
struct ChannelCurve {
    double start_level = 0, peak_level = 0, end_level = 0;
    int ramp_up_ms = 0, hold_ms = 0, ramp_down_ms = 0;
    double random_jitter_fraction = 0;  // > 0: the peak varies by up to this share per repeat
    bool start_from_current = false;    // climb from where the channel is, not from start_level
};

// A phase: optional curve per channel (absent = channel untouched, keeps its level).
struct Phase {
    std::string name;
    std::optional<ChannelCurve> vibration, suction;
    int repeat_cycles = 0;
    int rest_ms = 0;
};

inline constexpr const char* kCategoryControl = "control";
inline constexpr const char* kCategoryEndurance = "endurance";
inline constexpr const char* kCategoryEdge = "edge";
inline constexpr const char* kCategoryBody = "body";

struct Script {
    std::string name, description;
    std::string category;  // control | endurance | edge | body ("" for custom scripts)
    int level = 0;         // 1-3, 0 = unset
    std::vector<Phase> phases;
    double progression_per_cycle = 0;
};

// JSON in the 0.7 shape (a custom script saved by 0.7 loads here and back).
void to_json(nlohmann::json& j, const ChannelCurve& c);
void from_json(const nlohmann::json& j, ChannelCurve& c);
void to_json(nlohmann::json& j, const Phase& p);
void from_json(const nlohmann::json& j, Phase& p);
void to_json(nlohmann::json& j, const Script& s);
void from_json(const nlohmann::json& j, Script& s);

// ---- numbers ----
double clamp01(double v);
double clamp_range(double v, double lo, double hi);

// Feedback scale 1-10, target 7. Above: shorter hold, longer rest, lower peak
// (strongly); below: slightly longer hold, rest never shortened.
inline constexpr int kArousalTarget = 7;
struct Adjusted {
    double peak;
    int hold_ms, rest_ms;
};
Adjusted adjust_for_arousal(int arousal, double peak, int hold_ms, int rest_ms);
struct ArousalFactors {
    double peak, hold, rest;
};
ArousalFactors arousal_factors(int arousal);

// ---- scripts ----
const std::vector<Script>& builtin_scripts();
const Script* builtin_script(const std::string& name);

// Every level times factor (clamped), timing untouched; factor <= 0 or 1: unchanged.
Script scale_script(const Script& s, double factor);
// Empty string = valid, else the reason.
std::string validate_script(const Script& s);
// One start-from-current-aware, progress / feedback / jitter scaled curve.
ChannelCurve scaled_curve(const ChannelCurve& base, double progress, double peak_factor, double hold_factor,
                          const std::function<double()>& rng);

// ---- plan preview ----
struct CurvePoint {
    int at_ms;
    double level;
};
struct PhaseMarker {
    int at_ms;
    std::string name;
};
struct Preview {
    std::vector<CurvePoint> vibration, suction;
    int total_ms = 0;
    std::vector<PhaseMarker> markers;
    bool has_random_jitter = false;
};
// The nominal plan (no feedback, no random jitter).
Preview build_preview(const Script& s);
// The Custom form as a plan: one marker "Cycle N" per cycle.
Preview build_preview(const Options& o);
std::string validate_options(const Options& o);
// Mean planned level of the stronger channel between two times (the phase tint of the timeline).
double phase_strength(const Preview& p, int from_ms, int to_ms);

// ---- session ----
class Device {
public:
    virtual ~Device() = default;
    virtual void set_vibration(double level) = 0;
    virtual void set_suction(double level) = 0;
    virtual void stop() = 0;
};

struct CycleResult {  // Custom form, one per cycle
    int cycle_index = 0;
    double peak_intensity = 0;
    int hold_ms = 0, rest_ms = 0;
    double started_at_ms = 0, ended_at_ms = 0;  // session clock
    bool stopped_by_user = false;
    int reached_peak_after_ms = 0;
    int arousal_before = 0;
};
struct ScriptCycleResult {  // script, one per phase repeat
    int phase_index = 0;
    std::string phase_name;
    int phases_total = 0, repeat_index = 0, repeats_total = 0;
    double vibration_peak = 0, suction_peak = 0;
    int rest_ms = 0;
    double started_at_ms = 0, ended_at_ms = 0;
    bool stopped_by_user = false;
    int reached_peak_after_ms = 0;
    int arousal_before = 0;
    double peak_factor_applied = 1, rest_factor_applied = 1;
};

// Where the session is, for the live readout.
struct Position {
    int phase_index = 0, phases_total = 0;
    int repeat_index = 0, repeats_total = 0;  // within the phase (Custom: cycle / cycles)
    std::string phase_name;
    // The peaks this repeat / cycle aims at (after progression and feedback); 0 for an untouched channel.
    double vibration_peak = 0, suction_peak = 0;
};

class Session {
public:
    using Rng = std::function<double()>;  // uniform [0, 1)
    // nullptr and *error on an invalid request. The device must outlive the session.
    static std::unique_ptr<Session> custom(const Options& o, Device* dev, std::string* error, Rng rng = {});
    static std::unique_ptr<Session> script(const Script& s, Device* dev, std::string* error, Rng rng = {});
    ~Session();

    // Move the session clock on by dt_ms (ignored while paused or finished).
    void advance(double dt_ms);
    // End the current cycle now: levels to 0, then the full rest; the session goes on.
    void stop_cycle();
    // Both channels to 0 at once, the clock stands still; resume restores the levels.
    void pause();
    void resume();
    bool paused() const { return paused_; }
    // Feedback 1-10 (others ignored); shapes the NEXT cycle only.
    void report_arousal(int level);
    // End the session now (the user pressed End): the device is stopped, no closing ramp.
    void abort();
    bool finished() const { return finished_; }

    // What the device really gets: 0 while paused.
    double vibration() const { return paused_ ? 0 : vib_; }
    double suction() const { return paused_ ? 0 : suc_; }
    double elapsed_ms() const { return clock_; }  // session clock (time spent paused not counted)
    Position position() const { return pos_; }
    bool is_script() const { return script_mode_; }

    // Callbacks; set before the first advance().
    std::function<void(double vibration, double suction)> on_levels;
    std::function<void(const CycleResult&)> on_cycle;
    std::function<void(const ScriptCycleResult&)> on_script_cycle;

private:
    struct Op {
        enum Kind : uint8_t { Write, Wait, Check } kind;
        bool vib, suc;
        double v;  // Write: level; Wait: ms
    };
    struct Lane {
        std::vector<Op> ops;
        size_t pos = 0;
        double left = 0;
        bool waiting = false, done = true, stopped = false;
    };
    enum class Stage {
        Begin, Repeat, RestInterruptible, RestPlain, Closing,  // script
        RampUp, Hold, RampDown, Rest                            // custom
    };

    Session(Device* dev, Rng rng);
    void write(bool vib, bool suc, double level);
    void notify();
    static void ramp(Lane& l, bool vib, bool suc, double from, double to, int ms);
    static void hold(Lane& l, double ms);
    static void wait(Lane& l, double ms);
    static void start(Lane& l);
    double run_lane(Lane& l, double budget);
    double step(double budget, bool& changed);
    double step_script(double budget, bool& changed);
    double step_custom(double budget, bool& changed);
    void begin_repeat();
    void finish_repeat();
    void begin_cycle();
    void after_hold(bool stopped);
    void end_cycle();
    void finish();
    int take_arousal();

    Device* dev_;
    Rng rng_;
    bool script_mode_ = false;
    Options opts_;
    Script script_;
    int total_repeats_ = 0;

    Stage stage_ = Stage::Begin;
    Lane vl_, sl_;  // channel lanes (script) / lane 0 = vl_ (custom)
    double clock_ = 0;
    bool paused_ = false, finished_ = false, held_ = false;
    bool stop_requested_ = false;
    int arousal_ = 0;
    double vib_ = 0, suc_ = 0;
    Position pos_;

    // script state
    int pi_ = 0, ri_ = 0, global_ = 0;
    double last_vib_ = 0, last_suc_ = 0;
    std::optional<ChannelCurve> cur_vib_, cur_suc_;
    ScriptCycleResult sres_;
    int rest_ms_ = 0;

    // custom state
    int ci_ = 0;
    double level_ = 0, cycle_peak_ = 0, target_ = 0;
    CycleResult cres_;
};

}  // namespace samn::training
