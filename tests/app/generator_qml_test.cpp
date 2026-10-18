// The Generator page (P3.3): loads without QML warnings, a mouse drag on the
// frame sets the tip box in video pixels, Create runs and the result is
// handed on for review. The window is grabbed to a PNG when SAMN_TEST_SHOT
// names a file (generated test clip only).
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "generator_controller.h"
#include "icon_provider.h"
#include "synthetic_clips.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::GeneratorController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
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
}  // namespace

class GeneratorQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase() {
        qputenv("SAMN_CONFIG_DIR", dir_.path().toUtf8());
        g_previous = qInstallMessageHandler(capture);
    }

    void dragCreateAndReview() {
        const QString video = dir_.filePath("clip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(video.toStdU16String()), 8, 80.0, 1.0));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                property string reviewed
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20; onReviewRequested: (p) => reviewed = p }
            })",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1400, 820);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* gen = page->property("controller").value<GeneratorController*>();
        QVERIFY(gen);
        QVERIFY(!named(root.get(), "createButton")->isEnabled());
        QVERIFY(!named(root.get(), "usePlayerVideo")->isVisible());

        // "Use Player video" appears with the Player's video and opens it.
        page->setProperty("playerVideoUrl", QUrl::fromLocalFile(video));
        QTRY_VERIFY(named(root.get(), "usePlayerVideo")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "usePlayerVideo"), "clicked");
        QTRY_VERIFY(gen->hasVideo());
        QCOMPARE(named(root.get(), "videoLabel")->property("text").toString(), QStringLiteral("clip.mp4"));

        // A drag over the frame becomes the tip box in video pixels. The seek
        // row and box label appear with the video and shrink the frame, so
        // wait until the layout has settled.
        auto* frame = named(root.get(), "frameView");
        QTRY_VERIFY(named(root.get(), "frameSlider")->isVisible());
        QRectF ir;
        QTRY_VERIFY([&] {
            const QRectF now = frame->property("imageRect").toRectF();
            const bool same = now == ir && !now.isEmpty();
            ir = now;
            return same;
        }());
        QVERIFY(ir.width() > 100);
        const double s = ir.width() / 320.0;
        const QPointF a = frame->mapToScene(QPointF(ir.x() + 100 * s, ir.y() + 40 * s));
        const QPointF b = frame->mapToScene(QPointF(ir.x() + 220 * s, ir.y() + 200 * s));
        QTest::mousePress(&window, Qt::LeftButton, {}, a.toPoint());
        QTest::mouseMove(&window, ((a + b) / 2).toPoint());
        QTest::mouseMove(&window, b.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, b.toPoint());
        QTRY_VERIFY(gen->hasBox());
        QVERIFY(qAbs(gen->box().x() - 100) <= 2);
        QVERIFY(qAbs(gen->box().y() - 40) <= 2);
        QVERIFY(qAbs(gen->box().width() - 120) <= 3);
        QVERIFY(qAbs(gen->box().height() - 160) <= 3);
        QVERIFY(named(root.get(), "tipBox")->isVisible());
        QVERIFY(named(root.get(), "createButton")->isEnabled());

        // Feel: the style combo and a toggle reach the controller.
        auto* combo = named(root.get(), "styleCombo");
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 2));
        combo->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 2));
        QTRY_COMPARE(gen->profile(), QStringLiteral("autotune"));
        auto* invert = named(root.get(), "toggle_invert");
        QVERIFY(invert);
        QTest::mouseClick(&window, Qt::LeftButton, {},
                          invert->mapToScene(QPointF(invert->width() / 2, invert->height() / 2)).toPoint());
        QTRY_VERIFY(gen->invert());

        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT"); !shot.isEmpty()) window.grabWindow().save(QString::fromLocal8Bit(shot));

        QMetaObject::invokeMethod(named(root.get(), "createButton"), "clicked");
        QTRY_VERIFY(gen->running());
        QVERIFY(named(root.get(), "cancelButton")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("reviewed").toString().isEmpty(), 60000);
        QCOMPARE(root->property("reviewed").toString(), dir_.filePath("clip.samn"));
        QVERIFY(named(root.get(), "reviewButton")->isVisible());
        QCOMPARE(named(root.get(), "progressText")->property("text").toString(), QStringLiteral("100 %"));
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }
};

QTEST_MAIN(GeneratorQmlTest)
#include "generator_qml_test.moc"
