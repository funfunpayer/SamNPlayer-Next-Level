// The Inspector (Devices tab) loads without QML warnings, shows a connected
// device and a stroker's controls. Window grabbed to a PNG when
// SAMN_TEST_SHOT names a file (visual check with synthetic data only).
#include <QFile>
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

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::DeviceController;
using samn::ui::PlayerController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) g_messages << msg;
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
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            QQuickWindow window;
            window.resize(360, 900);
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
};

QTEST_MAIN(InspectorQmlTest)
#include "inspector_qml_test.moc"
