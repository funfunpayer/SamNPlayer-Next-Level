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

#include <functional>

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

bool isQmlType(const QObject* o, const char* qmlType) {
    return QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String(qmlType) + "_QMLTYPE");
}

bool effectivelyVisible(QQuickItem* item) {
    for (QQuickItem* p = item; p; p = p->parentItem())
        if (!p->isVisible()) return false;
    return true;
}

QQuickItem* visibleHelpChip(QQuickItem* root, const char* key) {
    for (auto* item : root->findChildren<QQuickItem*>()) {
        if (!isQmlType(item, "HelpChip")) continue;
        if (item->property("key").toString() != QLatin1String(key)) continue;
        if (!effectivelyVisible(item)) continue;
        return item;
    }
    return nullptr;
}

// Repeater delegates are visual children only, so walk childItems() as well.
QQuickItem* named(QQuickItem* root, const char* name) {
    if (auto* hit = root->findChild<QQuickItem*>(QLatin1String(name))) return hit;
    for (QQuickItem* child : root->childItems()) {
        if (child->objectName() == QLatin1String(name)) return child;
        if (auto* hit = named(child, name)) return hit;
    }
    return nullptr;
}

QStringList controlsPast(QQuickItem* box, qreal slack = 0.5) {
    QStringList bad;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* it) {
        for (QQuickItem* c : it->childItems()) {
            if (!c->isVisible() || c->width() <= 0 || c->height() <= 0) continue;
            const QByteArray cls = c->metaObject()->className();
            const qreal right = c->mapToItem(box, QPointF(c->width(), 0)).x();
            if (right > box->width() + slack &&
                (cls.contains("FlowButton") || cls.contains("FlowCombo") || cls.contains("HelpChip") ||
                 cls.contains("FlowToggle") || cls.contains("FlowSlider") || cls.contains("FlowTextField") ||
                 cls.contains("IconButton") || cls.contains("FlowTabs") || cls.contains("PlayButton") ||
                 cls.contains("TrainingRing") || cls.contains("DualRing") || cls.contains("NumField")))
                bad << QString("%1 %2 ends at %3 of %4")
                           .arg(QString::fromLatin1(cls), c->objectName())
                           .arg(right)
                           .arg(box->width());
            walk(c);
        }
    };
    walk(box);
    return bad;
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

    // 0.7 TR-02 / GAP-16: Esc interrupts the running cycle from anywhere on
    // the page (a window shortcut, so no field needs the focus).
    void escapeInterruptsTheRunningCycle() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                width: 1440; height: 860
                property alias page: tp
                DeviceController { id: dev }
                TrainingPage { id: tp; anchors.fill: parent; device: dev }
            })",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;  // declared first: it must outlive the root item
        window.resize(1440, 860);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* ctl = root->property("page").value<QQuickItem*>()->property("controller").value<TrainingController*>();
        QVERIFY(ctl);
        ctl->setCycles(2);
        ctl->setRampUpMs(100);
        ctl->setHoldMs(5000);
        ctl->setRestMs(100);
        QTest::keyClick(&window, Qt::Key_Escape);  // idle: nothing to interrupt
        QVERIFY(ctl->logLines().filter("stopped by you").isEmpty());
        QVERIFY(ctl->start());
        QTRY_VERIFY(ctl->vibration() > 0.5);
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_COMPARE(ctl->logLines().filter("stopped by you").size(), 1);
        QCOMPARE(ctl->state(), QString("running"));  // the cycle ends, the session goes on
        ctl->stop();
        QTRY_COMPARE(ctl->state(), QString("idle"));
    }

    // 0.8 training_video.js: the Video stage plays the user's film while the
    // session runs and pauses when it ends; the choice and the film are kept.
    void videoStagePlaysAlongWithTheSession() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport SamN\nItem { width: 1440; height: 860; property alias page: tp\n"
                     "DeviceController { id: dev }\nTrainingPage { id: tp; anchors.fill: parent; device: dev } }",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1440, 860);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* ctl = page->property("controller").value<TrainingController*>();
        QVERIFY(ctl);
        QCOMPARE(ctl->stage(), QString("rings"));
        auto* hint = page->findChild<QQuickItem*>("stageVideoHint");
        auto* media = page->findChild<QObject*>("stageMedia");
        QVERIFY(hint && media);
        QVERIFY(!hint->isVisible());

        ctl->setStage(QStringLiteral("video"));
        QTRY_VERIFY(hint->isVisible());  // no film yet
        ctl->setStage(QStringLiteral("nonsense"));
        QCOMPARE(ctl->stage(), QString("video"));

        const QString film = QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4");
        ctl->chooseVideo(QUrl::fromLocalFile(film).toString());
        QCOMPARE(ctl->videoPath(), QFileInfo(film).absoluteFilePath());
        QCOMPARE(ctl->videoName(), QString("testpattern_4s.mp4"));
        QCOMPARE(ctl->recentVideos().value(0), QFileInfo(film).absoluteFilePath());
        QVERIFY(ctl->videoLoop() && ctl->videoMuted());
        QTRY_VERIFY(!hint->isVisible());
        QCOMPARE(media->property("playbackState").toInt(), 0);  // stopped until the session runs

        ctl->setCycles(1);
        ctl->setRampUpMs(100);
        ctl->setHoldMs(20000);
        QVERIFY(ctl->start());
        QTRY_COMPARE(media->property("playbackState").toInt(), 1);  // playing
        ctl->stop();
        QTRY_COMPARE(ctl->state(), QString("idle"));
        QTRY_COMPARE(media->property("playbackState").toInt(), 2);  // paused

        // A second controller reads the kept stage and film.
        TrainingController again;
        QCOMPARE(again.stage(), QString("video"));
        QCOMPARE(again.videoPath(), QFileInfo(film).absoluteFilePath());
        // The Flow figure: the technique's shape while idle, the live level in a session.
        ctl->setStage(QStringLiteral("figure"));
        auto* readout = page->findChild<QQuickItem*>("figureReadout");
        QVERIFY(readout);
        QTRY_VERIFY(readout->isVisible());
        QVERIFY(readout->property("text").toString().endsWith("Idle"));
        QVERIFY(ctl->start());
        QTRY_VERIFY(readout->property("text").toString().contains("% \u00b7 up"));
        ctl->stop();
        QTRY_COMPARE(ctl->state(), QString("idle"));

        ctl->setVideoLoop(false);
        ctl->setStage(QStringLiteral("rings"));
        QTRY_VERIFY(!hint->isVisible());
        TrainingController third;
        QVERIFY(!third.videoLoop());
        QCOMPARE(third.stage(), QString("rings"));
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
            // On the Figure stage, 0.7's dual ring shows both levels.
            QVERIFY(ctl->setProperty("stage", "figure"));
            auto* dual = page->findChild<QQuickItem*>("dualRing");
            QVERIFY(dual);
            QTRY_VERIFY(dual->isVisible());
            QVERIFY(!dual->property("live").toBool());
            QVERIFY(ctl->start());
            QTRY_COMPARE(ctl->state(), QString("running"));
            QTRY_VERIFY(ctl->vibration() > 0.3);
            QTRY_VERIFY(dual->property("live").toBool());
            auto* vibValue = dual->findChild<QQuickItem*>("dualRingVibrationValue");
            QVERIFY(vibValue);
            QTRY_VERIFY(vibValue->property("text").toString() != "0%");
            QVERIFY(dual->findChild<QQuickItem*>("dualRingVibration")->property("opacity").toDouble() > 0.45);
            if (const QString shot = qEnvironmentVariable("SAMN_TEST_SHOT_FIGURE"); !shot.isEmpty()) window.grabWindow().save(shot);
            QTRY_COMPARE(ctl->state(), QString("idle"));  // a short session ends by itself
            QTRY_VERIFY(!dual->property("live").toBool());
            QVERIFY(ctl->setProperty("stage", "rings"));
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

    // PARITY item 21: Training HelpChips next to Mock / Session Mode / Stage /
    // Feedback (and Intensity once a named program is selected) resolve.
    void helpChipsResolveOnThePage() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                width: 1440; height: 860
                property alias page: tp
                DeviceController { id: dev }
                TrainingPage { id: tp; anchors.fill: parent; device: dev }
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
        auto* page = root->property("page").value<QQuickItem*>();
        auto* ctl = page->property("controller").value<TrainingController*>();
        QVERIFY(page && ctl);

        struct Row { const char* key; const char* snippet; };
        const Row custom[] = {
            {"training.mock", "simulated device"},
            {"training.session-mode", "Stop-Start"},
            {"training.stage", "Rings"},
            {"training.feedback", "Arousal"},
            {"training.cycles", "Number of training cycles"},
        };
        for (const Row& row : custom) {
            auto* chip = visibleHelpChip(page, row.key);
            QVERIFY2(chip, row.key);
            QVERIFY2(chip->property("text").toString().contains(QLatin1String(row.snippet)),
                     qPrintable(QString("%1: %2").arg(row.key, chip->property("text").toString())));
        }
        QVERIFY(!visibleHelpChip(page, "training.tier"));  // Intensity is for named programs
        QVERIFY(!ctl->programs().isEmpty());
        ctl->setScriptName(ctl->programs().first().toMap().value("name").toString());
        QVERIFY(ctl->scriptName() != QString());
        QTRY_VERIFY(visibleHelpChip(page, "training.tier"));
        QVERIFY(visibleHelpChip(page, "training.tier")->property("text").toString().contains(QLatin1String("Gentle")));
    }

    // 960 px is the Windows-wide-font stand-in (Generator #239 / #244): a Text
    // in a RowLayout without fillWidth keeps its glyph width as the row's
    // minimum, so the centre cards and stage chrome used to paint past the
    // page. Short windows must scroll rather than overflow.
    void pageFitsAt960AndRingsScale() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        for (const QSize size : {QSize(960, 600), QSize(1440, 860)}) {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
            QQmlComponent comp(&engine);
            comp.setData(R"(
                import QtQuick
                import SamN
                Item {
                    property alias page: tp
                    DeviceController { id: dev }
                    TrainingPage { id: tp; objectName: "page"; anchors.fill: parent; device: dev }
                })",
                         QUrl("qrc:/test/TrainingFit.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            QQuickWindow window;
            window.resize(size);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            root->setSize(size);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::qWait(200);
            auto* page = root->property("page").value<QQuickItem*>();
            QVERIFY(page);
            auto* ctl = page->property("controller").value<TrainingController*>();
            QVERIFY(ctl);

            auto* left = named(page, "leftPanel");
            auto* center = named(page, "centerScroll");
            auto* right = named(page, "rightPanel");
            auto* stage = named(page, "stage");
            auto* rings = named(page, "trainingRings");
            QVERIFY(left && center && right && stage && rings);
            QVERIFY2(left->mapToItem(page, QPointF(left->width(), 0)).x() <= page->width() + 0.5,
                     qPrintable(QString("left ends at %1 of %2").arg(left->mapToItem(page, QPointF(left->width(), 0)).x()).arg(page->width())));
            QVERIFY2(right->mapToItem(page, QPointF(right->width(), 0)).x() <= page->width() + 0.5,
                     qPrintable(QString("right ends at %1 of %2").arg(right->mapToItem(page, QPointF(right->width(), 0)).x()).arg(page->width())));
            QVERIFY2(center->mapToItem(page, QPointF(center->width(), 0)).x() <= page->width() + 0.5,
                     qPrintable(QString("center ends at %1 of %2").arg(center->mapToItem(page, QPointF(center->width(), 0)).x()).arg(page->width())));

            const QStringList stuck = controlsPast(page);
            QVERIFY2(stuck.isEmpty(),
                     qPrintable(QString("%1 control(s) stick out at %2x%3: %4")
                                    .arg(stuck.size())
                                    .arg(size.width())
                                    .arg(size.height())
                                    .arg(stuck.join("; "))));

            QCOMPARE(ctl->stage(), QString("rings"));
            QTRY_VERIFY(rings->isVisible());
            QVERIFY2(rings->mapToItem(stage, QPointF(rings->width(), 0)).x() <= stage->width() + 1.0,
                     qPrintable(QString("rings end at %1 of stage %2")
                                    .arg(rings->mapToItem(stage, QPointF(rings->width(), 0)).x())
                                    .arg(stage->width())));
            QVERIFY2(rings->height() <= stage->height() + 1.0,
                     qPrintable(QString("rings h %1 stage %2").arg(rings->height()).arg(stage->height())));

            ctl->setStage(QStringLiteral("figure"));
            auto* dual = named(page, "dualRing");
            QVERIFY(dual);
            QTRY_VERIFY(dual->isVisible());
            QVERIFY2(dual->mapToItem(stage, QPointF(dual->width(), 0)).x() <= stage->width() + 1.0,
                     qPrintable(QString("dualRing ends at %1 of stage %2")
                                    .arg(dual->mapToItem(stage, QPointF(dual->width(), 0)).x())
                                    .arg(stage->width())));
            QVERIFY2(dual->property("size").toReal() <= 128.0 + 0.5, "dualRing size above the 128 px draw box");
            QVERIFY2(dual->property("size").toReal() >= 72.0 - 0.5, "dualRing size collapsed");

            ctl->setStage(QStringLiteral("video"));
            QTRY_VERIFY(named(page, "stageVideoChoose")->isVisible());
            const QStringList videoStuck = controlsPast(stage);
            QVERIFY2(videoStuck.isEmpty(),
                     qPrintable(QString("video chrome sticks out at %1: %2").arg(size.width()).arg(videoStuck.join("; "))));
            ctl->setStage(QStringLiteral("rings"));

            auto* editor = named(page, "editor");
            QVERIFY(editor);
            QVERIFY(QMetaObject::invokeMethod(editor, "show", Q_ARG(QVariant, QString())));
            QVERIFY(editor->isVisible());
            auto* editorCard = named(page, "editorCard");
            QVERIFY(editorCard);
            QTRY_VERIFY(editorCard->width() > 0);
            QVERIFY2(editorCard->width() <= page->width() + 0.5,
                     qPrintable(QString("editor %1 of page %2").arg(editorCard->width()).arg(page->width())));
            const QStringList editorStuck = controlsPast(editorCard, 1.0);
            QVERIFY2(editorStuck.isEmpty(),
                     qPrintable(QString("editor controls stick out at %1: %2").arg(size.width()).arg(editorStuck.join("; "))));
            editor->setProperty("open", false);

            if (size.height() <= 600) {
                const qreal view = center->height();
                const qreal content = center->property("contentHeight").toReal();
                QVERIFY2(content + 0.5 >= view, qPrintable(QString("content %1 view %2").arg(content).arg(view)));
            }
        }
        qInstallMessageHandler(g_previous);
        QStringList loops;
        for (const QString& m : g_messages) {
            if (m.contains(QLatin1String("binding loop")) || m.contains(QLatin1String("layout loop")) ||
                m.contains(QLatin1String("QQuickLayout")))
                loops << m;
        }
        QVERIFY2(loops.isEmpty(), qPrintable(loops.join('\n')));
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }
};

QTEST_MAIN(TrainingQmlTest)
#include "training_qml_test.moc"
