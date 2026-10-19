// GeneratorController (P3.3): the Generator's five steps on a real, generated
// test clip (moving disc, no media in the repo). The engine runs on its own
// thread; events come back queued, so the tests wait with QTRY.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>

#include "generator_controller.h"
#include "samn/samn_file.h"
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
    void openingAVideoShowsItsFrameAndInfo() {
        GeneratorController g;
        QCOMPARE(g.step(), 1);
        QVERIFY(!g.openVideo(dir_.filePath("missing.mp4")));
        QVERIFY(g.status().contains("Cannot open"));
        QVERIFY(g.openVideo(QUrl::fromLocalFile(clip("a.mp4", 8)).toString()));
        QCOMPARE(g.videoSize(), QSize(320, 240));
        QCOMPARE(g.videoInfo(), QStringLiteral("0:08 · 320×240 · 25 fps"));
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
        QVERIFY(!g.create());  // no box yet
        QVERIFY(g.status().contains("tip box"));
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
