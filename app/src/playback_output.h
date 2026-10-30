#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "samn/funscript.h"
#include "samn/output_levels.h"
#include "samn/settings.h"
#include "samn/stroke_filter.h"

namespace samn::ui {

class DeviceController;

// What reaches the device while a script plays (0.7 player/sync.go, stroke.go,
// axes.go and app_stroke_filter.go), in C++ and without QML timers.
//
// The Player reports the playhead (script time) at every video position
// update (about 4 to 10 times a second, as the 0.7 timeupdate); each report
// sends:
//  - vibration and suction at the playhead (the mapped curves, 0-100) times
//    the channel level and the master intensity, or 0 when the channel is
//    switched off;
//  - for a stroker, the NEXT stroke action as "move there in the time until it
//    is due" (min 20 ms), through the stroke filter and the safety range;
//  - the same for the extra TCode axes the script carries.
// A start, a pause and a jump of the playhead (a step back of more than
// 250 ms or a step forward of more than 1.5 s) ease the first stroke move in:
// a full stroke takes at least 500 ms (soft seek). Each extra axis eases its
// own first move in the same way, from where it was sent last.
// With a speed limit, no move is shorter than the way from the last sent
// position takes at that speed (after the play-speed division); the home
// move counts the whole range.
// A pause, a stop and a missing position (1.5 s) stop the device and, when
// "Move home on pause" is on, send the stroker to the bottom of its range in
// 1.2 s. The emergency stop drops queued levels, stops at once and keeps the
// output silent until playback starts again.
// The levels go through Reduce peaks (when on), the channel level times the
// master intensity, the Extended-O scale and the soft start (0.7 setOutput):
//  - soft start: after the first position of a start, a pause or a quiet
//    period the levels ramp from 0 to the script in 10 steps over
//    softStartMs (0 = off). A pause, a stop or the watchdog inside the ramp
//    still stops the device at once, with the home move (0.7 PS-04);
//  - Extended-O: triggerExtendedO() lowers vibration and suction to
//    extendedOMin of the curve (the curve keeps its rhythm) over the
//    restore time, holds, and ramps back. A trigger while a cycle runs is
//    ignored. A minimum of 0 is silence (PS-11). The cycle runs on the wall
//    clock, also through a pause (0.7); the emergency stop and a new script
//    end it.
// A 20 ms timer re-sends the levels while a ramp runs, so the steps do not
// wait for the next position report.
// While playing, DeviceController::sessionActive is true.
class PlaybackOutput : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // Stroker: send the stroke and the extra axes, and the range (0-100 %,
    // at least 10 % apart) the device may move in.
    Q_PROPERTY(bool strokeOn READ strokeOn WRITE setStrokeOn NOTIFY outputChanged)
    Q_PROPERTY(int strokeLo READ strokeLo NOTIFY outputChanged)
    Q_PROPERTY(int strokeHi READ strokeHi NOTIFY outputChanged)
    // Stroke filter: "" (raw), "pchip" or "makima"; speed limit in units per
    // second (0 = none, else 50-1000); fill long pauses; home on pause.
    Q_PROPERTY(QString smooth READ smooth WRITE setSmooth NOTIFY outputChanged)
    Q_PROPERTY(int maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY outputChanged)
    Q_PROPERTY(bool idleFill READ idleFill WRITE setIdleFill NOTIFY outputChanged)
    Q_PROPERTY(bool homeOnPause READ homeOnPause WRITE setHomeOnPause NOTIFY outputChanged)
    // Per-channel on / off and level 0-100 (0.7 SetChannelLevels).
    Q_PROPERTY(bool vibrationOn READ vibrationOn WRITE setVibrationOn NOTIFY outputChanged)
    Q_PROPERTY(int vibrationLevel READ vibrationLevel WRITE setVibrationLevel NOTIFY outputChanged)
    Q_PROPERTY(bool suctionOn READ suctionOn WRITE setSuctionOn NOTIFY outputChanged)
    Q_PROPERTY(int suctionLevel READ suctionLevel WRITE setSuctionLevel NOTIFY outputChanged)
    // Master intensity 0-100 on top of the channel levels (0.7 Effects
    // "Intensity"; 0 is silence, a real value).
    Q_PROPERTY(int intensity READ intensity WRITE setIntensity NOTIFY outputChanged)
    // Play speed of the video (move durations are divided by it).
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY outputChanged)
    // Soft start of the levels in ms (0.7 playback.soft_start_ms): 0 = off,
    // else up to 3000; default 500.
    Q_PROPERTY(int softStartMs READ softStartMs WRITE setSoftStartMs NOTIFY outputChanged)
    // Reduce peaks (0.7 Effects): above 60 % softened, full comes out at 76 %.
    Q_PROPERTY(bool reducePeaks READ reducePeaks WRITE setReducePeaks NOTIFY outputChanged)
    // Extended-O (0.7 playback.extended_o_*): on / off, the minimum 0-1
    // (0 = off, a real value), the hold 1-60 s and the restore ramp 0-3000 ms.
    Q_PROPERTY(bool extendedOEnabled READ extendedOEnabled WRITE setExtendedOEnabled NOTIFY outputChanged)
    Q_PROPERTY(double extendedOMin READ extendedOMin WRITE setExtendedOMin NOTIFY outputChanged)
    Q_PROPERTY(double extendedOHoldS READ extendedOHoldS WRITE setExtendedOHoldS NOTIFY outputChanged)
    Q_PROPERTY(int extendedORestoreMs READ extendedORestoreMs WRITE setExtendedORestoreMs NOTIFY outputChanged)
    // A cycle is running; the trigger is available (playing and enabled).
    Q_PROPERTY(bool extendedOActive READ extendedOActive NOTIFY extendedOChanged)
    Q_PROPERTY(bool canTriggerExtendedO READ canTriggerExtendedO NOTIFY extendedOChanged)
public:
    static constexpr int kMinMoveMs = 20;
    static constexpr int kSoftSeekFullStrokeMs = 500;
    static constexpr int kSoftSeekJumpMs = 1500;
    static constexpr int kSoftSeekBackMs = 250;
    static constexpr int kHomeMs = 1200;
    static constexpr int kStaleMs = 1500;
    static constexpr int kMinStrokeSpan = 10;  // percent
    static constexpr int kIdleFillMs = 4000;
    static constexpr int kSoftStartDefaultMs = 500;
    static constexpr int kSoftStartMaxMs = 3000;
    static constexpr int kRampTickMs = 20;
    static constexpr double kExtendedOHoldMinS = 1;
    static constexpr double kExtendedOHoldMaxS = 60;
    static constexpr int kExtendedORestoreMaxMs = 3000;

    // settings: where the choices are kept (the 0.7 keys playback.stroke_*);
    // may be null. Not owned.
    explicit PlaybackOutput(samn::Settings* settings = nullptr, QObject* parent = nullptr);
    ~PlaybackOutput() override;

    void setDevice(DeviceController* device);
    DeviceController* device() const { return device_; }
    // stroke / vibration / suction 0-100; axes: TCode axis name -> actions.
    void setScript(std::vector<samn::Action> stroke, std::vector<samn::Action> vibration,
                   std::vector<samn::Action> suction,
                   std::map<std::string, std::vector<samn::Action>> axes = {});
    // New vibration / suction curves for the same script (a mapping setting
    // changed): they replace the old ones at once, also while playing; the
    // stroke, the soft start and an Extended-O cycle go on.
    void setLevels(std::vector<samn::Action> vibration, std::vector<samn::Action> suction);
    // The video starts or stops playing (pause, stop, end).
    void setPlaying(bool playing);
    // The playhead in script time, reported while the video plays.
    void update(qint64 scriptMs);
    // Stop everything now (the Emergency stop button).
    Q_INVOKABLE void emergencyStop();
    // C++ knob (tests): how long without a position counts as a pause.
    void setStaleMs(int ms) { stale_.setInterval(ms); }
    // C++ knob (tests): the clock (ms) the ramps run on; default monotonic.
    // Without the ramp timer, tests step the time and call update().
    void setClock(std::function<qint64()> clock, bool rampTimer = true);
    // Start an Extended-O cycle with the current settings. False when it is
    // not available (not playing, switched off) or a cycle already runs.
    Q_INVOKABLE bool triggerExtendedO();
    // The Extended-O scale now (1 without a cycle).
    double extendedOScale();

    bool playing() const { return playing_; }
    bool strokeOn() const { return strokeOn_; }
    void setStrokeOn(bool on);
    int strokeLo() const { return lo_; }
    int strokeHi() const { return hi_; }
    // Moves one end; the other follows to keep at least 10 % between them.
    Q_INVOKABLE void setStrokeRange(int lo, int hi, bool movedLo);
    QString smooth() const { return QString::fromStdString(filter_.smooth); }
    void setSmooth(const QString& s);
    int maxSpeed() const { return static_cast<int>(filter_.maxSpeed); }
    void setMaxSpeed(int v);
    bool idleFill() const { return filter_.idleFillMs > 0; }
    void setIdleFill(bool on);
    bool homeOnPause() const { return home_; }
    void setHomeOnPause(bool on);
    bool vibrationOn() const { return vibOn_; }
    void setVibrationOn(bool on);
    int vibrationLevel() const { return vibLevel_; }
    void setVibrationLevel(int v);
    bool suctionOn() const { return sucOn_; }
    void setSuctionOn(bool on);
    int suctionLevel() const { return sucLevel_; }
    void setSuctionLevel(int v);
    int intensity() const { return intensity_; }
    void setIntensity(int v);
    double speed() const { return speed_; }
    void setSpeed(double s);
    int softStartMs() const { return softStartMs_; }
    void setSoftStartMs(int ms);
    bool reducePeaks() const { return reducePeaks_; }
    void setReducePeaks(bool on);
    bool extendedOEnabled() const { return eoEnabled_; }
    void setExtendedOEnabled(bool on);
    double extendedOMin() const { return eo_.minLevel; }
    void setExtendedOMin(double v);
    double extendedOHoldS() const { return double(eo_.holdMs) / 1000.0; }
    void setExtendedOHoldS(double s);
    int extendedORestoreMs() const { return int(eo_.restoreMs); }
    void setExtendedORestoreMs(int ms);
    bool extendedOActive() const { return cycle_.has_value(); }
    bool canTriggerExtendedO() const { return playing_ && !halted_ && eoEnabled_; }

signals:
    void playingChanged();
    void outputChanged();
    // The emergency stop was pressed: the Player pauses the video.
    void emergencyStopped();
    void extendedOChanged();

private:
    bool deviceReady() const;
    void sendLevels(qint64 t);
    void sendStroke(qint64 t);
    void sendAxes(qint64 t);
    void goQuiet(bool home);
    void resetTargets();
    void refilter();
    void save(const char* key, const nlohmann::json& value);
    void load();
    int moveMs(qint64 dueAt, qint64 now) const;
    // The shortest move (real ms) over `way` (0-1 of the range) the speed
    // limit allows; 0 without a limit.
    int speedLimitMs(double way) const;
    qint64 now() const { return clock_ ? clock_() : mono_.elapsed(); }
    void rampTick();
    void updateRampTimer();
    void endExtendedO();
    bool sending() const { return playing_ && !halted_ && !quiet_ && deviceReady(); }

    samn::Settings* settings_ = nullptr;
    QPointer<DeviceController> device_;
    std::vector<samn::Action> stroke_, filtered_, vibration_, suction_;
    struct Axis {
        std::vector<samn::Action> actions;
        int next = -1;
        bool soft = true;     // the next move eases in (soft seek)
        double lastPos = -1;  // 0-1, -1 = not sent yet
    };
    std::map<std::string, Axis> axes_;
    samn::StrokeFilter filter_;
    bool home_ = false, strokeOn_ = true;
    int lo_ = 0, hi_ = 100;
    bool vibOn_ = true, sucOn_ = true;
    int vibLevel_ = 100, sucLevel_ = 100;
    int intensity_ = 100;
    double speed_ = 1.0;

    bool playing_ = false;
    bool halted_ = false;  // emergency stop: silent until the next start
    bool quiet_ = true;    // nothing sent since the last stop
    bool soft_ = true;
    int strokeNext_ = -1;
    double lastPos_ = -1;
    qint64 lastAt_ = -1;
    double lastVib_ = -1, lastSuc_ = -1;
    QTimer stale_;

    // Soft start, Reduce peaks, Extended-O.
    int softStartMs_ = kSoftStartDefaultMs;
    bool reducePeaks_ = false;
    bool eoEnabled_ = true;
    samn::ExtendedOOptions eo_;  // the settings (0.7 defaults)
    // When the running soft start began (clock ms); none = not ramping.
    std::optional<qint64> softFrom_;
    // The running Extended-O cycle; none = full height. "No cycle" is its
    // own state, so a minimum of 0 can never read as "unset" (PS-11).
    struct Cycle {
        qint64 from = 0;
        samn::ExtendedOOptions opts;
    };
    std::optional<Cycle> cycle_;
    std::function<qint64()> clock_;
    QElapsedTimer mono_;
    bool rampTimerOn_ = true;
    QTimer ramp_;
};

}  // namespace samn::ui
