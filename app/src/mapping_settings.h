#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <map>
#include <string>

#include "samn/playback_mapping.h"
#include "samn/settings.h"

namespace samn::ui {

class PlaybackOutput;

// The Player's mapping settings (0.7 "Playback & Extended-O": sync mode,
// tick, max speed, smoothing), the live contact controls (strength, off,
// sensitivity, curve) and the Effects presets (playback.effect_presets).
// Every change is saved at once and announced with mappingChanged(); the
// Player maps the script again (lanes and device output).
//
// Saved (0.7 keys): playback.sync_mode, playback.tick_ms,
// playback.max_speed, playback.smoothing, playback.contact_intensity,
// playback.effect_presets; and playback.contact_off (0.7 kept "Contact
// vibration off" only until the next script without contact). Sensitivity
// and curve start from each script's recipe when it opens (0.7).
class MappingSettings : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // independent, synchronized, alternating, vibration_only, suction_only,
    // suction_position.
    Q_PROPERTY(QString syncMode READ syncMode WRITE setSyncMode NOTIFY mappingChanged)
    Q_PROPERTY(QStringList syncModes READ syncModes CONSTANT)
    // 10-200 ms.
    Q_PROPERTY(int tickMs READ tickMs WRITE setTickMs NOTIFY mappingChanged)
    // 0.1-2 (position change per ms that counts as full intensity).
    Q_PROPERTY(double maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY mappingChanged)
    // 0-1 (0 is off, a real value).
    Q_PROPERTY(double smoothing READ smoothing WRITE setSmoothing NOTIFY mappingChanged)
    // Contact vibration: strength 0-2 (0 mutes), off, sensitivity 0.4-0.95
    // and curve (linear, soft, peak, impulse).
    Q_PROPERTY(double contactIntensity READ contactIntensity WRITE setContactIntensity NOTIFY mappingChanged)
    Q_PROPERTY(bool contactOff READ contactOff WRITE setContactOff NOTIFY mappingChanged)
    Q_PROPERTY(double contactSpan READ contactSpan WRITE setContactSpan NOTIFY mappingChanged)
    Q_PROPERTY(QString contactCurve READ contactCurve WRITE setContactCurve NOTIFY mappingChanged)
    Q_PROPERTY(QStringList contactCurves READ contactCurves CONSTANT)
    // Effects presets: the saved names (sorted) and the selection ("" =
    // Custom, "__default" = Default, else a name). Any change that leaves
    // the selected values makes the selection Custom again.
    Q_PROPERTY(QStringList effectPresets READ effectPresets NOTIFY presetsChanged)
    Q_PROPERTY(QString effectPreset READ effectPreset NOTIFY presetsChanged)
public:
    static constexpr const char* kDefaultPreset = "__default";

    // settings: where the choices are kept; may be null. output: the device
    // output whose intensity, soft start and Reduce peaks the presets hold.
    // Neither is owned.
    MappingSettings(samn::Settings* settings, PlaybackOutput* output, QObject* parent = nullptr);

    QString syncMode() const { return QString::fromStdString(choices_.sync_mode); }
    void setSyncMode(const QString& mode);
    QStringList syncModes() const;
    int tickMs() const { return int(choices_.tick_ms); }
    void setTickMs(int ms);
    double maxSpeed() const { return choices_.max_speed; }
    void setMaxSpeed(double v);
    double smoothing() const { return choices_.smoothing; }
    void setSmoothing(double v);
    double contactIntensity() const { return choices_.contact_intensity; }
    void setContactIntensity(double v);
    bool contactOff() const { return choices_.contact_off; }
    void setContactOff(bool on);
    double contactSpan() const { return span_; }
    void setContactSpan(double v);
    QString contactCurve() const { return QString::fromStdString(curve_); }
    void setContactCurve(const QString& c);
    QStringList contactCurves() const;

    // A script opened: sensitivity and curve from its recipe (0.7 loadScript).
    void resetContactKnobs(double recipeSpan, const std::string& recipeCurve);
    // What Play sends for a script whose recipe has / has not contact on.
    samn::PlaybackChoices choices(bool recipeHasContact) const;

    QStringList effectPresets() const;
    QString effectPreset() const { return selected_; }
    // Saves the current values under a name (trimmed, 40 characters, not
    // "__default"; an existing name is replaced) and selects it.
    Q_INVOKABLE bool saveEffectPreset(const QString& name);
    // Applies a saved preset or "__default" (0.7 FX_DEFAULTS); "" (Custom)
    // changes nothing. Fields a preset lacks keep their value.
    Q_INVOKABLE bool applyEffectPreset(const QString& name);
    Q_INVOKABLE bool deleteEffectPreset(const QString& name);

signals:
    void mappingChanged();
    void presetsChanged();

private:
    void load();
    void save(const char* key, const nlohmann::json& value);
    void changed();
    void select(const QString& name);
    void checkSelection();
    samn::EffectPreset current() const;

    samn::Settings* settings_ = nullptr;
    PlaybackOutput* output_ = nullptr;
    samn::PlaybackChoices choices_;
    double span_ = samn::DefaultContactVibrationSpan;
    std::string curve_ = samn::ContactCurveLinear;
    std::map<std::string, samn::EffectPreset> presets_;
    QString selected_;
    bool applying_ = false;
};

}  // namespace samn::ui
