// SettingsController and AppLog (D-146; 0.7 Settings, Log tab, About).
#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <fstream>

#include "app_log.h"
#include "samn/settings.h"
#include "settings_controller.h"

using samn::LogSeverity;
using samn::ui::AppLog;
using samn::ui::SettingsController;

class SettingsControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

private slots:
    void initTestCase() { qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8()); }
    void init() {
        QFile::remove(config_.filePath("settings.json"));
        AppLog::instance().buffer().SetLevel(LogSeverity::kInfo);
        AppLog::instance().buffer().Clear();
    }

    void defaultsAndTheyAreRemembered() {
        {
            SettingsController s;
            QCOMPARE(s.logLevel(), QStringLiteral("info"));
            QVERIFY(!s.connectTest());  // 0.7 device.connect_test [false]
            QVERIFY(s.honorInverted());  // 0.7 playback.honor_inverted [true]
            QCOMPARE(s.logLevels(), QStringList({"debug", "info", "warn", "error"}));
            QCOMPARE(s.configFolder(), config_.path());
            QSignalSpy changed(&s, &SettingsController::settingsChanged);
            s.setLogLevel("loud");  // unknown: kept
            QCOMPARE(s.logLevel(), QStringLiteral("info"));
            s.setLogLevel("Warning");
            QCOMPARE(s.logLevel(), QStringLiteral("warn"));
            s.setConnectTest(true);
            s.setHonorInverted(false);
            QCOMPARE(changed.count(), 3);
        }
        const samn::Settings file;
        QCOMPARE(QString::fromStdString(file.get("log.level").get<std::string>()), QStringLiteral("warn"));
        QVERIFY(file.get("device.connect_test").get<bool>());
        QVERIFY(!file.get("playback.honor_inverted").get<bool>());
        SettingsController again;
        QCOMPARE(again.logLevel(), QStringLiteral("warn"));
        QVERIFY(again.connectTest());
        QVERIFY(!again.honorInverted());
        // The startup hook sets the buffer's level from the file.
        AppLog::instance().buffer().SetLevel(LogSeverity::kDebug);
        SettingsController::applySavedLogLevel();
        QCOMPARE(AppLog::instance().buffer().level(), LogSeverity::kWarn);
    }

    void theLogTabShowsFiltersCopiesAndClears() {
        SettingsController s;
        QSignalSpy changed(&s, &SettingsController::logChanged);
        AppLog::instance().add(LogSeverity::kDebug, "below the level");
        AppLog::instance().add(LogSeverity::kInfo, "device connected");
        AppLog::instance().add(LogSeverity::kError, "write failed");
        QTRY_VERIFY(changed.count() >= 1);  // queued, once per event loop turn
        QCOMPARE(s.logCount(), 2);
        QVERIFY(s.logText().contains("INFO   device connected"));
        QVERIFY(!s.logText().contains("below the level"));
        s.setWarningsOnly(true);
        QCOMPARE(s.logCount(), 1);
        QVERIFY(s.logText().contains("ERROR  write failed"));
        QVERIFY(s.copyLog());
        QVERIFY(QGuiApplication::clipboard()->text().contains("write failed"));
        // Debug shows once the level is debug (applied live).
        s.setWarningsOnly(false);
        s.setLogLevel("debug");
        AppLog::instance().add(LogSeverity::kDebug, "now shown");
        QVERIFY(s.logText().contains("DEBUG  now shown"));
        s.clearLog();
        QCOMPARE(s.logCount(), 0);
        QVERIFY(!s.copyLog());  // nothing to copy
    }

    void theLogFileGetsEveryKeptLine() {
        QTemporaryDir dir;
        const auto path = std::filesystem::path(dir.filePath("logs/samnplayer2.log").toStdU16String());
        AppLog::instance().enableFile(path);
        AppLog::instance().add(LogSeverity::kWarn, "to the file");
        AppLog::instance().add(LogSeverity::kDebug, "not kept, not written");
        AppLog::instance().enableFile({});
        std::ifstream in(path);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        QVERIFY(all.find("WARN   to the file") != std::string::npos);
        QVERIFY(all.find("not kept") == std::string::npos);
        QCOMPARE(SettingsController().logFile(), QString());  // writing stopped
    }

    void aboutShowsTheLicenceTexts() {
        SettingsController s;
        QVERIFY(s.licenceText().contains("GNU GENERAL PUBLIC LICENSE"));
        QVERIFY(!s.thirdPartyText().isEmpty());
    }
};

QTEST_MAIN(SettingsControllerTest)
#include "settings_controller_test.moc"
