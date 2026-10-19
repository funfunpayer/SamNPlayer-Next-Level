#include "plugins.h"

#include "app_log.h"
#include "samn/settings.h"

namespace samn::ui {

namespace {
constexpr const char* kAiTrainKey = "plugins.aitrain.enabled";
}

Plugins::Plugins(QObject* parent) : QObject(parent) {
    try {
        const auto v = samn::Settings().get(kAiTrainKey);
        if (v.is_boolean()) ai_train_ = v.get<bool>();
    } catch (const std::exception&) {
        // Unreadable settings: the plugin stays off.
    }
}

void Plugins::setAiTrainEnabled(bool on) {
    if (on == ai_train_) return;
    ai_train_ = on;
    try {
        samn::Settings().set(kAiTrainKey, on);
    } catch (const std::exception& e) {
        AppLog::instance().add(samn::LogSeverity::kWarn, tr("Setting %1 not saved: %2").arg(kAiTrainKey, e.what()));
    }
    emit changed();
}

QVariantList Plugins::modes() const {
    QVariantList out;
    if (ai_train_)
        out << QVariantMap{{"id", "aitrain"}, {"name", tr("AI Train")}, {"icon", "layers"}, {"page", "AiTrainPage.qml"}};
    return out;
}

}  // namespace samn::ui
