// The AI Train page's label editor (D-155 §2.2), loaded the way the app
// loads it: open a clip, add labels, draw a box on the frame, scrub, mark a
// label absent, undo. Synthetic clips only; SAMN_TEST_SHOT names a PNG.
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "frame_provider.h"
#include "icon_provider.h"
#include "label_controller.h"
#include "synthetic_clips.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::LabelController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
}
// Walks the visual tree (Repeater items are not QObject children of the page).
QQuickItem* named(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (auto* c : root->childItems())
        if (auto* found = named(c, name)) return found;
    return nullptr;
}
}  // namespace

class AiTrainQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase() {
        qputenv("SAMN_CONFIG_DIR", dir_.path().toUtf8());
        g_previous = qInstallMessageHandler(capture);
    }

    void labelEditorDrawsScrubsAndMarks() {
        const QString clip = dir_.filePath("clip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(clip.toStdU16String()), 4, 80.0, 1.0));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport SamN\nRectangle { width: 1400; height: 800; color: \"#021024\"\n"
                     "property alias page: p\nAiTrainPage { id: p; anchors.fill: parent; anchors.margins: 20 } }",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1400, 800);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* labels = page->property("labels").value<LabelController*>();
        QVERIFY(labels);
        QVERIFY(!named(root.get(), "labelEditor")->isVisible());
        // The teachers' setup: NudeNet here or the model server answers A.
        QVERIFY(named(root.get(), "nudenetCard")->isVisible());
        QVERIFY(named(root.get(), "teacherCard")->isVisible());
        auto* here = named(root.get(), "kindNudeNet");
        QCOMPARE(here->isEnabled(), labels->localTeacherAvailable());
        QVERIFY(named(root.get(), "kindServer")->property("checked").toBool());
        if (here->isEnabled()) {
            QVERIFY(QMetaObject::invokeMethod(here, "clicked"));
            QCOMPARE(labels->teacherKind(), QString("nudenet"));
            QTRY_VERIFY(here->property("checked").toBool());
            QVERIFY(named(root.get(), "nudenetDownload")->isVisible());
            QCOMPARE(named(root.get(), "nudenetState")->property("text").toString(), QString("Not fetched yet"));
            labels->setTeacherKind("server");
        }
        QVERIFY(labels->openVideo(clip));
        QTRY_VERIFY(named(root.get(), "labelEditor")->isVisible());

        // A preset chip adds the label; drawing on the frame sets its box.
        QQuickItem* chip = nullptr;
        QTRY_VERIFY((chip = named(root.get(), "addLabel_glans")));
        QTest::mouseClick(&window, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        QTRY_COMPARE(labels->currentLabel(), QString("glans"));
        auto* frame = named(root.get(), "labelFrame");
        QRectF ir;
        QTRY_VERIFY((ir = frame->property("imageRect").toRectF()).width() > 100);
        const QPointF a = frame->mapToScene(ir.topLeft() + QPointF(ir.width() * 0.2, ir.height() * 0.2));
        const QPointF b = frame->mapToScene(ir.topLeft() + QPointF(ir.width() * 0.5, ir.height() * 0.6));
        QTest::mousePress(&window, Qt::LeftButton, {}, a.toPoint());
        QTest::mouseMove(&window, ((a + b) / 2).toPoint());
        QTest::mouseMove(&window, b.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, b.toPoint());
        QTRY_COMPARE(labels->lanes().value(0).toMap().value("keys").toList().size(), 1);
        const QVariantMap box = labels->boxes().value(0).toMap();
        QVERIFY(box.value("present").toBool());
        QVERIFY(qAbs(box.value("w").toDouble() - labels->videoSize().width() * 0.3) < 4);

        // Scrub with the keys and mark the label absent with X.
        QTest::keyClick(&window, Qt::Key_Period);
        QTRY_VERIFY(labels->previewMs() > 0);
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY(labels->previewMs() >= 1000);
        QTest::keyClick(&window, Qt::Key_X);
        QTRY_COMPARE(labels->lanes().value(0).toMap().value("keys").toList().size(), 2);
        QVERIFY(!labels->boxes().value(0).toMap().value("present").toBool());
        QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(labels->lanes().value(0).toMap().value("keys").toList().size(), 1);

        // The + on a row adds another object of its class: two breasts, a contact point.
        labels->seek(500);
        QVERIFY(labels->addLabel("breast"));
        QVERIFY(labels->setBox(QRectF(20, 150, 50, 50)));
        QQuickItem* plus = nullptr;
        QTRY_VERIFY((plus = named(root.get(), "addObject_breast")));
        QTest::mouseClick(&window, Qt::LeftButton, {}, plus->mapToScene(QPointF(plus->width() / 2, plus->height() / 2)).toPoint());
        QTRY_COMPARE(labels->currentLabel(), QString("breast 2"));
        QVERIFY(labels->setBox(QRectF(80, 150, 50, 50)));
        QVERIFY(labels->addLabel("contact"));
        QVERIFY(labels->setBox(QRectF(140, 100, 16, 16)));
        QCOMPARE(labels->lanes().size(), 4);
        QCOMPARE(labels->lanes()[2].toMap().value("classIndex").toInt(), 1);  // breast 2 shares the colour of breast
        // "Not this": the exclusion chip adds a zone the tracker stays out of.
        QQuickItem* ex = nullptr;
        QTRY_VERIFY((ex = named(root.get(), "addLabel_exclude")));
        QTest::mouseClick(&window, Qt::LeftButton, {}, ex->mapToScene(QPointF(ex->width() / 2, ex->height() / 2)).toPoint());
        QTRY_COMPARE(labels->currentLabel(), QString("exclude"));
        QVERIFY(labels->setBox(QRectF(200, 160, 90, 60)));
        QCOMPARE(labels->lanes().size(), 5);
        // Phases: pick "oral", drag over the phase lane, the badge shows it.
        QQuickItem* oral = nullptr;
        QTRY_VERIFY((oral = named(root.get(), "phaseChip_oral")));
        QTest::mouseClick(&window, Qt::LeftButton, {}, oral->mapToScene(QPointF(oral->width() / 2, oral->height() / 2)).toPoint());
        auto* paint = named(root.get(), "phasePaint");
        QVERIFY(paint);
        const QPointF p0 = paint->mapToScene(QPointF(2, paint->height() / 2));
        const QPointF p1 = paint->mapToScene(QPointF(paint->width() * 0.6, paint->height() / 2));
        QTest::mousePress(&window, Qt::LeftButton, {}, p0.toPoint());
        QTest::mouseMove(&window, ((p0 + p1) / 2).toPoint());
        QTest::mouseMove(&window, p1.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, p1.toPoint());
        QTRY_COMPARE(labels->phases().size(), 1);
        QCOMPARE(labels->phases()[0].toMap().value("phase").toString(), QString("oral"));
        labels->seek(500);
        QTRY_VERIFY(named(root.get(), "phaseBadge")->isVisible());
        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT"); !shot.isEmpty()) {
                        QTest::qWait(300);
            window.grabWindow().save(QString::fromLocal8Bit(shot));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }
};

QTEST_MAIN(AiTrainQmlTest)
#include "aitrain_qml_test.moc"
