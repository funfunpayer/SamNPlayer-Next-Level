// UpdateChecker (D-147: check only; 0.7 Settings → Check for updates, startup banner).
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "app_log.h"
#include "samn/settings.h"
#include "update_checker.h"

using samn::ui::AppLog;
using samn::ui::UpdateChecker;

class UpdateCheckerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    // The GitHub answer as a local file; SAMN_UPDATE_URL points at it.
    void serve(const QByteArray& json) {
        const QString path = dir_.filePath("latest.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(json);
        f.close();
        qputenv("SAMN_UPDATE_URL", QUrl::fromLocalFile(path).toString().toUtf8());
    }
    static void waitDone(UpdateChecker& u) { QTRY_VERIFY(u.state() != QLatin1String("checking")); }

private slots:
    void initTestCase() {
        qputenv("SAMN_CONFIG_DIR", dir_.path().toUtf8());
        QCoreApplication::setApplicationVersion("2.0.0-dev");
    }
    void init() { QFile::remove(dir_.filePath("settings.json")); }

    void startupSettingIsRemembered() {
        {
            UpdateChecker u;
            QVERIFY(u.checkOnStartup());  // 0.7 default on
            QSignalSpy spy(&u, &UpdateChecker::checkOnStartupChanged);
            u.setCheckOnStartup(false);
            QCOMPARE(spy.count(), 1);
        }
        QVERIFY(!samn::Settings().get("update.check_on_startup").get<bool>());
        UpdateChecker again;
        QVERIFY(!again.checkOnStartup());
        // Off: the startup check does not ask.
        serve(R"({"tag_name":"v9.0.0"})");
        again.checkIfEnabled();
        QCOMPARE(again.state(), QStringLiteral("idle"));
    }

    void aNewerReleaseIsOffered() {
        serve(R"({"tag_name":"v2.0.0","html_url":"https://github.com/funfunpayer/SamNPlayer-Next-Level/releases/tag/v2.0.0",)"
              R"("body":"## First release\n* Settings page"})");
        UpdateChecker u;
        u.check();
        QCOMPARE(u.state(), QStringLiteral("checking"));
        QCOMPARE(u.statusText(), QStringLiteral("Checking…"));
        waitDone(u);
        QCOMPARE(u.state(), QStringLiteral("available"));  // 2.0.0 is newer than 2.0.0-dev
        QCOMPARE(u.latestTag(), QStringLiteral("v2.0.0"));
        QCOMPARE(u.statusText(), QStringLiteral("Version v2.0.0 available (current: 2.0.0-dev)."));
        QCOMPARE(u.notesPreview(), QStringLiteral("First release · Settings page"));
        QVERIFY(!u.bannerVisible());  // a manual check shows it in Settings only
    }

    void theStartupCheckShowsTheBannerUntilClosed() {
        serve(R"({"tag_name":"v2.1.0"})");
        UpdateChecker u;
        u.checkIfEnabled();
        waitDone(u);
        QVERIFY(u.bannerVisible());
        u.dismissBanner();
        QVERIFY(!u.bannerVisible());
        QCOMPARE(u.state(), QStringLiteral("available"));
    }

    void nothingNewerOrNoPlainRelease() {
        UpdateChecker u;
        serve(R"({"tag_name":"v1.9.9"})");
        u.check(true);
        waitDone(u);
        QCOMPARE(u.state(), QStringLiteral("current"));
        QCOMPARE(u.statusText(), QStringLiteral("No update available (current: 2.0.0-dev)."));
        QVERIFY(!u.bannerVisible());
        serve(R"({"tag_name":"patch-v9.0.0-p1"})");  // 0.7: a patch tag is not a release
        u.check();
        waitDone(u);
        QCOMPARE(u.state(), QStringLiteral("current"));
        QVERIFY(u.latestTag().isEmpty());
    }

    void aFailureIsShownOrOnlyLogged() {
        AppLog::instance().buffer().Clear();
        qputenv("SAMN_UPDATE_URL", QUrl::fromLocalFile(dir_.filePath("missing.json")).toString().toUtf8());
        UpdateChecker u;
        u.check(true);
        waitDone(u);
        QCOMPARE(u.state(), QStringLiteral("error"));
        QVERIFY(u.statusText().startsWith(QStringLiteral("Update check failed: ")));
        QVERIFY(!u.bannerVisible());
        bool logged = false;
        for (const auto& e : AppLog::instance().buffer().Entries(true))
            logged = logged || (e.level == samn::LogSeverity::kWarn && e.text.rfind("Update check on startup: ", 0) == 0);
        QVERIFY(logged);
    }
};

QTEST_MAIN(UpdateCheckerTest)
#include "update_checker_test.moc"
