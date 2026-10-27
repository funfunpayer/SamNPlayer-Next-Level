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
        // 0.7's lede and step prompt, verbatim.
        QVERIFY(named(root.get(), "createLede")->isVisible());
        QVERIFY(named(root.get(), "stepPrompt")->property("text").toString().startsWith("Start here: choose a video."));

        // "Use Player video" appears with the Player's video and opens it.
        page->setProperty("playerVideoUrl", QUrl::fromLocalFile(video));
        QTRY_VERIFY(named(root.get(), "usePlayerVideo")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "usePlayerVideo"), "clicked");
        QTRY_VERIFY(gen->hasVideo());
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
        QTRY_VERIFY(page->property("playheadMs").toDouble() > 300);
        QVERIFY(QMetaObject::invokeMethod(page, "togglePlay"));
        QTRY_COMPARE(media->property("playbackState").toInt(), 2);
        QTRY_VERIFY(qAbs(gen->previewMs() - media->property("position").toDouble()) < 120);
        QVERIFY(QMetaObject::invokeMethod(page, "stop"));
        QTRY_VERIFY(gen->previewMs() < 50);

        // Find tip sets a box and shows the other candidates as numbered chips.
        QMetaObject::invokeMethod(named(root.get(), "findTip"), "clicked");
        QTRY_VERIFY(gen->finding());
        QTRY_VERIFY_WITH_TIMEOUT(!gen->finding(), 30000);
        QVERIFY(gen->hasBox());
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
        auto* invert = named(root.get(), "toggle_invert");
        QVERIFY(invert);
        QTest::mouseClick(&window, Qt::LeftButton, {},
                          invert->mapToScene(QPointF(invert->width() / 2, invert->height() / 2)).toPoint());
        QTRY_VERIFY(gen->invert());

        // G3: the feel follows the device. Nothing connected: Sam Neo 2.
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
        const QPointF c1 = frame->mapToScene(QPointF(ir.x() + 20 * s, ir.y() + 20 * s));
        const QPointF c2 = frame->mapToScene(QPointF(ir.x() + 80 * s, ir.y() + 70 * s));
        QTest::mousePress(&window, Qt::LeftButton, {}, c1.toPoint());
        QTest::mouseMove(&window, ((c1 + c2) / 2).toPoint());
        QTRY_VERIFY(named(root.get(), "contactDraft")->isVisible());
        QTest::mouseMove(&window, c2.toPoint());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, c2.toPoint());
        QTRY_COMPARE(gen->contactAreas().size(), 1);
        QVERIFY(!gen->markContact());
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
        QVERIFY(named(root.get(), "stepPrompt")->property("text").toString().startsWith("Step 5: Improve, then Play"));
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

        // A create tool writes new lanes, which appear under the stroke; the
        // "+ … lane" buttons are only there while a lane is missing.
        // They live in the Tools… menu (Owner, 26 Oct: compact tools).
        auto* menu = root->findChild<QObject*>("toolsMenu");
        QVERIFY(menu);
        QMetaObject::invokeMethod(named(root.get(), "toolsButton"), "clicked");
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(named(root.get(), "tool_addVibration")->isVisible());
        QVERIFY(named(root.get(), "tool_addSuction")->isVisible());
        QMetaObject::invokeMethod(named(root.get(), "tool_axes"), "clicked");
        QTRY_VERIFY(!menu->property("visible").toBool());  // a tool closes the menu
        QTRY_VERIFY(named(root.get(), "lane_vibration"));
        QTRY_VERIFY(named(root.get(), "lane_suction"));
        QMetaObject::invokeMethod(named(root.get(), "toolsButton"), "clicked");
        QTRY_VERIFY(menu->property("opened").toBool());
        QTRY_VERIFY(!named(root.get(), "tool_addVibration")->isVisible());
        QVERIFY(!named(root.get(), "tool_addSuction")->isVisible());
        QMetaObject::invokeMethod(menu, "close");
        QTRY_VERIFY(!menu->property("visible").toBool());
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
        QTest::qWait(200);
        QVERIFY(!gen->running() && !gen->finding());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join('\n')));
    }
};

QTEST_MAIN(GeneratorQmlTest)
#include "generator_qml_test.moc"
