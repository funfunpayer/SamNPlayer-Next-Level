// The Generator page (P3.3): loads without QML warnings, a mouse drag on the
// frame sets the tip box in video pixels, Create runs and the result is
// handed on for review. The window is grabbed to a PNG when SAMN_TEST_SHOT
// names a file (generated test clip only).
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "frame_provider.h"
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
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
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
        // Video / curves stage: remembered share (drag handle).
        QVERIFY(named(root.get(), "stageSplit"));
        QVERIFY(named(root.get(), "editorCard"));
        QVERIFY(named(root.get(), "stageSplitHandle"));
        QCOMPARE(gen->editorShare(), 0.40);
        // 0.7's lede and step prompt, verbatim.
        QVERIFY(named(root.get(), "createLede")->isVisible());
        QVERIFY(named(root.get(), "stepPrompt")->property("text").toString().startsWith("Start here: choose a video."));
        QVERIFY(named(root.get(), "createStepPrompt")->isVisible());
        QVERIFY(named(root.get(), "createStepPrompt")->property("text").toString().contains(QLatin1String("Create finds the tip")));
        QVERIFY(!named(root.get(), "reviewCard")->isVisible());
        QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);

        // "Use Player video" appears with the Player's video and opens it.
        page->setProperty("playerVideoUrl", QUrl::fromLocalFile(video));
        QTRY_VERIFY(named(root.get(), "usePlayerVideo")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "usePlayerVideo"), "clicked");
        QTRY_VERIFY(gen->hasVideo());
        // While the automatic search runs (D-159), Find tip takes it over
        // rather than stopping it, and Create can start (it waits for it).
        QVERIFY(gen->autoFinding());
        QCOMPARE(named(root.get(), "findTip")->property("text").toString(), QStringLiteral("Find tip"));
        QVERIFY(named(root.get(), "createButton")->isEnabled());
        QCOMPARE(named(root.get(), "videoLabel")->property("text").toString(), QStringLiteral("clip.mp4"));

        // D-154: the left column names the video and lists it under Recent videos;
        // it collapses to a strip.
        auto* panel = named(root.get(), "projectPanel");
        QVERIFY(panel);
        QCOMPARE(named(root.get(), "projectChooseVideo")->property("text").toString(), QStringLiteral("clip.mp4"));
        QTRY_COMPARE(named(root.get(), "projectRecentVideos")->property("model").toList().size(), 1);
        panel->setProperty("collapsed", true);
        QTRY_COMPARE(panel->width(), 44.0);
        panel->setProperty("collapsed", false);
        QTRY_COMPARE(panel->width(), 248.0);

        // D-154: the video plays as in the Player and pausing shows that frame.
        QVERIFY(named(root.get(), "generatorTransport")->isEnabled());
        QVERIFY(!named(root.get(), "generatorEmpty")->isVisible());
        auto* media = page->findChild<QObject*>("generatorMedia");
        QVERIFY(media);
        QVERIFY(QMetaObject::invokeMethod(page, "togglePlay"));
        QTRY_COMPARE(media->property("playbackState").toInt(), 1);
        // Offscreen FFmpeg sometimes reports Playing with a stuck position; the
        // rest of the Generator flow still needs to run.
        bool playheadMoved = false;
        for (int i = 0; i < 40 && !playheadMoved; ++i) {
            playheadMoved = page->property("playheadMs").toDouble() > 300;
            if (!playheadMoved) QTest::qWait(50);
        }
        if (playheadMoved) {
            QVERIFY(QMetaObject::invokeMethod(page, "togglePlay"));
            QTRY_COMPARE(media->property("playbackState").toInt(), 2);
            QTRY_VERIFY(qAbs(gen->previewMs() - media->property("position").toDouble()) < 120);
            QVERIFY(QMetaObject::invokeMethod(page, "stop"));
            QTRY_VERIFY(gen->previewMs() < 50);
        } else {
            qWarning("generatorMedia position stuck at 0 under offscreen — skipping play/pause checks");
            QVERIFY(QMetaObject::invokeMethod(page, "stop"));
        }

        // Find tip sets a box and shows the other candidates as numbered chips.
        // The search that started with the video (D-159) ends first; the
        // user's own search shows Stop finding.
        QTRY_VERIFY_WITH_TIMEOUT(!gen->finding(), 30000);
        QMetaObject::invokeMethod(named(root.get(), "findTip"), "clicked");
        QTRY_VERIFY(gen->finding());
        QVERIFY(!gen->autoFinding());
        QCOMPARE(named(root.get(), "findTip")->property("text").toString(), QStringLiteral("Stop finding"));
        QVERIFY(!named(root.get(), "createButton")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!gen->finding(), 30000);
        QVERIFY2(gen->hasBox(), qPrintable(gen->status()));
        QTRY_VERIFY(named(root.get(), "candidate_1"));
        QVERIFY(named(root.get(), "candidate_1")->isVisible());
        // A queued clip can be taken out again with its ×.
        QCOMPARE(gen->addToQueue({video}), 1);
        QQuickItem* remove = nullptr;
        QTRY_VERIFY((remove = named(root.get(), "removeQueued_0")) && remove->isVisible());
        QMetaObject::invokeMethod(remove, "clicked");
        QTRY_COMPARE(gen->queue().size(), 0);
        // Clear all takes the tip box (and any contact areas) away (the drag below sets a new one).
        QTRY_VERIFY(named(root.get(), "clearBox")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "clearBox"), "clicked");
        QTRY_VERIFY(!gen->hasBox());

        // A drag over the frame becomes the tip box in video pixels. The seek
        // row and box label appear with the video and shrink the frame, so
        // wait until the layout has settled.
        auto* frame = named(root.get(), "frameView");
        QTRY_VERIFY(named(root.get(), "boxLabel")->isVisible());
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
        // Click the box to pick it, Delete removes it (0.7: select a mark,
        // delete it); Esc lets go of a pick.
        QObject* gp = root->property("page").value<QObject*>();
        const QPoint inside = ((a + b) / 2).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, {}, inside);
        QTRY_COMPARE(gp->property("selectedMark").toInt(), -1);
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_COMPARE(gp->property("selectedMark").toInt(), -2);
        QVERIFY(gen->hasBox());
        QTest::mouseClick(&window, Qt::LeftButton, {}, inside);
        QTRY_COMPARE(gp->property("selectedMark").toInt(), -1);
        QTest::keyClick(&window, Qt::Key_Delete);
        QTRY_VERIFY(!gen->hasBox());
        // Drawn again for what follows.
        QTest::mousePress(&window, Qt::LeftButton, {}, a.toPoint());
        QTest::mouseMove(&window, ((a + b) / 2).toPoint());
        QTest::mouseMove(&window, b.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, b.toPoint());
        QTRY_VERIFY(gen->hasBox());
        QVERIFY(named(root.get(), "createButton")->isEnabled());
        // The coach under the preview (0.7 roi_help.js) and the next step.
        QVERIFY(named(root.get(), "previewCoach")->property("text").toString().contains("Tip box = CSRT stroke tracker."));
        // 0.7 seek row while marking: Time (s) in 0.5 s steps, +1 s, +5 s.
        QVERIFY(named(root.get(), "seekRow")->isVisible());
        auto* timeField = named(root.get(), "timeField");
        timeField->setProperty("text", "2.3");
        QMetaObject::invokeMethod(timeField, "accepted");
        QTRY_VERIFY(qAbs(page->property("playheadMs").toDouble() - 2500) < 60);
        QMetaObject::invokeMethod(named(root.get(), "plus1s"), "clicked");
        QTRY_VERIFY(qAbs(page->property("playheadMs").toDouble() - 3500) < 60);
        QMetaObject::invokeMethod(named(root.get(), "plus5s"), "clicked");
        QTRY_VERIFY(page->property("playheadMs").toDouble() > 7000);  // at most the end (0:08)
        QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 0));
        QVERIFY(!named(root.get(), "createLede")->isVisible());

        // Feel: the style combo and a toggle reach the controller.
        // Recommended is the default and first (D-160); Autotune is fourth.
        auto* combo = named(root.get(), "styleCombo");
        QCOMPARE(gen->profile(), QStringLiteral("recommended"));
        QCOMPARE(combo->property("currentIndex").toInt(), 0);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 3));
        combo->setProperty("currentIndex", 3);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 3));
        QTRY_COMPARE(gen->profile(), QStringLiteral("autotune"));
        // G4a: Invert / Sliding / Axis live under Create → Advanced (closed).
        auto* advanced = named(root.get(), "advancedPanel");
        QVERIFY(advanced);
        QVERIFY(named(root.get(), "advancedToggle"));
        QVERIFY(!named(root.get(), "toggle_invert")->isVisible());
        // Open via the property: Create may be scrolled away, so a click is flaky offscreen.
        QVERIFY(advanced->setProperty("advancedOpen", true));
        QTRY_VERIFY(named(root.get(), "toggle_invert")->isVisible());
        QVERIFY(named(root.get(), "toggle_slidingDynamics")->isVisible());
        QVERIFY(named(root.get(), "motionAxisCombo")->isVisible());
        // G4b Expert is nested under Advanced and closed until opened.
        auto* expert = named(root.get(), "expertPanel");
        QVERIFY(expert);
        QVERIFY(!named(root.get(), "expertSmooth")->isVisible());
        QVERIFY(expert->setProperty("expertOpen", true));
        QTRY_VERIFY(named(root.get(), "expertSmooth")->isVisible());
        QVERIFY(named(root.get(), "expertMinPeak"));
        QVERIFY(named(root.get(), "expertProminence"));
        QVERIFY(named(root.get(), "expertRdp"));
        QVERIFY(named(root.get(), "expertMaxSpeed"));
        QVERIFY(named(root.get(), "expertAnalysisFps"));
        QVERIFY(named(root.get(), "toggle_impulseMirror")->isVisible());
        gen->setEditorShare(0.52);
        QCOMPARE(gen->editorShare(), 0.52);
        // Toggle via signal: Advanced lives in a tall Create column and the
        // invert row can sit below the window (mouseClick then lands outside).
        auto* invert = named(root.get(), "toggle_invert");
        QMetaObject::invokeMethod(invert, "toggled", Q_ARG(bool, true));
        QTRY_VERIFY(gen->invert());
        QVERIFY(advanced->setProperty("advancedOpen", false));
        QVERIFY(expert->setProperty("expertOpen", false));
        QTRY_VERIFY(!named(root.get(), "toggle_invert")->isVisible());

        // Everyday Feel: Style + For device + summary. Scene / Show all / contact behind More.
        QVERIFY(named(root.get(), "styleCombo")->isVisible());
        QVERIFY(named(root.get(), "feelDeviceCombo")->isVisible());
        QVERIFY(named(root.get(), "feelSummary")->isVisible());
        QVERIFY(!named(root.get(), "feelMore")->isVisible());
        QVERIFY(!named(root.get(), "neoFeel")->isVisible());
        QVERIFY(!named(root.get(), "toggle_showAllFeel")->isVisible());
        QVERIFY(!named(root.get(), "contactMarks")->isVisible());
        QVERIFY(named(root.get(), "feelMoreToggle")->property("text").toString().startsWith("More"));
        QMetaObject::invokeMethod(named(root.get(), "feelMoreToggle"), "clicked");
        QTRY_VERIFY(named(root.get(), "feelMore")->isVisible());
        QVERIFY(named(root.get(), "toggle_showAllFeel")->isVisible());
        QVERIFY(named(root.get(), "neoFeel")->isVisible());
        QVERIFY(!named(root.get(), "vibSourceRow")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "feelBalance"), "moved", Q_ARG(double, 42.0));
        QTRY_COMPARE(gen->feelBalance(), 40);
        QTRY_VERIFY(named(root.get(), "feelSummary")->property("text").toString().contains("suction 40%"));
        auto* feelDevice = named(root.get(), "feelDeviceCombo");
        feelDevice->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(feelDevice, "activated", Q_ARG(int, 2));
        QTRY_COMPARE(gen->feelKind(), QStringLiteral("vibrator"));
        QTRY_VERIFY(named(root.get(), "vibSourceRow")->isVisible());
        QVERIFY(!named(root.get(), "neoFeel")->isVisible());
        QVERIFY(named(root.get(), "feelHint")->isVisible());
        QVERIFY(named(root.get(), "toggle_autoTip")->isVisible());
        QVERIFY(named(root.get(), "feelMoreToggle")->property("text").toString().startsWith("Fewer"));
        // Fold again, then drawn areas open the options and take the toggle away (no dead button).
        QMetaObject::invokeMethod(named(root.get(), "feelMoreToggle"), "clicked");
        QTRY_VERIFY(!named(root.get(), "feelMore")->isVisible());
        QVERIFY(named(root.get(), "feelMoreToggle")->property("text").toString().startsWith("More"));
        QMetaObject::invokeMethod(named(root.get(), "feelMoreToggle"), "clicked");
        QTRY_VERIFY(named(root.get(), "feelMore")->isVisible());
        gen->setFeelDevice("stroker");
        QTRY_VERIFY(!named(root.get(), "contactMarks")->isVisible());
        gen->setShowAllFeel(true);
        QTRY_VERIFY(named(root.get(), "neoFeel")->isVisible());
        QVERIFY(named(root.get(), "contactMarks")->isVisible());
        gen->setShowAllFeel(false);
        gen->setFeelDevice("auto");
        gen->setFeelBalance(0);

        // Contact: on by default; "Mark contact area" makes the next drag a
        // contact area and leaves the tip box alone.
        QVERIFY(gen->contactVibration());
        QVERIFY(named(root.get(), "contactMarks")->isVisible());
        // Sensitivity and curve reach the controller.
        auto* curve = named(root.get(), "contactCurveCombo");
        QCOMPARE(curve->property("currentIndex").toInt(), 1);  // soft
        curve->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(curve, "activated", Q_ARG(int, 2));
        QTRY_COMPARE(gen->contactCurve(), QStringLiteral("peak"));
        QMetaObject::invokeMethod(named(root.get(), "contactSpan"), "moved", Q_ARG(double, 0.5));
        QTRY_COMPARE(gen->contactSpan(), 0.5);
        // 0.7 labels: low = earlier, high = deep strokes only.
        QTRY_COMPARE(named(root.get(), "contactSpanLabel")->property("text").toString(), QStringLiteral("earlier"));
        gen->setContactSpan(0.9);
        QTRY_COMPARE(named(root.get(), "contactSpanLabel")->property("text").toString(), QStringLiteral("deep only"));
        gen->setContactSpan(0.7);
        QTRY_COMPARE(named(root.get(), "contactSpanLabel")->property("text").toString(), QStringLiteral("0.70"));
        QMetaObject::invokeMethod(named(root.get(), "markContact"), "clicked");
        QTRY_VERIFY(gen->markContact());
        const QRect tip = gen->box();
        // The frame has moved and resized since it was measured above (cards open,
        // rows change); measure it again, settled, right before the drag.
        {
            QRectF last;
            int steady = 0;
            QTRY_VERIFY([&] {
                const QRectF now = frame->property("imageRect").toRectF();
                steady = (now == last && !now.isEmpty()) ? steady + 1 : 0;
                last = now;
                QTest::qWait(40);
                return steady >= 3;
            }());
            ir = last;
        }
        const double sc = ir.width() / 320.0;
        const QPointF c1 = frame->mapToScene(QPointF(ir.x() + 20 * sc, ir.y() + 20 * sc));
        const QPointF c2 = frame->mapToScene(QPointF(ir.x() + 80 * sc, ir.y() + 70 * sc));
        QTest::mousePress(&window, Qt::LeftButton, {}, c1.toPoint());
        QTest::mouseMove(&window, ((c1 + c2) / 2).toPoint());
        QTRY_VERIFY(named(root.get(), "contactDraft")->isVisible());
        QTest::mouseMove(&window, c2.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, c2.toPoint());
        QTRY_COMPARE(gen->contactAreas().size(), 1);
        QVERIFY(!gen->markContact());
        // Drawn areas keep the options open and take the toggle away (no dead button).
        QTRY_VERIFY(named(root.get(), "feelMore")->isVisible());
        QTRY_VERIFY(!named(root.get(), "feelMoreToggle")->isVisible());
        QCOMPARE(gen->box(), tip);
        const QVariantMap area = gen->contactAreas().first().toMap();
        QVERIFY(qAbs(area.value("x").toInt() - 20) <= 2);
        QVERIFY(qAbs(area.value("w").toInt() - 60) <= 3);
        QCOMPARE(area.value("cls").toString(), QStringLiteral("nipples"));
        QTRY_VERIFY(named(root.get(), "contactArea_0"));
        QVERIFY(named(root.get(), "contactArea_0")->isVisible());
        // 0.7 selects the mark just drawn: "Selected: …" with its class.
        QTRY_COMPARE(gp->property("selectedMark").toInt(), 0);
        QCOMPARE(named(root.get(), "boxLabel")->property("text").toString(), QStringLiteral("Selected: Contact (Nipples)"));
        QVERIFY(named(root.get(), "deleteSelectedMark")->isVisible());
        QVERIFY(gen->status().startsWith(QStringLiteral("Snack 2 done")));
        // + Another contact area: magenta, with the extra type.
        // Refresh imageRect after the selection row appears — offscreen CI
        // can reflow a few pixels (same flake class as the Shift-drag harden).
        QTRY_VERIFY(!frame->property("imageRect").toRectF().isEmpty());
        ir = frame->property("imageRect").toRectF();
        const double sExtra = ir.width() / 320.0;
        auto* extraType = named(root.get(), "extraClassCombo");
        extraType->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(extraType, "activated", Q_ARG(int, 1));
        QTRY_COMPARE(gen->extraClass(), QStringLiteral("mouth"));
        QMetaObject::invokeMethod(named(root.get(), "addExtra"), "clicked");
        QTRY_VERIFY(gen->markExtra());
        const QPointF e1 = frame->mapToScene(QPointF(ir.x() + 20 * sExtra, ir.y() + 150 * sExtra));
        const QPointF e2 = frame->mapToScene(QPointF(ir.x() + 70 * sExtra, ir.y() + 200 * sExtra));
        QTest::mousePress(&window, Qt::LeftButton, {}, e1.toPoint());
        QTest::mouseMove(&window, ((e1 + e2) / 2).toPoint());
        QTRY_VERIFY(named(root.get(), "contactDraft")->isVisible());
        QTest::mouseMove(&window, e2.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, e2.toPoint());
        QTRY_COMPARE(gen->contactAreas().size(), 2);
        QCOMPARE(gen->contactAreas().at(1).toMap().value("cls").toString(), QStringLiteral("mouth"));
        QCOMPARE(gen->status(), QStringLiteral("Extra contact #1 (mouth) added, stay fixed."));
        QTRY_COMPARE(gp->property("selectedMark").toInt(), 1);
        QCOMPARE(named(root.get(), "boxLabel")->property("text").toString(), QStringLiteral("Selected: Extra contact #1 (Mouth)"));
        QVERIFY(frame->property("imageRect").toRectF().width() > 100);
        // Its class from the row: (any).
        auto* picked = named(root.get(), "selectedClassCombo");
        picked->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(picked, "activated", Q_ARG(int, 0));
        QTRY_COMPARE(gen->contactAreas().at(1).toMap().value("cls").toString(), QString());
        QVERIFY(named(root.get(), "clearExtras")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "deleteSelectedMark"), "clicked");
        QTRY_COMPARE(gen->contactAreas().size(), 1);
        QCOMPARE(gen->status(), QStringLiteral("Extra contact removed."));
        QCOMPARE(gp->property("selectedMark").toInt(), -2);
        // Shift-drag draws the primary again (0.7 Shift = partner); the tip stays.
        // Windows CI (offscreen): (1) mouse modifiers alone often miss QML —
        // hold Shift with keyPress; (2) imageRect can keep twitching after the
        // selection row closes, so do not wait for two identical frames;
        // (3) under ctest the first Shift-drag can be a no-op — retry once.
        QTRY_VERIFY(!frame->property("imageRect").toRectF().isEmpty());
        QTest::qWait(200);
        ir = frame->property("imageRect").toRectF();
        QVERIFY(ir.width() > 100);
        const double sShift = ir.width() / 320.0;
        auto shiftDragPrimary = [&] {
            const QPointF a = frame->mapToScene(QPointF(ir.x() + 20 * sShift, ir.y() + 150 * sShift));
            const QPointF b = frame->mapToScene(QPointF(ir.x() + 100 * sShift, ir.y() + 210 * sShift));
            QTest::keyPress(&window, Qt::Key_Shift);
            QCoreApplication::processEvents();
            QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, a.toPoint());
            // QWindow mouseMove is (window, pos, delay) — no button arg (MSVC/Qt 6.7).
            QTest::mouseMove(&window, ((a + b) / 2).toPoint(), 40);
            QTest::mouseMove(&window, b.toPoint(), 40);
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, b.toPoint());
            QTest::keyRelease(&window, Qt::Key_Shift);
            QCoreApplication::processEvents();
        };
        shiftDragPrimary();
        if (qAbs(gen->contactAreas().first().toMap().value("y").toInt() - 150) > 2) {
            QTest::qWait(100);
            shiftDragPrimary();
        }
        QTRY_VERIFY2(qAbs(gen->contactAreas().first().toMap().value("y").toInt() - 150) <= 2,
                     qPrintable(QString("contact y=%1").arg(gen->contactAreas().first().toMap().value("y").toInt())));
        QCOMPARE(gen->contactAreas().size(), 1);
        QCOMPARE(gen->box(), tip);
        // Shift-click on a candidate makes it the contact area (0.7), not the tip.
        auto* chip1 = named(root.get(), "candidate_0");
        QVERIFY(chip1);
        const QVariantMap cand0 = gen->tipCandidates().first().toMap();
        QTest::keyPress(&window, Qt::Key_Shift);
        QCoreApplication::processEvents();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ShiftModifier,
                          chip1->mapToScene(QPointF(chip1->width() / 2, chip1->height() / 2)).toPoint());
        QTest::keyRelease(&window, Qt::Key_Shift);
        QTRY_COMPARE(gen->contactAreas().first().toMap().value("x").toInt(), cand0.value("x").toInt());
        QCOMPARE(gen->box(), tip);
        QTRY_COMPARE(gp->property("selectedMark").toInt(), 0);
        // Esc: "Mark deselected."
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_COMPARE(gp->property("selectedMark").toInt(), -2);
        QCOMPARE(gen->status(), QStringLiteral("Mark deselected."));
        gen->setExtraClass("");
        // Off hides the classes and the areas; on brings them back.
        gen->setContactVibration(false);
        QTRY_VERIFY(!named(root.get(), "contactMarks")->isVisible());
        QTRY_VERIFY(!named(root.get(), "contactArea_0"));
        gen->setContactVibration(true);
        QTRY_VERIFY(named(root.get(), "contactArea_0"));

        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT"); !shot.isEmpty()) window.grabWindow().save(QString::fromLocal8Bit(shot));

        // Curve-edit undo is the check here; Off keeps Create from adding an Optimize step.
        gen->setOptimizeTarget(QStringLiteral("off"));
        QMetaObject::invokeMethod(named(root.get(), "createButton"), "clicked");
        QTRY_VERIFY(gen->running());
        QVERIFY(named(root.get(), "cancelButton")->isVisible());
        // D-144: the result opens here, in the Editor, not in the Player.
        auto* edit = root->property("page").value<QObject*>()->property("editor").value<QObject*>();
        QVERIFY(edit);
        QTRY_VERIFY_WITH_TIMEOUT(edit->property("hasScript").toBool(), 60000);
        QVERIFY(root->property("reviewed").toString().isEmpty());
        QCOMPARE(named(root.get(), "progressText")->property("text").toString(), QStringLiteral("100 %"));
        // 0.7's quality box with the post-generate review.
        QTRY_VERIFY(named(root.get(), "qualityBox")->isVisible());
        QVERIFY(named(root.get(), "qualityLine")->property("text").toString().startsWith("Signal Quality (Quality Doctor): "));
        QVERIFY(named(root.get(), "reviewLine")->property("text").toString().startsWith("Direction: "));
        QVERIFY(named(root.get(), "stepPrompt")->property("text").toString().startsWith("Step 5: Review"));
        QTRY_VERIFY(named(root.get(), "reviewCard")->isVisible());
        QVERIFY(named(root.get(), "optimizeAfterCreate")->isVisible());
        QVERIFY(named(root.get(), "postAdaptOptimize")->isVisible());
        QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
        QVERIFY(!named(root.get(), "toggle_invert")->isVisible());
        QVERIFY(!named(root.get(), "tool_optimize"));
        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_DONE"); !shot.isEmpty()) window.grabWindow().save(QString::fromLocal8Bit(shot));
        // After Create the seek row gives way to the editor's timeline; the
        // 0–100 gauge shows the stroke at the playhead, its chip switches it.
        QVERIFY(!named(root.get(), "seekRow")->isVisible());
        QTRY_VERIFY(named(root.get(), "generatorGauge")->isVisible());
        const int g = named(root.get(), "generatorGauge")->property("value").toInt();
        QVERIFY2(g >= 0 && g <= 100, qPrintable(QString::number(g)));
        auto* chip = named(root.get(), "posOverlayToggle");
        QVERIFY(chip->isVisible());
        QTest::mouseClick(&window, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        QTRY_VERIFY(!named(root.get(), "generatorGauge")->isVisible());
        QVERIFY(!GeneratorController().posOverlay());  // remembered
        gen->setPosOverlay(true);
        // The frame strip under the curves loads its frames off the UI thread.
        QVERIFY(named(root.get(), "frameStrip")->isVisible());
        QTRY_VERIFY(named(root.get(), "frameThumb"));
        QTRY_COMPARE(named(root.get(), "frameThumb")->property("status").toInt(), 1);  // Image.Ready
        QTRY_VERIFY(named(root.get(), "lane_stroke"));
        auto* lane = named(root.get(), "lane_stroke");
        QTRY_VERIFY(lane->isVisible() && lane->height() > 40);
        auto at = [&](QQuickItem* item, double fx, double fy) {
            return item->mapToScene(QPointF(item->width() * fx, item->height() * fy)).toPoint();
        };

        // A click on the curve sets a point there and saves; Undo takes it back.
        QVERIFY(!edit->property("canUndo").toBool());
        QTest::mousePress(&window, Qt::LeftButton, {}, at(lane, 0.37, 0.05));
        QTest::mouseMove(&window, at(lane, 0.38, 0.1));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, at(lane, 0.38, 0.1));
        QTRY_VERIFY(edit->property("canUndo").toBool());
        auto* undo = named(root.get(), "undoButton");
        QTest::mouseClick(&window, Qt::LeftButton, {}, at(undo, 0.5, 0.5));
        QTRY_VERIFY(!edit->property("canUndo").toBool());
        QVERIFY(edit->property("canRedo").toBool());

        // Drag on the ruler marks a range; a range tool works on it.
        auto* ruler = named(root.get(), "ruler");
        QTest::mousePress(&window, Qt::LeftButton, {}, at(ruler, 0.2, 0.5));
        QTest::mouseMove(&window, at(ruler, 0.4, 0.5));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, at(ruler, 0.6, 0.5));
        QTRY_VERIFY(edit->property("hasMarker").toBool());
        auto* invertRange = named(root.get(), "tool_invert");
        QCOMPARE(invertRange->property("tip").toString(), QStringLiteral("Invert range"));
        QMetaObject::invokeMethod(invertRange, "clicked");
        QTRY_VERIFY(edit->property("canUndo").toBool());
        QCOMPARE(edit->property("undoText").toString(), QStringLiteral("Undo: Invert range"));
        // Without a marker the whole curve flips only after 0.7's question.
        QMetaObject::invokeMethod(edit, "clearMarker");
        QTRY_COMPARE(invertRange->property("tip").toString(), QStringLiteral("Invert all"));
        auto* ask = root->findChild<QObject*>("invertAllDialog");
        QVERIFY(ask);
        QMetaObject::invokeMethod(invertRange, "clicked");  // opens the question (no native dialog offscreen)
        QCOMPARE(ask->property("text").toString(), QStringLiteral("Invert the whole Stroke curve? (Undo can bring it back.)"));
        QCOMPARE(edit->property("undoText").toString(), QStringLiteral("Undo: Invert range"));  // nothing yet
        QMetaObject::invokeMethod(ask, "accepted");
        QTRY_COMPARE(edit->property("undoText").toString(), QStringLiteral("Undo: Invert curve"));

        // Align bar: axes / From sound / beat bar (Everyday Optimize is on Review).
        QVERIFY(named(root.get(), "alignBar")->isVisible());
        QVERIFY(named(root.get(), "tool_axes")->isVisible());
        QVERIFY(named(root.get(), "tool_sound")->isVisible());
        QVERIFY(named(root.get(), "softEdges")->isVisible());
        QVERIFY(named(root.get(), "capIntensity"));
        QCOMPARE(page->property("capIntensity").toInt(), 400);
        page->setProperty("capIntensity", 300);
        QCOMPARE(page->property("capIntensity").toInt(), 300);
        // Axes from stroke without opening Tools…; "+ … lane" stays in the menu.
        QMetaObject::invokeMethod(named(root.get(), "tool_axes"), "clicked");
        QTRY_VERIFY(named(root.get(), "lane_vibration"));
        QTRY_VERIFY(named(root.get(), "lane_suction"));
        auto* menu = root->findChild<QObject*>("toolsMenu");
        QVERIFY(menu);
        QMetaObject::invokeMethod(named(root.get(), "toolsButton"), "clicked");
        QTRY_VERIFY(menu->property("opened").toBool());
        QTRY_VERIFY(!named(root.get(), "tool_addVibration")->isVisible());  // lanes already present
        QVERIFY(!named(root.get(), "tool_addSuction")->isVisible());
        QMetaObject::invokeMethod(menu, "close");
        QTRY_VERIFY(!menu->property("visible").toBool());
        // Still can open Tools… for +lane when a lane is missing: undo axes, then check.
        QMetaObject::invokeMethod(edit, "undo");
        QTRY_VERIFY(!named(root.get(), "lane_vibration"));
        QMetaObject::invokeMethod(named(root.get(), "toolsButton"), "clicked");
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(named(root.get(), "tool_addVibration")->isVisible());
        QVERIFY(named(root.get(), "tool_addSuction")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "tool_addVibration"), "clicked");
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTRY_VERIFY(named(root.get(), "lane_vibration"));
        // Restore both feel lanes for the height / ghost checks below.
        QMetaObject::invokeMethod(named(root.get(), "tool_axes"), "clicked");
        QTRY_VERIFY(named(root.get(), "lane_suction"));
        // 0.7 lane height: each feel lane keeps at least 34 px at this window size.
        QTRY_VERIFY(named(root.get(), "lane_vibration")->height() >= 34);
        QVERIFY(named(root.get(), "lane_suction")->height() >= 34);
        // 0.7 stroke ghost: the feel lanes carry the stroke for timing.
        QCOMPARE(named(root.get(), "lane_stroke")->property("ghostPoints").toInt(), 0);
        QTRY_VERIFY(named(root.get(), "lane_vibration")->property("ghostPoints").toInt() >= 2);
        QCOMPARE(named(root.get(), "lane_suction")->property("ghostPoints").toInt(),
                 named(root.get(), "lane_vibration")->property("ghostPoints").toInt());
        QCOMPARE(edit->property("undoText").toString(), QStringLiteral("Undo: vibration and suction from the stroke"));

        // Scenes (D-145): the side tab switches the right panel; "+ Scene here"
        // starts a scene at the shown frame, and it appears as a band.
        QVERIFY(!named(root.get(), "scenesCard")->isVisible());
        auto* tabs = named(root.get(), "sideTabs");
        QTest::mouseClick(&window, Qt::LeftButton, {}, tabs->mapToScene(QPointF(tabs->width() - 20, tabs->height() / 2)).toPoint());
        QTRY_VERIFY(named(root.get(), "scenesCard")->isVisible());
        QVERIFY(!named(root.get(), "createButton")->isVisible());
        gen->seekPreview(0);
        QMetaObject::invokeMethod(named(root.get(), "addScene"), "clicked");
        gen->seekPreview(4000);
        QMetaObject::invokeMethod(named(root.get(), "addScene"), "clicked");
        QTRY_COMPARE(edit->property("scenes").toList().size(), 2);
        QTRY_VERIFY(named(root.get(), "sceneBand_1"));
        QVERIFY(named(root.get(), "sceneBand_1")->isVisible());
        QTRY_VERIFY(named(root.get(), "sceneCard_1"));
        // A click on a band selects that scene and shows its start.
        auto* band0 = named(root.get(), "sceneBand_0");
        QTRY_VERIFY(band0->height() > 10 && band0->width() > 20);  // the strip has just been laid out
        QTest::mouseClick(&window, Qt::LeftButton, {}, band0->mapToScene(QPointF(band0->width() / 2, band0->height() / 2)).toPoint());
        QTRY_COMPARE(page->property("selectedScene").toInt(), 0);
        QTRY_VERIFY(gen->previewMs() < 100);
        // The feel combo: index 2 is Oral after "Script default" and Classic.
        auto* feel = named(root.get(), "sceneFeel_1");
        feel->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(feel, "activated", Q_ARG(int, 2));
        QTRY_COMPARE(edit->property("scenes").toList().at(1).toMap().value("feel").toString(), QStringLiteral("oral"));
        // The rebuilt card shows the saved feel.
        QTRY_COMPARE(named(root.get(), "sceneFeel_1")->property("currentIndex").toInt(), 2);
        QCOMPARE(named(root.get(), "sceneFeel_0")->property("currentIndex").toInt(), 0);
        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_SCENES"); !shot.isEmpty()) window.grabWindow().save(QString::fromLocal8Bit(shot));
        QTest::mouseClick(&window, Qt::LeftButton, {}, tabs->mapToScene(QPointF(20, tabs->height() / 2)).toPoint());
        QTRY_VERIFY(named(root.get(), "createButton")->isVisible());

        // "Play in the Player" hands the edited script over.
        QVERIFY(named(root.get(), "reviewButton")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "reviewButton"), "clicked");
        QCOMPARE(root->property("reviewed").toString(), dir_.filePath("clip.samn"));
        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_EDIT"); !shot.isEmpty()) window.grabWindow().save(QString::fromLocal8Bit(shot));
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    // 0.7 "always create": the Player hands a video over; the page finds
    // the tip and starts as soon as Create can.
    void createForFindsTipAndRuns() {
        QTemporaryDir out;
        const QString video = out.filePath("auto.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(video.toStdU16String()), 6, 80.0, 1.0));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/Wrapper2.qml"));
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
        QVariant ok;
        QVERIFY(QMetaObject::invokeMethod(page, "createFor", Q_RETURN_ARG(QVariant, ok),
                                          Q_ARG(QVariant, QUrl::fromLocalFile(video)), Q_ARG(QVariant, true)));
        QVERIFY(ok.toBool());
        QVERIFY(gen->hasVideo());
        // The tip search, then the run, without a click.
        QTRY_VERIFY_WITH_TIMEOUT(gen->hasBox() && !gen->finding(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out.filePath("auto.funscript")) && !gen->running(), 60000);
        // 0.7's done line and review (quality, direction, O-zone).
        QVERIFY2(gen->status().startsWith("Done — Emotion Script ready in Play."), qPrintable(gen->status()));
        const QVariantMap review = gen->review();
        QVERIFY(review.contains("quality"));
        QVERIFY(review.value("quality").toInt() >= 0 && review.value("quality").toInt() <= 100);
        QVERIFY(!review.value("direction").toString().isEmpty());
        QVERIFY(review.value("ozone").toString().startsWith("O-zone"));
        // Without `start` it only opens the video.
        QVERIFY(QMetaObject::invokeMethod(page, "createFor", Q_RETURN_ARG(QVariant, ok),
                                          Q_ARG(QVariant, QUrl::fromLocalFile(video)), Q_ARG(QVariant, false)));
        QVERIFY(gen->review().isEmpty());  // a new video, no result yet
        // It looks for the tip by itself (D-159) but does not create.
        QTRY_VERIFY_WITH_TIMEOUT(!gen->finding(), 30000);
        QTest::qWait(200);
        QVERIFY(!gen->running());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }


    // Everyday Create: Advanced closed, Review only when a script exists,
    // one Optimize combo on Review, Feel extras behind More.
    void everydayCreateSurfaceStructure() {
        g_messages.clear();
        QTemporaryDir scripts;
        QVERIFY(scripts.isValid());
        const QString script = scripts.filePath(QStringLiteral("clip.funscript"));
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}]})");
        }
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/EverydaySurface.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1400, 820);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* edit = page->property("editor").value<QObject*>();
        QVERIFY(page && edit);
        QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
        QVERIFY(!named(root.get(), "toggle_invert")->isVisible());
        QVERIFY(named(root.get(), "createStepPrompt")->isVisible());
        QVERIFY(!named(root.get(), "reviewCard")->isVisible());
        QVERIFY(!named(root.get(), "feelMore")->isVisible());
        QVERIFY(named(root.get(), "styleCombo")->isVisible());
        QVERIFY(named(root.get(), "feelDeviceCombo")->isVisible());
        QVERIFY(named(root.get(), "feelSummary")->isVisible());
        QVERIFY(!named(root.get(), "tool_optimize"));
        bool opened = false;
        QVERIFY(QMetaObject::invokeMethod(edit, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, script)));
        QVERIFY(opened);
        QTRY_VERIFY(named(root.get(), "reviewCard")->isVisible());
        QVERIFY(named(root.get(), "improvePanel")->isVisible());
        QVERIFY(named(root.get(), "optimizeAfterCreate")->isVisible());
        QVERIFY(named(root.get(), "postAdaptOptimize")->isVisible());
        QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
        QVERIFY(!named(root.get(), "toggle_invert")->isVisible());
        QVERIFY(!named(root.get(), "tool_optimize"));
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    // Review holds the one Everyday Optimize combo. Unset follows feelKind
    // (nothing connected → Sam Neo 2). Off is explicit "off".
    void optimizeTargetCombosSaveTheChoice() {
        g_messages.clear();
        QTemporaryDir scripts;
        QVERIFY(scripts.isValid());
        const QString script = scripts.filePath(QStringLiteral("clip.funscript"));
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}]})");
        }
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/OptimizeTarget.qml"));
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
        auto* edit = page->property("editor").value<QObject*>();
        QVERIFY(page && gen && edit);
        gen->setOptimizeTarget(QString());
        QCOMPARE(gen->optimizeTarget(), QString());
        bool opened = false;
        QVERIFY(QMetaObject::invokeMethod(edit, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, script)));
        QVERIFY(opened);
        QTRY_VERIFY(named(root.get(), "alignBar")->isVisible());
        QTRY_VERIFY(named(root.get(), "reviewCard")->isVisible());
        QTRY_VERIFY(named(root.get(), "postCreateAdapt")->isVisible());
        QVERIFY(!named(root.get(), "tool_optimize"));
        QVERIFY(!named(root.get(), "optimizeTarget"));
        auto* combo = named(root.get(), "optimizeAfterCreate");
        QVERIFY(combo);
        // Unset + auto feel (Sam Neo 2) → combo shows Sam Neo 2.
        QCOMPARE(combo->property("currentIndex").toInt(), 2);

        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
        QCOMPARE(gen->optimizeTarget(), QStringLiteral("off"));
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1)));
        QCOMPARE(gen->optimizeTarget(), QStringLiteral("general"));
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 1);

        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 2)));
        QCOMPARE(gen->optimizeTarget(), QStringLiteral("neo2"));
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 2);
        gen->setOptimizeTarget(QString());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    void suggestTipAiDisabledWithoutModelAndApplyCommits() {
        g_messages.clear();
        const QString video = dir_.filePath("ai_tip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(video.toStdU16String()), 8, 80.0, 1.0));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/AiTip.qml"));
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
        QVERIFY(page && gen);
        gen->setAutoTip(false);
        QVERIFY(gen->openVideo(video, false));
        QTRY_VERIFY(named(root.get(), "suggestAiTip"));
        QVERIFY(!named(root.get(), "suggestAiTip")->isEnabled());
        QVERIFY(named(root.get(), "aiTipHint")->isVisible());
        QVERIFY(!named(root.get(), "applyAiTip")->isVisible());
        QVERIFY(!gen->hasBox());

        gen->setFakeAiHitsForTest({QVariantMap{{"x", 40},
                                               {"y", 50},
                                               {"w", 48},
                                               {"h", 36},
                                               {"score", 0.88},
                                               {"label", "penis"},
                                               {"cls", 14}}});
        QTRY_VERIFY(named(root.get(), "suggestAiTip")->isEnabled());
        QMetaObject::invokeMethod(named(root.get(), "suggestAiTip"), "clicked");
        QTRY_VERIFY(gen->hasAiTipProposal());
        QVERIFY(!gen->hasBox());
        QTRY_VERIFY(named(root.get(), "applyAiTip")->isVisible());
        QTRY_VERIFY(named(root.get(), "aiTipProposal")->isVisible());
        QTRY_VERIFY(named(root.get(), "aiDetection_0"));
        QMetaObject::invokeMethod(named(root.get(), "applyAiTip"), "clicked");
        QTRY_COMPARE(gen->box(), QRect(40, 50, 48, 36));
        QTRY_VERIFY(!named(root.get(), "applyAiTip")->isVisible());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    // Review-card Improve panel: Start/End, Fill and Heal defaults, button
    // runs EditorController::improveScript on the open script.
    void improvePanelShowsAndRuns() {
        g_messages.clear();
        QTemporaryDir scripts;
        QVERIFY(scripts.isValid());
        const QString script = scripts.filePath(QStringLiteral("clip.funscript"));
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":10},{"at":2000,"pos":90}]})");
        }
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/ImprovePanel.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1400, 820);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* edit = page->property("editor").value<QObject*>();
        QVERIFY(page && edit);
        bool opened = false;
        QVERIFY(QMetaObject::invokeMethod(edit, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, script)));
        QVERIFY(opened);
        QTRY_VERIFY(named(root.get(), "reviewCard")->isVisible());
        QTRY_VERIFY(named(root.get(), "improvePanel")->isVisible());
        QVERIFY(named(root.get(), "improveStart"));
        QVERIFY(named(root.get(), "improveEnd"));
        QCOMPARE(named(root.get(), "improveFill")->property("checked").toBool(), true);
        QCOMPARE(named(root.get(), "improveHeal")->property("checked").toBool(), true);
        QVERIFY(named(root.get(), "improveButton")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "improveButton"), "clicked");
        const QString status = edit->property("status").toString();
        QVERIFY2(status.startsWith("Improved:"), qPrintable(status));
        QTRY_VERIFY(named(root.get(), "improveStatus")->isVisible());
        QVERIFY2(named(root.get(), "improveStatus")->property("text").toString().startsWith("Improved:"),
                 qPrintable(named(root.get(), "improveStatus")->property("text").toString()));
        const QString other = scripts.filePath(QStringLiteral("other.funscript"));
        {
            QFile f(other);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100},{"at":2000,"pos":0}]})");
        }
        opened = false;
        QVERIFY(QMetaObject::invokeMethod(edit, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, other)));
        QVERIFY(opened);
        QTRY_VERIFY(!named(root.get(), "improveStatus")->isVisible());
        QCOMPARE(named(root.get(), "improvePanel")->property("lastStatus").toString(), QString());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }
    // The transport in a narrow card with wide left controls (what Windows fonts
    // make of the time and speed): the slider, then the 10 s buttons, give way
    // and nothing overlaps; wide, everything is there. Without allowTight (the
    // Player) nothing is hidden.
    void transportClosesUpBeforeItOverlaps() {
        g_messages.clear();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                property alias tight: t
                property alias loose: l
                property real w: 900
                width: w; height: 300
                VideoTransport { id: t; objectName: "tightTransport"; compact: true; allowTight: true; width: parent.w; y: 0
                    leftItems: [Item { width: 220; height: 24 }] }
                VideoTransport { id: l; objectName: "looseTransport"; compact: true; width: parent.w; y: 120
                    leftItems: [Item { width: 220; height: 24 }] }
            })",
                     QUrl("qrc:/test/Transport.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.resize(900, 300);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* t = named(root.get(), "tightTransport");
        auto* l = named(root.get(), "looseTransport");
        // Wide: all there, not crowded.
        QTRY_VERIFY(!t->property("crowded").toBool());
        QVERIFY(!t->property("tight").toBool());
        QVERIFY(!t->property("veryTight").toBool());
        // Narrower: the slider goes first, then the 10 s buttons; never crowded.
        for (int w : {700, 620, 560, 520, 480, 460, 440}) {
            root->setProperty("w", w);
            QTest::qWait(80);
            QTRY_VERIFY2(!t->property("crowded").toBool(), qPrintable(QString("crowded at %1").arg(w)));
        }
        QVERIFY(t->property("veryTight").toBool());
        QVERIFY(!named(t, "back10Button")->isVisible());
        // The same width without allowTight (the Player) hides nothing.
        QVERIFY(!l->property("tight").toBool());
        QVERIFY(named(l, "back10Button")->isVisible());
        QVERIFY(named(l, "volume")->isVisible());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    void tipBoxFollowsPlayheadWhenPathExists() {
        g_messages.clear();
        const QString video = dir_.filePath("live_tip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(video.toStdU16String()), 4, 80.0, 1.0));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Rectangle {
                width: 1400; height: 820; color: "#021024"
                property alias page: gp
                GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
            })",
                     QUrl("qrc:/test/LiveTip.qml"));
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
        QVERIFY(page && gen);
        gen->setAutoTip(false);
        QVERIFY(gen->openVideo(video, false));
        QTRY_VERIFY(gen->hasVideo());
        gen->setBox(QRect(100, 40, 120, 160));
        QTRY_VERIFY(named(root.get(), "tipBox")->isVisible());
        gen->setTipPathForTest({QVariantMap{{"at", 0}, {"x", 40.0}, {"y", 80.0}},
                                QVariantMap{{"at", 2000}, {"x", 200.0}, {"y", 160.0}}});
        QTRY_VERIFY(gen->hasTipPath());
        auto* frame = named(root.get(), "frameView");
        QRectF ir;
        QTRY_VERIFY([&] {
            const QRectF now = frame->property("imageRect").toRectF();
            const bool same = now == ir && !now.isEmpty();
            ir = now;
            return same;
        }());
        auto* tip = named(root.get(), "tipBox");
        QVERIFY(QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(qAbs(page->property("playheadMs").toDouble()) < 80);
        const qreal x0 = tip->x();
        const qreal y0 = tip->y();
        const qreal w0 = tip->width();
        const qreal h0 = tip->height();
        QVERIFY(QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 2000)));
        QTRY_VERIFY(qAbs(page->property("playheadMs").toDouble() - 2000) < 80);
        QVERIFY2(qAbs(tip->x() - x0) > 20, qPrintable(QString("x %1 vs %2").arg(x0).arg(tip->x())));
        QVERIFY2(qAbs(tip->y() - y0) > 10, qPrintable(QString("y %1 vs %2").arg(y0).arg(tip->y())));
        QCOMPARE(tip->width(), w0);  // seed size kept
        QCOMPARE(tip->height(), h0);
        QVERIFY(named(root.get(), "previewCoach")->property("text").toString().contains("follows the tip"));
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }

    // The Create side (Video, Feel, Create) is one scroll area: on a short
    // window every control can be brought into view.
    void createSideScrollsOnAShortWindow() {
        g_messages.clear();
        const QString video = dir_.filePath("scroll.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(video.toStdU16String()), 4, 80.0, 1.0));
        const QString script = dir_.filePath("scroll.funscript");
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"actions":[{"at":0,"pos":10},{"at":2000,"pos":90},{"at":4000,"pos":20}]})");
        }
        for (const QSize size : {QSize(960, 600), QSize(1180, 700), QSize(1400, 820)}) {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
            engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
            QQmlComponent comp(&engine);
            comp.setData(R"(
                import QtQuick
                import SamN
                Rectangle {
                    color: "#021024"
                    property alias page: gp
                    GeneratorPage { id: gp; anchors.fill: parent; anchors.margins: 20 }
                })",
                         QUrl("qrc:/test/CreateScroll.qml"));
            QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
            QQuickWindow window;
            window.resize(size);
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
            QVERIFY(root);
            root->setParentItem(window.contentItem());
            root->setSize(size);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* page = root->property("page").value<QQuickItem*>();
            auto* gen = page->property("controller").value<GeneratorController*>();
            QVERIFY(gen);
            gen->openVideo(video);
            QTRY_VERIFY(gen->hasVideo());
            gen->setContactVibration(true);
            QTest::qWait(300);
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_SCROLL"); !shot.isEmpty())
                window.grabWindow().save(QString::fromLocal8Bit(shot + "_" + QByteArray::number(size.width()) + "_default.png"));
            page->setProperty("feelMore", true);
            auto* scroll = named(root.get(), "createScroll");
            QVERIFY(scroll);
            QTRY_VERIFY(scroll->property("contentHeight").toReal() > 0);
            // The opened feel options need a moment to lay out.
            QTRY_VERIFY(named(root.get(), "feelMore")->isVisible());
            QTest::qWait(300);
            const qreal view = scroll->height();
            const qreal content = scroll->property("contentHeight").toReal();
            QVERIFY2(view > 200, qPrintable(QString("view %1").arg(view)));
            // Whether an item lies fully inside the scroll area's viewport.
            auto inView = [&](const char* name) {
                auto* item = named(root.get(), name);
                if (!item || !item->isVisible()) return false;
                const qreal top = item->mapToItem(scroll, QPointF(0, 0)).y();
                return top >= -0.5 && top + item->height() <= scroll->height() + 0.5;
            };
            auto* edit = page->property("editor").value<QObject*>();
            bool opened = false;
            QVERIFY(QMetaObject::invokeMethod(edit, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, script)));
            QVERIFY(opened);
            QTRY_VERIFY(named(root.get(), "alignBar")->isVisible());
            // The stage never paints over the side panels: its cards and their
            // rows stay inside the stage column, on the narrowest window too.
            auto* stage = named(root.get(), "stageSplit");
            QTRY_VERIFY(stage->width() > 300);
            QTRY_COMPARE(named(root.get(), "projectPanel")->width(), size.width() < 1220 ? 44.0 : 248.0);
            for (const char* name : {"editorCard", "generatorTransport", "tipRow", "editorToolbar", "alignBar", "rangeBar", "ruler"}) {
                auto* item = named(root.get(), name);
                QVERIFY2(item, name);
                // The layout settles a moment after the video and the script open.
                auto right = [&] { return item->mapToItem(stage, QPointF(item->width(), 0)).x(); };
                // On a failure the message names the chain of parents and their widths,
                // and the widest children of the first parent that is too wide.
                auto chain = [&] {
                    QString out;
                    for (QQuickItem* p = item; p && p != stage->parentItem(); p = p->parentItem())
                        out += QString(" <%1 %2 w=%3 iw=%4 x=%5>").arg(p->objectName(), QString::fromLatin1(p->metaObject()->className()))
                                   .arg(p->width()).arg(p->implicitWidth()).arg(p->x());
                    return out;
                };
                // The layout hints (minimum / preferred width, fill) of the children of
                // the first QQuickColumnLayout above the item: a child whose minimum is
                // wider than the column makes the whole column overflow.
                auto hints = [&] {
                    QString out;
                    QQuickItem* col = nullptr;
                    for (QQuickItem* p = item; p && !col; p = p->parentItem())
                        if (QByteArray(p->metaObject()->className()).contains("ColumnLayout")) col = p;
                    if (!col) return out;
                    std::function<void(QQuickItem*, int)> dump = [&](QQuickItem* c, int depth) {
                        QObject* att = nullptr;
                        for (QObject* o : c->children())
                            if (QByteArray(o->metaObject()->className()) == "QQuickLayoutAttached") att = o;
                        out += QString("\n%1  [%2 %3 vis=%4 w=%5 iw=%6 min=%7 pref=%8 fill=%9]")
                                   .arg(QString(depth * 4, ' '), c->objectName(), QString::fromLatin1(c->metaObject()->className()))
                                   .arg(c->isVisible()).arg(c->width()).arg(c->implicitWidth())
                                   .arg(att ? att->property("minimumWidth").toString() : QStringLiteral("-"))
                                   .arg(att ? att->property("preferredWidth").toString() : QStringLiteral("-"))
                                   .arg(att ? att->property("fillWidth").toString() : QStringLiteral("-"));
                        if (QByteArray(c->metaObject()->className()).contains("RowLayout") && depth == 0)
                            for (QQuickItem* d : c->childItems()) dump(d, depth + 1);
                    };
                    for (QQuickItem* c : col->childItems()) dump(c, 0);
                    return out;
                };
                QTRY_VERIFY2(right() <= stage->width() + 0.5,
                             qPrintable(QString("%1 ends at %2 of %3:%4%5").arg(name).arg(right()).arg(stage->width()).arg(chain(), hints())));
            }
            // A toolbar wider than its place scrolls to its last control.
            auto* bar = named(root.get(), "rangeBar");
            QVERIFY(bar);
            if (bar->property("overflows").toBool()) {
                bar->setProperty("contentX", bar->property("contentWidth").toReal() - bar->width());
                auto* last = named(root.get(), "markerText");
                QVERIFY(last);
                QTRY_VERIFY(last->mapToItem(bar, QPointF(last->width(), 0)).x() <= bar->width() + 0.5);
                bar->setProperty("contentX", 0);
            }
            QCOMPARE(named(root.get(), "projectPanel")->property("collapsed").toBool(), size.width() < 1220);
            // The transport's controls do not overlap on any of the sizes.
            QVERIFY(!named(root.get(), "generatorTransport")->property("crowded").toBool());
            // A selected mark does not change the height of the tip row (the video would jump).
            auto* tip = named(root.get(), "tipRow");
            const qreal tipH = tip->height();
            QCOMPARE(tip->property("implicitHeight").toReal(), tipH);
            QVERIFY(inView("chooseVideo"));
            const QByteArray tag = QByteArray::number(size.width());
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_SCROLL"); !shot.isEmpty())
                window.grabWindow().save(QString::fromLocal8Bit(shot + "_" + tag + "_top.png"));
            if (size.height() <= 600) {
                // Short window: more content than room, the bar is on.
                QVERIFY2(content > view, qPrintable(QString("content %1 view %2").arg(content).arg(view)));
                QVERIFY(!inView("createButton"));
            }
            QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
            QVERIFY(!named(root.get(), "toggle_invert")->isVisible());
            QTRY_VERIFY(named(root.get(), "reviewCard")->isVisible());
            QTRY_VERIFY(named(root.get(), "reviewCard")->height() > 40);
            QTRY_VERIFY(named(root.get(), "reviewButton")->isVisible());
            const qreal contentNow = scroll->property("contentHeight").toReal();
            auto* reviewBtn = named(root.get(), "reviewButton");
            const qreal reviewY = reviewBtn->mapToItem(scroll->property("contentItem").value<QQuickItem*>(), QPointF(0, 0)).y();
            scroll->setProperty("contentY", qBound<qreal>(0, reviewY - 20, qMax<qreal>(0, contentNow - view)));
            QTRY_VERIFY2(inView("reviewButton"),
                         qPrintable(QString("review y=%1 h=%2 content=%3 view=%4 cardH=%5")
                                        .arg(reviewY)
                                        .arg(reviewBtn->height())
                                        .arg(contentNow)
                                        .arg(view)
                                        .arg(named(root.get(), "reviewCard")->height())));
            QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_SCROLL"); !shot.isEmpty())
                window.grabWindow().save(QString::fromLocal8Bit(shot + "_" + tag + "_bottom.png"));
            // Nothing in the Create side sticks out sideways (a chip or label that widens a
            // row would push a control out of its card at the narrow width).
            {
                auto* side = named(root.get(), "createScroll")->property("contentItem").value<QQuickItem*>();
                QVERIFY(side);
                std::function<void(QQuickItem*, QStringList&)> walk = [&](QQuickItem* it, QStringList& bad) {
                    for (QQuickItem* c : it->childItems()) {
                        if (!c->isVisible() || c->width() <= 0 || c->height() <= 0) continue;
                        const QByteArray cls = c->metaObject()->className();
                        const qreal right = c->mapToItem(scroll, QPointF(c->width(), 0)).x();
                        // Text with elide / wrap and the layouts themselves may be as wide as their parent; a real
                        // control (button, combo, chip, toggle, slider) must end inside the scroll area.
                        if (right > scroll->width() - 14 &&
                            (cls.contains("FlowButton") || cls.contains("FlowCombo") || cls.contains("HelpChip") ||
                             cls.contains("FlowToggle") || cls.contains("FlowSlider") || cls.contains("FlowTextField") ||
                             cls.contains("IconButton") || cls.contains("FlowTabs")))
                            bad << QString("%1 %2 ends at %3 of %4").arg(QString::fromLatin1(cls), c->objectName()).arg(right).arg(scroll->width());
                        walk(c, bad);
                    }
                };
                QStringList bad;
                QTest::qWait(200);
                walk(side, bad);
                if (!bad.isEmpty()) {
                    // Say where the width comes from: the first offender's ancestors, and for each
                    // layout among them its children with their widths and layout hints.
                    auto hint = [](QQuickItem* c) {
                        QObject* att = nullptr;
                        for (QObject* o : c->children())
                            if (QByteArray(o->metaObject()->className()) == "QQuickLayoutAttached") att = o;
                        return QString("%1 '%2' vis=%3 w=%4 iw=%5 min=%6 pref=%7 fill=%8")
                            .arg(QString::fromLatin1(c->metaObject()->className()), c->objectName()).arg(c->isVisible())
                            .arg(c->width()).arg(c->implicitWidth())
                            .arg(att ? att->property("minimumWidth").toString() : QStringLiteral("-"))
                            .arg(att ? att->property("preferredWidth").toString() : QStringLiteral("-"))
                            .arg(att ? att->property("fillWidth").toString() : QStringLiteral("-"));
                    };
                    QString why;
                    const QByteArray first = bad.first().section(' ', 1, 1).toLatin1();
                    QQuickItem* off = nullptr;
                    std::function<void(QQuickItem*)> find = [&](QQuickItem* it) {
                        for (QQuickItem* c : it->childItems()) {
                            if (off) return;
                            if (!first.isEmpty() && c->objectName() == QString::fromLatin1(first) && c->isVisible()) { off = c; return; }
                            find(c);
                        }
                    };
                    find(side);
                    for (QQuickItem* p = off; p && p != side->parentItem(); p = p->parentItem()) {
                        why += "\n  ^ " + hint(p);
                        if (QByteArray(p->metaObject()->className()).contains("Layout"))
                            for (QQuickItem* d : p->childItems())
                                if (d->isVisible() && d->width() > 0) why += "\n      - " + hint(d);
                    }
                    QFAIL(qPrintable(QString("%1 control(s) stick out at %2 px (scroll %3): %4%5")
                                         .arg(bad.size()).arg(size.width()).arg(scroll->width()).arg(bad.join("; "), why)));
                }
            }
            // Every control of the side can be reached: walk the content in steps.
            QCOMPARE(named(root.get(), "advancedPanel")->property("advancedOpen").toBool(), false);
            for (const char* name : {"styleCombo", "feelDeviceCombo", "toggle_contactVibration", "tipClassCombo",
                                     "contactClassCombo", "contactCurveCombo", "createButton", "reviewButton"}) {
                auto* item = named(root.get(), name);
                QVERIFY2(item, name);
                QVERIFY2(item->isVisible(), name);
                const qreal y = item->mapToItem(scroll->property("contentItem").value<QQuickItem*>(), QPointF(0, 0)).y();
                const qreal maxY = qMax<qreal>(0, scroll->property("contentHeight").toReal() - view);
                scroll->setProperty("contentY", qBound<qreal>(0, y - 20, maxY));
                QTRY_VERIFY2(inView(name),
                             qPrintable(QString("%1 y=%2 h=%3 maxY=%4 view=%5 cy=%6 ch=%7")
                                            .arg(name)
                                            .arg(y)
                                            .arg(item->height())
                                            .arg(maxY)
                                            .arg(view)
                                            .arg(scroll->property("contentY").toReal())
                                            .arg(scroll->property("contentHeight").toReal())));
            }
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }
};

QTEST_MAIN(GeneratorQmlTest)
#include "generator_qml_test.moc"
