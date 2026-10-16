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

std::vector<samn::Action> sorted(std::vector<samn::Action> a) {
    std::stable_sort(a.begin(), a.end(), [](const samn::Action& x, const samn::Action& y) { return x.at < y.at; });
    return a;
}

// 0: off, else 50-1000 (0.7 clampMaxSpeed).
int clampSpeedLimit(int v) { return v <= 0 ? 0 : std::clamp(v, 50, 1000); }

}  // namespace

PlaybackOutput::PlaybackOutput(samn::Settings* settings, QObject* parent) : QObject(parent), settings_(settings) {
    stale_.setSingleShot(true);
    stale_.setInterval(kStaleMs);
    connect(&stale_, &QTimer::timeout, this, [this] {
        // No position for a while: the video stands (paused, buffering).
        if (playing_ && !halted_) goQuiet(true);
    });
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
        filter_.smooth = (s == "pchip" || s == "makima") ? s : "";
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

void PlaybackOutput::setDevice(DeviceController* device) {
    if (device_ == device) return;
    device_ = device;
    resetTargets();
    quiet_ = true;
}

void PlaybackOutput::setScript(std::vector<samn::Action> stroke, std::vector<samn::Action> vibration,
                               std::vector<samn::Action> suction,
                               std::map<std::string, std::vector<samn::Action>> axes) {
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

void PlaybackOutput::refilter() {
    filtered_ = samn::ApplyStrokeFilter(stroke_, filter_);
    strokeNext_ = -1;
}

void PlaybackOutput::resetTargets() {
    strokeNext_ = -1;
    soft_ = true;  // the next move eases in
    lastVib_ = lastSuc_ = -1;
    for (auto& [name, a] : axes_) a.next = -1;
}

bool PlaybackOutput::deviceReady() const { return device_ && device_->connected(); }

void PlaybackOutput::setPlaying(bool playing) {
    if (playing == playing_) return;
    playing_ = playing;
    if (playing) {
        halted_ = false;
        resetTargets();
        lastAt_ = -1;
        if (device_) device_->setSessionActive(true);
    } else {
        stale_.stop();
        // After an emergency stop the device is already stopped: no home move.
        if (!halted_) goQuiet(true);
        if (device_) device_->setSessionActive(false);
    }
    emit playingChanged();
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
        for (auto& [name, a] : axes_) a.next = -1;
    }
    lastAt_ = t;
    quiet_ = false;
    sendLevels(t);
    sendStroke(t);
    sendAxes(t);
}

void PlaybackOutput::sendLevels(qint64 t) {
    const auto level = [&](const std::vector<samn::Action>& lane, bool on, int pct) {
        if (!on) return 0.0;
        return std::clamp(samn::PositionAt(lane, t).value_or(0) / 100.0, 0.0, 1.0) * pct / 100.0;
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
    if (soft_) {
        const double way = lastPos_ >= 0 ? std::abs(pos - lastPos_) : 1.0;  // unknown start: the whole range
        dur = std::max(dur, int(way * kSoftSeekFullStrokeMs));
    }
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
        device_->setAxis(QString::fromStdString(name), it->pos / 100.0, moveMs(it->at, t));
        a.next = next;
    }
}

void PlaybackOutput::goQuiet(bool home) {
    const bool wasQuiet = quiet_;
    quiet_ = true;
    resetTargets();
    if (wasQuiet || !deviceReady()) return;
    // A stroker halts where it is; "home" then moves it slowly to the bottom.
    device_->stop();
    if (home && home_ && strokeOn_ && device_->capLinear() && !filtered_.empty()) {
        device_->setPosition(lo_ / 100.0, kHomeMs);
        lastPos_ = lo_ / 100.0;
    }
}

void PlaybackOutput::emergencyStop() {
    halted_ = true;
    stale_.stop();
    quiet_ = true;
    resetTargets();
    if (device_) {
        device_->emergencyStop();
        device_->setSessionActive(false);
    }
    emit emergencyStopped();
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
    const std::string v = (s == "pchip" || s == "makima") ? s.toStdString() : std::string();
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

void PlaybackOutput::setSpeed(double s) {
    if (!(s > 0) || s == speed_) return;
    speed_ = s;
    emit outputChanged();
}

}  // namespace samn::ui
