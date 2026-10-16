#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

// The SamN QML module is a static library: import its plugin explicitly.
Q_IMPORT_QML_PLUGIN(SamNPlugin)

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("SamNPlayer2");
    QGuiApplication::setOrganizationName("SamNPlayer");
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Sora.ttf");
    QFontDatabase::addApplicationFont(":/qt/qml/SamN/qml/fonts/Figtree.ttf");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("startupPath", argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // Resource path instead of loadFromModule (Qt 6.5+) so Qt 6.4 works too.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/SamN/qml/Main.qml")));
    // Test hook: SAMN_SCREENSHOT=file.png [SAMN_SCREENSHOT_DELAY_MS] saves the
    // window and quits (used for visual checks with test-pattern media only).
    if (const QString shot = qEnvironmentVariable("SAMN_SCREENSHOT"); !shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("SAMN_SCREENSHOT_DELAY_MS");
        QTimer::singleShot(delay > 0 ? delay : 2500, &app, [&engine, shot] {
            if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) w->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
