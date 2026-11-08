#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QQmlEngine;
class QJSEngine;

namespace samn::ui {

// One mode of the app (D-165): what the tab row, the Ctrl+N keys and the page
// stack need to know. No kernel logic: the page itself is QML.
struct ModeDescriptor {
    QString id;        // unique, stable ("player", "generator", ...)
    QString title;     // the tab label
    QString icon;      // an icon name of qml/icons
    QColor hue;        // the tab icon colour while the tab is not current
    QString tooltip;   // accessible description of the tab ("" = none)
    QString shortcut;  // key sequence ("" = Ctrl+<position>, 1..9)
    QString page;      // QML file next to Main.qml ("AiTrainPage.qml")
    bool visible = true;
    bool enabled = true;  // a visible but disabled mode is shown dimmed and cannot be chosen
    // Player, Generator and Training are wired into Main.qml by hand (their
    // signals and loaders); they come first, are always visible and cannot be
    // removed. Every other mode is a page loaded from `page`.
    bool builtin = false;
};

// The registry of modes (D-165): compiled-in modules register at start, the
// tab row, the keys and the page stack read it. Process-wide, as AppLog; also
// the QML singleton "ModeRegistry". Order of registration = order of the tabs.
class ModeRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // The visible modes in tab order, as maps {id, title, icon, hue, tooltip,
    // shortcut, page, enabled, builtin}; shortcut is resolved.
    Q_PROPERTY(QVariantList modes READ modes NOTIFY changed)
public:
    static ModeRegistry& instance();
    static ModeRegistry* create(QQmlEngine*, QJSEngine*);

    // False for an empty or already used id, or an empty title.
    bool registerMode(const ModeDescriptor& mode);
    // False for an unknown or builtin id.
    bool unregisterMode(const QString& id);
    Q_INVOKABLE bool setVisible(const QString& id, bool visible);
    Q_INVOKABLE bool setEnabled(const QString& id, bool enabled);
    Q_INVOKABLE bool contains(const QString& id) const;

    QVariantList modes() const { return cache_; }
    int visibleCount() const { return int(cache_.size()); }

signals:
    void changed();

private:
    ModeRegistry();
    void rebuild();
    ModeDescriptor* find(const QString& id);

    std::vector<ModeDescriptor> all_;
    QVariantList cache_;
};

}  // namespace samn::ui
