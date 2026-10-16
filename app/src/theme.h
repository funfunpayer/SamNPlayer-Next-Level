#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// The SamNPlayer Flow look (0.7 theme_flow.css) for QML: near-black navy,
// glass cards, cyan glow, orange accents. All colours and sizes live here, so
// another look (or another interface) only changes this class and the QML.
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
#undef SAMN_COLOR
    Q_PROPERTY(QString font READ font CONSTANT)
    Q_PROPERTY(QString fontSoft READ fontSoft CONSTANT)
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int animFast READ animFast CONSTANT)
    Q_PROPERTY(int animNormal READ animNormal CONSTANT)

public:
    explicit Theme(QObject* parent = nullptr) : QObject(parent) {}
    QString font() const { return QStringLiteral("Sora"); }
    QString fontSoft() const { return QStringLiteral("Figtree"); }
    int radius() const { return 18; }
    int radiusSmall() const { return 12; }
    int animFast() const { return 140; }
    int animNormal() const { return 220; }
};

}  // namespace samn::ui
