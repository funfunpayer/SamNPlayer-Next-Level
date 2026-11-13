// GeneratorController (P3.3): the Generator's five steps on a real, generated
// test clip (moving disc, no media in the repo). The engine runs on its own
// thread; events come back queued, so the tests wait with QTRY.
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>

#include "ai_tip.h"
#include "generator_controller.h"
#include "samn/samn_file.h"
#include "samn/settings.h"
#include "samn/vision/onnx_detector.h"
#include "synthetic_clips.h"

#include <QDir>
#include <QFile>
#include <opencv2/core.hpp>

using samn::ui::GeneratorController;
namespace clips = samn::engine::testclips;

class GeneratorControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    QString clip(const char* name, int seconds) {
        const QString p = dir_.filePath(QString::fromLatin1(name));
        if (!QFileInfo::exists(p)) clips::WriteMoving(std::filesystem::path(p.toStdU16String()), seconds, 80.0, 1.0);
        return p;
    }

private slots:
    // The controller reads and writes settings: never the user's own file.
    void initTestCase() { qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8()); }

    void optimizeTargetAcceptsOffAndClearsJunk() {
        GeneratorController g;
        QCOMPARE(g.optimizeTarget(), QString());
        g.setOptimizeTarget(QStringLiteral("neo2"));
        QCOMPARE(g.optimizeTarget(), QStringLiteral("neo2"));
        g.setOptimizeTarget(QStringLiteral("off"));
        QCOMPARE(g.optimizeTarget(), QStringLiteral("off"));
        g.setOptimizeTarget(QStringLiteral("nope"));
        QCOMPARE(g.optimizeTarget(), QString());
        g.setOptimizeTarget(QStringLiteral("general"));
        QCOMPARE(g.optimizeTarget(), QStringLiteral("general"));
        g.setOptimizeTarget(QString());
        QCOMPARE(g.optimizeTarget(), QString());
    }

    void openingAVideoShowsItsFrameAndInfo() {
        GeneratorController g;
        QCOMPARE(g.step(), 1);
        QVERIFY(!g.openVideo(dir_.filePath("missing.mp4")));
        QVERIFY(g.status().contains("Cannot open"));
        QVERIFY(g.openVideo(QUrl::fromLocalFile(clip("a.mp4", 8)).toString()));
        QCOMPARE(g.videoSize(), QSize(320, 240));
        // The codec as the decoder reports it (the clip is MPEG-4 part 2); never a guessed H.264.
        QVERIFY2(g.videoInfo() == QStringLiteral("0:08 · 320×240 · 25 fps · MPEG-4") ||
                     g.videoInfo() == QStringLiteral("0:08 · 320×240 · 25 fps · codec unknown"),
                 qPrintable(g.videoInfo()));
        QVERIFY(!g.frame().isNull());
        QCOMPARE(g.frame().size(), QSize(320, 240));
        QVERIFY(qAbs(g.previewMs() - 800) <= 40);  // 0.7 poster: 10 % in
        QCOMPARE(g.step(), 2);
        g.seekPreview(5000);
        QVERIFY(qAbs(g.previewMs() - 5000) <= 40);
        g.seekPreview(999999);  // clamped to the clip
        QVERIFY(g.previewMs() < 8000);
    }

    void theBoxStaysInsideTheFrameAndTinyBoxesAreClicks() {
        GeneratorController g;
        QVERIFY(g.openVideo(clip("a.mp4", 8)));
        QSignalSpy box(&g, &GeneratorController::boxChanged);
        g.setBox(QRect(300, 200, 60, 60));
        QCOMPARE(g.box(), QRect(300, 200, 20, 40));
        g.setBox(QRect(10, 10, 5, 40));  // narrower than 8 px: a click, no box
        QVERIFY(!g.hasBox());
        QVERIFY(g.boxLabel().contains("Create finds the tip"));
        g.setBox(QRect(220, 200, -120, -160));  // dragged up and left
        QCOMPARE(g.box(), QRect(100, 40, 120, 160));
        QVERIFY(g.boxLabel().contains("x=100 y=40 w=120 h=160"));
        QCOMPARE(g.step(), 3);
        QVERIFY(box.count() >= 3);
        QVERIFY(!g.hasTipPath());  // preview is queued, not started on this stack
        QVERIFY(g.tipAt(0).isEmpty());
    }

    void tipAtFollowsAnInjectedPathAndClearsWithTheBox() {
        GeneratorController g;
        g.setAutoTip(false);
        QVERIFY(g.openVideo(clip("tip_inject.mp4", 2), false));
        QVERIFY(!g.hasTipPath());
        QVERIFY(g.tipAt(0).isEmpty());
        g.setBox(QRect(100, 40, 120, 160));
        g.setTipPathForTest({QVariantMap{{"at", 0}, {"x", 40.0}, {"y", 50.0}},
                             QVariantMap{{"at", 1000}, {"x", 80.0}, {"y", 90.0}}});
        QVERIFY(g.hasTipPath());
        QCOMPARE(g.tipAt(0).value("x").toDouble(), 40.0);
        QCOMPARE(g.tipAt(0).value("y").toDouble(), 50.0);
        QCOMPARE(g.tipAt(1000).value("x").toDouble(), 80.0);
        QCOMPARE(g.tipAt(1100).value("y").toDouble(), 90.0);  // nearest
        QVERIFY(g.tipAt(5000).isEmpty());                    // past the path
        QVERIFY(g.boxLabel().contains("follows motion"));
        g.clearBox();
        QVERIFY(!g.hasTipPath());
        QVERIFY(g.tipAt(0).isEmpty());
    }

    // ClassifyTipPov on the preview path: large lower box → first person,
    // small box → third. Contact suggestion is copy only (D-055).
    void povKindFromInjectedTipPath() {
        GeneratorController g;
        g.setAutoTip(false);
        QVERIFY(g.openVideo(clip("pov_tip.mp4", 4), false));
        QCOMPARE(g.povKind(), QStringLiteral("unknown"));
        QVERIFY(g.povHint().isEmpty());

        QVariantList lower;
        for (int t = 0; t <= 3000; t += 500)
            lower << QVariantMap{{"at", t}, {"x", 160.0}, {"y", 160.0}};
        g.setBox(QRect(60, 80, 200, 160));
        g.setTipPathForTest(lower);
        QCOMPARE(g.povKind(), QStringLiteral("first_person"));
        QVERIFY(g.povConfidence() > 0);
        QCOMPARE(g.povHint(), QStringLiteral("Close-up — a contact area may help."));
        QCOMPARE(g.contactAreas().size(), 0);  // never a silent second ROI

        g.setMarkContact(true);
        QVERIFY(g.addContactArea(QRect(20, 20, 40, 40)));
        QCOMPARE(g.povKind(), QStringLiteral("first_person"));
        QCOMPARE(g.povHint(), QStringLiteral("Close-up."));
        QCOMPARE(g.contactAreas().size(), 1);  // the user drew it

        g.clearContactAreas();
        g.setBox(QRect(148, 108, 24, 24));
        g.setTipPathForTest(lower);
        QCOMPARE(g.povKind(), QStringLiteral("third_person"));
        QCOMPARE(g.povHint(), QStringLiteral("Wide view."));
        QCOMPARE(g.contactAreas().size(), 0);

        g.clearBox();
        QCOMPARE(g.povKind(), QStringLiteral("unknown"));
        QVERIFY(g.povHint().isEmpty());
    }

    void setBoxBuildsAPreviewPathThatMovesWithTime() {
        GeneratorController g;
        g.setAutoTip(false);
        QVERIFY(g.openVideo(clip("tip_preview.mp4", 2), false));
        const double t0 = g.previewMs();
        g.setBox(QRect(100, 40, 120, 160));
        QTRY_VERIFY_WITH_TIMEOUT(g.hasTipPath(), 60000);
        const QVariantMap a = g.tipAt(t0);
        const QVariantMap b = g.tipAt(t0 + 800);
        QVERIFY2(!a.isEmpty(), qPrintable(g.status()));
        QVERIFY(!b.isEmpty());
        QVERIFY2(qAbs(a.value("y").toDouble() - b.value("y").toDouble()) > 5.0,
                 qPrintable(QString("y %1 vs %2").arg(a.value("y").toDouble()).arg(b.value("y").toDouble())));
    }

    void createWritesTheScriptAndHandsItToThePlayer() {
        QTemporaryDir own;
        const QString video = own.filePath("b.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 8, 80.0, 1.0);
        GeneratorController g;
        QVERIFY(g.openVideo(video));
        // No box yet: Create finds the tip first (0.7); Cancel stops both.
        QVERIFY(g.create());
        QVERIFY(g.finding());
        QCOMPARE(g.status(), QStringLiteral("Finding tip, then creating…"));
        g.cancel();
        QCOMPARE(g.status(), QStringLiteral("Tip find canceled."));
        QTRY_VERIFY(!g.finding());
        QTest::qWait(100);
        QVERIFY(!g.running());  // the find was canceled, so no run follows
        g.setBox(QRect(100, 40, 120, 160));
        g.setProfile("autotune");
        g.setProfile("tj");  // not offered here: falls back to Normal
        QCOMPARE(g.profile(), QStringLiteral("standard"));
        QSignalSpy created(&g, &GeneratorController::created);
        QSignalSpy progress(&g, &GeneratorController::progressChanged);
        QVERIFY(g.create());
        QVERIFY(g.running());
        QCOMPARE(g.step(), 4);
        QVERIFY(!g.create());  // one run at a time
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        QVERIFY(!g.running());
        QCOMPARE(g.step(), 5);
        QCOMPARE(g.percent(), 100);
        QVERIFY(progress.count() > 20);
        const QString samn = created.first().first().toString();
        QCOMPARE(samn, own.filePath("b.samn"));
        QVERIFY(QFileInfo::exists(own.filePath("b.funscript")));
        const samn::Document d = samn::LoadDocument(std::filesystem::path(samn.toStdU16String()));
        QVERIFY(!d.general.empty());
        QVERIFY(d.trajectory.has_value() && !d.trajectory->tip.empty());
        QVERIFY(g.hasTipPath());
        QVERIFY(!d.o_markers.empty());  // auto O-zone on by default
        QVERIFY(g.log().join('\n').contains("O-marker set"));
        QVERIFY(g.scriptExists());

        // Again without overwrite: refused, the old files stay.
        QVERIFY(g.create(false));
        QTRY_VERIFY_WITH_TIMEOUT(!g.running(), 10000);
        QVERIFY(g.status().contains("exists already"));
        QCOMPARE(created.count(), 1);
        // With overwrite: replaced.
        QVERIFY(g.create(true));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 2, 60000);
    }

    void createWithoutABoxFindsTheTipAndWrites() {
        QTemporaryDir own;
        const QString video = own.filePath("nobox.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 8, 80.0, 1.0);
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(video, false));
        QVERIFY(!g.hasBox());
        QCOMPARE(g.boxLabel(), QStringLiteral("Create finds the tip — no box to paint."));
        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.create());
        QVERIFY(g.finding());
        QCOMPARE(g.status(), QStringLiteral("Finding tip, then creating…"));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        QVERIFY(g.hasBox());
        QVERIFY(QFileInfo::exists(own.filePath("nobox.funscript")));
        QVERIFY2(g.status().startsWith("Done"), qPrintable(g.status()));
    }

    void createWithoutABoxRetriesWhenTheFirstFindIsWeak() {
        GeneratorController g;
        QStringList seen;
        connect(&g, &GeneratorController::statusChanged, this, [&] { seen << g.status(); });
        const QString still = dir_.filePath("still_create.mp4");
        clips::WriteMoving(std::filesystem::path(still.toStdU16String()), 6, 0.0, 1.0);
        QVERIFY(g.openVideo(still));
        QVERIFY(g.autoFinding());
        QVERIFY(g.create());
        QCOMPARE(g.status(), QStringLiteral("Finding tip, then creating…"));
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding() && !g.running(), 60000);
        QVERIFY(seen.contains(QStringLiteral("Retrying find…")));
        QVERIFY2(g.status().contains(QLatin1String("Create again")) ||
                     g.status().contains(QLatin1String("Find tip as override")),
                 qPrintable(g.status()));
        QVERIFY(!g.hasBox());
        QVERIFY(!QFileInfo::exists(dir_.filePath("still_create.funscript")));
    }

    void createWithoutABoxSeedsAStrongAiTipWhenMotionIsWeak() {
        QTemporaryDir own;
        const QString video = own.filePath("still_ai.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 4, 0.0, 1.0);
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(video, false));
        QVERIFY(!g.hasBox());
        g.setFakeAiHitsForTest({QVariantMap{{"x", 20},
                                            {"y", 30},
                                            {"w", 40},
                                            {"h", 50},
                                            {"score", 0.9},
                                            {"label", "penis"},
                                            {"cls", 14}}});
        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.create());
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        QCOMPARE(g.box(), QRect(20, 30, 40, 50));
        QVERIFY(QFileInfo::exists(own.filePath("still_ai.funscript")));
    }

    void contactAreasAndClassesGoIntoTheScript() {
        QTemporaryDir own;
        const QString video = own.filePath("c.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 3, 80.0, 1.0);
        GeneratorController g;
        QVERIFY(g.contactVibration());  // 0.7: on by default
        QCOMPARE(g.tipClass(), QStringLiteral("glans"));
        QCOMPARE(g.contactClass(), QStringLiteral("nipples"));
        QCOMPARE(g.tipClasses(), QStringList({"glans", "penis"}));
        QCOMPARE(g.contactClasses(), QStringList({"mouth", "nipples", "breasts"}));
        QVERIFY(!g.addContactArea(QRect(10, 10, 40, 40)));  // no video
        QVERIFY(g.openVideo(video));
        g.setBox(QRect(100, 40, 120, 160));
        g.setTipClass("penis");
        g.setTipClass("hand");  // not offered: kept
        QCOMPARE(g.tipClass(), QStringLiteral("penis"));
        QSignalSpy changed(&g, &GeneratorController::contactChanged);
        g.setMarkContact(true);
        QVERIFY(!g.addContactArea(QRect(10, 10, 4, 40)));  // a click, not an area
        QVERIFY(!g.markContact());                         // marking ends either way
        QVERIFY(g.addContactArea(QRect(130, 90, 60, 60)));
        g.setExtraClass("mouth");
        g.setMarkExtra(true);
        QVERIFY(g.addContactArea(QRect(300, 200, 60, 60)));  // clipped to the frame
        QCOMPARE(g.contactAreas().size(), 2);
        QCOMPARE(g.contactAreas().at(1).toMap().value("w").toInt(), 20);
        QCOMPARE(g.contactAreas().at(1).toMap().value("cls").toString(), QStringLiteral("mouth"));
        QVERIFY(changed.count() >= 3);
        QCOMPARE(g.box(), QRect(100, 40, 120, 160));  // the tip box is untouched

        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.create());
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        const samn::Document d = samn::LoadDocument(std::filesystem::path(own.filePath("c.samn").toStdU16String()));
        QVERIFY(d.recipe.contact_vibration);
        QVERIFY(d.contact_marks.has_value());
        QCOMPARE(QString::fromStdString(d.contact_marks->tip_class), QStringLiteral("penis"));
        QVERIFY(d.contact_marks->primary.has_value());
        QCOMPARE(QString::fromStdString(d.contact_marks->primary->cls), QStringLiteral("nipples"));
        QCOMPARE(d.contact_marks->extras.size(), size_t(1));
        QVERIFY(d.trajectory.has_value() && !d.trajectory->tip.empty());

        g.removeContactArea(0);
        QCOMPARE(g.contactAreas().size(), 1);
        // Another video: the areas belonged to the old one.
        QVERIFY(g.openVideo(clip("a.mp4", 8)));
        QVERIFY(g.contactAreas().isEmpty());
    }

    // 0.7's marking (G2a): the primary is drawn again in place, extras come
    // with their own type, each mark says what comes next, a selection is
    // deleted, Clear extras keeps the tip and the primary.
    void marksFollowTheOldMarking() {
        GeneratorController g;
        QVERIFY(g.openVideo(clip("m.mp4", 6)));
        g.setExtraClass("");  // remembered from an earlier test otherwise
        QVERIFY(!g.markTip(QRect(5, 5, 3, 3)));  // a click, not a box
        QVERIFY(g.markTip(QRect(100, 40, 120, 160)));
        QVERIFY(g.status().startsWith(QStringLiteral("Snack 1 done — tip box is the stroke tracker (CSRT).")));
        g.setContactVibration(false);
        QVERIFY(g.markTip(QRect(100, 40, 100, 160)));
        QVERIFY(g.status().startsWith(QStringLiteral("Tip box set — CSRT follows this look")));
        g.setContactVibration(true);

        // + Another before a primary: the first area is the primary.
        g.setMarkExtra(true);
        QCOMPARE(g.status(), QStringLiteral("Another contact area: drag on preview (magenta)."));
        QVERIFY(g.addContactArea(QRect(10, 10, 40, 40)));
        QCOMPARE(g.contactAreas().first().toMap().value("cls").toString(), QStringLiteral("nipples"));
        QVERIFY(g.status().startsWith(QStringLiteral("Snack 2 done — partner touch (gold).")));
        // Drawn again (no mode, the page's Shift-drag): it moves.
        QVERIFY(g.addContactArea(QRect(20, 20, 40, 40)));
        QCOMPARE(g.contactAreas().size(), 1);
        QCOMPARE(g.contactAreas().first().toMap().value("x").toInt(), 20);

        // Mark contact and + Another exclude each other.
        g.setMarkContact(true);
        g.setMarkExtra(true);
        QVERIFY(!g.markContact());
        QVERIFY(g.addContactArea(QRect(80, 80, 30, 30)));
        QCOMPARE(g.status(), QStringLiteral("Extra contact #1 added, stay fixed."));
        QCOMPARE(g.contactAreas().at(1).toMap().value("cls").toString(), QString());
        g.setExtraClass("breasts");
        g.setExtraClass("hand");  // not offered: kept
        QCOMPARE(g.extraClass(), QStringLiteral("breasts"));
        g.setMarkExtra(true);
        QVERIFY(g.addContactArea(QRect(150, 80, 30, 30)));
        QCOMPARE(g.status(), QStringLiteral("Extra contact #2 (breasts) added, stay fixed."));

        g.setAreaClass(1, "mouth");
        QCOMPARE(g.contactAreas().at(1).toMap().value("cls").toString(), QStringLiteral("mouth"));
        QCOMPARE(g.status(), QStringLiteral("Extra contact class → mouth"));
        g.setAreaClass(0, "");  // the primary keeps a class
        QCOMPARE(g.contactAreas().at(0).toMap().value("cls").toString(), QStringLiteral("nipples"));

        g.deleteMark(2);
        QCOMPARE(g.status(), QStringLiteral("Extra contact removed."));
        QCOMPARE(g.contactAreas().size(), 2);
        g.setMarkExtra(true);
        QVERIFY(g.addContactArea(QRect(150, 80, 30, 30)));
        g.clearExtras();
        QCOMPARE(g.contactAreas().size(), 1);
        QVERIFY(g.hasBox());
        g.deleteMark(0);
        QCOMPARE(g.status(), QStringLiteral("Contact mark cleared."));
        QVERIFY(g.contactAreas().isEmpty());
        g.deleteMark(-1);
        QCOMPARE(g.status(), QStringLiteral("Tip mark cleared — Find tip area or paint again."));
        QVERIFY(!g.hasBox());
    }

    // Ignore region (black soft mask): add, select encoding, clear with extras.
    void ignoreRegionsAreSoftMasks() {
        GeneratorController g;
        QVERIFY(g.openVideo(clip("mask.mp4", 6)));
        QVERIFY(g.maskRois().isEmpty());
        g.setMarkIgnore(true);
        QVERIFY(g.markIgnore());
        QVERIFY(!g.markContact());
        QVERIFY(!g.markExtra());
        QVERIFY(g.addMaskRoi(QRect(20, 30, 50, 40)));
        QCOMPARE(g.status(), QStringLiteral("Ignore region (black) #1 added."));
        QVERIFY(!g.markIgnore());
        QCOMPARE(g.maskRois().size(), 1);
        QCOMPARE(g.maskRois().first().toMap().value("x").toInt(), 20);
        QCOMPARE(g.maskRois().first().toMap().value("w").toInt(), 50);
        g.setMarkIgnore(true);
        QVERIFY(g.addMaskRoi(QRect(200, 10, 40, 40)));
        QCOMPARE(g.status(), QStringLiteral("Ignore region (black) #2 added."));
        QCOMPARE(g.maskRois().size(), 2);
        g.deleteMark(GeneratorController::kMaskSelectBase + 0);
        QCOMPARE(g.status(), QStringLiteral("Ignore region removed."));
        QCOMPARE(g.maskRois().size(), 1);
        QCOMPARE(g.maskRois().first().toMap().value("x").toInt(), 200);
        // Clear extras also drops soft masks (0.7 help gen-extras-clear).
        g.clearExtras();
        QVERIFY(g.maskRois().isEmpty());
        QCOMPARE(g.status(), QStringLiteral("Soft masks cleared."));
        // Tiny box is a click, not a mask.
        QVERIFY(!g.addMaskRoi(QRect(1, 1, 4, 4)));
        QVERIFY(g.maskRois().isEmpty());
    }

    // G2b: Partner fixed / Stay fixed set ContactMarkBox.fixed and are remembered.
    void partnerFixedAndStayFixedFlags() {
        {
            GeneratorController g;
            QVERIFY(!g.partnerFixed());
            QVERIFY(g.extrasStayFixed());
            QVERIFY(g.openVideo(clip("fix.mp4", 6)));
            g.setExtraClass("");
            QVERIFY(g.addContactArea(QRect(10, 10, 40, 40)));
            QCOMPARE(g.contactAreas().first().toMap().value("fixed").toBool(), false);
            g.setPartnerFixed(true);
            QCOMPARE(g.contactAreas().first().toMap().value("fixed").toBool(), true);
            g.setMarkExtra(true);
            QVERIFY(g.addContactArea(QRect(80, 80, 30, 30)));
            QCOMPARE(g.status(), QStringLiteral("Extra contact #1 added, stay fixed."));
            QCOMPARE(g.contactAreas().at(1).toMap().value("fixed").toBool(), true);
            g.setExtrasStayFixed(false);
            QCOMPARE(g.contactAreas().at(1).toMap().value("fixed").toBool(), false);
            g.setMarkExtra(true);
            QVERIFY(g.addContactArea(QRect(150, 80, 30, 30)));
            QCOMPARE(g.status(), QStringLiteral("Extra contact #2 added, follows own path."));
            QCOMPARE(g.contactAreas().at(2).toMap().value("fixed").toBool(), false);
        }
        {
            GeneratorController g;
            QVERIFY(g.partnerFixed());
            QVERIFY(!g.extrasStayFixed());
            g.setPartnerFixed(false);
            g.setExtrasStayFixed(true);
        }
    }

    // An extra moved up to primary takes the contact class.
    void anExtraMovedUpTakesAClass() {
        GeneratorController g;
        QVERIFY(g.openVideo(clip("p.mp4", 6)));
        g.setExtraClass("");  // remembered from an earlier test otherwise
        QVERIFY(g.addContactArea(QRect(10, 10, 40, 40)));
        g.setMarkExtra(true);
        QVERIFY(g.addContactArea(QRect(80, 80, 30, 30)));
        g.removeContactArea(0);
        QCOMPARE(g.contactAreas().size(), 1);
        QCOMPARE(g.contactAreas().first().toMap().value("cls").toString(), QStringLiteral("nipples"));
    }

    // 0.7 Feel probe: live PreviewContactVibration for Sensitivity / Curve.
    void contactFeelProbeFollowsSpanAndCurve() {
        GeneratorController g;
        g.setContactSpan(0.40);
        g.setContactCurve("linear");
        const QVariantMap early = g.contactFeelProbe();
        QVERIFY(early.value("stroke").toList().size() >= 20);
        QVERIFY(early.value("vib").toList().size() == early.value("stroke").toList().size());
        g.setContactSpan(0.90);
        const QVariantMap deep = g.contactFeelProbe();
        QVERIFY(early.value("activePct").toDouble() > deep.value("activePct").toDouble() + 5.0);
        g.setContactSpan(0.75);
        g.setContactCurve("soft");
        const double softActive = g.contactFeelProbe().value("activePct").toDouble();
        g.setContactCurve("impulse");
        const double impulseActive = g.contactFeelProbe().value("activePct").toDouble();
        QVERIFY(impulseActive < softActive);
        QVERIFY(!g.contactFeelProbe().value("hint").toString().isEmpty());
        // Restore defaults so later tests that expect soft / 0.75 stay clean.
        g.setContactSpan(0.75);
        g.setContactCurve("soft");
    }

    // 0.7 Step 3: sensitivity and curve of contact vibration, written into
    // the script; the Generator's choices are remembered (generator.* keys).
    void contactFeelAndChoicesAreRemembered() {
        {
            GeneratorController g;
            QCOMPARE(g.contactSpan(), 0.75);
            QCOMPARE(g.contactCurve(), QStringLiteral("soft"));
            g.setContactSpan(0.52);  // the 0.7 slider steps by 5
            QCOMPARE(g.contactSpan(), 0.5);
            g.setContactSpan(2);
            QCOMPARE(g.contactSpan(), 0.95);
            g.setContactSpan(0.6);
            g.setContactCurve("peak");
            g.setContactCurve("nonsense");  // unknown: linear, as 0.7's NormalizeContactCurve
            QCOMPARE(g.contactCurve(), QStringLiteral("linear"));
            g.setContactCurve("impulse");
            g.setProfile("weich");
            g.setSceneCuts(false);
            g.setContactVibration(false);
            g.setTipClass("penis");
            g.setContactClass("mouth");
            g.setInvert(true);
        }
        const samn::Settings s;
        QCOMPARE(s.get("generator.contact_vibration_span").get<int>(), 60);  // percent, as 0.7
        QCOMPARE(QString::fromStdString(s.get("generator.contact_vibration_curve").get<std::string>()), QStringLiteral("impulse"));
        {
            GeneratorController g;
            QCOMPARE(g.contactSpan(), 0.6);
            QCOMPARE(g.contactCurve(), QStringLiteral("impulse"));
            QCOMPARE(g.profile(), QStringLiteral("weich"));
            QVERIFY(!g.sceneCuts());
            QVERIFY(!g.contactVibration());
            QCOMPARE(g.tipClass(), QStringLiteral("penis"));
            QCOMPARE(g.contactClass(), QStringLiteral("mouth"));
            QVERIFY(!g.invert());  // per video, not remembered
            // The script gets the sensitivity and the curve.
            QTemporaryDir own;
            const QString video = own.filePath("f.mp4");
            clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 2, 80.0, 1.0);
            QVERIFY(g.openVideo(video));
            g.setBox(QRect(100, 40, 120, 160));
            g.setContactVibration(true);
            QSignalSpy created(&g, &GeneratorController::created);
            QVERIFY(g.create());
            QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
            const samn::Document d = samn::LoadDocument(std::filesystem::path(own.filePath("f.samn").toStdU16String()));
            QVERIFY(d.recipe.contact_vibration);
            QCOMPARE(d.recipe.contact_vibration_span, 0.6);
            QCOMPARE(QString::fromStdString(d.recipe.contact_vibration_curve), QStringLiteral("impulse"));
            // Back to the defaults for the other tests.
            g.setProfile("standard");
            g.setSceneCuts(true);
            g.setTipClass("glans");
            g.setContactClass("nipples");
            g.setContactSpan(0.75);
            g.setContactCurve("soft");
        }
    }

    // G4a Advanced: Sliding / Adaptive / axis are remembered; defaults on/on/auto.
    void advancedShellChoicesAreRemembered() {
        {
            GeneratorController g;
            QVERIFY(g.slidingDynamics());
            QVERIFY(g.adaptiveKeyframes());
            QCOMPARE(g.motionAxis(), QStringLiteral("auto"));
            g.setSlidingDynamics(false);
            g.setAdaptiveKeyframes(false);
            g.setMotionAxis("x");
            QCOMPARE(g.motionAxis(), QStringLiteral("x"));
            g.setMotionAxis("nonsense");  // unknown → auto (like feelDevice)
            QCOMPARE(g.motionAxis(), QStringLiteral("auto"));
            g.setMotionAxis("y");
        }
        const samn::Settings s;
        QCOMPARE(s.get("generator.sliding_dynamics").get<bool>(), false);
        QCOMPARE(s.get("generator.adaptive_keyframe").get<bool>(), false);
        QCOMPARE(QString::fromStdString(s.get("generator.axis").get<std::string>()), QStringLiteral("y"));
        {
            GeneratorController g;
            QVERIFY(!g.slidingDynamics());
            QVERIFY(!g.adaptiveKeyframes());
            QCOMPARE(g.motionAxis(), QStringLiteral("y"));
            // Restore defaults for later tests that share the settings file.
            g.setSlidingDynamics(true);
            g.setAdaptiveKeyframes(true);
            g.setMotionAxis("auto");
        }
    }

    // G4b Expert: smooth / spacing / prominence / RDP / max-speed / analysis-fps remembered with steps.
    void expertKnobChoicesAreRemembered() {
        {
            GeneratorController g;
            QCOMPARE(g.smoothWindow(), 11);
            QCOMPARE(g.minPeakDistanceMs(), 150);
            QCOMPARE(g.peakProminence(), 0.0);
            QCOMPARE(g.rdpTolerance(), 0.0);
            QCOMPARE(g.maxSpeed(), 0);
            QCOMPARE(g.maxAnalysisFps(), 0);
            QCOMPARE(g.editorShare(), 0.40);
            QCOMPARE(g.hybridReacquire(), false);
            g.setSmoothWindow(20);  // even → 19
            QCOMPARE(g.smoothWindow(), 19);
            g.setMinPeakDistanceMs(157);  // → 150
            QCOMPARE(g.minPeakDistanceMs(), 150);
            g.setMinPeakDistanceMs(160);
            g.setPeakProminence(0.37);  // → 0.35
            QCOMPARE(g.peakProminence(), 0.35);
            g.setRdpTolerance(1.2);  // → 1.0
            QCOMPARE(g.rdpTolerance(), 1.0);
            g.setMaxSpeed(175);  // → 150
            QCOMPARE(g.maxSpeed(), 150);
            g.setMaxAnalysisFps(17);  // → 15
            QCOMPARE(g.maxAnalysisFps(), 15);
            g.setEditorShare(0.55);
            QCOMPARE(g.editorShare(), 0.55);
            g.setEditorShare(0.9);  // clamp
            QCOMPARE(g.editorShare(), 0.65);
            g.setHybridReacquire(true);
            QVERIFY(g.hybridReacquire());
        }
        const samn::Settings s;
        QCOMPARE(s.get("generator.smooth_window").get<int>(), 19);
        QCOMPARE(s.get("generator.min_peak_distance_ms").get<int>(), 160);
        QCOMPARE(s.get("generator.peak_prominence").get<double>(), 0.35);
        QCOMPARE(s.get("generator.rdp_tolerance").get<double>(), 1.0);
        QCOMPARE(s.get("generator.max_speed").get<int>(), 150);
        QCOMPARE(s.get("generator.max_analysis_fps").get<int>(), 15);
        QCOMPARE(s.get("generator.editor_share").get<double>(), 0.65);
        QCOMPARE(s.get("generator.hybrid_reacquire").get<bool>(), true);
        {
            GeneratorController g;
            QCOMPARE(g.smoothWindow(), 19);
            QCOMPARE(g.minPeakDistanceMs(), 160);
            QCOMPARE(g.peakProminence(), 0.35);
            QCOMPARE(g.rdpTolerance(), 1.0);
            QCOMPARE(g.maxSpeed(), 150);
            QCOMPARE(g.maxAnalysisFps(), 15);
            QCOMPARE(g.editorShare(), 0.65);
            QVERIFY(g.hybridReacquire());
            // Restore Everyday defaults for later tests.
            g.setSmoothWindow(11);
            g.setMinPeakDistanceMs(150);
            g.setPeakProminence(0);
            g.setRdpTolerance(0);
            g.setMaxSpeed(0);
            g.setMaxAnalysisFps(0);
            g.setEditorShare(0.40);
            // Unset — do not write false (that means user Off and blocks
            // FunGen2 auto-on in later tip/Apply tests that share settings).
            samn::Settings().set("generator.hybrid_reacquire", nullptr);
        }
    }

    // G3 (0.7 gen_feel.js): the feel follows the device; the Sam Neo 2 feel
    // goes into the new script right after Create, the default changes nothing.
    void theNeoFeelGoesIntoTheNewScript() {
        GeneratorController g;
        QCOMPARE(g.feelDevice(), QStringLiteral("auto"));
        QCOMPARE(g.feelKind(), QStringLiteral("neo2"));  // nothing connected
        g.setConnectedKind("vibrator");
        QCOMPARE(g.feelKind(), QStringLiteral("vibrator"));
        QVERIFY(!g.feelHint().isEmpty());
        g.setFeelDevice("nonsense");
        QCOMPARE(g.feelDevice(), QStringLiteral("auto"));
        g.setConnectedKind("");
        g.setFeelBalance(34);  // steps of 10
        QCOMPARE(g.feelBalance(), 30);
        g.setFeelBalance(500);
        QCOMPARE(g.feelBalance(), 100);
        g.setFeelBalance(0);
        // Auto scene only from a marked contact area.
        QCOMPARE(g.autoScene(), QString());

        QTemporaryDir own;
        const QString video = own.filePath("n.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 2, 80.0, 1.0);
        QVERIFY(g.openVideo(video));
        g.setBox(QRect(100, 40, 120, 160));
        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.create());
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        const auto samnPath = std::filesystem::path(own.filePath("n.samn").toStdU16String());
        samn::Document d = samn::LoadDocument(samnPath);
        QVERIFY(d.recipe.scene_feel.empty());
        QVERIFY(!d.recipe.vibration_follows_suction);
        QCOMPARE(d.recipe.feel_balance, 0.0);
        QVERIFY(!g.log().join('\n').contains("Sam Neo 2 feel"));

        g.setSceneFeel("oral");
        g.setFeelBalance(30);
        g.setVibFollow(true);
        QVERIFY(g.feelSummary().contains("Oral"));
        QVERIFY(g.create(true));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 2, 60000);
        QCOMPARE(created.last().first().toString(), own.filePath("n.samn"));
        d = samn::LoadDocument(samnPath);
        QCOMPARE(QString::fromStdString(d.recipe.scene_feel), QStringLiteral("oral"));
        QVERIFY(d.recipe.vibration_follows_suction);
        QCOMPARE(d.recipe.feel_balance, 0.3);
        QVERIFY(g.log().join('\n').contains("Sam Neo 2 feel"));
        QVERIFY(QFileInfo(own.filePath("n.funscript")).size() > 0);

        // Another device: the Neo feel is not written.
        g.setFeelDevice("stroker");
        QVERIFY(!g.applyFeel(own.filePath("n.samn")));
        // Remembered.
        {
            GeneratorController again;
            QCOMPARE(again.feelDevice(), QStringLiteral("stroker"));
            QCOMPARE(again.sceneFeel(), QStringLiteral("oral"));
            QCOMPARE(again.feelBalance(), 30);
            QVERIFY(again.vibFollow());
        }
        // Back to the defaults for the other tests.
        g.setFeelDevice("auto");
        g.setSceneFeel("auto");
        g.setFeelBalance(0);
        g.setVibFollow(false);
    }

    // Unset key → sound+contact (vibrator default). A saved "" is Motion only.
    void vibSourceDefaultsToSoundAndContactAndKeepsASavedChoice() {
        QTemporaryDir own;
        const QByteArray prev = qgetenv("SAMN_CONFIG_DIR");
        qputenv("SAMN_CONFIG_DIR", own.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("SAMN_CONFIG_DIR", prev); });
        {
            GeneratorController g;
            QCOMPARE(g.vibSource(), QStringLiteral("sound+contact"));
        }
        {
            samn::Settings s;
            s.set("generator.feel.vib_source", std::string());
            GeneratorController g;
            QCOMPARE(g.vibSource(), QString());
        }
        {
            samn::Settings s;
            s.set("generator.feel.vib_source", std::string("sound"));
            GeneratorController g;
            QCOMPARE(g.vibSource(), QStringLiteral("sound"));
        }
    }

    // The auto scene follows the primary area's class, not the default class
    // for new marks, and the Feel page hears about a change.
    void theAutoSceneFollowsThePrimaryAreaClass() {
        GeneratorController g;
        g.setSceneFeel("auto");
        QTemporaryDir own;
        const QString video = own.filePath("a.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 2, 80.0, 1.0);
        QVERIFY(g.openVideo(video));
        g.setBox(QRect(100, 40, 120, 160));
        QCOMPARE(g.contactClass(), QStringLiteral("nipples"));  // the default for new marks
        g.setMarkContact(true);
        QVERIFY(g.addContactArea(QRect(130, 90, 60, 60)));
        QCOMPARE(g.autoScene(), QStringLiteral("breast"));
        QSignalSpy feel(&g, &GeneratorController::feelChanged);
        g.setAreaClass(0, "mouth");
        QVERIFY(feel.count() >= 1);
        QCOMPARE(g.contactClass(), QStringLiteral("nipples"));
        QCOMPARE(g.autoScene(), QStringLiteral("oral"));

        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.create());
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        const samn::Document d = samn::LoadDocument(std::filesystem::path(own.filePath("a.samn").toStdU16String()));
        QCOMPARE(QString::fromStdString(d.recipe.scene_feel), QStringLiteral("oral"));
    }

    void findTipProposesTheBoxAndCandidates() {
        GeneratorController g;
        // The explicit Find tip only: no automatic search on open.
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(!g.findTip());  // no video
        QVERIFY(g.openVideo(clip("tip.mp4", 8)));
        QVERIFY(g.findTip());
        QVERIFY(g.finding());
        QVERIFY(!g.findTip());  // one search at a time
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        // 0.7 parity (docs/benchmarks/P3_TIP_FIND.md): the same clip gives this box.
        QCOMPARE(g.box(), QRect(116, 96, 86, 48));
        QVERIFY(g.status().startsWith("Tip found"));
        QCOMPARE(g.tipCandidates().size(), 6);
        const QVariantMap second = g.tipCandidates().at(1).toMap();
        g.pickCandidate(1);
        QCOMPARE(g.box(), QRect(second.value("x").toInt(), second.value("y").toInt(), second.value("w").toInt(),
                                second.value("h").toInt()));
        g.pickCandidate(99);  // ignored
        QCOMPARE(g.box().x(), second.value("x").toInt());

        // A new video drops the candidates; cancel ends a search.
        QVERIFY(g.openVideo(clip("tip2.mp4", 8)));
        QVERIFY(g.tipCandidates().isEmpty());
        QVERIFY(g.findTip());
        g.cancelFind();
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY(g.status().contains("canceled") || g.status().startsWith("Tip found"));
    }

    // D-159: opening a video looks for the tip by itself; a clear find is the
    // box, a box drawn meanwhile stays, and the switch turns it off.
    void openingAVideoFindsTheTipByItself() {
        GeneratorController g;
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.autoTip());
        QVERIFY(g.openVideo(clip("auto1.mp4", 8)));
        QVERIFY(g.finding());
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY2(g.hasBox(), qPrintable(g.status()));
        QVERIFY(g.status().startsWith("Tip found"));
        QCOMPARE(g.tipCandidates().size(), 6);

        // The user's box drawn during the search is kept.
        QVERIFY(g.openVideo(clip("auto2.mp4", 8)));
        QVERIFY(g.finding());
        const QRect mine(10, 10, 40, 40);
        g.setBox(mine);
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QCOMPARE(g.box(), mine);
        QVERIFY(g.status().startsWith("Your box stays"));
        QCOMPARE(g.tipCandidates().size(), 6);

        // Off: opening only opens, and the choice is kept.
        g.setAutoTip(false);
        QVERIFY(g.openVideo(clip("auto3.mp4", 8)));
        QVERIFY(!g.finding());
        QVERIFY(!g.hasBox());
        {
            GeneratorController again;
            QVERIFY(!again.autoTip());
        }
    }

    // A video opened while the search for the one before still runs gets
    // its own search once that one has ended (none when opened for editing).
    void aVideoOpenedDuringASearchGetsItsOwn() {
        GeneratorController g;
        QVERIFY(g.openVideo(clip("first.mp4", 8)));
        QVERIFY(g.autoFinding());
        QVERIFY(g.openVideo(clip("second.mp4", 6)));
        QVERIFY(g.finding());
        QVERIFY(g.autoFinding());
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY2(g.hasBox(), qPrintable(g.status()));
        QVERIFY2(g.status().startsWith("Tip found"), qPrintable(g.status()));
        QCOMPARE(g.tipCandidates().size(), 6);

        // The same length: the old search looked from the same frame.
        QVERIFY(g.openVideo(clip("third.mp4", 8)));
        QVERIFY(g.openVideo(clip("fourth.mp4", 8)));
        QVERIFY(!g.hasBox());
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY2(g.hasBox(), qPrintable(g.status()));
        QVERIFY2(g.status().startsWith("Tip found"), qPrintable(g.status()));

        // Opened for editing: the old search ends and none follows.
        QVERIFY(g.openVideo(clip("fifth.mp4", 8)));
        QVERIFY(g.openVideo(clip("sixth.mp4", 6), false));
        QVERIFY(g.finding());  // the old video's search still ends
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QTest::qWait(200);
        QVERIFY(!g.finding());
        QVERIFY(!g.hasBox());
        QVERIFY(g.tipCandidates().isEmpty());
        QCOMPARE(g.status(), QStringLiteral("Create finds the tip — no box to paint."));
    }

    // The Player's "Create script" for a video while the search for the one
    // before still runs: the tip is found on the new video, then the run.
    void createForAVideoOpenedDuringASearchRuns() {
        GeneratorController g;
        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.openVideo(clip("before.mp4", 8)));
        QVERIFY(g.openVideo(clip("after.mp4", 6)));
        QVERIFY(g.create(false));
        QCOMPARE(g.status(), QStringLiteral("Finding tip, then creating…"));
        QVERIFY(!g.autoFinding());  // the user's now
        QTRY_VERIFY_WITH_TIMEOUT(g.running() || created.count() > 0, 30000);
        QVERIFY(g.hasBox());
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        QCOMPARE(created.first().first().toString(), dir_.filePath("after.samn"));
        QVERIFY(QFileInfo::exists(dir_.filePath("after.funscript")));
        QVERIFY(!QFileInfo::exists(dir_.filePath("before.funscript")));
    }

    // Create with the user's box ends the automatic search; its late answer
    // neither touches the box nor the run's status.
    void createWithABoxEndsTheAutomaticSearch() {
        GeneratorController g;
        QSignalSpy created(&g, &GeneratorController::created);
        QVERIFY(g.openVideo(clip("boxed.mp4", 8)));
        QVERIFY(g.autoFinding());
        const QRect mine(100, 40, 120, 160);
        g.setBox(mine);
        QVERIFY(g.create(true));
        QVERIFY(g.running());
        QCOMPARE(g.status(), QStringLiteral("Creating the script…"));
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QTest::qWait(100);
        QCOMPARE(g.box(), mine);
        QVERIFY2(g.status() == QStringLiteral("Creating the script…") || g.status().startsWith("Done"),
                 qPrintable(g.status()));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 60000);
        QVERIFY2(g.status().startsWith("Done"), qPrintable(g.status()));
        QCOMPARE(g.box(), mine);
    }

    // Find tip during the automatic search: from the same frame it takes
    // that search over (its answer counts as the user's: even a weak find
    // becomes the box); from another frame a new search starts there.
    void findTipTakesTheAutomaticSearchOver() {
        GeneratorController g;
        QStringList seen;
        connect(&g, &GeneratorController::statusChanged, this, [&] { seen << g.status(); });
        const QString looking = QStringLiteral("Looking for the rhythmic motion…");

        const QString still = dir_.filePath("still_take.mp4");
        clips::WriteMoving(std::filesystem::path(still.toStdU16String()), 6, 0.0, 1.0);
        QVERIFY(g.openVideo(still));
        QVERIFY(g.autoFinding());
        QVERIFY(g.findTip());
        QVERIFY(g.finding());
        QVERIFY(!g.autoFinding());
        QVERIFY(!g.findTip());  // the user's own: one at a time
        g.showStatus("taken over");
        seen.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY(!seen.contains(looking));  // the same search, not a new one
        QVERIFY2(g.hasBox(), qPrintable(g.status()));
        QVERIFY2(g.status().startsWith("Only weak motion found"), qPrintable(g.status()));

        QVERIFY(g.openVideo(clip("restart.mp4", 8)));
        QVERIFY(g.autoFinding());
        g.seekPreview(5000);
        QVERIFY(g.findTip());
        QVERIFY(g.finding());
        QVERIFY(!g.autoFinding());
        g.showStatus("restarted");
        seen.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY(seen.contains(looking));  // a new search from 5 s
        QVERIFY2(g.hasBox(), qPrintable(g.status()));
        QVERIFY2(g.status().startsWith("Tip found"), qPrintable(g.status()));
        QCOMPARE(g.tipCandidates().size(), 6);
    }

    // Create queue ends the automatic search; the user's own search must end first.
    void theQueueEndsTheAutomaticSearch() {
        GeneratorController g;
        QSignalSpy finished(&g, &GeneratorController::queueFinished);
        QCOMPARE(g.addToQueue({clip("queued.mp4", 6)}), 1);
        QVERIFY(g.openVideo(clip("shown.mp4", 8)));
        QVERIFY(g.findTip());  // taken over: the user's
        QVERIFY(!g.runQueue(true));
        QVERIFY2(g.status().startsWith("Find tip is still running"), qPrintable(g.status()));
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);

        QVERIFY(g.openVideo(clip("shown2.mp4", 8)));
        QVERIFY(g.autoFinding());
        QVERIFY(g.runQueue(true));
        QVERIFY(g.queueRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!g.finding(), 30000);
        QVERIFY(!g.hasBox());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        QCOMPARE(g.queue().at(0).toMap().value("status").toString(), QStringLiteral("done"));
        QVERIFY(QFileInfo::exists(dir_.filePath("queued.funscript")));
        QVERIFY2(g.status().startsWith("Queue finished: 1 created"), qPrintable(g.status()));
    }

    void destroyingDuringAFindIsSafe() {
        {
            GeneratorController g;
            QVERIFY(g.openVideo(clip("tip3.mp4", 8)));
            QVERIFY(g.findTip());
        }
        QTest::qWait(50);
    }

    void theQueueCreatesEachVideoInOrder() {
        GeneratorController g;
        QTemporaryDir own;
        auto make = [&](const char* name, int seconds, double amp) {
            const QString p = own.filePath(QString::fromLatin1(name));
            clips::WriteMoving(std::filesystem::path(p.toStdU16String()), seconds, amp, 1.0);
            return p;
        };
        const QString a = make("a.mp4", 8, 80.0), still = make("still.mp4", 4, 0.0);
        const QString missing = own.filePath("missing.mp4");
        QCOMPARE(g.addToQueue({QUrl::fromLocalFile(a).toString(), still, missing, a}), 3);  // a once
        QCOMPARE(g.queue().size(), 3);
        QVERIFY(g.queueSummary().contains("3 waiting"));
        QSignalSpy finished(&g, &GeneratorController::queueFinished);
        QVERIFY(g.runQueue());
        QVERIFY(g.queueRunning());
        QVERIFY(!g.create());   // nothing else while the queue runs
        QVERIFY(!g.findTip());
        QCOMPARE(g.addToQueue({own.filePath("x.mp4")}), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        QVERIFY(!g.queueRunning());
        auto status = [&](int i) { return g.queue().at(i).toMap().value("status").toString(); };
        QCOMPARE(status(0), QStringLiteral("done"));
        QCOMPARE(status(1), QStringLiteral("failed"));  // only weak motion
        QVERIFY(g.queue().at(1).toMap().value("message").toString().contains("weak"));
        QCOMPARE(status(2), QStringLiteral("failed"));
        QVERIFY(QFileInfo::exists(own.filePath("a.funscript")));
        QVERIFY(!QFileInfo::exists(own.filePath("still.funscript")));
        QCOMPARE(finished.at(0).at(0).toInt(), 1);
        QVERIFY(g.status().startsWith("Queue finished: 1 created"));
        QVERIFY(!g.runQueue());  // nothing waiting

        // Again with a script there: skipped, unless overwrite.
        g.clearQueue();
        QVERIFY(g.queue().isEmpty());
        g.addToQueue({a});
        QVERIFY(g.runQueue(false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 60000);
        QCOMPARE(status(0), QStringLiteral("skipped"));
        g.clearQueue();
        g.addToQueue({a});
        QVERIFY(g.runQueue(true));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 60000);
        QCOMPARE(status(0), QStringLiteral("done"));

        // Cancel stops the rest.
        g.clearQueue();
        g.addToQueue({make("b.mp4", 8, 80.0), make("c.mp4", 8, 80.0)});
        QVERIFY(g.runQueue());
        g.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, 60000);
        QCOMPARE(status(1), QStringLiteral("canceled"));
        QVERIFY(!QFileInfo::exists(own.filePath("c.funscript")));
        g.removeFromQueue(0);
        QCOMPARE(g.queue().size(), 1);
    }

    void twoVideosWithOneScriptNameDoNotOverwriteEachOther() {
        GeneratorController g;
        QTemporaryDir own;
        const QString mp4 = own.filePath("x.mp4"), mkv = own.filePath("x.mkv");
        clips::WriteMoving(std::filesystem::path(mp4.toStdU16String()), 8, 80.0, 1.0);
        clips::WriteMoving(std::filesystem::path(mkv.toStdU16String()), 8, 80.0, 1.0);
        QCOMPARE(g.addToQueue({mp4, mkv}), 2);
        QSignalSpy finished(&g, &GeneratorController::queueFinished);
        QVERIFY(g.runQueue(true));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        auto item = [&](int i) { return g.queue().at(i).toMap(); };
        QCOMPARE(item(0).value("status").toString(), QStringLiteral("done"));
        QCOMPARE(item(1).value("status").toString(), QStringLiteral("failed"));
        QCOMPARE(item(1).value("message").toString(), QStringLiteral("same script name as x.mp4"));

        // A later run counts what an earlier one wrote (review of #140).
        const QString avi = own.filePath("x.avi");
        clips::WriteMoving(std::filesystem::path(avi.toStdU16String()), 8, 80.0, 1.0);
        QCOMPARE(g.addToQueue({avi}), 1);
        QVERIFY(g.runQueue(true));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 60000);
        QCOMPARE(item(2).value("status").toString(), QStringLiteral("failed"));
        QCOMPARE(item(2).value("message").toString(), QStringLiteral("same script name as x.mp4"));
    }

    void cancelStopsTheRunAndWritesNothing() {
        QTemporaryDir own;
        const QString video = own.filePath("long.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 40, 80.0, 1.0);
        GeneratorController g;
        QVERIFY(g.openVideo(video));
        g.setBox(QRect(100, 40, 120, 160));
        QVERIFY(g.create());
        QTRY_VERIFY_WITH_TIMEOUT(g.percent() > 0, 30000);
        g.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!g.running(), 10000);
        QCOMPARE(g.status(), QStringLiteral("Canceled."));
        QVERIFY(!QFileInfo::exists(own.filePath("long.funscript")));
        QVERIFY(!QFileInfo::exists(own.filePath("long.samn")));
        QCOMPARE(g.step(), 3);
    }

    void aiTipHelperPicksMatchingClassWithoutFallback() {
        using samn::ui::AiTipModel;
        using samn::ui::ListAiHits;
        using samn::ui::PickExpectedTip;
        QVERIFY(samn::ui::LabelsMatchTip("penis", "glans"));
        QVERIFY(samn::ui::LabelsMatchTip("glans", "penis"));
        QVERIFY(!samn::ui::LabelsMatchTip("face", "glans"));
        AiTipModel nn;
        nn.kind = samn::ui::AiTipKind::kNudeNet;
        nn.nudenet = true;
        samn::vision::Detection penis;
        penis.cls = 14;  // MALE_GENITALIA_EXPOSED → penis
        penis.score = 0.9f;
        penis.box = cv::Rect2f(10, 20, 40, 50);
        samn::vision::Detection face;
        face.cls = 1;  // FACE_FEMALE → face
        face.score = 0.8f;
        face.box = cv::Rect2f(100, 10, 30, 30);
        samn::vision::Detection covered;
        covered.cls = 16;  // FEMALE_BREAST_COVERED → no label
        covered.score = 0.95f;
        covered.box = cv::Rect2f(5, 5, 40, 40);
        const auto hits = ListAiHits({face, penis, covered}, nn);
        QCOMPARE(int(hits.size()), 2);
        const auto pick = PickExpectedTip(hits, "glans");
        QVERIFY(pick);
        QCOMPARE(pick->x, 10);
        QCOMPARE(pick->y, 20);
        QCOMPARE(QString::fromStdString(pick->label), QStringLiteral("penis"));
        QVERIFY(!PickExpectedTip(hits, "mouth"));
        const auto breast = samn::ui::ContactClassForLabel("breast");
        QVERIFY(breast);
        QCOMPARE(QString::fromStdString(*breast), QStringLiteral("breasts"));
        const auto nipples = samn::ui::ContactClassForLabel("nipples");
        QVERIFY(nipples);
        QCOMPARE(QString::fromStdString(*nipples), QStringLiteral("nipples"));
        const auto mouth = samn::ui::ContactClassForLabel("mouth");
        QVERIFY(mouth);
        QCOMPARE(QString::fromStdString(*mouth), QStringLiteral("mouth"));
        QVERIFY(!samn::ui::ContactClassForLabel("face"));
        QVERIFY(!samn::ui::ContactClassForLabel("penis"));
        QVERIFY(!samn::ui::ContactClassForLabel("glans"));
    }

    void funGenYoloMapsOntoOurLabelsAndLoadsBeforeNudeNet() {
        using samn::ui::AiTipKind;
        using samn::ui::AiTipModel;
        using samn::ui::FindAiTipModel;
        using samn::ui::FunGenClassNames;
        using samn::ui::FunGenToLabel;
        using samn::ui::LabelForDetection;
        using samn::ui::ListAiHits;
        using samn::ui::PickExpectedTip;
        QCOMPARE(QString::fromStdString(FunGenToLabel("penis")), QStringLiteral("penis"));
        QCOMPARE(QString::fromStdString(FunGenToLabel("glans")), QStringLiteral("glans"));
        QCOMPARE(QString::fromStdString(FunGenToLabel("breast")), QStringLiteral("breast"));
        QCOMPARE(QString::fromStdString(FunGenToLabel("face")), QStringLiteral("mouth"));
        QCOMPARE(QString::fromStdString(FunGenToLabel("hand")), QStringLiteral("hand"));
        QVERIFY(FunGenToLabel("anus").empty());
        QVERIFY(FunGenToLabel("navel").empty());
        QCOMPARE(int(FunGenClassNames().size()), 11);
        QCOMPARE(QString::fromStdString(FunGenClassNames()[2]), QStringLiteral("penis"));

        AiTipModel fg;
        fg.kind = AiTipKind::kFunGen;
        fg.names = FunGenClassNames();
        samn::vision::Detection tip;
        tip.cls = 2;  // penis
        tip.score = 0.9f;
        tip.box = cv::Rect2f(10, 20, 40, 50);
        samn::vision::Detection face;
        face.cls = 0;  // face → mouth
        face.score = 0.8f;
        face.box = cv::Rect2f(100, 10, 30, 30);
        samn::vision::Detection anus;
        anus.cls = 6;
        anus.score = 0.95f;
        anus.box = cv::Rect2f(5, 5, 40, 40);
        const auto hits = ListAiHits({face, tip, anus}, fg);
        QCOMPARE(int(hits.size()), 2);
        QCOMPARE(QString::fromStdString(hits[0].label), QStringLiteral("mouth"));
        const auto pick = PickExpectedTip(hits, "glans");
        QVERIFY(pick);
        QCOMPARE(QString::fromStdString(pick->label), QStringLiteral("penis"));
        QCOMPARE(QString::fromStdString(LabelForDetection(anus, fg)), QString());

        if (!samn::vision::OnnxDetector::Available()) return;
        const QString models = dir_.filePath("config/models");
        QVERIFY(QDir().mkpath(models));
        const QString fungen = models + "/fungen.onnx";
        const QString nudenet = models + "/nudenet.onnx";
        QFile::remove(fungen);
        QFile::remove(nudenet);
        QVERIFY(QFile::copy(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_yolo.onnx", fungen));
        QVERIFY(QFile::copy(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_nudenet.onnx", nudenet));
        {
            QFile classes(models + "/fungen.classes.json");
            QVERIFY(classes.open(QIODevice::WriteOnly));
            classes.write(R"({"classes":["penis","breast"]})");
        }
        const auto m = FindAiTipModel();
        QVERIFY(m);
        QCOMPARE(m->kind, AiTipKind::kFunGen);
        QFile::remove(fungen);
        const auto nn = FindAiTipModel();
        QVERIFY(nn);
        QCOMPARE(nn->kind, AiTipKind::kNudeNet);
        QFile::remove(nudenet);
    }

    void suggestAiTipStaysPendingUntilApply() {
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(!g.aiTipAvailable());
        QVERIFY(g.aiTipHint().contains(QLatin1String("No AI model")) ||
                g.aiTipHint().contains(QLatin1String("no ONNX Runtime")));
        QVERIFY(!g.suggestAiTip());  // no video
        QVERIFY(g.openVideo(clip("ai_tip.mp4", 8), false));
        QVERIFY(!g.hasBox());
        QVERIFY(!g.suggestAiTip());  // no model
        QVERIFY(!g.hasBox());
        QVERIFY(g.status().contains(QLatin1String("No AI model")) ||
                g.status().contains(QLatin1String("no ONNX Runtime")));

        g.setFakeAiHitsForTest({QVariantMap{{"x", 20},
                                            {"y", 30},
                                            {"w", 40},
                                            {"h", 50},
                                            {"score", 0.9},
                                            {"label", "penis"},
                                            {"cls", 14}},
                                QVariantMap{{"x", 80},
                                            {"y", 10},
                                            {"w", 24},
                                            {"h", 24},
                                            {"score", 0.7},
                                            {"label", "face"},
                                            {"cls", 1}}});
        QVERIFY(g.aiTipAvailable());
        QVERIFY(g.suggestAiTip());
        QVERIFY(g.hasAiTipProposal());
        QVERIFY(!g.hasBox());  // Apply required
        QCOMPARE(g.aiDetections().size(), 2);
        QCOMPARE(g.aiTipProposal().value("x").toInt(), 20);
        QCOMPARE(g.aiTipProposal().value("label").toString(), QStringLiteral("penis"));
        // Everyday default: key unset (not Off). Shared settings from earlier
        // Expert knob tests must not leave false or auto-on is skipped.
        samn::Settings().set("generator.hybrid_reacquire", nullptr);
        QVERIFY(g.applyAiTip());
        QCOMPARE(g.box(), QRect(20, 30, 40, 50));
        QVERIFY(!g.hasAiTipProposal());
        QCOMPARE(g.status(), QStringLiteral("Tip box applied from AI proposal."));
        // FunGen2-like: setting a tip path turns AI re-acquire on when a model
        // is present and the user never saved Off (Advanced stays closed).
        QVERIFY(g.hybridReacquire());
        samn::Settings().set("generator.hybrid_reacquire", nullptr);
    }

    void dismissAndPickAiDetectionStillNeedApply() {
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(clip("ai_tip2.mp4", 8), false));
        g.setFakeAiHitsForTest({QVariantMap{{"x", 12},
                                            {"y", 14},
                                            {"w", 32},
                                            {"h", 32},
                                            {"score", 0.6},
                                            {"label", "face"},
                                            {"cls", 1}}});
        QVERIFY(g.suggestAiTip());
        QVERIFY(!g.hasAiTipProposal());  // face ≠ glans, no fallback
        QCOMPARE(g.aiDetections().size(), 1);
        QVERIFY(!g.hasBox());
        g.pickAiDetection(1);  // ignored
        QVERIFY(!g.hasAiTipProposal());
        g.pickAiDetection(0);
        QVERIFY(g.hasAiTipProposal());
        QVERIFY(!g.hasBox());
        g.dismissAiTip();
        QVERIFY(!g.hasAiTipProposal());
        QVERIFY(g.aiDetections().isEmpty());
        QVERIFY(!g.hasBox());
        QCOMPARE(g.status(), QStringLiteral("AI tip proposal dismissed."));
        QVERIFY(!g.applyAiTip());
    }

    void listAiDetectionsClickApplyDoesNotWriteUntilApply() {
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(clip("ai_show_all.mp4", 8), false));
        QVERIFY(!g.listAiDetections());  // no model
        QVERIFY(!g.hasBox());
        g.setFakeAiHitsForTest({QVariantMap{{"x", 20},
                                            {"y", 30},
                                            {"w", 40},
                                            {"h", 50},
                                            {"score", 0.9},
                                            {"label", "penis"},
                                            {"cls", 14}},
                                QVariantMap{{"x", 80},
                                            {"y", 10},
                                            {"w", 28},
                                            {"h", 24},
                                            {"score", 0.8},
                                            {"label", "breast"},
                                            {"cls", 3}},
                                QVariantMap{{"x", 4},
                                            {"y", 4},
                                            {"w", 24},
                                            {"h", 24},
                                            {"score", 0.5},
                                            {"label", "face"},
                                            {"cls", 1}}});
        QVERIFY(g.listAiDetections());
        QCOMPARE(g.aiDetections().size(), 3);
        QVERIFY(!g.hasAiTipProposal());  // Show all does not auto-pick
        QVERIFY(!g.hasAiContactProposal());
        QVERIFY(!g.hasBox());
        QVERIFY(g.contactAreas().isEmpty());
        g.pickAiDetection(0);
        QVERIFY(g.hasAiTipProposal());
        QVERIFY(!g.hasBox());
        QVERIFY(g.applyAiTip());
        QCOMPARE(g.box(), QRect(20, 30, 40, 50));
        QVERIFY(!g.hasAiTipProposal());
        QCOMPARE(g.status(), QStringLiteral("Tip box applied from AI proposal."));
    }

    void shiftClickSafeAiTagAddsContactWithoutWritingTip() {
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(clip("ai_shift_safe.mp4", 8), false));
        g.setFakeAiHitsForTest({QVariantMap{{"x", 20},
                                            {"y", 30},
                                            {"w", 40},
                                            {"h", 50},
                                            {"score", 0.9},
                                            {"label", "penis"},
                                            {"cls", 14}},
                                QVariantMap{{"x", 80},
                                            {"y", 10},
                                            {"w", 28},
                                            {"h", 24},
                                            {"score", 0.8},
                                            {"label", "breast"},
                                            {"cls", 3}}});
        QVERIFY(g.listAiDetections());
        QVERIFY(!g.hasBox());
        QVERIFY(g.pickAiDetectionAsContact(1));  // breast → breasts, Contact vib on
        QVERIFY(!g.hasBox());
        QVERIFY(!g.hasAiTipProposal());
        QCOMPARE(g.contactAreas().size(), 1);
        QCOMPARE(g.contactAreas().first().toMap().value("x").toInt(), 80);
        QCOMPARE(g.contactAreas().first().toMap().value("cls").toString(), QStringLiteral("breasts"));
        QVERIFY(!g.hasAiContactProposal());
    }

    void shiftClickUnsafeAiTagNeedsApplyContact() {
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] { g.setAutoTip(true); });
        QVERIFY(g.openVideo(clip("ai_shift_face.mp4", 8), false));
        const QRect before = g.box();
        g.setFakeAiHitsForTest({QVariantMap{{"x", 12},
                                            {"y", 14},
                                            {"w", 32},
                                            {"h", 32},
                                            {"score", 0.6},
                                            {"label", "face"},
                                            {"cls", 1}}});
        QVERIFY(g.listAiDetections());
        QVERIFY(!g.pickAiDetectionAsContact(0));  // face is not a safe contact class
        QVERIFY(g.hasAiContactProposal());
        QVERIFY(g.contactAreas().isEmpty());
        QCOMPARE(g.box(), before);
        QVERIFY(!g.hasBox());
        QVERIFY(g.applyAiContact());
        QCOMPARE(g.contactAreas().size(), 1);
        QCOMPARE(g.contactAreas().first().toMap().value("x").toInt(), 12);
        QCOMPARE(g.contactAreas().first().toMap().value("cls").toString(), QStringLiteral("nipples"));
        QVERIFY(!g.hasAiContactProposal());
        QVERIFY(!g.hasBox());
        QCOMPARE(g.status(), QStringLiteral("Contact area applied from AI proposal."));
    }

    void suggestAiTipOnTinyOnnxDoesNotWriteTheBox() {
        if (!samn::vision::OnnxDetector::Available()) {
            QSKIP("this build has no ONNX Runtime");
        }
        const QString src = QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_nudenet.onnx";
        QVERIFY(QFileInfo::exists(src));
        const QString models = dir_.filePath("config/models");
        QVERIFY(QDir().mkpath(models));
        const QString dest = models + "/nudenet.onnx";
        QFile::remove(dest);
        QVERIFY(QFile::copy(src, dest));
        GeneratorController g;
        g.setAutoTip(false);
        const auto restore = qScopeGuard([&] {
            g.setAutoTip(true);
            QFile::remove(dest);
        });
        QVERIFY(g.openVideo(clip("ai_onnx.mp4", 8), false));
        QVERIFY(g.aiTipAvailable());
        const QRect before = g.box();
        QVERIFY(g.suggestAiTip());
        QTRY_VERIFY_WITH_TIMEOUT(!g.suggestingAiTip(), 30000);
        QCOMPARE(g.box(), before);  // never silent-write
        QVERIFY(!g.hasBox());
    }

    void closingTheAppDuringARunReturnsPromptly() {
        QTemporaryDir own;
        const QString video = own.filePath("long.mp4");
        clips::WriteMoving(std::filesystem::path(video.toStdU16String()), 40, 80.0, 1.0);
        auto* g = new GeneratorController;
        QVERIFY(g->openVideo(video));
        g->setBox(QRect(100, 40, 120, 160));
        QVERIFY(g->create());
        QTest::qWait(200);
        const auto t0 = std::chrono::steady_clock::now();
        delete g;  // cancels and joins the worker
        QVERIFY(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
        QCoreApplication::processEvents();  // late queued events find no controller
        QVERIFY(!QFileInfo::exists(own.filePath("long.funscript")));
    }
};

QTEST_MAIN(GeneratorControllerTest)
#include "generator_controller_test.moc"
