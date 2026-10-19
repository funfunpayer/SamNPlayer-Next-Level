#include "settings_controller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QUrl>

#include "app_log.h"

namespace samn::ui {

namespace {

QString ReadResource(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

QString FsQ(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

}  // namespace

SettingsController::SettingsController(QObject* parent) : QObject(parent) {
    try {
        settings_ = std::make_unique<samn::Settings>();
        const auto level = settings_->get("log.level");
        if (level.is_string()) {
            if (const auto l = samn::ParseLogLevel(level.get<std::string>()))
                log_level_ = QString::fromLatin1(samn::LogLevelName(*l));
        }
        const auto test = settings_->get("device.connect_test");
        if (test.is_boolean()) connect_test_ = test.get<bool>();
        const auto honor = settings_->get("playback.honor_inverted");
        if (honor.is_boolean()) honor_inverted_ = honor.get<bool>();
    } catch (const std::exception&) {
        // Unreadable settings: the defaults stay.
    }
    connect(&AppLog::instance(), &AppLog::changed, this, &SettingsController::logChanged);
}

void SettingsController::applySavedLogLevel() {
    try {
        const auto level = samn::Settings().get("log.level");
        if (level.is_string())
            if (const auto l = samn::ParseLogLevel(level.get<std::string>())) AppLog::instance().buffer().SetLevel(*l);
    } catch (const std::exception&) {
    }
}

void SettingsController::save(const char* key, const nlohmann::json& value) {
    if (!settings_) return;
    try {
        settings_->set(key, value);
    } catch (const std::exception& e) {
        AppLog::instance().add(samn::LogSeverity::kWarn, tr("Setting %1 not saved: %2").arg(key, e.what()));
    }
}

QStringList SettingsController::logLevels() const { return {"debug", "info", "warn", "error"}; }

void SettingsController::setLogLevel(const QString& level) {
    const auto l = samn::ParseLogLevel(level.toStdString());
    if (!l) return;
    const QString name = QString::fromLatin1(samn::LogLevelName(*l));
    AppLog::instance().buffer().SetLevel(*l);  // applied live, as 0.7
    if (name == log_level_) return;
    log_level_ = name;
    save("log.level", name.toStdString());
    emit settingsChanged();
}

void SettingsController::setConnectTest(bool on) {
    if (on == connect_test_) return;
    connect_test_ = on;
    save("device.connect_test", on);
    emit settingsChanged();
}

void SettingsController::setHonorInverted(bool on) {
    if (on == honor_inverted_) return;
    honor_inverted_ = on;
    save("playback.honor_inverted", on);
    emit settingsChanged();
}

void SettingsController::setWarningsOnly(bool on) {
    if (on == warnings_only_) return;
    warnings_only_ = on;
    emit logChanged();
}

QString SettingsController::logText() const {
    QStringList lines;
    for (const samn::LogEntry& e : AppLog::instance().buffer().Entries(warnings_only_))
        lines << QString::fromStdString(samn::FormatLogLine(e));
    return lines.join(QLatin1Char('\n'));
}

int SettingsController::logCount() const { return int(AppLog::instance().buffer().Entries(warnings_only_).size()); }

QString SettingsController::logFile() const { return FsQ(AppLog::instance().filePath()); }

QString SettingsController::configFolder() const { return FsQ(samn::config_dir()); }

QString SettingsController::version() const { return QCoreApplication::applicationVersion(); }

QString SettingsController::licenceText() const { return ReadResource(QStringLiteral(":/legal/LICENSE")); }

QString SettingsController::thirdPartyText() const { return ReadResource(QStringLiteral(":/legal/THIRD_PARTY.md")); }

void SettingsController::clearLog() {
    AppLog::instance().buffer().Clear();
    emit logChanged();
}

bool SettingsController::copyLog() {
    const QString text = logText();
    if (text.isEmpty()) return false;
    if (auto* clip = QGuiApplication::clipboard()) clip->setText(text);
    return true;
}

bool SettingsController::openLogFolder() {
    const auto file = AppLog::instance().filePath();
    if (file.empty()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(FsQ(file.parent_path())));
}

bool SettingsController::openConfigFolder() {
    const QString dir = configFolder();
    QDir().mkpath(dir);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

}  // namespace samn::ui
