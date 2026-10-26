// GeneratorController (P3.3): the Generator's five steps on a real, generated
// test clip (moving disc, no media in the repo). The engine runs on its own
// thread; events come back queued, so the tests wait with QTRY.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>

#include "generator_controller.h"
#include "samn/samn_file.h"
#include "samn/settings.h"
#include "synthetic_clips.h"

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
        QVERIFY(g.boxLabel().contains("No tip box"));
        g.setBox(QRect(220, 200, -120, -160));  // dragged up and left
        QCOMPARE(g.box(), QRect(100, 40, 120, 160));
        QVERIFY(g.boxLabel().contains("x=100 y=40 w=120 h=160"));
        QCOMPARE(g.step(), 3);
        QVERIFY(box.count() >= 3);
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
            QCOMPARE(g.status(), QStringLiteral("Extra contact #2 added, follows partner path."));
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

    void findTipProposesTheBoxAndCandidates() {
        GeneratorController g;
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
