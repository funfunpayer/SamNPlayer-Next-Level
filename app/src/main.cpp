#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include "icon_provider.h"

// The SamN QML module is a static library: import its plugin explicitly.
Q_IMPORT_QML_PLUGIN(SamNPlugin)

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("SamNPlayer2");
    // Shown in the top bar; "-dev" unless a release build (0.7 "dev" label).
    QGuiApplication::setApplicationVersion(QStringLiteral(SAMN_VERSION));
    QGuiApplication::setOrganizationName("SamNPlayer");
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Sora.ttf");
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Figtree.ttf");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
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
    // Resource path instead of loadFromModule (Qt 6.5+) so Qt 6.4 works too.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/SamN/qml/Main.qml")));
    // A broken interface must fail loudly (the CI smoke test relies on it).
    if (engine.rootObjects().isEmpty()) return 1;
    // Test hook: SAMN_SCREENSHOT=file.png [SAMN_SCREENSHOT_DELAY_MS] saves the
    // window and quits (used for visual checks with test-pattern media only).
    if (const QString shot = qEnvironmentVariable("SAMN_SCREENSHOT"); !shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("SAMN_SCREENSHOT_DELAY_MS");
        QTimer::singleShot(delay > 0 ? delay : 2500, &app, [&engine, shot] {
            auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            QCoreApplication::exit(w && w->grabWindow().save(shot) ? 0 : 2);
        });
    }
    return app.exec();
}
