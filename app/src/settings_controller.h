#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "samn/settings.h"

namespace samn::ui {

// The Settings page behind the gear (D-146; 0.7 Tools → Settings / Log /
// About): app-wide choices saved under 0.7's keys, the log view and the
// licence texts. Player-, Generator- and device-specific settings stay with
// their controllers; the page binds to those directly.
class SettingsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0.7 "log.level" (debug | info | warn | error, default info), applied at once.
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY settingsChanged)
    Q_PROPERTY(QStringList logLevels READ logLevels CONSTANT)
    // 0.7 "device.connect_test" (default off): a short pulse after connecting.
    Q_PROPERTY(bool connectTest READ connectTest WRITE setConnectTest NOTIFY settingsChanged)
    // 0.7 "playback.honor_inverted" (default on): a script's "inverted": true flips it.
    Q_PROPERTY(bool honorInverted READ honorInverted WRITE setHonorInverted NOTIFY settingsChanged)
    // The Log tab: "HH:MM:SS.mmm  LEVEL  message" lines, newest last.
    Q_PROPERTY(bool warningsOnly READ warningsOnly WRITE setWarningsOnly NOTIFY logChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(int logCount READ logCount NOTIFY logChanged)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)
    Q_PROPERTY(QString configFolder READ configFolder CONSTANT)
    // About.
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString licenceText READ licenceText CONSTANT)
    Q_PROPERTY(QString thirdPartyText READ thirdPartyText CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);

    QString logLevel() const { return log_level_; }
    void setLogLevel(const QString& level);
    QStringList logLevels() const;
    bool connectTest() const { return connect_test_; }
    void setConnectTest(bool on);
    bool honorInverted() const { return honor_inverted_; }
    void setHonorInverted(bool on);
    bool warningsOnly() const { return warnings_only_; }
    void setWarningsOnly(bool on);
    QString logText() const;
    int logCount() const;
    QString logFile() const;
    QString configFolder() const;
    QString version() const;
    QString licenceText() const;
    QString thirdPartyText() const;

    // 0.7 Log tab: Clear, Copy all (to the clipboard; false when empty),
    // Open folder.
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE bool copyLog();
    Q_INVOKABLE bool openLogFolder();
    Q_INVOKABLE bool openConfigFolder();

    // The log level of the settings file, for startup (before any controller).
    static void applySavedLogLevel();

signals:
    void settingsChanged();
    void logChanged();

private:
    void save(const char* key, const nlohmann::json& value);

    std::unique_ptr<samn::Settings> settings_;
    QString log_level_ = QStringLiteral("info");
    bool connect_test_ = false;
    bool honor_inverted_ = true;
    bool warnings_only_ = false;
};

}  // namespace samn::ui
