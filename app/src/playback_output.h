#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <cstdint>
#include <map>
#include <vector>

#include "samn/funscript.h"
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
//  - vibration and suction at the playhead (the mapper's curves, 0-100) times
//    the channel level, or 0 when the channel is switched off;
//  - for a stroker, the NEXT stroke action as "move there in the time until it
//    is due" (min 20 ms), through the stroke filter and the safety range;
//  - the same for the extra TCode axes the script carries.
// A start, a pause and a jump of the playhead (a step back of more than
// 250 ms or a step forward of more than 1.5 s) ease the first stroke move in:
// a full stroke takes at least 500 ms (soft seek).
// A pause, a stop and a missing position (1.5 s) stop the device and, when
// "Move home on pause" is on, send the stroker to the bottom of its range in
// 1.2 s. The emergency stop drops queued levels, stops at once and keeps the
// output silent until playback starts again.
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
    // Play speed of the video (move durations are divided by it).
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY outputChanged)
public:
    static constexpr int kMinMoveMs = 20;
    static constexpr int kSoftSeekFullStrokeMs = 500;
    static constexpr int kSoftSeekJumpMs = 1500;
    static constexpr int kSoftSeekBackMs = 250;
    static constexpr int kHomeMs = 1200;
    static constexpr int kStaleMs = 1500;
    static constexpr int kMinStrokeSpan = 10;  // percent
    static constexpr int kIdleFillMs = 4000;

    // settings: where the choices are kept (the 0.7 keys playback.stroke_*);
    // may be null. Not owned.
    explicit PlaybackOutput(samn::Settings* settings = nullptr, QObject* parent = nullptr);

    void setDevice(DeviceController* device);
    DeviceController* device() const { return device_; }
    // stroke / vibration / suction 0-100; axes: TCode axis name -> actions.
    void setScript(std::vector<samn::Action> stroke, std::vector<samn::Action> vibration,
                   std::vector<samn::Action> suction,
                   std::map<std::string, std::vector<samn::Action>> axes = {});
    // The video starts or stops playing (pause, stop, end).
    void setPlaying(bool playing);
    // The playhead in script time, reported while the video plays.
    void update(qint64 scriptMs);
    // Stop everything now (the Emergency stop button).
    Q_INVOKABLE void emergencyStop();
    // C++ knob (tests): how long without a position counts as a pause.
    void setStaleMs(int ms) { stale_.setInterval(ms); }

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
    double speed() const { return speed_; }
    void setSpeed(double s);

signals:
    void playingChanged();
    void outputChanged();
    // The emergency stop was pressed: the Player pauses the video.
    void emergencyStopped();

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

    samn::Settings* settings_ = nullptr;
    QPointer<DeviceController> device_;
    std::vector<samn::Action> stroke_, filtered_, vibration_, suction_;
    struct Axis {
        std::vector<samn::Action> actions;
        int next = -1;
    };
    std::map<std::string, Axis> axes_;
    samn::StrokeFilter filter_;
    bool home_ = false, strokeOn_ = true;
    int lo_ = 0, hi_ = 100;
    bool vibOn_ = true, sucOn_ = true;
    int vibLevel_ = 100, sucLevel_ = 100;
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
};

}  // namespace samn::ui
