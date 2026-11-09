#include "mode_registry.h"

#include <QQmlEngine>
#include <QVariantMap>

#include "plugins.h"

namespace samn::ui {

namespace {
// The Flow hues of Theme (theme.h): cyan, orange, blue.
const char* const kCyan = "#00d9f4";
const char* const kOrange = "#ff8a23";
const char* const kBlue = "#00b4ff";
}  // namespace

ModeRegistry::ModeRegistry() {
    // Player, Generator, Training (D-144, D-145), then the plugin modes (D-155).
    add({"player", tr("Player"), "player", QColor::fromString(kCyan), {}, {}, "PlayerPage.qml", true, true, true});
    add({"generator", tr("Generator"), "create", QColor::fromString(kOrange), {}, {}, "GeneratorPage.qml", true, true, true});
    add({"training", tr("Training"), "training", QColor::fromString(kBlue), {}, {}, "TrainingPage.qml", true, true, true});
    add({"aitrain", tr("AI Train"), "layers", QColor::fromString(kOrange), {}, {}, "AiTrainPage.qml",
         Plugins::aiTrainStored(), true, false});
}

ModeRegistry& ModeRegistry::instance() {
    static ModeRegistry registry;
    return registry;
}

ModeRegistry* ModeRegistry::create(QQmlEngine*, QJSEngine*) {
    ModeRegistry& r = instance();
    // A new engine starts from the saved plugin switch.
    r.setVisible(QStringLiteral("aitrain"), Plugins::aiTrainStored());
    QQmlEngine::setObjectOwnership(&r, QQmlEngine::CppOwnership);
    return &r;
}

ModeDescriptor* ModeRegistry::find(const QString& id) {
    for (ModeDescriptor& m : all_)
        if (m.id == id) return &m;
    return nullptr;
}

bool ModeRegistry::contains(const QString& id) const {
    for (const ModeDescriptor& m : all_)
        if (m.id == id) return true;
    return false;
}

bool ModeRegistry::registerMode(const ModeDescriptor& mode) {
    if (mode.builtin) return false;
    return add(mode);
}

bool ModeRegistry::add(const ModeDescriptor& mode) {
    if (mode.id.isEmpty() || mode.title.isEmpty() || contains(mode.id)) return false;
    all_.push_back(mode);
    rebuild();
    return true;
}

bool ModeRegistry::unregisterMode(const QString& id) {
    for (auto it = all_.begin(); it != all_.end(); ++it) {
        if (it->id != id) continue;
        if (it->builtin) return false;
        all_.erase(it);
        rebuild();
        return true;
    }
    return false;
}

bool ModeRegistry::setVisible(const QString& id, bool visible) {
    ModeDescriptor* m = find(id);
    if (!m || m->builtin) return false;
    if (m->visible != visible) {
        m->visible = visible;
        rebuild();
    }
    return true;
}

bool ModeRegistry::setEnabled(const QString& id, bool enabled) {
    ModeDescriptor* m = find(id);
    if (!m || m->builtin) return false;
    if (m->enabled != enabled) {
        m->enabled = enabled;
        rebuild();
    }
    return true;
}

void ModeRegistry::rebuild() {
    QVariantList out;
    QVariantList pages;
    for (const ModeDescriptor& m : all_) {
        if (!m.visible) continue;
        if (!m.builtin) pages << QVariantMap{{"id", m.id}, {"page", m.page}};
        const int position = int(out.size()) + 1;
        QString shortcut = m.shortcut;
        if (shortcut.isEmpty() && position <= 9) shortcut = QStringLiteral("Ctrl+") + QString::number(position);
        out << QVariantMap{{"id", m.id},         {"title", m.title}, {"icon", m.icon},       {"hue", m.hue},
                           {"tooltip", m.tooltip}, {"shortcut", shortcut}, {"page", m.page}, {"enabled", m.enabled},
                           {"builtin", m.builtin}};
    }
    cache_ = out;
    const bool pagesDiffer = pages != pages_;
    pages_ = pages;
    emit changed();
    if (pagesDiffer) emit pagesChanged();
}

}  // namespace samn::ui
