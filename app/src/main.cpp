#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QPointer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include "app_log.h"
#include "frame_provider.h"
#include "samn/settings.h"
#include "settings_controller.h"
#include "icon_provider.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

// The SamN QML module is a static library: import its plugin explicitly.
Q_IMPORT_QML_PLUGIN(SamNPlugin)

namespace {
// Milliseconds since the operating system started this process (DLL loading
// included), or -1 where it cannot be read; for the startup line in the log.
qint64 MsSinceProcessStart() {
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const auto ticks = [](const FILETIME& f) { return (qint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return (ticks(now) - ticks(created)) / 10000;  // 100 ns steps
#elif defined(Q_OS_LINUX)
    QFile stat(QStringLiteral("/proc/self/stat")), uptime(QStringLiteral("/proc/uptime"));
    if (!stat.open(QIODevice::ReadOnly) || !uptime.open(QIODevice::ReadOnly)) return -1;
    const QByteArray line = stat.readAll();
    // Field 22 (start time in clock ticks) counts from after the ")" of the name.
    const QList<QByteArray> fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
    const long hz = sysconf(_SC_CLK_TCK);
    if (fields.size() < 20 || hz <= 0) return -1;
    const double startS = fields.at(19).toDouble() / double(hz);
    const double upS = uptime.readAll().split(' ').value(0).toDouble();
    return upS > startS ? qint64((upS - startS) * 1000) : -1;
#else
    return -1;
#endif
}
}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("SamNPlayer2");
    // Shown in the top bar; "-dev" unless a release build (0.7 "dev" label).
    QGuiApplication::setApplicationVersion(QStringLiteral(SAMN_VERSION));
    QGuiApplication::setOrganizationName("SamNPlayer");
    // The log (Settings → Log): Qt's messages and the device log, also in
    // <config>/logs/samnplayer2.log, at the saved level (0.7 log.level).
    samn::ui::AppLog::installMessageHandler();
    samn::ui::SettingsController::applySavedLogLevel();
    samn::ui::AppLog::instance().enableFile(samn::config_dir() / "logs" / "samnplayer2.log");
    qInfo().noquote() << "SamNPlayer" << QGuiApplication::applicationVersion() << "started";
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Sora.ttf");
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Figtree.ttf");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
    engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
    // Files on the command line open like a drop: several scripts become the play list.
    QStringList startupPaths;
    for (int i = 1; i < argc; ++i) startupPaths << QString::fromLocal8Bit(argv[i]);
    engine.rootContext()->setContextProperty("startupPaths", startupPaths);
    // Test hooks (synthetic data only): SAMN_MODE=1..4 starts in that mode, SAMN_TRAINING_START=<program
    // name | custom> starts a Training on the Mock device at once.
    const int mode = qEnvironmentVariableIntValue("SAMN_MODE");
    engine.rootContext()->setContextProperty("startMode", mode >= 1 && mode <= 4 ? mode - 1 : 0);
    engine.rootContext()->setContextProperty("trainingStart", qEnvironmentVariable("SAMN_TRAINING_START"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // The loading screen first (Owner 26 Oct): it paints before the main
    // window is built and stays until that window is whole.
    QElapsedTimer startup;
    startup.start();
    // Resource path instead of loadFromModule (Qt 6.5+) so Qt 6.4 works too.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/SamN/qml/Splash.qml")));
    QPointer<QQuickWindow> splash = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
    if (splash) {
        // Let it reach the screen before the main window blocks the thread.
        bool painted = false;
        QObject::connect(splash, &QQuickWindow::frameSwapped, splash, [&painted] { painted = true; });
        QElapsedTimer wait;
        wait.start();
        while (!painted && wait.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/SamN/qml/Main.qml")));
    // A broken interface must fail loudly (the CI smoke test relies on it).
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(engine.rootObjects().size() - 1));
    if (!window || window == splash) return 1;
    const qint64 builtMs = startup.elapsed();
    // Shown once the background pages are there, at the latest after 10 s.
    auto show = [window, splash, &startup, builtMs] {
        if (window->isVisible()) return;
        window->show();
        if (splash) splash->close();
        const qint64 process = MsSinceProcessStart();
        qInfo().noquote() << QStringLiteral("Startup: interface built in %1 ms, shown after %2 ms%3")
                                 .arg(builtMs)
                                 .arg(startup.elapsed())
                                 .arg(process >= 0 ? QStringLiteral(" (%1 ms since the program started)").arg(process) : QString());
    };
    auto* readyPoll = new QTimer(window);
    readyPoll->setInterval(30);
    QObject::connect(readyPoll, &QTimer::timeout, window, [window, readyPoll, show] {
        if (!window->property("ready").toBool()) return;
        readyPoll->stop();
        show();
    });
    readyPoll->start();
    QTimer::singleShot(10000, window, show);
    // Test hook: SAMN_SMOKE_PLAY=1 plays the video opened at start (the
    // package smoke test checks that its first picture comes).
    if (qEnvironmentVariableIsSet("SAMN_SMOKE_PLAY")) QMetaObject::invokeMethod(window, "smokePlay");
    // Test hook: SAMN_SCREENSHOT=file.png [SAMN_SCREENSHOT_DELAY_MS] saves the
    // window and quits (used for visual checks with test-pattern media only).
    if (const QString shot = qEnvironmentVariable("SAMN_SCREENSHOT"); !shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("SAMN_SCREENSHOT_DELAY_MS");
        QTimer::singleShot(delay > 0 ? delay : 2500, &app, [window, shot] {
            QCoreApplication::exit(window->isVisible() && window->grabWindow().save(shot) ? 0 : 2);
        });
    }
    return app.exec();
}
