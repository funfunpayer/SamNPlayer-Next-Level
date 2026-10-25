#pragma once

#include <QColor>
#include <QObject>
#include <QProcessEnvironment>
#include <QtQml/qqmlregistration.h>

#include "app_log.h"
#include "samn/settings.h"

namespace samn::ui {

// The SamNPlayer Flow look (0.7 theme_flow.css) for QML: near-black navy,
// glass cards, cyan glow, orange accents. All colours and sizes live here, so
// another look (or another interface) only changes this class and the QML.
//
// Reduced motion (D-092, UI-07): Settings → General, key "ui.reduced_motion",
// or SAMN_NO_ANIM=1 for tests. Durations become 0; decorative loops stop.
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
#define SAMN_COLOR(name, value) \
    Q_PROPERTY(QColor name READ name CONSTANT) \
public: \
    QColor name() const { return QColor::fromString(value); } \
private:
    SAMN_COLOR(bgTop, "#021024")
    SAMN_COLOR(bgBottom, "#01070f")
    SAMN_COLOR(cardTop, "#c70e2038")      // rgba(14,32,56,0.78) as #AARRGGBB
    SAMN_COLOR(cardBottom, "#c7071222")   // rgba(7,18,34,0.78)
    SAMN_COLOR(cardBorder, "#335aaaff")   // rgba(90,170,255,0.20)
    SAMN_COLOR(cyan, "#00d9f4")
    SAMN_COLOR(cyanSoft, "#3fd0ff")
    SAMN_COLOR(blue, "#00b4ff")
    SAMN_COLOR(orange, "#ff8a23")
    SAMN_COLOR(text, "#e8f2fb")
    SAMN_COLOR(textDim, "#9fb3c8")
    SAMN_COLOR(textMuted, "#6f8399")
    SAMN_COLOR(laneVibration, "#00d9f4")
    SAMN_COLOR(laneSuction, "#b896e8")
    SAMN_COLOR(trainVibration, "#03d8f2")  // the Training rings and curves (0.7 training_rings.js)
    SAMN_COLOR(trainSuction, "#ff982e")
    SAMN_COLOR(danger, "#ff6b7a")
    SAMN_COLOR(good, "#3ddc97")
#undef SAMN_COLOR
    Q_PROPERTY(QString font READ font CONSTANT)
    Q_PROPERTY(QString fontSoft READ fontSoft CONSTANT)
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(int animFast READ animFast NOTIFY reducedMotionChanged)
    Q_PROPERTY(int animNormal READ animNormal NOTIFY reducedMotionChanged)
    Q_PROPERTY(int animSlow READ animSlow NOTIFY reducedMotionChanged)
    Q_PROPERTY(int animAmbient READ animAmbient NOTIFY reducedMotionChanged)

public:
    explicit Theme(QObject* parent = nullptr) : QObject(parent) {
        if (qEnvironmentVariableIsSet("SAMN_NO_ANIM")) {
            reduced_motion_ = true;
            return;
        }
        try {
            const auto v = samn::Settings().get("ui.reduced_motion");
            if (v.is_boolean()) reduced_motion_ = v.get<bool>();
        } catch (const std::exception&) {
            // Unreadable settings: motion stays on.
        }
    }
    QString font() const { return QStringLiteral("Sora"); }
    QString fontSoft() const { return QStringLiteral("Figtree"); }
    int radius() const { return 18; }
    int radiusSmall() const { return 12; }
    bool reducedMotion() const { return reduced_motion_; }
    void setReducedMotion(bool on) {
        if (on == reduced_motion_) return;
        reduced_motion_ = on;
        if (!qEnvironmentVariableIsSet("SAMN_NO_ANIM")) {
            try {
                samn::Settings().set("ui.reduced_motion", on);
            } catch (const std::exception& e) {
                AppLog::instance().add(samn::LogSeverity::kWarn,
                                       tr("Setting ui.reduced_motion not saved: %1").arg(e.what()));
            }
        }
        emit reducedMotionChanged();
    }
    int animFast() const { return reduced_motion_ ? 0 : 140; }
    int animNormal() const { return reduced_motion_ ? 0 : 220; }
    // Soft UI transitions (mode face, empty home settle).
    int animSlow() const { return reduced_motion_ ? 0 : 420; }
    // Long ambient loops (idle ring drift, empty bloom); loops also check
    // reducedMotion and stop.
    int animAmbient() const { return reduced_motion_ ? 0 : 9000; }

signals:
    void reducedMotionChanged();

private:
    bool reduced_motion_ = false;
};

}  // namespace samn::ui
