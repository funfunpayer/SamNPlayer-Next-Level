#include "mapping_settings.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

#include "device_controller.h"
#include "playback_output.h"

namespace samn::ui {

namespace {

constexpr const char* kKeyContactOff = "playback.contact_off";

const char* const kSyncModes[] = {"independent", "synchronized", "alternating",
                                  "vibration_only", "suction_only", "suction_position"};
const char* const kCurves[] = {"linear", "soft", "peak", "impulse"};

bool validSync(const std::string& s) {
    return std::find_if(std::begin(kSyncModes), std::end(kSyncModes), [&](const char* m) { return s == m; }) !=
           std::end(kSyncModes);
}

int64_t clampTick(double ms) { return std::clamp<int64_t>(std::llround(ms), samn::kTickMinMs, samn::kTickMaxMs); }

}  // namespace

MappingSettings::MappingSettings(samn::Settings* settings, PlaybackOutput* output, QObject* parent)
    : QObject(parent), settings_(settings), output_(output) {
    load();
    if (output_) connect(output_, &PlaybackOutput::outputChanged, this, &MappingSettings::checkSelection);
}

void MappingSettings::load() {
    if (!settings_) return;
    try {
        // A missing or broken value takes the default; a stored 0 stays 0
        // where 0 is a legal setting (smoothing, contact strength; PS-11).
        const auto num = [&](const char* k, double d) {
            const auto v = settings_->get(k, d);
            const double x = v.is_number() ? v.get<double>() : d;
            return std::isfinite(x) ? x : d;
        };
        const auto sync = settings_->get(samn::kKeySyncMode, "independent");
        const std::string s = sync.is_string() ? sync.get<std::string>() : std::string();
        choices_.sync_mode = validSync(s) ? s : "independent";
        choices_.tick_ms = clampTick(num(samn::kKeyTickMs, double(samn::kDefaultTickMs)));
        choices_.max_speed =
            std::clamp(num(samn::kKeyMapMaxSpeed, samn::kDefaultMapMaxSpeed), samn::kMapMaxSpeedMin, samn::kMapMaxSpeedMax);
        choices_.smoothing = std::clamp(num(samn::kKeySmoothing, samn::kDefaultSmoothing), 0.0, 1.0);
        choices_.contact_intensity = std::clamp(num(samn::kKeyContactIntensity, 1.0), 0.0, samn::kContactIntensityMax);
        const auto off = settings_->get(kKeyContactOff, false);
        choices_.contact_off = off.is_boolean() && off.get<bool>();
        const auto fx = settings_->get(samn::kKeyEffectPresets, "");
        presets_ = samn::ParseEffectPresets(fx.is_string() ? fx.get<std::string>() : std::string());
    } catch (const std::exception&) {
        // A broken settings file keeps the defaults.
    }
}

void MappingSettings::save(const char* key, const nlohmann::json& value) {
    if (!settings_) return;
    try {
        settings_->set(key, value);
    } catch (const std::exception&) {
        // Not saving a slider position must never stop the playback.
    }
}

void MappingSettings::changed() {
    emit mappingChanged();
    checkSelection();
}

QStringList MappingSettings::syncModes() const {
    QStringList out;
    for (const char* m : kSyncModes) out << QString::fromLatin1(m);
    return out;
}

QStringList MappingSettings::contactCurves() const {
    QStringList out;
    for (const char* c : kCurves) out << QString::fromLatin1(c);
    return out;
}

void MappingSettings::setSyncMode(const QString& mode) {
    const std::string m = mode.toStdString();
    if (!validSync(m) || m == choices_.sync_mode) return;
    choices_.sync_mode = m;
    save(samn::kKeySyncMode, m);
    changed();
}

void MappingSettings::setTickMs(int ms) {
    const int64_t v = clampTick(ms);
    if (v == choices_.tick_ms) return;
    choices_.tick_ms = v;
    save(samn::kKeyTickMs, v);
    changed();
}

void MappingSettings::setMaxSpeed(double v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, samn::kMapMaxSpeedMin, samn::kMapMaxSpeedMax);
    if (v == choices_.max_speed) return;
    choices_.max_speed = v;
    save(samn::kKeyMapMaxSpeed, v);
    changed();
}

void MappingSettings::setSmoothing(double v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, 0.0, 1.0);
    if (v == choices_.smoothing) return;
    choices_.smoothing = v;
    save(samn::kKeySmoothing, v);
    changed();
}

void MappingSettings::setContactIntensity(double v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, 0.0, samn::kContactIntensityMax);
    if (v == choices_.contact_intensity) return;
    choices_.contact_intensity = v;
    save(samn::kKeyContactIntensity, v);
    changed();
}

void MappingSettings::setContactOff(bool on) {
    if (on == choices_.contact_off) return;
    choices_.contact_off = on;
    save(kKeyContactOff, on);
    changed();
}

void MappingSettings::setContactSpan(double v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, samn::kContactSpanMin, samn::kContactSpanMax);
    if (v == span_) return;
    span_ = v;
    changed();
}

void MappingSettings::setContactCurve(const QString& c) {
    const std::string v = samn::NormalizeContactCurve(c.toStdString());
    if (v == curve_) return;
    curve_ = v;
    changed();
}

void MappingSettings::resetContactKnobs(double recipeSpan, const std::string& recipeCurve) {
    // 0.7: the recipe's span (0.75 when it has none) and curve.
    span_ = samn::EffectiveContactSpan(recipeSpan);
    curve_ = samn::NormalizeContactCurve(recipeCurve);
    emit mappingChanged();
}

samn::PlaybackChoices MappingSettings::choices(bool recipeHasContact) const {
    samn::PlaybackChoices c = choices_;
    c.contact_span = span_;
    c.contact_curve = curve_;
    return samn::ChoicesForScript(c, recipeHasContact);
}

// ---------------------------------------------------------------------------
// Effects presets
// ---------------------------------------------------------------------------

samn::EffectPreset MappingSettings::current() const {
    samn::EffectPreset p;
    p.smoothing = choices_.smoothing;
    p.max_speed = choices_.max_speed;
    p.tick_ms = double(choices_.tick_ms);
    if (output_) {
        p.intensity = output_->intensity();
        p.soft_start_ms = output_->softStartMs();
        p.reduce_peaks = output_->reducePeaks();
    }
    return p;
}

QStringList MappingSettings::effectPresets() const {
    QStringList out;
    for (const auto& [name, p] : presets_) out << QString::fromStdString(name);
    out.sort(Qt::CaseInsensitive);
    return out;
}

void MappingSettings::select(const QString& name) {
    if (name == selected_) return;
    selected_ = name;
    emit presetsChanged();
}

// The selection stays only while the values are still the preset's.
void MappingSettings::checkSelection() {
    if (applying_ || selected_.isEmpty()) return;
    samn::EffectPreset want;
    if (selected_ == QLatin1String(kDefaultPreset)) {
        want = samn::DefaultEffectPreset();
    } else {
        const auto it = presets_.find(selected_.toStdString());
        if (it == presets_.end()) return select({});
        want = it->second;
    }
    const samn::EffectPreset now = current();
    const auto same = [](const std::optional<double>& w, const std::optional<double>& n, double step) {
        return !w || !n || std::abs(*w - *n) <= step / 2;
    };
    const bool match = same(want.intensity, now.intensity, 1) && same(want.smoothing, now.smoothing, 0.01) &&
                       same(want.max_speed, now.max_speed, 0.01) && same(want.tick_ms, now.tick_ms, 1) &&
                       same(want.soft_start_ms, now.soft_start_ms, 1) &&
                       (!want.reduce_peaks || !now.reduce_peaks || *want.reduce_peaks == *now.reduce_peaks);
    if (!match) select({});
}

bool MappingSettings::saveEffectPreset(const QString& name) {
    const std::string n = samn::CleanPresetName(name.toStdString());
    if (n.empty()) return false;
    presets_[n] = current();
    save(samn::kKeyEffectPresets, samn::SerializeEffectPresets(presets_));
    selected_ = QString::fromStdString(n);
    emit presetsChanged();
    return true;
}

bool MappingSettings::applyEffectPreset(const QString& name) {
    samn::EffectPreset p;
    if (name.isEmpty()) {
        select({});
        return true;
    }
    if (name == QLatin1String(kDefaultPreset)) {
        p = samn::DefaultEffectPreset();
    } else {
        const auto it = presets_.find(name.toStdString());
        if (it == presets_.end()) return false;
        p = it->second;
    }
    applying_ = true;
    if (p.smoothing) setSmoothing(*p.smoothing);
    if (p.max_speed) setMaxSpeed(*p.max_speed);
    if (p.tick_ms) setTickMs(int(std::lround(*p.tick_ms)));
    if (output_) {
        if (p.intensity) output_->setIntensity(int(std::lround(*p.intensity)));
        if (p.soft_start_ms) output_->setSoftStartMs(int(std::lround(*p.soft_start_ms)));
        if (p.reduce_peaks) output_->setReducePeaks(*p.reduce_peaks);
    }
    applying_ = false;
    selected_ = name;
    emit presetsChanged();
    return true;
}

bool MappingSettings::deleteEffectPreset(const QString& name) {
    if (presets_.erase(name.toStdString()) == 0) return false;
    save(samn::kKeyEffectPresets, samn::SerializeEffectPresets(presets_));
    selected_.clear();
    emit presetsChanged();
    return true;
}

}  // namespace samn::ui
