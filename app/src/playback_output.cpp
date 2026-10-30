#include "playback_output.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "device_controller.h"
#include "samn/curve_geometry.h"
#include "samn/settings.h"

namespace samn::ui {

namespace {

constexpr const char* kKeySmooth = "playback.stroke_smooth";
constexpr const char* kKeyMaxSpeed = "playback.stroke_max_speed";
constexpr const char* kKeyIdleFill = "playback.stroke_idle_fill";
constexpr const char* kKeyHome = "playback.stroke_home_on_pause";
constexpr const char* kKeyStrokeOn = "playback.stroke_on";
constexpr const char* kKeyLo = "playback.stroke_lo";
constexpr const char* kKeyHi = "playback.stroke_hi";
constexpr const char* kKeyVibOn = "playback.channel_vibration_on";
constexpr const char* kKeyVibLevel = "playback.channel_vibration_level";
constexpr const char* kKeySucOn = "playback.channel_suction_on";
constexpr const char* kKeySucLevel = "playback.channel_suction_level";
constexpr const char* kKeyIntensity = "playback.channel_intensity";
constexpr const char* kKeySoftStart = "playback.soft_start_ms";
constexpr const char* kKeyReducePeaks = "playback.channel_reduce_peaks";
constexpr const char* kKeyEoEnabled = "playback.extended_o_enabled";
constexpr const char* kKeyEoMin = "playback.extended_o_min";
constexpr const char* kKeyEoHold = "playback.extended_o_hold_seconds";
constexpr const char* kKeyEoRestore = "playback.extended_o_restore_ms";

std::vector<samn::Action> sorted(std::vector<samn::Action> a) {
    std::stable_sort(a.begin(), a.end(), [](const samn::Action& x, const samn::Action& y) { return x.at < y.at; });
    return a;
}

// 0: off, else 50-1000 (0.7 clampMaxSpeed).
int clampSpeedLimit(int v) { return v <= 0 ? 0 : std::clamp(v, 50, 1000); }

int clampSoftStart(int v) { return std::clamp(v, 0, PlaybackOutput::kSoftStartMaxMs); }
int64_t holdMs(double s) {
    if (!std::isfinite(s)) s = 10;  // the 0.7 default
    return std::llround(std::clamp(s, PlaybackOutput::kExtendedOHoldMinS, PlaybackOutput::kExtendedOHoldMaxS) * 1000);
}

}  // namespace

PlaybackOutput::PlaybackOutput(samn::Settings* settings, QObject* parent) : QObject(parent), settings_(settings) {
    stale_.setSingleShot(true);
    stale_.setInterval(kStaleMs);
    connect(&stale_, &QTimer::timeout, this, [this] {
        // No position for a while: the video stands (paused, buffering).
        if (playing_ && !halted_) goQuiet(true);
    });
    mono_.start();
    ramp_.setInterval(kRampTickMs);
    connect(&ramp_, &QTimer::timeout, this, &PlaybackOutput::rampTick);
    load();
}

void PlaybackOutput::load() {
    if (!settings_) return;
    try {
        const auto str = [&](const char* k) {
            const auto v = settings_->get(k, "");
            return v.is_string() ? v.get<std::string>() : std::string();
        };
        const auto num = [&](const char* k, double d) {
            const auto v = settings_->get(k, d);
            return v.is_number() ? v.get<double>() : d;
        };
        const auto flag = [&](const char* k, bool d) {
            const auto v = settings_->get(k, d);
            return v.is_boolean() ? v.get<bool>() : d;
        };
        const std::string s = str(kKeySmooth);
        filter_.smooth = samn::NormalizeSmooth(s);
        const double ms = num(kKeyMaxSpeed, 0);
        filter_.maxSpeed = std::isfinite(ms) ? clampSpeedLimit(int(std::lround(ms))) : 0;
        filter_.idleFillMs = flag(kKeyIdleFill, false) ? kIdleFillMs : 0;
        home_ = flag(kKeyHome, false);
        strokeOn_ = flag(kKeyStrokeOn, true);
        int lo = int(std::lround(num(kKeyLo, 0))), hi = int(std::lround(num(kKeyHi, 100)));
        lo = std::clamp(lo, 0, 100 - kMinStrokeSpan);
        hi = std::clamp(hi, lo + kMinStrokeSpan, 100);
        lo_ = lo;
        hi_ = hi;
        vibOn_ = flag(kKeyVibOn, true);
        sucOn_ = flag(kKeySucOn, true);
        vibLevel_ = std::clamp(int(std::lround(num(kKeyVibLevel, 100))), 0, 100);
        sucLevel_ = std::clamp(int(std::lround(num(kKeySucLevel, 100))), 0, 100);
        // A missing or broken value is "unset" and takes the default; a
        // stored 0 stays 0 (PS-11).
        const auto finite = [&](const char* k, double d) {
            const double v = num(k, d);
            return std::isfinite(v) ? v : d;
        };
        intensity_ = std::clamp(int(std::lround(finite(kKeyIntensity, 100))), 0, 100);
        softStartMs_ = clampSoftStart(int(std::lround(finite(kKeySoftStart, kSoftStartDefaultMs))));
        reducePeaks_ = flag(kKeyReducePeaks, false);
        eoEnabled_ = flag(kKeyEoEnabled, true);
        const samn::ExtendedOOptions d;
        eo_.minLevel = std::clamp(finite(kKeyEoMin, d.minLevel), 0.0, 1.0);
        eo_.holdMs = holdMs(finite(kKeyEoHold, double(d.holdMs) / 1000.0));
        eo_.restoreMs = std::clamp<int64_t>(std::llround(finite(kKeyEoRestore, double(d.restoreMs))), 0, kExtendedORestoreMaxMs);
    } catch (const std::exception&) {
        // A broken settings value keeps the defaults.
    }
}

void PlaybackOutput::save(const char* key, const nlohmann::json& value) {
    if (!settings_) return;
    try {
        settings_->set(key, value);
    } catch (const std::exception&) {
        // Not saving a slider position must never stop the playback.
    }
}

PlaybackOutput::~PlaybackOutput() {
    // The session hold is this output's own: it ends with it, and so does
    // what it was sending.
    if (playing_ && device_) {
        if (!halted_) device_->stop();
        device_->setSessionActiveFor(this, false);
    }
}

void PlaybackOutput::setDevice(DeviceController* device) {
    if (device_ == device) return;
    if (playing_ && device_) device_->setSessionActiveFor(this, false);
    device_ = device;
    // An emergency stop gave the hold up; output stays halted until a new play.
    if (playing_ && !halted_ && device_) device_->setSessionActiveFor(this, true);
    resetTargets();
    // Where the new device is, is not known: moves from there are measured
    // over the whole range (speed limit, soft seek).
    lastPos_ = -1;
    for (auto& entry : axes_) entry.second.lastPos = -1;
    quiet_ = true;
}

void PlaybackOutput::setScript(std::vector<samn::Action> stroke, std::vector<samn::Action> vibration,
                               std::vector<samn::Action> suction,
                               std::map<std::string, std::vector<samn::Action>> axes) {
    endExtendedO();  // a new script is a new session
    stroke_ = sorted(std::move(stroke));
    vibration_ = std::move(vibration);
    suction_ = std::move(suction);
    axes_.clear();
    for (auto& [name, acts] : axes)
        if (!acts.empty()) axes_[name].actions = sorted(std::move(acts));
    refilter();
    resetTargets();
    lastAt_ = -1;
}

void PlaybackOutput::setLevels(std::vector<samn::Action> vibration, std::vector<samn::Action> suction) {
    vibration_ = std::move(vibration);
    suction_ = std::move(suction);
    lastVib_ = lastSuc_ = -1;
    if (sending() && lastAt_ >= 0) sendLevels(lastAt_);
}

void PlaybackOutput::refilter() {
    filtered_ = samn::ApplyStrokeFilter(stroke_, filter_);
    strokeNext_ = -1;
}

void PlaybackOutput::resetTargets() {
    strokeNext_ = -1;
    soft_ = true;  // the next move eases in
    lastVib_ = lastSuc_ = -1;
    for (auto& [name, a] : axes_) {
        a.next = -1;
        a.soft = true;
    }
}

bool PlaybackOutput::deviceReady() const { return device_ && device_->connected(); }

void PlaybackOutput::setPlaying(bool playing) {
    if (playing == playing_) return;
    playing_ = playing;
    if (playing) {
        halted_ = false;
        resetTargets();
        lastAt_ = -1;
        if (device_) device_->setSessionActiveFor(this, true);
    } else {
        stale_.stop();
        // After an emergency stop the device is already stopped: no home move.
        if (!halted_) goQuiet(true);
        if (device_) device_->setSessionActiveFor(this, false);
    }
    emit playingChanged();
    emit extendedOChanged();
}

void PlaybackOutput::update(qint64 t) {
    if (!playing_ || halted_) return;
    stale_.start();
    if (!deviceReady()) {
        lastAt_ = t;
        return;
    }
    // A jump of the playhead: the first stroke move eases in (soft seek).
    if (lastAt_ >= 0 && (t < lastAt_ - kSoftSeekBackMs || t > lastAt_ + kSoftSeekJumpMs)) {
        soft_ = true;
        strokeNext_ = -1;
        for (auto& [name, a] : axes_) {
            a.next = -1;
            a.soft = true;
        }
    }
    lastAt_ = t;
    // The first position after a start, a pause or a quiet period: the
    // levels ramp up (soft start).
    if (quiet_) {
        if (softStartMs_ > 0)
            softFrom_ = now();
        else
            softFrom_.reset();
    }
    quiet_ = false;
    sendLevels(t);
    sendStroke(t);
    sendAxes(t);
    updateRampTimer();
}

void PlaybackOutput::sendLevels(qint64 t) {
    const qint64 n = now();
    double soft = 1;
    if (softFrom_) {
        soft = samn::SoftStartFactor(n - *softFrom_, softStartMs_);
        if (!samn::SoftStartRunning(n - *softFrom_, softStartMs_)) softFrom_.reset();
    }
    const double scale = extendedOScale() * soft;
    const auto level = [&](const std::vector<samn::Action>& lane, bool on, int pct) {
        if (!on) return 0.0;
        return samn::OutputLevel(samn::PositionAt(lane, t).value_or(0) / 100.0, reducePeaks_, scale,
                                 pct / 100.0 * intensity_ / 100.0);
    };
    if (device_->capVibration() && !vibration_.empty()) {
        const double v = level(vibration_, vibOn_, vibLevel_);
        if (lastVib_ < 0 || std::abs(v - lastVib_) >= 0.005) {
            device_->setVibration(v);
            lastVib_ = v;
        }
    }
    if (device_->capSuction() && !suction_.empty()) {
        const double s = level(suction_, sucOn_, sucLevel_);
        if (lastSuc_ < 0 || std::abs(s - lastSuc_) >= 0.005) {
            device_->setSuction(s);
            lastSuc_ = s;
        }
    }
}

int PlaybackOutput::moveMs(qint64 dueAt, qint64 now) const {
    const double ms = double(dueAt - now) / std::max(0.1, speed_);
    return std::max<int>(kMinMoveMs, int(ms));
}

int PlaybackOutput::speedLimitMs(double way) const {
    if (filter_.maxSpeed <= 0) return 0;
    return int(std::ceil(way * 100.0 * 1000.0 / filter_.maxSpeed));
}

void PlaybackOutput::sendStroke(qint64 t) {
    if (!device_->capLinear() || filtered_.empty()) return;
    if (!strokeOn_) {
        strokeNext_ = -1;  // switched back on: the next position sends
        return;
    }
    const auto it = std::upper_bound(filtered_.begin(), filtered_.end(), t,
                                     [](qint64 v, const samn::Action& a) { return v < a.at; });
    if (it == filtered_.end()) return;  // past the last action: hold
    const int next = int(it - filtered_.begin());
    if (next == strokeNext_) return;    // already moving there
    const double lo = lo_ / 100.0, hi = hi_ / 100.0;
    const double pos = lo + it->pos / 100.0 * (hi - lo);
    int dur = moveMs(it->at, t);
    const double way = lastPos_ >= 0 ? std::abs(pos - lastPos_) : 1.0;  // unknown start: the whole range
    if (soft_) dur = std::max(dur, int(way * kSoftSeekFullStrokeMs));
    // The filter limits the script; a move starts at a position report, so a
    // late report, dense actions or a play speed above 1 would still squeeze
    // the way from the last sent position into a shorter time.
    dur = std::max(dur, speedLimitMs(way));
    device_->setPosition(pos, dur);
    soft_ = false;
    lastPos_ = pos;
    strokeNext_ = next;
}

void PlaybackOutput::sendAxes(qint64 t) {
    if (axes_.empty() || !strokeOn_) {
        for (auto& [name, a] : axes_) a.next = -1;
        return;
    }
    const QStringList have = device_->axes();
    for (auto& [name, a] : axes_) {
        if (name == "stroke" || !have.contains(QString::fromStdString(name))) continue;
        const auto it = std::upper_bound(a.actions.begin(), a.actions.end(), t,
                                         [](qint64 v, const samn::Action& x) { return v < x.at; });
        if (it == a.actions.end()) continue;
        const int next = int(it - a.actions.begin());
        if (next == a.next) continue;
        const double pos = it->pos / 100.0;
        int dur = moveMs(it->at, t);
        if (a.soft) {  // eases in like the stroke
            const double way = a.lastPos >= 0 ? std::abs(pos - a.lastPos) : 1.0;
            dur = std::max(dur, int(way * kSoftSeekFullStrokeMs));
        }
        device_->setAxis(QString::fromStdString(name), pos, dur);
        a.soft = false;
        a.lastPos = pos;
        a.next = next;
    }
}

void PlaybackOutput::goQuiet(bool home) {
    const bool wasQuiet = quiet_;
    quiet_ = true;
    resetTargets();
    // A running soft start ends here: nothing of the ramp is sent after the
    // stop, and the ramp state never turns the stop into a no-op (PS-04).
    softFrom_.reset();
    updateRampTimer();
    if (wasQuiet || !deviceReady()) return;
    // A stroker halts where it is; "home" then moves it slowly to the bottom.
    device_->stop();
    if (home && home_ && strokeOn_ && device_->capLinear() && !filtered_.empty()) {
        // Where the stop left the stroker is unknown: the speed limit counts the whole range.
        device_->setPosition(lo_ / 100.0, std::max(kHomeMs, speedLimitMs(1.0)));
        lastPos_ = lo_ / 100.0;
    }
}

void PlaybackOutput::emergencyStop() {
    halted_ = true;
    stale_.stop();
    quiet_ = true;
    resetTargets();
    softFrom_.reset();
    endExtendedO();
    ramp_.stop();
    if (device_) {
        device_->emergencyStop();
        device_->setSessionActiveFor(this, false);
    }
    emit emergencyStopped();
    emit extendedOChanged();
}

void PlaybackOutput::setStrokeOn(bool on) {
    if (on == strokeOn_) return;
    strokeOn_ = on;
    strokeNext_ = -1;
    save(kKeyStrokeOn, on);
    emit outputChanged();
}

void PlaybackOutput::setStrokeRange(int lo, int hi, bool movedLo) {
    lo = std::clamp(lo, 0, 100);
    hi = std::clamp(hi, 0, 100);
    // At least 10 % apart: the end being moved pushes the other one.
    if (hi - lo < kMinStrokeSpan) {
        if (movedLo)
            hi = std::min(100, lo + kMinStrokeSpan), lo = hi - kMinStrokeSpan;
        else
            lo = std::max(0, hi - kMinStrokeSpan), hi = lo + kMinStrokeSpan;
    }
    if (lo == lo_ && hi == hi_) return;
    lo_ = lo;
    hi_ = hi;
    strokeNext_ = -1;  // the running move is re-sent inside the new range
    save(kKeyLo, lo_);
    save(kKeyHi, hi_);
    emit outputChanged();
}

void PlaybackOutput::setSmooth(const QString& s) {
    const std::string v = samn::NormalizeSmooth(s.toStdString());
    if (v == filter_.smooth) return;
    filter_.smooth = v;
    save(kKeySmooth, v);
    refilter();
    emit outputChanged();
}

void PlaybackOutput::setMaxSpeed(int v) {
    v = clampSpeedLimit(v);
    if (v == int(filter_.maxSpeed)) return;
    filter_.maxSpeed = v;
    save(kKeyMaxSpeed, v);
    refilter();
    emit outputChanged();
}

void PlaybackOutput::setIdleFill(bool on) {
    if (on == idleFill()) return;
    filter_.idleFillMs = on ? kIdleFillMs : 0;
    save(kKeyIdleFill, on);
    refilter();
    emit outputChanged();
}

void PlaybackOutput::setHomeOnPause(bool on) {
    if (on == home_) return;
    home_ = on;
    save(kKeyHome, on);
    emit outputChanged();
}

void PlaybackOutput::setVibrationOn(bool on) {
    if (on == vibOn_) return;
    vibOn_ = on;
    lastVib_ = -1;
    save(kKeyVibOn, on);
    emit outputChanged();
}

void PlaybackOutput::setVibrationLevel(int v) {
    v = std::clamp(v, 0, 100);
    if (v == vibLevel_) return;
    vibLevel_ = v;
    save(kKeyVibLevel, v);
    emit outputChanged();
}

void PlaybackOutput::setSuctionOn(bool on) {
    if (on == sucOn_) return;
    sucOn_ = on;
    lastSuc_ = -1;
    save(kKeySucOn, on);
    emit outputChanged();
}

void PlaybackOutput::setSuctionLevel(int v) {
    v = std::clamp(v, 0, 100);
    if (v == sucLevel_) return;
    sucLevel_ = v;
    save(kKeySucLevel, v);
    emit outputChanged();
}

void PlaybackOutput::setIntensity(int v) {
    v = std::clamp(v, 0, 100);
    if (v == intensity_) return;
    intensity_ = v;
    save(kKeyIntensity, v);
    if (sending() && lastAt_ >= 0) sendLevels(lastAt_);
    emit outputChanged();
}

void PlaybackOutput::setSpeed(double s) {
    if (!(s > 0) || s == speed_) return;
    speed_ = s;
    emit outputChanged();
}

void PlaybackOutput::setClock(std::function<qint64()> clock, bool rampTimer) {
    clock_ = std::move(clock);
    rampTimerOn_ = rampTimer;
    updateRampTimer();
}

double PlaybackOutput::extendedOScale() {
    if (!cycle_) return 1;
    const qint64 t = now() - cycle_->from;
    if (t >= samn::ExtendedOTotalMs(cycle_->opts)) {
        endExtendedO();
        return 1;
    }
    return samn::ExtendedOScale(cycle_->opts, t);
}

void PlaybackOutput::endExtendedO() {
    if (!cycle_) return;
    cycle_.reset();
    emit extendedOChanged();
}

bool PlaybackOutput::triggerExtendedO() {
    if (!canTriggerExtendedO()) return false;
    extendedOScale();  // ends a cycle whose time is up
    if (cycle_) return false;  // one at a time: the trigger is ignored (0.7)
    cycle_ = Cycle{now(), samn::Clamped(eo_)};
    emit extendedOChanged();
    if (sending() && lastAt_ >= 0) sendLevels(lastAt_);  // the first step at once
    updateRampTimer();
    return true;
}

void PlaybackOutput::rampTick() {
    extendedOScale();  // ends a finished cycle, also while paused
    if (sending() && lastAt_ >= 0) sendLevels(lastAt_);
    updateRampTimer();
}

void PlaybackOutput::updateRampTimer() {
    const bool need = rampTimerOn_ && ((softFrom_ && sending()) || cycle_);
    if (need && !ramp_.isActive())
        ramp_.start();
    else if (!need && ramp_.isActive())
        ramp_.stop();
}

void PlaybackOutput::setSoftStartMs(int ms) {
    ms = clampSoftStart(ms);
    if (ms == softStartMs_) return;
    softStartMs_ = ms;
    save(kKeySoftStart, ms);
    emit outputChanged();
}

void PlaybackOutput::setReducePeaks(bool on) {
    if (on == reducePeaks_) return;
    reducePeaks_ = on;
    save(kKeyReducePeaks, on);
    if (sending() && lastAt_ >= 0) sendLevels(lastAt_);
    emit outputChanged();
}

void PlaybackOutput::setExtendedOEnabled(bool on) {
    if (on == eoEnabled_) return;
    eoEnabled_ = on;
    save(kKeyEoEnabled, on);
    emit outputChanged();
    emit extendedOChanged();
}

void PlaybackOutput::setExtendedOMin(double v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, 0.0, 1.0);
    if (v == eo_.minLevel) return;
    eo_.minLevel = v;
    save(kKeyEoMin, v);
    emit outputChanged();
}

void PlaybackOutput::setExtendedOHoldS(double s) {
    if (!std::isfinite(s)) return;
    const int64_t ms = holdMs(s);
    if (ms == eo_.holdMs) return;
    eo_.holdMs = ms;
    save(kKeyEoHold, double(ms) / 1000.0);
    emit outputChanged();
}

void PlaybackOutput::setExtendedORestoreMs(int ms) {
    ms = std::clamp(ms, 0, kExtendedORestoreMaxMs);
    if (ms == eo_.restoreMs) return;
    eo_.restoreMs = ms;
    save(kKeyEoRestore, ms);
    emit outputChanged();
}

}  // namespace samn::ui
