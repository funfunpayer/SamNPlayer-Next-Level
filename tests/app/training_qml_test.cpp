// The Training page loads without QML warnings, follows the device (rings only for the
// channels it has), runs a session on the Mock device and its script editor builds,
// validates and saves a program. The window is grabbed to a PNG when SAMN_TEST_SHOT
// names a file (synthetic data only).
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
#include "training_controller.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::DeviceController;
using samn::ui::TrainingController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
}

// Visible TrainingRing items (they carry a "label" property).
int visibleRings(QQuickItem* root) {
    int n = 0;
    for (auto* item : root->findChildren<QQuickItem*>())
        if (item->isVisible() && item->property("label").isValid() && item->property("level").isValid()) ++n;
    return n;
}
}  // namespace

class TrainingQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }

    void pageRunsASessionAndTheEditorSavesAProgram() {
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
                    width: 1440; height: 860
                    property alias device: dev
                    property alias page: tp
                    DeviceController { id: dev }
                    TrainingPage { id: tp; objectName: "page"; anchors.fill: parent; device: dev }
                })",
                         QUrl("qrc:/test/Wrapper.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            QQuickWindow window;
            window.resize(1440, 860);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            auto* dev = root->property("device").value<DeviceController*>();
            auto* page = root->property("page").value<QQuickItem*>();
            auto* ctl = page->property("controller").value<TrainingController*>();
            QVERIFY(dev && page && ctl);
            QCOMPARE(visibleRings(root.get()), 2);  // nothing known: both rings

            // A session on the Mock device: Play starts it, the ring follows the level.
            ctl->setCycles(1);
            ctl->setRampUpMs(300);
            ctl->setHoldMs(200);
            ctl->setRestMs(100);
            ctl->setProgression(0);
            QVERIFY(ctl->start());
            QTRY_COMPARE(ctl->state(), QString("running"));
            QTRY_VERIFY(ctl->vibration() > 0.3);
            QTRY_COMPARE(ctl->state(), QString("idle"));  // a short session ends by itself
            QCOMPARE(ctl->recap().value("title").toString(), QString("Stop-Start"));

            QTRY_COMPARE(dev->state(), QString("disconnected"));  // Training closes the Mock it opened
            // A stroker has neither motor: no ring, the note says why, Start is refused.
            samn_test::TcpLineSink sink;
            QVERIFY(sink.listening());
            ctl->setMock(false);
            dev->connectTCode("tcp://" + sink.hostport());
            QTRY_COMPARE(dev->state(), QString("connected"));
            QTRY_VERIFY(!ctl->vibrationAvailable() && !ctl->suctionAvailable());
            QTRY_COMPARE(visibleRings(root.get()), 0);
            QVERIFY(ctl->deviceNote().contains("has no vibration or suction motor"));
            QVERIFY(!ctl->start());
            dev->disconnectDevice();
            QTRY_COMPARE(dev->state(), QString("disconnected"));
            QTRY_COMPARE(visibleRings(root.get()), 2);

            // The editor: open new, name it, check, save.
            auto* editor = page->findChild<QQuickItem*>("editor");
            QVERIFY(editor);
            QVERIFY(!editor->isVisible());
            QVERIFY(QMetaObject::invokeMethod(editor, "show", Q_ARG(QVariant, QString())));
            QVERIFY(editor->isVisible());
            QTRY_VERIFY(editor->property("statusOk").toBool());  // a new script has one valid phase ...
            QVariantMap draft = editor->property("draft").toMap();
            QCOMPARE(ctl->saveScript(draft), QString("script needs a name"));  // ... but needs a name to save
            draft["name"] = "From the editor";
            draft["phases"] = QVariantList();
            QVERIFY(editor->setProperty("draft", draft));
            QVERIFY(QMetaObject::invokeMethod(editor, "check"));
            QVERIFY(editor->property("status").toString().startsWith("Not valid yet: a script needs at least 1 phase"));
            draft = editor->property("draft").toMap();
            draft["phases"] = QVariantList{QVariantMap{{"name", "P"}, {"repeatCycles", 2}, {"restMs", 100},
                {"vibration", QVariantMap{{"startLevel", 0.0}, {"peakLevel", 0.5}, {"endLevel", 0.0}, {"rampUpMs", 100}, {"holdMs", 0}, {"rampDownMs", 100}, {"randomJitterFraction", 0.0}}}}};
            QVERIFY(editor->setProperty("draft", draft));
            QVERIFY(QMetaObject::invokeMethod(editor, "check"));
            QVERIFY2(editor->property("statusOk").toBool(), qPrintable(editor->property("status").toString()));
            QVERIFY(editor->property("status").toString().startsWith("Valid: 1 phase,"));
            QCOMPARE(ctl->saveScript(editor->property("draft").toMap()), QString());
            QCOMPARE(ctl->myPrograms().size(), 1);
            QVERIFY(QMetaObject::invokeMethod(editor, "show", Q_ARG(QVariant, QString("From the editor"))));
            QCOMPARE(editor->property("original").toString(), QString("From the editor"));
            QTest::qWait(100);
            if (const QString shot = qEnvironmentVariable("SAMN_TEST_SHOT"); !shot.isEmpty()) window.grabWindow().save(shot);
            editor->setProperty("open", false);

        }
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }
};

QTEST_MAIN(TrainingQmlTest)
#include "training_qml_test.moc"
