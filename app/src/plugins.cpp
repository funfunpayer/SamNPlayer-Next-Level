#include "plugins.h"

#include "app_log.h"
#include "mode_registry.h"
#include "samn/settings.h"

namespace samn::ui {

namespace {
constexpr const char* kAiTrainKey = "plugins.aitrain.enabled";
}

bool Plugins::aiTrainStored() {
    try {
        const auto v = samn::Settings().get(kAiTrainKey);
        if (v.is_boolean()) return v.get<bool>();
    } catch (const std::exception&) {
        // Unreadable settings: the plugin stays off.
    }
    return false;
}

Plugins::Plugins(QObject* parent) : QObject(parent), ai_train_(aiTrainStored()) {}

void Plugins::setAiTrainEnabled(bool on) {
    if (on == ai_train_) return;
    ai_train_ = on;
    try {
        samn::Settings().set(kAiTrainKey, on);
    } catch (const std::exception& e) {
        AppLog::instance().add(samn::LogSeverity::kWarn, tr("Setting %1 not saved: %2").arg(kAiTrainKey, e.what()));
    }
    ModeRegistry::instance().setVisible(QStringLiteral("aitrain"), on);  // the tab row follows
    emit changed();
}

}  // namespace samn::ui
