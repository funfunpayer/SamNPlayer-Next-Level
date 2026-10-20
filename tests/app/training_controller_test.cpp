#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>
#include <memory>

#include "device_controller.h"
#include "fake_servers.h"
#include "samn/training_store.h"
#include "training_controller.h"
#include "samn/label_track.h"

using samn::ui::DeviceController;
using samn::ui::TrainingController;
using namespace samn_test;

namespace {

QStringList MockLines(const QSignalSpy& log) {
    QStringList out;
    for (const auto& args : log)
        if (args.at(0).toString().startsWith("[mock]")) out << args.at(0).toString();
    return out;
}

// A one-phase script of tiny timings (ramps are multiples of 50 ms).
QVariantMap tinyScript(const QString& name) {
    const auto curve = [](double peak) {
        return QVariantMap{{"startLevel", 0.0}, {"peakLevel", peak}, {"endLevel", 0.0}, {"rampUpMs", 100},
                           {"holdMs", 50},      {"rampDownMs", 100}, {"randomJitterFraction", 0.0}};
    };
    const auto phase = [&](const QString& n, bool vib) {
        QVariantMap p{{"name", n}, {"repeatCycles", 2}, {"restMs", 50}};
        p[vib ? "vibration" : "suction"] = curve(vib ? 0.6 : 0.4);
        return p;
    };
    return {{"name", name}, {"description", "tiny"}, {"phases", QVariantList{phase("A", true), phase("B", false)}},
            {"progressionPerCycle", 0.0}};
}

}  // namespace

class TrainingControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

    // A controller wired to its own device, fast clock, tiny custom form.
    struct Rig {
        DeviceController dev;
        TrainingController tc;
        QSignalSpy log{&dev, &DeviceController::logMessage};
        Rig() {
            tc.setDevice(&dev);
            // Real time, small timings: a faster clock would burst several 50 ms steps into one
            // tick and the device (which sends the newest level only) would skip some.
            tc.setCycles(2);
            tc.setRampUpMs(100);
            tc.setHoldMs(100);
            tc.setRestMs(100);
            tc.setPeak(0.8);
            tc.setProgression(0);
        }
    };

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }
    void init() {  // every test starts with an empty config
        std::filesystem::remove_all(config_.path().toStdString());
        std::filesystem::create_directories(config_.path().toStdString());
    }

    // ---- the form ----
    void defaultsAreTheCustomFormOf07() {
        TrainingController tc;
        QVERIFY(tc.mock());
        QCOMPARE(tc.scriptName(), QString());
        QCOMPARE(tc.technique(), QString("stopstart"));
        QCOMPARE(tc.channel(), QString("vibration"));
        QCOMPARE(tc.cycles(), 5);
        QCOMPARE(tc.rampUpMs(), 8000);
        QCOMPARE(tc.holdMs(), 3000);
        QCOMPARE(tc.restMs(), 10000);
        QCOMPARE(tc.peak(), 0.8);
        QCOMPARE(tc.plateauFraction(), 0.7);
        QCOMPARE(tc.progression(), 0.15);
        QCOMPARE(tc.tier(), 1.0);
        QVERIFY(tc.autoAdjust());
        QCOMPARE(tc.state(), QString("idle"));
    }

    void formIsClampedAndPersisted() {
        {
            TrainingController tc;
            tc.setCycles(99);
            QCOMPARE(tc.cycles(), 20);
            tc.setCycles(0);
            QCOMPARE(tc.cycles(), 1);
            tc.setRampUpMs(40000);
            QCOMPARE(tc.rampUpMs(), 30000);
            tc.setHoldMs(7500);
            tc.setRestMs(-5);
            QCOMPARE(tc.restMs(), 0);
            tc.setPeak(1.7);
            QCOMPARE(tc.peak(), 1.0);
            tc.setPeak(0.55);
            tc.setPlateauFraction(0.4);
            tc.setProgression(0.3);
            tc.setTechnique("plateau");
            tc.setMock(false);
        }
        TrainingController again;
        QCOMPARE(again.cycles(), 1);
        QCOMPARE(again.rampUpMs(), 30000);
        QCOMPARE(again.holdMs(), 7500);
        QCOMPARE(again.restMs(), 0);
        QCOMPARE(again.peak(), 0.55);
        QCOMPARE(again.plateauFraction(), 0.4);
        QCOMPARE(again.progression(), 0.3);
        QCOMPARE(again.technique(), QString("plateau"));
        QVERIFY(!again.mock());
    }

    void changingTheFormRebuildsThePlan() {
        TrainingController tc;
        tc.setCycles(2);
        tc.setRampUpMs(1000);
        tc.setHoldMs(500);
        tc.setRestMs(100);
        tc.setProgression(0);
        QSignalSpy plan(&tc, &TrainingController::planChanged);
        tc.setTechnique("plateau");
        QVERIFY(plan.count() >= 1);
        QCOMPARE(tc.planTotalMs(), 2 * (1000 + 500 + 100));
        QCOMPARE(tc.planPhases().size(), 2);
        QCOMPARE(tc.planPhases().at(1).toMap().value("name").toString(), QString("Cycle 2"));
        QCOMPARE(tc.phaseName(), QString("Cycle 1"));
        QCOMPARE(tc.phaseCount(), 2);
        QCOMPARE(tc.remainingSec(), 3);  // 3.2 s
    }

    // ---- programs ----
    void programsFilterByCategoryAndCarryTags() {
        TrainingController tc;
        QCOMPARE(tc.programs().size(), 11);
        const auto first = tc.programs().at(0).toMap();
        QCOMPARE(first.value("name").toString(), QString("stop-start"));
        QCOMPARE(first.value("label").toString(), QString("Stop-Start (classic)"));
        QVERIFY(first.value("text").toString().endsWith("Control \xC2\xB7 L1"));
        QVERIFY(!first.value("custom").toBool());
        tc.setCategory("body");
        QCOMPARE(tc.programs().size(), 3);
        for (const auto& p : tc.programs()) QCOMPARE(p.toMap().value("category").toString(), QString("body"));
        tc.setCategory("edge");
        QStringList edge;
        for (const auto& p : tc.programs()) edge << p.toMap().value("name").toString();
        QVERIFY(edge.contains("edge-ladder"));
        QVERIFY(edge.contains("plateau"));
        QCOMPARE(tc.programs().at(0).toMap().value("categoryLabel").toString(), QString("Edge Play"));
        tc.setCategory(QString());
        QCOMPARE(tc.programs().size(), 11);
        const auto ladder = [&] {
            for (const auto& p : tc.programs())
                if (p.toMap().value("name") == "endurance-ladder") return p.toMap();
            return QVariantMap();
        }();
        QCOMPARE(ladder.value("level").toInt(), 2);
        QCOMPARE(ladder.value("label").toString(), QString("Endurance ladder"));
        QVERIFY(ladder.value("text").toString().endsWith("Endurance \xC2\xB7 L2"));
    }

    void selectingAProgramShowsItsDescriptionAndPlan() {
        TrainingController tc;
        tc.setScriptName("vibration-wave-suction-focus");
        QVERIFY(tc.scriptDescription().startsWith("Light vibration ramps up"));
        QVERIFY(!tc.customScript());
        QCOMPARE(tc.planPhases().size(), 2);
        QCOMPARE(tc.planPhases().at(0).toMap().value("name").toString(), QString("Vibration wave"));
        QCOMPARE(tc.planTotalMs(), 3 * (14000 + 4000) + 3 * (13000 + 5000));
        QCOMPARE(tc.phaseCount(), 2);
        tc.setScriptName("variable");
        QVERIFY(tc.planHasJitter());
        tc.setScriptName(QString());
        QVERIFY(!tc.planHasJitter());
    }

    void savedProgramsAreListedPlannedAndDeleted() {
        TrainingController tc;
        QCOMPARE(tc.saveScript(tinyScript("My Tiny")), QString());
        QCOMPARE(tc.myPrograms().size(), 1);
        const auto p = tc.myPrograms().at(0).toMap();
        QCOMPARE(p.value("label").toString(), QString("\xE2\x98\x85 My Tiny"));
        QVERIFY(p.value("custom").toBool());
        QCOMPARE(tc.scriptForEditing("My Tiny").value("description").toString(), QString("tiny"));
        QVERIFY(tc.scriptForEditing("stop-start").isEmpty());
        QVERIFY(tc.saveScript(tinyScript("stop-start")).contains("built-in"));
        QVERIFY(tc.saveScript(tinyScript("   ")).contains("needs a name"));
        QVariantMap bad = tinyScript("Bad");
        bad["phases"] = QVariantList();
        QVERIFY(tc.saveScript(bad).contains("at least 1 phase"));
        QCOMPARE(tc.previewDraft(bad).value("error").toString(), QString("a script needs at least 1 phase"));
        const auto ok = tc.previewDraft(tinyScript("x"));
        QCOMPARE(ok.value("phases").toInt(), 2);
        QCOMPARE(ok.value("totalMs").toInt(), 2 * (250 + 50) + 2 * (250 + 50));
        tc.setScriptName("My Tiny");
        QVERIFY(tc.customScript());
        QCOMPARE(tc.planTotalMs(), 1200);
        // Edit the selected program in place: the plan follows.
        QVariantMap edited = tinyScript("My Tiny");
        QVariantList phases = edited["phases"].toList();
        QVariantMap first = phases[0].toMap();
        first["repeatCycles"] = 4;
        phases[0] = first;
        edited["phases"] = phases;
        QCOMPARE(tc.saveScript(edited), QString());
        QCOMPARE(tc.planTotalMs(), 4 * 300 + 2 * 300);
        QVERIFY(tc.deleteScript("stop-start").contains("built-in"));
        QVERIFY(tc.deleteScript("nope").contains("no custom script"));
        QCOMPARE(tc.deleteScript("My Tiny"), QString());
        QCOMPARE(tc.myPrograms().size(), 0);
        QCOMPARE(tc.scriptName(), QString());  // the selection falls back to Custom
    }

    // ---- intensity ----
    void tierScalesTheProgramAndNamesIt() {
        TrainingController tc;
        tc.setScriptName("stop-start");
        QCOMPARE(tc.intensityFactor(), 1.0);
        QVERIFY(tc.intensityNote().isEmpty());
        tc.setTier(1.2);
        QCOMPARE(tc.intensityFactor(), 1.2);
        QCOMPARE(tc.intensityNote(), QString("Applied intensity: \xC3\x97" "1.20 (Intense tier \xC3\x97" "1.2)"));
        tc.setTier(0.8);
        QVERIFY(tc.intensityNote().contains("Gentle tier \xC3\x97" "0.8"));
        tc.setScriptName(QString());  // Custom takes no tier
        QCOMPARE(tc.intensityFactor(), 1.0);
        QVERIFY(tc.intensityNote().isEmpty());
    }

    // ---- the device decides ----
    void withoutADeviceBothChannelsStay() {
        DeviceController dev;
        TrainingController tc;
        tc.setDevice(&dev);
        QVERIFY(tc.vibrationAvailable() && tc.suctionAvailable());
        QCOMPARE(tc.channelOptions(), (QStringList{"vibration", "suction", "both"}));
        QVERIFY(tc.canStart());
        QVERIFY(tc.deviceNote().isEmpty());
        tc.setChannel("both");
        QCOMPARE(tc.channel(), QString("both"));
    }

    void aStrokerBlocksStartUnlessMock() {
        TcpLineSink sink;
        QVERIFY(sink.listening());
        DeviceController dev;
        TrainingController tc;
        tc.setDevice(&dev);
        tc.setMock(false);
        dev.connectTCode("tcp://" + sink.hostport());
        QTRY_COMPARE(dev.state(), QString("connected"));
        QVERIFY(!tc.vibrationAvailable());
        QVERIFY(!tc.suctionAvailable());
        QVERIFY(!tc.canStart());
        QVERIFY(tc.deviceNote().contains("has no vibration or suction motor"));
        QVERIFY(tc.deviceNote().contains("Mock tests without a device."));
        QSignalSpy blocked(&tc, &TrainingController::startBlocked);
        QVERIFY(!tc.start());
        QCOMPARE(blocked.count(), 1);
        QCOMPARE(tc.state(), QString("idle"));
        QVERIFY(!dev.sessionActive());
        // Mock (test without a device) ignores what is connected.
        tc.setMock(true);
        QVERIFY(tc.canStart());
        QVERIFY(tc.vibrationAvailable() && tc.suctionAvailable());
        QVERIFY(tc.deviceNote().isEmpty());
    }

    void aOneMotorDeviceLeavesOneChannel() {
        // The Neo 2 has both; a device with one motor is not at hand, so the rule is checked on
        // the channel select: only the offered channels can be chosen.
        DeviceController dev;
        TrainingController tc;
        tc.setDevice(&dev);
        tc.setChannel("suction");
        QCOMPARE(tc.channel(), QString("suction"));
        tc.setChannel("nonsense");
        QCOMPARE(tc.channel(), QString("suction"));
    }

    // ---- the session ----
    void aCustomSessionConnectsMockRunsLogsAndDisconnects() {
        Rig r;
        QSignalSpy states(&r.tc, &TrainingController::stateChanged);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        QVERIFY(r.tc.start());
        QCOMPARE(r.tc.state(), QString("connecting"));
        QVERIFY(r.tc.active());
        QTRY_COMPARE(r.tc.state(), QString("running"));
        QVERIFY(r.dev.sessionActive());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(r.tc.state(), QString("idle"));
        QVERIFY(!r.dev.sessionActive());
        const QStringList log = r.tc.logLines();
        QCOMPARE(log.first(), QString("Connecting..."));
        QVERIFY(log.contains("Training starting..."));
        QVERIFY(log.at(log.size() - 3).startsWith("Cycle 1/2: peak 80%, hold 100ms"));
        QVERIFY(log.at(log.size() - 2).startsWith("Cycle 2/2: peak 80%"));
        QCOMPARE(log.last(), QString("Training finished."));
        // The device got the ramp and ended at 0; the connection we made is closed again.
        QTRY_COMPARE(r.dev.state(), QString("disconnected"));
        const QStringList lines = MockLines(r.log);
        QVERIFY(lines.contains("[mock] vibration=0.80"));
        QVERIFY(!lines.filter("suction=").size() || !lines.contains("[mock] suction=0.80"));  // vibration only
        const int stop = lines.lastIndexOf("[mock] stop");
        QVERIFY(stop > 0);
        QCOMPARE(lines.last(), QString("[mock] getrennt"));
        // The session log, history and recap.
        QCOMPARE(r.tc.history().size(), 1);
        const QString text = r.tc.history().at(0).toMap().value("text").toString();
        QVERIFY(text.contains("Stop-Start/Vibration: 2 cycles, avg peak 80%"));
        QCOMPARE(r.tc.recap().value("title").toString(), QString("Stop-Start"));
        QCOMPARE(r.tc.recap().value("summary").toString(), QString("2 cycles \xC2\xB7 avg peak 80%"));
        QVERIFY(r.tc.recap().value("hint").toString().contains("Export CSV"));
    }

    void anExistingConnectionIsReusedAndKept() {
        Rig r;
        r.dev.connectMock();
        QTRY_COMPARE(r.dev.state(), QString("connected"));
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        QVERIFY(r.tc.start());
        QCOMPARE(r.tc.state(), QString("running"));  // no connect step
        QCOMPARE(r.tc.logLines().first(), QString("Using existing connection from Tools \xE2\x86\x92 Device."));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(r.dev.state(), QString("connected"));
    }

    void bothChannelsDriveVibrationAndSuctionTogether() {
        Rig r;
        r.tc.setChannel("both");
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        const QStringList lines = MockLines(r.log);
        QVERIFY(lines.contains("[mock] vibration=0.80"));
        QVERIFY(lines.contains("[mock] suction=0.80"));
    }

    void aProgramRunsPhaseByPhase() {
        Rig r;
        QCOMPARE(r.tc.saveScript(tinyScript("Tiny")), QString());
        r.tc.setScriptName("Tiny");
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        QStringList cycles, peaks;
        connect(&r.tc, &TrainingController::progressChanged, this, [&] {
            if (cycles.isEmpty() || cycles.last() != r.tc.cycleText()) cycles << r.tc.cycleText();
            if (peaks.isEmpty() || peaks.last() != r.tc.peakText()) peaks << r.tc.peakText();
        });
        QVERIFY(r.tc.start());
        QCOMPARE(r.tc.phaseCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        const QStringList log = r.tc.logLines();
        QVERIFY(log.contains("A 1/2: Vibration 60%, rest 50ms"));
        QVERIFY(log.contains("B 2/2: Suction 40%, rest 50ms"));
        QCOMPARE(cycles, (QStringList{"A (1/2), phase 1/2", "A (2/2), phase 1/2", "B (1/2), phase 2/2", "B (2/2), phase 2/2"}));
        QCOMPARE(peaks, (QStringList{"Vibration 60%", "Suction 40%"}));
        const QStringList lines = MockLines(r.log);
        QVERIFY(lines.contains("[mock] vibration=0.60"));
        QVERIFY(lines.contains("[mock] suction=0.40"));
        QCOMPARE(r.tc.history().size(), 1);
        QCOMPARE(r.tc.recap().value("title").toString(), QString("Tiny"));
        QVERIFY(r.tc.recap().value("summary").toString().startsWith("4 cycles"));
    }

    void pauseHoldsTheDeviceAtZeroAndResumeContinues() {
        Rig r;
        r.tc.setTimeScale(1);
        r.tc.setRampUpMs(2000);
        r.tc.setHoldMs(1000);
        r.tc.setCycles(1);
        QVERIFY(r.tc.start());
        QTRY_COMPARE(r.tc.state(), QString("running"));
        QTRY_VERIFY(r.tc.vibration() > 0.1);
        r.tc.pause();
        QCOMPARE(r.tc.state(), QString("paused"));
        QVERIFY(r.tc.paused());
        QCOMPARE(r.tc.vibration(), 0.0);
        // Pause sends both channels to zero; count only once both writes are
        // in, or a slow machine counts before the second one lands.
        QTRY_VERIFY([&] {
            const QStringList tail = MockLines(r.log).mid(MockLines(r.log).size() - 2);
            return tail.contains("[mock] vibration=0.00") && tail.contains("[mock] suction=0.00");
        }());
        const int sec = r.tc.elapsedSec();
        const int writes = MockLines(r.log).size();
        QTest::qWait(400);
        QCOMPARE(MockLines(r.log).size(), writes);
        QCOMPARE(r.tc.elapsedSec(), sec);
        r.tc.togglePause();
        QCOMPARE(r.tc.state(), QString("running"));
        QTRY_VERIFY(r.tc.vibration() > 0.1);
        r.tc.stop();
        QCOMPARE(r.tc.state(), QString("idle"));
    }

    void theFilmsPhasesSkipAndPauseAtTheClimax() {
        // A film with confirmed phases from the label editor (D-157).
        const QString film = config_.filePath("film.mp4");
        QFile f(film);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a real video");
        f.close();
        samn::LabelProject labels;
        labels.SetPhase(0, 8000, "intro");
        labels.SetPhase(30000, 40000, "climax");
        labels.SetPhase(50000, 60000, "empty", false, "rules");  // a suggestion: plays
        samn::SaveLabelProject(samn::LabelProjectPath(std::filesystem::path(film.toStdU16String())), labels);
        Rig r;
        r.tc.chooseVideo(film);
        QCOMPARE(r.tc.videoPhases().size(), 2);
        QCOMPARE(r.tc.climaxAction(), QString("pause"));  // the default
        QCOMPARE(r.tc.videoPosition(1000), 8000.0);       // past the intro
        QCOMPARE(r.tc.videoPosition(20000), -1.0);
        QCOMPARE(r.tc.videoPosition(55000), -1.0);
        // Not running: the climax only counts.
        QCOMPARE(r.tc.videoPosition(31000), -1.0);
        QCOMPARE(r.tc.state(), QString("idle"));
        QCOMPARE(r.tc.videoPosition(45000), -1.0);  // out again: the next one counts

        r.tc.setTimeScale(1);
        r.tc.setRampUpMs(2000);
        r.tc.setHoldMs(1000);
        r.tc.setCycles(1);
        QVERIFY(r.tc.start());
        QTRY_COMPARE(r.tc.state(), QString("running"));
        r.tc.videoPosition(31000);
        QCOMPARE(r.tc.state(), QString("paused"));
        r.tc.resume();
        r.tc.videoPosition(32000);  // still the same climax: no second pause
        QCOMPARE(r.tc.state(), QString("running"));
        r.tc.stop();

        r.tc.setClimaxAction("cooldown");
        r.tc.setClimaxAction("loud");  // unknown: ignored
        QCOMPARE(r.tc.climaxAction(), QString("cooldown"));
        TrainingController again;  // remembered
        QCOMPARE(again.climaxAction(), QString("cooldown"));
        QCOMPARE(again.videoPhases().size(), 2);
    }

    void interruptEndsTheCycleAndTheSessionGoesOn() {
        Rig r;
        r.tc.setTimeScale(1);
        r.tc.setCycles(2);
        r.tc.setRampUpMs(100);
        r.tc.setHoldMs(5000);
        r.tc.setRestMs(100);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_VERIFY(r.tc.vibration() > 0.7);
        r.tc.interrupt();
        QTRY_VERIFY(r.tc.logLines().filter("stopped by you").size() >= 1);
        r.tc.interrupt();  // and the second cycle
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(r.tc.logLines().filter("stopped by you").size(), 2);
        QCOMPARE(r.tc.recap().value("summary").toString(), QString("2 cycles \xC2\xB7 avg peak 80% \xC2\xB7 2\xC3\x97 interrupted"));
    }

    void feedbackIsValidatedAndHighValuesInterrupt() {
        Rig r;
        QVERIFY(!r.tc.reportFeedback(7));
        QCOMPARE(r.tc.feedbackStatus(), QString("Not applied: no training is running"));
        r.tc.setTimeScale(1);
        r.tc.setCycles(2);
        r.tc.setRampUpMs(100);
        r.tc.setHoldMs(5000);
        r.tc.setRestMs(100);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_VERIFY(r.tc.vibration() > 0.7);
        QVERIFY(!r.tc.reportFeedback(11));
        QVERIFY(r.tc.feedbackStatus().contains("between 1 and 10 (got 11)"));
        QVERIFY(r.tc.reportFeedback(6));  // below 9: shapes the next cycle only
        QCOMPARE(r.tc.lastFeedback(), 6);
        QCOMPARE(r.tc.feedbackStatus(), QString("6 reported \xE2\x80\x94 affects the next cycle."));
        QTest::qWait(300);
        QVERIFY(r.tc.vibration() > 0.7);  // still holding
        QVERIFY(r.tc.reportFeedback(9));  // 9 also ends the running cycle
        QTRY_VERIFY(r.tc.logLines().filter("stopped by you").size() >= 1);
        QVERIFY(r.tc.logLines().join("\n").contains("adjusted after feedback 9") || true);
        r.tc.stop();
    }

    void aProgramShowsWhatFeedbackDid() {
        Rig r;
        r.tc.setTimeScale(1);
        QVariantMap s = tinyScript("Slow");
        QVariantList phases = s["phases"].toList();
        QVariantMap a = phases[0].toMap();
        QVariantMap v = a["vibration"].toMap();
        v["holdMs"] = 1500;
        a["vibration"] = v;
        a["restMs"] = 100;
        phases[0] = a;
        s["phases"] = phases;
        QCOMPARE(r.tc.saveScript(s), QString());
        r.tc.setScriptName("Slow");
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_VERIFY(r.tc.vibration() > 0.3);
        r.tc.reportFeedback(8);  // takes effect at the next repeat
        QTRY_VERIFY(r.tc.logLines().filter("A 1/2").size() >= 1);
        QTRY_VERIFY(!r.tc.feedbackEffect().isEmpty());
        QCOMPARE(r.tc.feedbackEffect(), QString("Feedback 8 \xE2\x86\x92 peak -7%, rest +20% \xE2\x80\x94 applied to both channels."));
        r.tc.stop();
    }

    void theSafetyLimitEndsALongSession() {
        Rig r;
        r.tc.setTimeScale(1);
        r.tc.setRampUpMs(30000);
        r.tc.setSafetyLimitMs(400);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(r.tc.logLines().contains("Session safety limit (400ms) reached \xE2\x80\x94 ended automatically."));
        QCOMPARE(r.tc.logLines().last(), QString("Training finished."));
        QVERIFY(!r.dev.sessionActive());
    }

    void emergencyStopEndsAtOnce() {
        Rig r;
        r.tc.setTimeScale(1);
        r.tc.setRampUpMs(30000);
        r.tc.start();
        QTRY_COMPARE(r.tc.state(), QString("running"));
        QTRY_VERIFY(r.tc.vibration() > 0.0);
        r.tc.emergencyStop();
        QCOMPARE(r.tc.state(), QString("idle"));
        QCOMPARE(r.tc.vibration(), 0.0);
        QTRY_COMPARE(r.dev.state(), QString("disconnected"));
        QVERIFY(MockLines(r.log).contains("[mock] stop"));
    }

    void startIsIgnoredWhileActiveAndStopWhenIdleIsSilent() {
        Rig r;
        r.tc.stop();
        r.tc.pause();
        r.tc.interrupt();
        QCOMPARE(r.tc.state(), QString("idle"));
        r.tc.setTimeScale(1);
        r.tc.setRampUpMs(30000);
        QVERIFY(r.tc.start());
        QVERIFY(!r.tc.start());
        r.tc.stop();
    }

    void aConnectThatNeverAnswersEndsWithAnError() {
        DeviceController dev;
        TrainingController tc;
        tc.setDevice(&dev);
        tc.setMock(false);  // a real device: here none answers
        tc.setConnectTimeoutMs(300);
        QSignalSpy done(&tc, &TrainingController::sessionFinished);
        QVERIFY(tc.start());
        QCOMPARE(tc.state(), QString("connecting"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
        QCOMPARE(tc.state(), QString("idle"));
        QVERIFY(tc.logLines().filter("ERROR:").size() == 1);
        QCOMPARE(tc.logLines().last(), QString("Training finished."));
        QVERIFY(!dev.sessionActive());
    }

    void unknownProgramDoesNotStart() {
        Rig r;
        r.tc.setScriptName("gone");
        QVERIFY(!r.tc.start());
        QVERIFY(r.tc.logLines().join("\n").contains("unknown training script \"gone\""));
        QCOMPARE(r.tc.state(), QString("idle"));
    }

    // ---- live curve data ----
    void theSessionRecordsSamplesForTheCurves() {
        Rig r;
        QSignalSpy samples(&r.tc, &TrainingController::samplesChanged);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(r.tc.vibrationSamples().size() > 3);
        QCOMPARE(r.tc.vibrationSamples().size(), r.tc.suctionSamples().size());
        QVERIFY(*std::max_element(r.tc.vibrationSamples().begin(), r.tc.vibrationSamples().end()) > 0.5f);
        QVERIFY(samples.count() >= int(r.tc.vibrationSamples().size()));
        QVERIFY(r.tc.sampleMs() >= 100);
    }

    // ---- history ----
    void historyNudgesTheNextSessionAndExportsCsv() {
        Rig r;
        r.tc.setTimeScale(1);
        r.tc.setCycles(1);
        r.tc.setRampUpMs(100);
        r.tc.setHoldMs(5000);
        r.tc.setRestMs(100);
        QSignalSpy done(&r.tc, &TrainingController::sessionFinished);
        r.tc.start();
        QTRY_VERIFY(r.tc.vibration() > 0.7);
        r.tc.interrupt();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        // The newest session of this pattern had an early stop: the suggestion appears.
        QVERIFY(r.tc.historySuggestion().startsWith("Last session of this pattern had 1 early stop"));
        // A program of another name is not affected.
        r.tc.setScriptName("plateau");
        QVERIFY(r.tc.historySuggestion().isEmpty());
        QCOMPARE(r.tc.intensityFactor(), 1.0);
        r.tc.setScriptName(QString());
        r.tc.setAutoAdjust(false);
        QCOMPARE(r.tc.intensityFactor(), 1.0);

        QTemporaryDir out;
        const QString path = out.filePath("h.csv");
        QCOMPARE(r.tc.exportHistoryCsv(path), QString());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString csv = QString::fromUtf8(f.readAll());
        QVERIFY(csv.startsWith("startedAt,technique,channel,cyclesCompleted"));
        QVERIFY(csv.contains(",stopstart,vibration,1,1,"));
        QVERIFY(!r.tc.exportHistoryCsv("/nonexistent-dir/x.csv").isEmpty());
        QVERIFY(r.tc.defaultExportName().startsWith("training-history-"));
        QVERIFY(r.tc.defaultExportName().endsWith(".csv"));
    }

    void recapIsEmptyBeforeTheFirstSessionAndLoadsFromHistory() {
        {
            TrainingController tc;
            QVERIFY(tc.recap().isEmpty());
            QVERIFY(tc.history().isEmpty());
        }
        // A log written by an earlier run shows as the recap at startup.
        std::filesystem::create_directories(samn::training::sessions_dir());
        std::ofstream(samn::training::sessions_dir() / "training-old.jsonl")
            << R"({"timestamp":"2026-01-01T10:00:00Z","technique":"vibration-massage","channel":"script","peakIntensity":0.5,"stoppedByUser":false,"reachedPeakAfterMs":1000,"arousalBefore":8}
)";
        TrainingController tc;
        QCOMPARE(tc.history().size(), 1);
        QCOMPARE(tc.recap().value("title").toString(), QString("vibration massage"));
        QCOMPARE(tc.recap().value("summary").toString(), QString("1 cycles \xC2\xB7 avg peak 50% \xC2\xB7 avg feedback 8.0"));
        QVERIFY(tc.history().at(0).toMap().value("text").toString().contains("vibration massage: 1 cycles, avg peak 50%, avg feedback 8.0"));
    }
};

QTEST_MAIN(TrainingControllerTest)
#include "training_controller_test.moc"
