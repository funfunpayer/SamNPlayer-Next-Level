// The Inspector (Devices tab) loads without QML warnings, shows a connected
// device and a stroker's controls. Window grabbed to a PNG when
// SAMN_TEST_SHOT names a file (visual check with synthetic data only).
#include <QFile>
#include <filesystem>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "device_controller.h"
#include "fake_servers.h"
#include "icon_provider.h"
#include "player_controller.h"
#include "samn/samn_file.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::DeviceController;
using samn::ui::PlayerController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // Only QML problems count: platform messages (fonts, offscreen, Windows
    // runner) are not the Inspector's and differ between machines.
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
}
}  // namespace

class InspectorQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }

    void devicesTabLoadsWithoutWarnings() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));  // Qt 6.4 does not add it
            engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
            QQmlComponent comp(&engine);
            comp.setData(R"(
                import QtQuick
                import SamN
                Item {
                    width: 360; height: 900
                    property alias device: dev
                    property alias player: pc
                    DeviceController { id: dev }
                    PlayerController { id: pc; device: dev }
                    InspectorPanel { id: panel; objectName: "panel"; anchors.fill: parent; ctrl: pc; device: dev }
                })",
                         QUrl("qrc:/test/Wrapper.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            // The window is declared first so it outlives the root item:
            // the root sits in the window's content item, and destroying the
            // window first leaves it with a dangling parent (a crash at the
            // end of the test on Windows).
            QQuickWindow window;
            window.resize(360, 900);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            auto* dev = root->property("device").value<DeviceController*>();
            auto* player = root->property("player").value<PlayerController*>();
            QVERIFY(dev && player);
            // The panel shows the saved choice and the state: not connected.
            QTest::qWait(100);
            // A connected stroker brings its own controls.
            samn_test::UdpLineSink sink;
            QVERIFY(sink.listening());
            dev->connectTCode(sink.hostport());
            QTRY_COMPARE_WITH_TIMEOUT(dev->state(), QString("connected"), 10000);
            QTest::qWait(200);
            const QString shot = qEnvironmentVariable("SAMN_TEST_SHOT");
            if (!shot.isEmpty()) QVERIFY(window.grabWindow().save(shot));
            // A Sam Neo 2 class device brings the output rows.
            dev->disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!dev->connected(), 10000);
            dev->connectMock();
            QTRY_COMPARE_WITH_TIMEOUT(dev->state(), QString("connected"), 10000);
            QTest::qWait(200);
            const QString shot2 = qEnvironmentVariable("SAMN_TEST_SHOT_MOCK");
            if (!shot2.isEmpty()) QVERIFY(window.grabWindow().save(shot2));
            dev->disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!dev->connected(), 10000);
        }
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // Playback tab: the Extended-O button follows the playback and starts a
    // cycle; while it runs the button is disabled (a press would be ignored).
    void extendedOButtonTriggersWhilePlaying() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
            QQmlComponent comp(&engine);
            comp.setData(R"(
                import QtQuick
                import SamN
                Item {
                    width: 360; height: 1100
                    property alias player: pc
                    DeviceController { id: dev }
                    PlayerController { id: pc; device: dev }
                    InspectorPanel { anchors.fill: parent; ctrl: pc; device: dev; tab: 2 }
                })",
                         QUrl("qrc:/test/Wrapper2.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            QQuickWindow window;
            window.resize(360, 1100);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* player = root->property("player").value<PlayerController*>();
            QVERIFY(player);
            auto* button = root->findChild<QQuickItem*>("eoTrigger");
            QVERIFY(button);
            QVERIFY(!button->isEnabled());  // not playing
            player->setPlaying(true);
            QTRY_VERIFY(button->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
            QVERIFY(player->output()->extendedOActive());
            QTRY_VERIFY(!button->isEnabled());
            const QString shot = qEnvironmentVariable("SAMN_TEST_SHOT_PLAYBACK");
            if (!shot.isEmpty()) QVERIFY(window.grabWindow().save(shot));
            player->setPlaying(false);
        }
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // Playback tab: the Effects card (presets, Intensity, Smoothing, Max
    // speed, Tick, Sync mode) and the contact and strength controls of an
    // Emotion Script drive the controller; no QML warnings.
    void playbackTabMappingControls() {
        QTemporaryDir files;
        const QString src = files.filePath("tj.funscript");
        {
            QFile f(src);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":20},{"at":400,"pos":90},{"at":800,"pos":15},{"at":1200,"pos":95},{"at":1600,"pos":30}],
                "metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","contact_vibration":true,"contact_vibration_span":0.75}}})");
        }
        samn::Document doc = samn::LoadFunscriptFile(std::filesystem::u8path(src.toStdString()));
        doc.strength_presets = samn::DefaultStrengthPresets();
        doc.active_strength = "normal";
        const QString path = files.filePath("feel.samn");
        samn::SaveDocument(std::filesystem::u8path(path.toStdString()), doc);

        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
            QQmlComponent comp(&engine);
            comp.setData(R"(
                import QtQuick
                import SamN
                Item {
                    width: 360; height: 1700
                    property alias player: pc
                    DeviceController { id: dev }
                    PlayerController { id: pc; device: dev }
                    InspectorPanel { anchors.fill: parent; ctrl: pc; device: dev; tab: 2 }
                })",
                         QUrl("qrc:/test/Wrapper3.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            QQuickWindow window;
            window.resize(360, 1700);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* player = root->property("player").value<PlayerController*>();
            QVERIFY(player);
            QVERIFY(player->openPath(path));
            auto* m = player->mapping();
            const auto item = [&](const char* name) {
                auto* it = root->findChild<QQuickItem*>(name);
                if (!it) qWarning("no %s", name);
                return it;
            };
            for (const char* n : {"fxPreset", "fxName", "fxSave", "fxDelete", "syncMode", "contactOff", "contactCurve", "strengthPreset"}) {
                QVERIFY2(item(n) && item(n)->isVisible(), n);
            }
            // Sync mode: the combo's choice reaches the controller.
            QVERIFY(QMetaObject::invokeMethod(item("syncMode"), "activated", Q_ARG(int, 5)));
            QCOMPARE(m->syncMode(), QString("suction_position"));
            QTRY_COMPARE(item("syncMode")->property("currentIndex").toInt(), 5);
            // Contact vibration off, the curve and the strength preset.
            QVERIFY(QMetaObject::invokeMethod(item("contactOff"), "clicked"));
            QVERIFY(m->contactOff());
            QVERIFY(QMetaObject::invokeMethod(item("contactOff"), "clicked"));
            QVERIFY(!m->contactOff());
            QVERIFY(item("contactCurve")->isEnabled());  // recipe mode with contact vibration
            QVERIFY(QMetaObject::invokeMethod(item("contactCurve"), "activated", Q_ARG(int, 2)));
            QCOMPARE(m->contactCurve(), QString("peak"));
            QCOMPARE(item("strengthPreset")->property("currentIndex").toInt(), 2);  // —, soft, normal
            QVERIFY(QMetaObject::invokeMethod(item("strengthPreset"), "activated", Q_ARG(int, 3)));
            QCOMPARE(player->activeStrength(), QString("strong"));
            // Effects preset: name, Save, select, Delete.
            QVERIFY(!item("fxSave")->isEnabled());
            QVERIFY(!item("fxDelete")->isEnabled());
            item("fxName")->setProperty("text", "Calm");
            QTRY_VERIFY(item("fxSave")->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(item("fxSave"), "clicked"));
            QCOMPARE(m->effectPresets(), QStringList({"Calm"}));
            QCOMPARE(item("fxName")->property("text").toString(), QString());
            QTRY_COMPARE(item("fxPreset")->property("currentIndex").toInt(), 2);  // Custom, Default, Calm
            QVERIFY(item("fxDelete")->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(item("fxPreset"), "activated", Q_ARG(int, 1)));
            QCOMPARE(m->effectPreset(), QString("__default"));
            QCOMPARE(m->syncMode(), QString("suction_position"));  // not part of a preset (0.7)
            QTRY_VERIFY(!item("fxDelete")->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(item("fxPreset"), "activated", Q_ARG(int, 2)));
            QCOMPARE(m->effectPreset(), QString("Calm"));
            QTest::qWait(100);
            const QString shot = qEnvironmentVariable("SAMN_TEST_SHOT_EFFECTS");
            if (!shot.isEmpty()) QVERIFY(window.grabWindow().save(shot));
            QVERIFY(QMetaObject::invokeMethod(item("fxDelete"), "clicked"));
            QVERIFY(m->effectPresets().isEmpty());
            QTRY_COMPARE(item("fxPreset")->property("currentIndex").toInt(), 0);
        }
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }
};

QTEST_MAIN(InspectorQmlTest)
#include "inspector_qml_test.moc"
