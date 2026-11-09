// PlayerController: the Player logic behind the QML page (ported from the
// Python play page tests and 0.7 behaviour).
#include <QFile>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "samn/curve_geometry.h"
#include <QRegularExpression>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <filesystem>
#include <memory>

#include "audio_level.h"
#include "curve_item.h"
#include "device_controller.h"
#include "heatmap_item.h"
#include "player_controller.h"
#include "theme.h"
#include "samn/marks.h"
#include "samn/script_finder.h"
#include "samn/playback_mapping.h"
#include "samn/range_edit.h"
#include "samn/contact_marks.h"
#include "samn/samn_file.h"
#include "samn/settings.h"

using samn::ui::AudioLevelDecoder;
using samn::ui::CurveItem;
using samn::ui::DeviceController;
using samn::ui::HeatmapItem;
using samn::ui::PlayerController;

namespace {

void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray zigzag(int n = 41, int stepMs = 250) {
    QByteArray s = "{\"actions\":[";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        s += QStringLiteral("{\"at\":%1,\"pos\":%2}").arg(i * stepMs).arg(i % 2 ? 0 : 100).toUtf8();
    }
    return s + "]}";
}

// The 0.7 comparison scripts (core playback_mapping_test): 13 points.
const char* kRefActions = R"([{"at":0,"pos":20},{"at":200,"pos":40},{"at":400,"pos":70},{"at":600,"pos":90},
  {"at":800,"pos":55},{"at":1000,"pos":20},{"at":1200,"pos":85},{"at":1400,"pos":30},{"at":1600,"pos":20},
  {"at":1800,"pos":60},{"at":2000,"pos":95},{"at":2200,"pos":10},{"at":2400,"pos":50}])";

QByteArray tjFunscript() {
    return QByteArray(R"({"actions":)") + kRefActions + R"(,"metadata":{"profile":"tj","device_recipe":{"sync":"suction_position",
      "min_suction":0.2,"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true,"contact_vibration_span":0.75}}})";
}

// A tj Emotion Script with the default strength presets.
void writeTjSamn(const QString& path, const std::string& active) {
    samn::Document d = samn::LoadFunscriptFile([&] {
        const QString tmp = path + ".src.funscript";
        writeFile(tmp, tjFunscript());
        return std::filesystem::u8path(tmp.toStdString());
    }());
    QFile::remove(path + ".src.funscript");
    d.strength_presets = samn::DefaultStrengthPresets();
    d.active_strength = active;
    samn::SaveDocument(std::filesystem::u8path(path.toStdString()), d);
}

// What the device gets at `ms` (a multiple of the tick) for these choices:
// the core pipeline, rounded like the lanes.
std::pair<int, int> expectedAt(const QString& path, const samn::PlaybackChoices& choices, int64_t ms) {
    samn::Document d = samn::IsSamnPath(path.toStdString()) ? samn::LoadDocument(std::filesystem::u8path(path.toStdString()))
                                                           : samn::LoadFunscriptFile(std::filesystem::u8path(path.toStdString()));
    const samn::MapOptions base = samn::MapOptionsFromScript(samn::Document(d).ToFunscript());
    const auto plan = samn::PlanPlayback(base, d.profile, samn::ChoicesForScript(choices, d.recipe.contact_vibration),
                                         d.ActivePreset());
    for (const auto& f : samn::PlaybackFrames(d.general, plan))
        if (f.at == ms) return {int(std::lround(f.vibration * 100)), int(std::lround(f.suction * 100))};
    return {-1, -1};
}

}  // namespace


// 16-bit stereo WAV: 1 s silence, then 1 s of a 440 Hz tone at half scale.
static QString writeToneWav(const QString& dir) {
    const int rate = 8000, ch = 2;
    QByteArray pcm;
    for (int i = 0; i < 2 * rate; ++i) {
        const qint16 v = i < rate ? 0 : qint16(16000 * std::sin(2 * M_PI * 440 * i / rate));
        for (int c = 0; c < ch; ++c) pcm.append(reinterpret_cast<const char*>(&v), 2);
    }
    QByteArray h;
    auto u32 = [&h](quint32 v) { h.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&h](quint16 v) { h.append(reinterpret_cast<const char*>(&v), 2); };
    h.append("RIFF"); u32(36 + pcm.size()); h.append("WAVEfmt "); u32(16); u16(1); u16(ch);
    u32(rate); u32(rate * ch * 2); u16(ch * 2); u16(16); h.append("data"); u32(pcm.size());
    const QString path = dir + "/tone.wav";
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(h + pcm);
    return path;
}

class PlayerControllerTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> cfg_;

private slots:
    void init() {
        // Every test gets its own settings folder (never the user's).
        cfg_ = std::make_unique<QTemporaryDir>();
        qputenv("SAMN_CONFIG_DIR", cfg_->path().toUtf8());
    }

    void scriptFindsVideoBeside() {
        QTemporaryDir d;
        writeFile(d.filePath("Clip.funscript"), zigzag());
        writeFile(d.filePath("Clip.mp4"), "");
        PlayerController c;
        QVERIFY(c.openPath(d.filePath("Clip.funscript")));
        QCOMPARE(c.pointCount(), 41);
        QCOMPARE(c.scriptDurationMs(), qint64(10000));
        QCOMPARE(c.videoName(), QStringLiteral("Clip.mp4"));
        QVERIFY(!c.noScriptForVideo());
    }

    void videoFindsScriptInFolder() {
        QTemporaryDir d;
        QDir(d.path()).mkdir("lib");
        QDir(d.path()).mkdir("videos");
        writeFile(d.filePath("lib/my clip.funscript"), zigzag());
        const QString vid = d.filePath("videos/My.Clip (2019) [1080p].mp4");
        writeFile(vid, "");
        PlayerController c;
        c.addScriptFolder(d.filePath("lib"));
        QVERIFY(c.openPath(QUrl::fromLocalFile(vid).toString()));
        QCOMPARE(c.scriptName(), QStringLiteral("my clip.funscript"));
        QCOMPARE(c.videoName(), QFileInfo(vid).fileName());
    }

    void videoWithoutScriptThenFolderAdded() {
        QTemporaryDir d;
        QDir(d.path()).mkdir("lib");
        const QString vid = d.filePath("Lonely.mp4");
        writeFile(vid, "");
        PlayerController c;
        QVERIFY(c.openPath(vid));
        QVERIFY(c.noScriptForVideo());
        c.addScriptFolder(d.filePath("lib"));
        QVERIFY(c.lastError().contains("in that folder either"));
        writeFile(d.filePath("lib/lonely.funscript"), zigzag());
        c.addScriptFolder(d.filePath("lib"));  // already listed: searches again
        QVERIFY(c.hasScript());
        QVERIFY(!c.noScriptForVideo());
        QCOMPARE(c.scriptFolders(), QStringList{d.filePath("lib")});
    }

    void variantsSwitchAndKeepVideo() {
        QTemporaryDir d;
        writeFile(d.filePath("Clip.mp4"), "");
        writeFile(d.filePath("Clip.funscript"), zigzag());
        writeFile(d.filePath("Clip (Soft).funscript"), zigzag(11));
        PlayerController c;
        QVERIFY(c.openPath(d.filePath("Clip.mp4")));
        QCOMPARE(c.variants().size(), 2);
        int soft = -1;
        for (int i = 0; i < c.variants().size(); ++i)
            if (c.variants()[i].toMap().value("label").toString() == "soft") soft = i;
        QVERIFY(soft >= 0);
        QVERIFY(c.chooseVariant(soft));
        QCOMPARE(c.scriptName(), QStringLiteral("Clip (Soft).funscript"));
        QCOMPARE(c.pointCount(), 11);
        QCOMPARE(c.videoName(), QStringLiteral("Clip.mp4"));
        QCOMPARE(c.variantIndex(), soft);
        // 0.7 #pb-variant names: "Main script", the label with a capital.
        QStringList shown;
        for (const auto& v : c.variants()) shown << v.toMap().value("display").toString();
        QVERIFY(shown.contains(QStringLiteral("Main script")));
        QVERIFY(shown.contains(QStringLiteral("Soft")));
    }

    void variantInScriptFolderSaysSo() {
        QTemporaryDir d;
        QDir().mkpath(d.filePath("videos"));
        QDir().mkpath(d.filePath("scripts"));
        writeFile(d.filePath("videos/Clip.mp4"), "");
        writeFile(d.filePath("videos/Clip.funscript"), zigzag());
        writeFile(d.filePath("scripts/Clip (Hard).funscript"), zigzag(11));
        PlayerController c;
        c.addScriptFolder(d.filePath("scripts"));
        QVERIFY(c.openPath(d.filePath("videos/Clip.mp4")));
        QStringList shown;
        for (const auto& v : c.variants()) shown << v.toMap().value("display").toString();
        QVERIFY2(shown.contains(QStringLiteral("Hard · script folder")), qPrintable(shown.join(", ")));
        const QString script = c.scriptPath();
        QCOMPARE(c.pointCount(), 41);
        c.setVideoPositionMs(1500);
        QCOMPARE(c.playheadMs(), qint64(1500));
        c.removeScriptFolder(d.filePath("scripts"));
        QStringList after;
        for (const auto& v : c.variants()) after << v.toMap().value("display").toString();
        QCOMPARE(after, QStringList{QStringLiteral("Main script")});
        QCOMPARE(c.scriptPath(), script);
        QCOMPARE(c.scriptName(), QStringLiteral("Clip.funscript"));
        QCOMPARE(c.pointCount(), 41);
        QCOMPARE(c.playheadMs(), qint64(1500));
        QVERIFY(c.hasScript());
        QCOMPARE(c.videoName(), QStringLiteral("Clip.mp4"));
        QCOMPARE(c.variantIndex(), 0);
    }

    void addingScriptFolderRefreshesVariantsWithoutReload() {
        QTemporaryDir d;
        QDir().mkpath(d.filePath("more"));
        writeFile(d.filePath("Clip.mp4"), "");
        writeFile(d.filePath("Clip.funscript"), zigzag());
        writeFile(d.filePath("more/Clip (Soft).funscript"), zigzag(11));
        PlayerController c;
        QVERIFY(c.openPath(d.filePath("Clip.mp4")));
        QCOMPARE(c.variants().size(), 1);
        QCOMPARE(c.scriptName(), QStringLiteral("Clip.funscript"));
        QCOMPARE(c.pointCount(), 41);
        const QString script = c.scriptPath();
        c.setVideoPositionMs(4000);
        QCOMPARE(c.playheadMs(), qint64(4000));
        QSignalSpy scriptSpy(&c, &PlayerController::scriptChanged);
        c.addScriptFolder(d.filePath("more"));
        QCOMPARE(scriptSpy.count(), 0);
        QCOMPARE(c.variants().size(), 2);
        QStringList shown;
        for (const auto& v : c.variants()) shown << v.toMap().value("display").toString();
        QVERIFY2(shown.contains(QStringLiteral("Main script")), qPrintable(shown.join(", ")));
        QVERIFY2(shown.contains(QStringLiteral("Soft · script folder")), qPrintable(shown.join(", ")));
        QCOMPARE(c.scriptPath(), script);
        QCOMPARE(c.scriptName(), QStringLiteral("Clip.funscript"));
        QCOMPARE(c.pointCount(), 41);
        QCOMPARE(c.playheadMs(), qint64(4000));
        QVERIFY(c.variantIndex() >= 0);
        QCOMPARE(c.variants().at(c.variantIndex()).toMap().value("path").toString(), script);
        c.removeScriptFolder(d.filePath("more"));
        QCOMPARE(c.variants().size(), 1);
        QCOMPARE(c.scriptPath(), script);
        QCOMPARE(c.playheadMs(), qint64(4000));
        QCOMPARE(c.variants().at(0).toMap().value("display").toString(), QStringLiteral("Main script"));
    }

    void autoCreateAndVideoFiles() {
        {
            PlayerController c;
            QVERIFY(!c.autoCreate());  // 0.7 default off
            QSignalSpy spy(&c, &PlayerController::autoCreateChanged);
            c.setAutoCreate(true);
            c.setAutoCreate(true);
            QCOMPARE(spy.count(), 1);
        }
        PlayerController again;
        QVERIFY(again.autoCreate());
        again.setAutoCreate(false);
        QVERIFY(again.isVideoFile(QStringLiteral("file:///x/Clip.M2TS")));
        QVERIFY(again.isVideoFile(QStringLiteral("/x/a.ogv")));
        QVERIFY(!again.isVideoFile(QStringLiteral("/x/a.funscript")));
        // Dialog filters share Theme.videoGlobs with core VIDEO_EXTS.
        const QString globs = samn::ui::Theme().videoGlobs();
        for (const auto& ext : samn::VIDEO_EXTS)
            QVERIFY2(globs.contains(QStringLiteral("*") + QString::fromStdString(ext)),
                     qPrintable(QStringLiteral("missing %1 in %2").arg(QString::fromStdString(ext), globs)));
        QVERIFY(again.isVideoFile(QStringLiteral("/x/clip.ts")));
    }

    void offsetSavedPerScriptAndPlayhead() {
        QTemporaryDir d;
        writeFile(d.filePath("A.funscript"), zigzag());
        writeFile(d.filePath("B.funscript"), zigzag());
        CurveItem curve;
        {
            PlayerController c;
            c.attachCurve(&curve);
            QVERIFY(c.openScript(d.filePath("A.funscript")));
            c.setOffsetMs(25000);  // clamped to ±10 s
            QCOMPARE(c.offsetMs(), 10000);
            c.setOffsetMs(-10001);
            QCOMPARE(c.offsetMs(), -10000);
            c.setOffsetMs(300);
            c.setVideoPositionMs(5000);
            QCOMPARE(c.playheadMs(), qint64(4700));
            QCOMPARE(curve.playheadMs(), qint64(4700));
            c.setVideoPositionMs(100);
            QCOMPARE(c.playheadMs(), qint64(0));  // never below 0
        }
        PlayerController c2;
        QVERIFY(c2.openScript(d.filePath("A.funscript")));
        QCOMPARE(c2.offsetMs(), 300);
        QVERIFY(c2.openScript(d.filePath("B.funscript")));
        QCOMPARE(c2.offsetMs(), 0);
    }

    void driveSourceAndEmotionScript() {
        QTemporaryDir d;
        const QString path = d.filePath("drive.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.playbackSource(), QString("recipe"));
        const QString samn = c.saveAsEmotionScript();
        QVERIFY2(!samn.isEmpty(), qPrintable(c.lastError()));
        QVERIFY(samn.endsWith(QStringLiteral(".samn")));
        QVERIFY(c.toolNote().contains("Emotion Script saved"));
        QVERIFY(c.setPlaybackSource("axes"));
        QCOMPARE(c.playbackSource(), QString("axes"));
        QVERIFY(c.toolNote().contains("Axes"));
        QVERIFY(!c.contactKnobsLive());  // axes mode: contact path off
        const samn::Document doc = samn::LoadDocument(std::filesystem::u8path(c.scriptPath().toStdString()));
        QCOMPARE(doc.playback_source, std::string(samn::PlaybackAxes));
        QVERIFY(doc.vibration.size() >= 2);
        QVERIFY(c.setPlaybackSource("recipe"));
        QCOMPARE(c.playbackSource(), QString("recipe"));
    }

    // 0.7 FPS-Snap: seeks land on frame boundaries; 0 = off; remembered.
    void fpsSnapSeeksToFrameBoundaries() {
        QTemporaryDir d;
        const QString path = d.filePath("snap.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.fpsSnap(), 0.0);
        c.setVideoPositionMs(666);
        QCOMPARE(c.playheadMs(), qint64(666));
        c.setFpsSnap(25);
        QCOMPARE(c.fpsSnap(), 25.0);
        c.setVideoPositionMs(666);
        // 25 fps → 40 ms frames; 666 → 680 (16.65 → 17 frames).
        QCOMPARE(c.playheadMs(), qint64(samn::SnapMs(666, 25)));
        c.setFpsSnap(0);
        c.setVideoPositionMs(666);
        QCOMPARE(c.playheadMs(), qint64(666));
        c.setFpsSnap(30);
        QCOMPARE(c.fpsSnap(), 30.0);
    }

    // 0.7 From audio BPM: needs a linked video; refuses cleanly without one.
    void guessBpmFromAudioNeedsVideo() {
        QTemporaryDir d;
        const QString path = d.filePath("bpm.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QVERIFY(!c.guessBpmFromAudio());
        QVERIFY(c.toolNote().contains("video") || c.lastError().contains("video"));
    }

    // 0.7 Auto sync: needs script + video; Apply sets offset from suggestion.
    void autoSyncNeedsScriptAndVideo() {
        QTemporaryDir d;
        const QString path = d.filePath("sync.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(!c.suggestAudioSync());
        QVERIFY(c.openScript(path));
        QVERIFY(!c.suggestAudioSync());
        QVERIFY(c.autoSyncNote().contains("video") || c.lastError().contains("video"));
        QVERIFY(!c.autoSyncCanApply());
        QVERIFY(!c.applyAudioSyncSuggestion());
        // Synthetic suggestion path: set offset manually after a fake high-confidence state
        // is not exposed; Apply with none stays false.
        QCOMPARE(c.autoSyncConfidence(), QStringLiteral("none"));
    }

    // 0.7 Sound Curve: loudness → Vibration lane in the companion .samn.
    void soundCurveWritesVibrationAndUndoes() {
        QTemporaryDir d;
        const QString path = d.filePath("sound.funscript");
        writeFile(path, zigzag(41, 250));
        PlayerController bare;
        QVERIFY(bare.openScript(path));
        QVERIFY(!bare.proposeSoundCurve());
        QVERIFY(bare.lastError().contains("video") || bare.toolNote().contains("video"));

        const QString wav = writeToneWav(d.path());
        PlayerController c;
        QVERIFY(c.openScript(path, wav));
        QSignalSpy ready(&c, &PlayerController::soundCurveReady);
        QVERIFY(c.proposeSoundCurve());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0 || c.toolNote().contains(QStringLiteral("short"))
                                     || c.toolNote().startsWith(QStringLiteral("Sound Curve:")),
                                 20000);
        if (ready.isEmpty()) QSKIP("No readable sound track in this environment");
        QVERIFY(c.pendingSoundCurvePoints() >= 2);
        QVERIFY(c.confirmSoundCurve());
        QVERIFY(c.toolNote().contains(QStringLiteral("Sound Curve")));
        QVERIFY(c.canUndoSoundCurve());
        const QString samnPath = d.filePath("sound.samn");
        QVERIFY(QFileInfo::exists(samnPath));
        const samn::Document doc = samn::LoadDocument(std::filesystem::u8path(samnPath.toStdString()));
        QVERIFY(doc.vibration.size() >= 2);
        QCOMPARE(QString::fromStdString(doc.playback_source), QStringLiteral("axes"));
        QVERIFY(c.undoSoundCurve());
        QVERIFY(!c.canUndoSoundCurve());
        const samn::Document after = samn::LoadDocument(std::filesystem::u8path(samnPath.toStdString()));
        QVERIFY(after.vibration.empty());
    }

    // 0.7 Scene markers strip (pb.scenes): off by default in the Player.
    void showScenesDefaultsOffAndRemembers() {
        PlayerController c;
        QVERIFY(!c.showScenes());
        QSignalSpy spy(&c, &PlayerController::viewOptionsChanged);
        c.setShowScenes(true);
        QVERIFY(c.showScenes());
        QCOMPARE(spy.count(), 1);
        c.setShowScenes(true);  // no-op
        QCOMPARE(spy.count(), 1);
        c.setShowScenes(false);
        QVERIFY(!c.showScenes());
        QCOMPARE(spy.count(), 2);
    }

    // 0.7 "Audio vibration" (playback.audio_vibration): live vib from loudness.
    void audioVibrationDefaultsOffAndSwapsOnPlay() {
        QTemporaryDir d;
        const QString path = d.filePath("avib.funscript");
        writeFile(path, zigzag(41, 100));
        const QString wav = writeToneWav(d.path());
        PlayerController c;
        QVERIFY(c.openScript(path, wav));
        QVERIFY(!c.audioVibration());
        QVERIFY(!c.audioVibrationReady());

        // Setting on without an envelope yet: Play keeps script vibration.
        c.setAudioVibration(true);
        QVERIFY(c.audioVibration());
        c.setVideoPositionMs(0);
        const int scriptVib = c.vibrationNow();
        const int scriptSuc = c.suctionNow();
        c.setPlayAudioEnvelopeForTest({});  // ignore warm race; force empty
        QVERIFY(!c.audioVibrationReady());
        c.setPlaying(true);
        QCOMPARE(c.vibrationNow(), scriptVib);
        QCOMPARE(c.suctionNow(), scriptSuc);
        c.setPlaying(false);

        // Inject silence→loud envelope — vibration follows; suction stays.
        std::vector<double> env(80, 0.0);
        for (size_t i = 40; i < env.size(); ++i) env[i] = 1.0;
        c.setPlayAudioEnvelopeForTest(env);
        QVERIFY(c.audioVibrationReady());
        c.setVideoPositionMs(0);
        c.setPlaying(true);
        QCOMPARE(c.suctionNow(), scriptSuc);
        QVERIFY(c.vibrationNow() <= 5);       // silent region
        c.setVideoPositionMs(2500);           // loud region (~50 * 50 ms)
        QVERIFY(c.vibrationNow() >= 90);
        c.setPlaying(false);
        c.setVideoPositionMs(0);
        QCOMPARE(c.vibrationNow(), scriptVib);  // script lanes restored

        PlayerController remembered;
        QVERIFY(remembered.audioVibration());  // persisted
        c.setAudioVibration(false);            // leave shared settings clean
    }

    // 0.7 "Mock (no device)" (playback.mock): Play auto-connects Mock.
    void playbackMockDefaultsOnAndConnectsOnPlay() {
        QTemporaryDir d;
        const QString path = d.filePath("mock.funscript");
        writeFile(path, zigzag(21, 100));
        DeviceController dev;
        PlayerController c;
        c.setDevice(&dev);
        QVERIFY(c.openScript(path));
        QVERIFY(c.playbackMock());
        QVERIFY(!dev.connected());
        c.setPlaying(true);
        QTRY_COMPARE_WITH_TIMEOUT(dev.state(), QStringLiteral("connected"), 5000);
        QVERIFY(dev.isMock());
        QVERIFY(c.output()->playing());
        c.setPlaying(false);
        c.setPlaybackMock(false);
        QVERIFY(!c.playbackMock());
        PlayerController remembered;
        QVERIFY(!remembered.playbackMock());
        // Off: Play does not auto-connect (disconnect first).
        dev.disconnectDevice();
        QTRY_COMPARE(dev.state(), QStringLiteral("disconnected"));
        PlayerController off;
        off.setDevice(&dev);
        QVERIFY(off.openScript(path));
        QVERIFY(!off.playbackMock());
        off.setPlaying(true);
        QVERIFY(!dev.connected());
        QVERIFY(!dev.connecting());
        off.setPlaying(false);
        off.setPlaybackMock(true);  // leave shared settings at the 0.7 default
    }

    // 0.7 "Video play starts device" (playback.video_play_autostart).
    void videoPlayAutostartDefaultsOnAndGatesDevice() {
        QTemporaryDir d;
        const QString path = d.filePath("auto.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QVERIFY(c.videoPlayAutostart());
        c.setMediaPlaying(true);
        QVERIFY(c.output()->playing());
        c.setMediaPlaying(false);
        QVERIFY(!c.output()->playing());
        c.setVideoPlayAutostart(false);
        QVERIFY(!c.videoPlayAutostart());
        c.setMediaPlaying(true);
        QVERIFY(!c.output()->playing());  // film without the toy
        c.setPlaying(true);              // explicit script Play still drives
        QVERIFY(c.output()->playing());
        c.setPlaying(false);
        PlayerController again;
        QVERIFY(!again.videoPlayAutostart());  // remembered in this config dir
    }

    // 0.7 CreateScriptAxis / "+ Axis…": centred companion file, never overwrite.
    void createCompanionAxisAddsLane() {
        QTemporaryDir d;
        const QString path = d.filePath("multi.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.missingAxisChoices().size(), 5);
        QVERIFY(!c.axisLanes().contains(QStringLiteral("surge")));
        const QString created = c.createCompanionAxis(QStringLiteral("surge"));
        QVERIFY2(!created.isEmpty(), qPrintable(c.lastError()));
        QVERIFY(created.endsWith(QStringLiteral("multi.surge.funscript")));
        QVERIFY(c.axisLanes().contains(QStringLiteral("surge")));
        QCOMPARE(c.activeLane(), QStringLiteral("surge"));
        QCOMPARE(c.missingAxisChoices().size(), 4);
        const auto actions = samn::LoadFunscript(std::filesystem::u8path(created.toStdString())).actions;
        QCOMPARE(actions.size(), size_t(2));
        QCOMPARE(actions[0].pos, 50);
        QCOMPARE(actions[1].pos, 50);
        QCOMPARE(actions[1].at, int64_t(2000));
        QVERIFY(c.createCompanionAxis(QStringLiteral("surge")).isEmpty());  // no overwrite
        QVERIFY(c.lastError().contains("already exists") || c.toolNote().contains("Axis"));
    }

    void bakeOffsetWritesItIntoTheScript() {
        QTemporaryDir d;
        const QString path = d.filePath("Bake.funscript");
        writeFile(path, R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100},{"at":2000,"pos":0}],)"
                        R"("metadata":{"bookmarks":[{"name":"b","time":1500}],)"
                        R"("chapters":[{"name":"c","startTime":"00:00:00.000","endTime":"00:00:01.500"}]}})");
        PlayerController c;
        QVERIFY(c.openScript(path));
        QVERIFY(!c.bakeOffset());  // nothing to bake at 0
        c.setOffsetMs(-300);
        QVERIFY(c.bakeOffset());
        QCOMPARE(c.offsetMs(), 0);
        QVERIFY(c.toolNote().contains("1 point"));  // the point at 0 went before the start
        const auto actions = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(actions.size(), size_t(2));
        QCOMPARE(actions[0].at, int64_t(700));
        QCOMPARE(samn::LoadBookmarks(std::filesystem::u8path(path.toStdString()))[0].time_ms, int64_t(1200));
        const auto chapters = samn::LoadChapters(std::filesystem::u8path(path.toStdString()));
        QCOMPARE(chapters[0].start_ms, int64_t(0));  // cut at 0
        QCOMPARE(chapters[0].end_ms, int64_t(1200));
        // Reopened: the offset is 0 and stays 0.
        PlayerController again;
        QVERIFY(again.openScript(path));
        QCOMPARE(again.offsetMs(), 0);

        // A .samn moves together with the .funscript beside it.
        const QString samnPath = d.filePath("Bake.samn");
        samn::Document doc;
        doc.version = samn::SamnCurrentVersion;
        doc.kind = samn::SamnKind;
        doc.general = {{0, 0}, {1000, 100}, {2000, 0}};
        doc.vibration = {{0, 50}, {2000, 50}};
        samn::SaveDocument(std::filesystem::u8path(samnPath.toStdString()), doc);
        PlayerController s;
        QVERIFY(s.openScript(samnPath));
        s.setOffsetMs(500);
        QVERIFY(s.bakeOffset());
        const samn::Document back = samn::LoadDocument(std::filesystem::u8path(samnPath.toStdString()));
        QCOMPARE(back.general.front().at, int64_t(500));
        QCOMPARE(back.vibration.back().at, int64_t(2500));
        QCOMPARE(samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions.front().at, int64_t(1200));
        QVERIFY(s.toolNote().contains("500 ms"));

        // Every other script time moves too: O-markers, the loop marker, the
        // companion axis files, tracking gaps, scene and phase ranges, the tip
        // path, in the .funscript's metadata and in the .samn. A span that
        // ends before the start is dropped, one that only starts there is cut.
        const auto fs = [](const QString& p) { return std::filesystem::u8path(p.toStdString()); };
        const QString all = d.filePath("All.funscript");
        writeFile(all, R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100},{"at":2000,"pos":0}],"inverted":true,)"
                       R"("metadata":{"creator":"keep me","bookmarks":[{"name":"b","time":1500}],)"
                       R"("oMarkers":[{"startMs":100,"endMs":250,"kind":"primary","intensity":0.5},)"
                       R"({"startMs":200,"endMs":600,"kind":"secondary","intensity":0.4},)"
                       R"({"startMs":1000,"endMs":1600,"kind":"primary","intensity":1}],)"
                       R"("tracking_gaps":[{"start_ms":100,"end_ms":200},{"start_ms":1100,"end_ms":1300}],)"
                       R"("device_recipe":{"sync":"independent","scene_ranges":[{"start_ms":0,"end_ms":1000,"feel":"oral"}],)"
                       R"("phase_ranges":[{"start_ms":1000,"end_ms":2000,"phase":"climax"}]},)"
                       R"("trajectory":{"width":640,"height":360,"tip":[{"atMs":100,"x":1,"y":2},{"atMs":1100,"x":3,"y":4}]},)"
                       R"("samn_axes":{"vibration":[{"at":0,"pos":10},{"at":2000,"pos":20}]}}})");
        writeFile(d.filePath("All.surge.funscript"), R"({"actions":[{"at":0,"pos":50},{"at":1000,"pos":20},{"at":2000,"pos":80}]})");
        const QString allSamn = d.filePath("All.samn");
        {
            samn::Document a;
            a.version = samn::SamnCurrentVersion;
            a.kind = samn::SamnKind;
            a.general = {{0, 0}, {1000, 100}, {2000, 0}};
            a.vibration = {{0, 50}, {2000, 50}};
            a.bookmarks = {{"b", 1500}};
            a.chapters = {{"c", 0, 1500}};
            a.o_markers = {{1000, 1600, "primary", 1.0}, {100, 250, "secondary", 0.5}};
            a.tracking_gaps = {{1100, 1300}};
            a.recipe.scene_ranges = {{0, 1000, "oral"}};
            a.recipe.phase_ranges = {{1000, 2000, "climax"}};
            a.trajectory = samn::TrajectoryData{640, 360, {{100, 1, 2}, {1100, 3, 4}}, {}};
            samn::SaveDocument(fs(allSamn), a);
        }
        samn::SaveMarker(fs(allSamn), {1000, 2000});
        PlayerController m;
        QVERIFY(m.openScript(all));
        QCOMPARE(m.scriptPath(), allSamn);
        m.setOffsetMs(-300);
        QVERIFY(m.bakeOffset());
        const samn::Document moved = samn::LoadDocument(fs(allSamn));
        QCOMPARE(moved.general.front().at, int64_t(700));
        QCOMPARE(moved.o_markers.size(), size_t(1));  // [100, 250] lies before the start
        QCOMPARE(moved.o_markers[0].start_ms, int64_t(700));
        QCOMPARE(moved.o_markers[0].end_ms, int64_t(1300));
        QCOMPARE(moved.tracking_gaps.size(), size_t(1));
        QCOMPARE(moved.tracking_gaps[0].start_ms, int64_t(800));
        QCOMPARE(moved.tracking_gaps[0].end_ms, int64_t(1000));
        QCOMPARE(moved.recipe.scene_ranges[0].start_ms, int64_t(0));  // cut at 0
        QCOMPARE(moved.recipe.scene_ranges[0].end_ms, int64_t(700));
        QCOMPARE(moved.recipe.phase_ranges[0].start_ms, int64_t(700));
        QCOMPARE(moved.recipe.phase_ranges[0].end_ms, int64_t(1700));
        QVERIFY(moved.trajectory);
        QCOMPARE(moved.trajectory->tip.size(), size_t(1));
        QCOMPARE(moved.trajectory->tip[0].at_ms, int64_t(800));
        QCOMPARE(moved.bookmarks[0].time_ms, int64_t(1200));
        QCOMPARE(moved.chapters[0].end_ms, int64_t(1200));
        // The loop marker and the axis file.
        const auto loop = samn::LoadMarker(fs(allSamn));
        QVERIFY(loop);
        QCOMPARE(loop->start_ms, int64_t(700));
        QCOMPARE(loop->end_ms, int64_t(1700));
        QVERIFY(m.hasMarker());
        const auto surge = samn::LoadFunscript(fs(d.filePath("All.surge.funscript"))).actions;
        QCOMPARE(surge.size(), size_t(2));
        QCOMPARE(surge[0].at, int64_t(700));
        QCOMPARE(surge[0].pos, 20);
        // The .funscript beside it: its own marks and metadata, its flag kept.
        const samn::FunscriptView view = samn::LoadFunscript(fs(all));
        QVERIFY(view.inverted);
        QCOMPARE(view.actions.front().at, int64_t(700));
        QCOMPARE(view.metadata.creator, std::string("keep me"));
        const auto oms = samn::LoadOMarkers(fs(all));
        QCOMPARE(oms.size(), size_t(2));
        QCOMPARE(oms[0].start_ms, int64_t(0));  // [200, 600] cut at 0
        QCOMPARE(oms[0].end_ms, int64_t(300));
        QCOMPARE(oms[1].start_ms, int64_t(700));
        QCOMPARE(view.metadata.tracking_gaps.size(), size_t(1));
        QCOMPARE(view.metadata.tracking_gaps[0].start_ms, int64_t(800));
        QVERIFY(view.metadata.device_recipe);
        QCOMPARE(view.metadata.device_recipe->scene_ranges[0].end_ms, int64_t(700));
        QCOMPARE(view.metadata.device_recipe->phase_ranges[0].start_ms, int64_t(700));
        QCOMPARE(QString::fromStdString(view.metadata.device_recipe->sync), QStringLiteral("independent"));
        QVERIFY(view.metadata.trajectory);
        QCOMPARE(view.metadata.trajectory->tip.size(), size_t(1));
        QCOMPARE(view.metadata.trajectory->tip[0].at_ms, int64_t(800));
        QVERIFY(view.metadata.samn_axes);
        QCOMPARE(view.metadata.samn_axes->vibration.size(), size_t(1));
        QCOMPARE(view.metadata.samn_axes->vibration[0].at, int64_t(1700));
        QCOMPARE(samn::LoadBookmarks(fs(all))[0].time_ms, int64_t(1200));

    }

    // Everything is read and moved before anything is written: a broken
    // .samn leaves every file as it was and the offset set.
    void aFailedBakeChangesNoFile() {
        QTemporaryDir d;
        const auto fs = [](const QString& p) { return std::filesystem::u8path(p.toStdString()); };
        const QString path = d.filePath("Fail.funscript"), samnPath = d.filePath("Fail.samn"),
                      surgePath = d.filePath("Fail.surge.funscript");
        writeFile(path, R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100},{"at":2000,"pos":0}],)"
                        R"("metadata":{"bookmarks":[{"name":"b","time":1500}]}})");
        writeFile(surgePath, R"({"actions":[{"at":0,"pos":50},{"at":2000,"pos":80}]})");
        samn::Document doc;
        doc.version = samn::SamnCurrentVersion;
        doc.kind = samn::SamnKind;
        doc.general = {{0, 0}, {1000, 100}, {2000, 0}};
        samn::SaveDocument(fs(samnPath), doc);
        samn::SaveMarker(fs(samnPath), {1000, 2000});
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.scriptPath(), samnPath);
        c.setOffsetMs(250);
        const auto bytes = [](const QString& p) {
            QFile file(p);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const QByteArray funscriptBefore = bytes(path), surgeBefore = bytes(surgePath),
                         markerBefore = bytes(samnPath + ".marker.json");
        writeFile(samnPath, "{broken");
        QVERIFY(!c.bakeOffset());
        QCOMPARE(c.offsetMs(), 250);
        QCOMPARE(bytes(path), funscriptBefore);
        QCOMPARE(bytes(surgePath), surgeBefore);
        QCOMPARE(bytes(samnPath + ".marker.json"), markerBefore);
        QCOMPARE(bytes(samnPath), QByteArray("{broken"));
    }

    void optimizeForFillsHealsAndWritesTheNeo2Feel() {
        QTemporaryDir d;
        const QString path = d.filePath("opt.funscript");
        {
            QByteArray body = R"({"version":"1.0","actions":[)";
            for (int t = 0; t <= 1000; t += 100)
                body += QStringLiteral("{\"at\":%1,\"pos\":%2},").arg(t).arg(t / 100 % 2 ? 90 : 10).toUtf8();
            body += R"({"at":1500,"pos":99},{"at":4000,"pos":90},{"at":4100,"pos":10},{"at":4200,"pos":90}],)"
                    R"("metadata":{"tracking_gaps":[{"start_ms":1100,"end_ms":1900}]}})";
            writeFile(path, body);
        }
        PlayerController c;
        QVERIFY(c.openScript(path));
        const int before = c.pointCount();
        QVERIFY(c.optimizeFor("nonsense").isEmpty());
        const QString msg = c.optimizeFor("general");
        QVERIFY2(msg.startsWith("Ready for any device"), qPrintable(msg));
        QVERIFY2(msg.contains("healed 1 tracking gap"), qPrintable(msg));
        QVERIFY2(msg.contains("filled"), qPrintable(msg));
        QVERIFY(c.pointCount() > before);
        QCOMPARE(c.toolNote(), msg);
        const auto stroke = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(int(stroke.size()), c.pointCount());
        for (const auto& a : stroke) QVERIFY(!(a.at == 1500 && a.pos == 99));

        const QString neo = c.optimizeFor("neo2");
        QVERIFY2(neo.startsWith("Neo 2 ready") && neo.contains("vibration and suction written"), qPrintable(neo));
        QVERIFY(c.scriptPath().endsWith(QStringLiteral(".samn")));
        const samn::Document doc = samn::LoadDocument(std::filesystem::u8path(c.scriptPath().toStdString()));
        QVERIFY(doc.vibration.size() >= 2);
        QVERIFY(doc.suction.size() >= 2);
        QVERIFY(doc.recipe.contact_vibration);
    }

    void axesFromStrokeWritesNeoLanes() {
        QTemporaryDir d;
        const QString path = d.filePath("axes.funscript");
        writeFile(path, zigzag(21, 100));
        PlayerController c;
        QVERIFY(c.openScript(path));
        QVERIFY(c.axesFromStroke());
        QVERIFY(c.scriptPath().endsWith(QStringLiteral(".samn")));
        QVERIFY(c.toolNote().contains("Feel channels baked"));
        const samn::Document doc = samn::LoadDocument(std::filesystem::u8path(c.scriptPath().toStdString()));
        QVERIFY(doc.vibration.size() >= 2);
        QVERIFY(doc.suction.size() >= 2);

        // Share for other apps: community .funscript beside the .samn.
        const QString shared = c.exportFunscript();
        QVERIFY2(!shared.isEmpty(), qPrintable(c.lastError()));
        QCOMPARE(shared, d.filePath("axes.funscript"));
        QVERIFY(c.toolNote().contains("Shared for other apps"));
        const auto view = samn::LoadFunscript(std::filesystem::u8path(shared.toStdString()));
        QVERIFY(view.actions.size() >= 2);
        QVERIFY(!view.inverted);
    }

    void loopOverMarkedRange() {
        QTemporaryDir d;
        writeFile(d.filePath("A.funscript"), zigzag());
        {
            PlayerController c;
            QVERIFY(c.openScript(d.filePath("A.funscript")));
            QVERIFY(!c.hasMarker());
            QCOMPARE(c.markerLabel(), QString("(no selection)"));
            QCOMPARE(c.loopJumpFor(5000), qint64(-1));
            // A / B at the playhead mark the range; the loop only runs when on.
            c.setLoopStart(1000);
            c.setLoopEnd(3000);
            QVERIFY(c.hasMarker());
            QCOMPARE(c.markerLabel(), QString("Marked: 1.0s - 3.0s"));
            QVERIFY(!c.loopActive());
            QCOMPARE(c.loopJumpFor(3000), qint64(-1));
            c.setLoopEnabled(true);
            QVERIFY(c.loopActive());
            QCOMPARE(c.loopJumpFor(2999), qint64(-1));
            QCOMPARE(c.loopJumpFor(3000), qint64(1000));
            // The range is script time: an offset moves the jump in video time.
            c.setOffsetMs(500);
            QCOMPARE(c.loopJumpFor(3499), qint64(-1));
            QCOMPARE(c.loopJumpFor(3500), qint64(1500));
            c.setOffsetMs(0);
            // Saved beside the script as 0.7 does.
            QVERIFY(QFile::exists(d.filePath("A.funscript.marker.json")));
        }
        {
            PlayerController c;
            QVERIFY(c.openScript(d.filePath("A.funscript")));
            QCOMPARE(c.markerStartMs(), 1000.0);
            QCOMPARE(c.markerEndMs(), 3000.0);
            // A heatmap click (< 150 ms) seeks, a drag marks.
            QCOMPARE(c.finishHeatmapDrag(4000, 4100), qint64(4000));
            QCOMPARE(c.markerEndMs(), 3000.0);
            QCOMPARE(c.finishHeatmapDrag(6000, 4000), qint64(-1));
            QCOMPARE(c.markerStartMs(), 4000.0);
            QCOMPARE(c.markerEndMs(), 6000.0);
            c.zoomToSelection();
            QCOMPARE(c.viewStartMs(), 3800.0);
            QCOMPARE(c.viewEndMs(), 6200.0);
            c.clearMarker();
            QVERIFY(!c.hasMarker());
            QVERIFY(!QFile::exists(d.filePath("A.funscript.marker.json")));
            // Keys 1-9: n x 10 % of the timeline.
            QCOMPARE(c.jumpTenthVideoMs(5), qint64(c.scriptDurationMs() / 2));
        }
    }

    void editMarks() {
        QTemporaryDir d;
        writeFile(d.filePath("M.funscript"), zigzag());
        {
            PlayerController c;
            QVERIFY(c.openScript(d.filePath("M.funscript")));
            QSignalSpy marks(&c, &PlayerController::marksChanged);
            // Bookmarks at the playhead (video time -> script time).
            c.setOffsetMs(200);
            QVERIFY(c.addBookmark("", 2700));
            QVERIFY(c.addBookmark("  Late one  ", 5200));
            QCOMPARE(c.bookmarks().size(), 2);
            QCOMPARE(c.bookmarks()[0].toMap()["label"].toString(), QString("Bookmark · 2.5s"));
            QCOMPARE(c.bookmarks()[1].toMap()["name"].toString(), QString("Late one"));
            QCOMPARE(c.bookmarkVideoMs(0), qint64(2700));
            QVERIFY(c.renameBookmark(0, "First"));
            QVERIFY(c.removeBookmark(1));
            c.setOffsetMs(0);
            // Chapters and O-markers need a marked range.
            QVERIFY(!c.addChapterFromSelection("x"));
            QVERIFY(!c.lastError().isEmpty());
            c.setMarker(1000, 4000);
            QVERIFY(c.addChapterFromSelection("Build-up"));
            QCOMPARE(c.chapters()[0].toMap()["label"].toString(), QString("Build-up · 1.0s - 4.0s"));
            QVERIFY(c.addOMarkerFromSelection("primary"));
            c.setOMarkerIntensity(0.25);
            c.setMarker(5000, 6000);
            QVERIFY(c.addOMarkerFromSelection("secondary"));
            QCOMPARE(c.oMarkers().size(), 2);
            QCOMPARE(c.oMarkers()[1].toMap()["label"].toString(), QString("Secondary: 5.0s - 6.0s · 25%"));
            // Key O: 4 s primary from the playhead.
            QVERIFY(c.addOMarkerAt(7000));
            QCOMPARE(c.oMarkers().size(), 3);
            QCOMPARE(c.oMarkers()[2].toMap()["label"].toString(), QString("Primary: 7.0s - 11.0s"));
            QVERIFY(c.removeOMarker(2));
            QVERIFY(marks.count() >= 7);
        }
        {
            // Everything was saved in the script.
            PlayerController c;
            QVERIFY(c.openScript(d.filePath("M.funscript")));
            QCOMPARE(c.bookmarks().size(), 1);
            QCOMPARE(c.bookmarks()[0].toMap()["name"].toString(), QString("First"));
            QCOMPARE(c.chapters().size(), 1);
            QCOMPARE(c.oMarkers().size(), 2);
            QCOMPARE(c.oMarkerIntensity(), 0.25);
            QVERIFY(c.removeOMarker(0));
            QVERIFY(c.removeChapter(0));
            // A failed save keeps the old list and says why.
            QFile::remove(d.filePath("M.funscript"));
            QVERIFY(!c.addBookmark("gone", 100));
            QCOMPARE(c.bookmarks().size(), 1);
            QVERIFY(c.lastError().startsWith("Could not save"));
        }
    }

    void bpmGrid() {
        PlayerController c;
        c.setBpm(10);  // out of 20-240
        QCOMPARE(c.bpm(), 0.0);
        c.setBpm(120);
        QCOMPARE(c.beatPeriodMs(), 0.0);  // grid off
        c.setBpmGrid(true);
        QCOMPARE(c.beatPeriodMs(), 500.0);
        QCOMPARE(c.effectiveBpm(), 120.0);
    }

    void bpmFromStrokeWhenFieldBlank() {
        QTemporaryDir d;
        // Peaks every 500 ms → 120 BPM.
        QByteArray body = "{\"actions\":[";
        for (int i = 0; i < 12; ++i) {
            if (i) body += ",";
            const int at = i * 250;
            body += QStringLiteral("{\"at\":%1,\"pos\":%2}").arg(at).arg(i % 2 ? 90 : 10).toUtf8();
        }
        body += "]}";
        writeFile(d.filePath("beat.funscript"), body);
        PlayerController c;
        QVERIFY(c.openScript(d.filePath("beat.funscript")));
        QCOMPARE(c.bpm(), 0.0);
        QVERIFY(c.effectiveBpm() >= 20.0);
        c.setBpmGrid(true);
        QVERIFY(c.beatPeriodMs() > 0);
        // Call first: QCOMPARE arg order is unspecified, so bpm() must not
        // be sampled before guess fills it.
        const double guessed = c.guessBpmFromStroke();
        QCOMPARE(guessed, 120.0);
        QCOMPARE(c.bpm(), guessed);
        QVERIFY(c.bpmGrid());
        QVERIFY(c.toolNote().contains("BPM from the stroke"));

        // Grid preference is remembered.
        PlayerController again;
        QVERIFY(again.bpmGrid());
    }

    void curveEditOnHeatmapSelection() {
        QTemporaryDir d;
        const QString path = d.filePath("edit.funscript");
        // Fast 0↔100 jumps (intensity 5000) so Cap at 150 stretches time.
        writeFile(path, R"({"actions":[{"at":0,"pos":0},{"at":10,"pos":100},{"at":20,"pos":0},{"at":30,"pos":100},{"at":40,"pos":0}]})");
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.capIntensity(), 400.0);
        QVERIFY(!c.capSelection());
        QVERIFY(c.lastError().contains("Mark a range"));
        c.setMarker(0, 40);
        c.setCapIntensity(150);
        QCOMPARE(c.capIntensity(), 150.0);
        QVERIFY(c.capSelection());
        QVERIFY(c.toolNote().contains("Cap speed"));
        const auto capped = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QVERIFY(capped.back().at > 40);
        QCOMPARE(c.pointCount(), 5);

        PlayerController s;
        QVERIFY(s.openScript(path));
        // Scale around mid points (still at original pos 100 / 0 after cap).
        s.setMarker(capped[1].at - 1, capped[2].at + 1);
        s.setScaleSoftEdges(false);
        QVERIFY(s.scaleSelection(0.5));
        const auto scaled = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(scaled[1].pos, 75);  // 100 -> 50 + 50*0.5
        QCOMPARE(scaled[2].pos, 25);  // 0 -> 50 + (0-50)*0.5

        PlayerController del;
        QVERIFY(del.openScript(path));
        const int before = del.pointCount();
        del.setMarker(scaled[1].at - 1, scaled[2].at + 1);
        QVERIFY(del.deleteSelection());
        QVERIFY(del.pointCount() < before);
        QVERIFY(!del.hasMarker());
    }

    void invertOnHeatmapSelectionAndAll() {
        QTemporaryDir d;
        const QString path = d.filePath("inv.funscript");
        writeFile(path, R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":90},{"at":2000,"pos":20},{"at":3000,"pos":80}]})");
        PlayerController range;
        QVERIFY(range.openScript(path));
        range.setMarker(500, 1500);
        QVERIFY(range.invertSelection());
        QVERIFY(range.toolNote().contains("Invert range"));
        const auto mid = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(mid[0].pos, 10);
        QCOMPARE(mid[1].pos, 10);  // 90 inverted
        QCOMPARE(mid[2].pos, 20);
        QCOMPARE(mid[3].pos, 80);
        range.clearMarker();  // drop the sidecar so Invert all is exercised without a mark

        PlayerController all;
        QVERIFY(all.openScript(path));
        QVERIFY(!all.hasMarker());
        QVERIFY(all.invertAll());
        QVERIFY(all.toolNote().contains("Invert curve"));
        const auto flipped = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(flipped[0].pos, 90);
        QCOMPARE(flipped[1].pos, 90);
        QCOMPARE(flipped[2].pos, 80);
        QCOMPARE(flipped[3].pos, 20);
    }

    // Cap / Scale / Delete / Invert share the Generator's EditSession: one
    // step before the write, undo and redo round-trip the file, and opening
    // another script drops the history.
    void curveEditUndoRedoRestoresFileAndClearsOnOpen() {
        QTemporaryDir d;
        const QString path = d.filePath("edit.funscript");
        writeFile(path, R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":90},{"at":2000,"pos":20},{"at":3000,"pos":80}]})");
        PlayerController c;
        QVERIFY(c.openScript(path));
        QCOMPARE(c.pointCount(), 4);
        QVERIFY(!c.canUndo());
        QVERIFY(!c.canRedo());
        QVERIFY(!c.undo());
        QVERIFY(!c.capSelection());
        QVERIFY(!c.canUndo());

        c.setMarker(500, 2500);
        QSignalSpy history(&c, &PlayerController::historyChanged);
        QVERIFY(c.deleteSelection());
        QVERIFY(history.count() >= 1);
        QCOMPARE(c.pointCount(), 2);
        QVERIFY(c.canUndo());
        QVERIFY(!c.canRedo());
        QVERIFY(c.undoText().contains(QStringLiteral("Delete range")));

        const auto edited = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(int(edited.size()), 2);
        QCOMPARE(edited[0].at, int64_t(0));
        QCOMPARE(edited[0].pos, 10);
        QCOMPARE(edited[1].at, int64_t(3000));
        QCOMPARE(edited[1].pos, 80);

        QVERIFY(c.undo());
        QCOMPARE(c.pointCount(), 4);
        QVERIFY(!c.canUndo());
        QVERIFY(c.canRedo());
        QVERIFY(c.redoText().contains(QStringLiteral("Delete range")));
        const auto restored = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(int(restored.size()), 4);
        QCOMPARE(restored[0].pos, 10);
        QCOMPARE(restored[1].at, int64_t(1000));
        QCOMPARE(restored[1].pos, 90);
        QCOMPARE(restored[2].at, int64_t(2000));
        QCOMPARE(restored[2].pos, 20);
        QCOMPARE(restored[3].at, int64_t(3000));
        QCOMPARE(restored[3].pos, 80);

        QVERIFY(c.redo());
        QCOMPARE(c.pointCount(), 2);
        QVERIFY(c.canUndo());
        QVERIFY(!c.canRedo());
        const auto again = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(int(again.size()), 2);
        QCOMPARE(again[0].pos, 10);
        QCOMPARE(again[1].at, int64_t(3000));
        QCOMPARE(again[1].pos, 80);

        const QString other = d.filePath("other.funscript");
        writeFile(other, zigzag());
        QVERIFY(c.openScript(other));
        QVERIFY(!c.canUndo());
        QVERIFY(!c.canRedo());
        QCOMPARE(c.pointCount(), 41);
        QVERIFY(!c.undo());
        QCOMPARE(c.pointCount(), 41);
        const auto kept = samn::LoadFunscript(std::filesystem::u8path(path.toStdString())).actions;
        QCOMPARE(int(kept.size()), 2);
        QCOMPARE(kept[1].at, int64_t(3000));
        QCOMPARE(kept[1].pos, 80);
    }

    void speedHighlights() {
        QTemporaryDir d;
        writeFile(d.filePath("S.funscript"), zigzag());
        CurveItem stroke, vib;
        vib.setProperty("lane", "vibration");
        {
            PlayerController c;
            c.attachCurve(&stroke);
            c.attachCurve(&vib);
            QVERIFY(c.openScript(d.filePath("S.funscript")));
            QVERIFY(c.speedHighlights());  // 0.7 default: on, 400
            QCOMPARE(c.speedHighlightThresh(), 400.0);
            QCOMPARE(stroke.speedLimit(), 400.0);
            QCOMPARE(vib.speedLimit(), 0.0);  // only the main curve
            c.setSpeedHighlightThresh(10);    // clamped to 50-2000
            QCOMPARE(c.speedHighlightThresh(), 50.0);
            c.setSpeedHighlightThresh(-1);    // not > 0 -> 400
            QCOMPARE(c.speedHighlightThresh(), 400.0);
            c.setSpeedHighlightThresh(550);
            c.setSpeedHighlights(false);
            QCOMPARE(stroke.speedLimit(), 0.0);
        }
        PlayerController c2;  // saved in the settings
        QVERIFY(c2.openScript(d.filePath("S.funscript")));
        QVERIFY(!c2.speedHighlights());
        QCOMPARE(c2.speedHighlightThresh(), 550.0);
    }

    void playlist() {
        QTemporaryDir d;
        for (const char* n : {"A", "B", "C"}) writeFile(d.filePath(QString(n) + ".funscript"), zigzag());
        PlayerController c;
        // Open… makes a one-item list: no list shown.
        QVERIFY(c.openPath(d.filePath("A.funscript")));
        QVERIFY(!c.playlistVisible());
        // Add to list: the loaded script comes first.
        QVERIFY(c.addToPlaylist(d.filePath("B.funscript")));
        QVERIFY(!c.addToPlaylist(d.filePath("B.funscript")));
        QCOMPARE(c.lastError(), QStringLiteral("Already in list: B"));
        QVERIFY(c.playlistVisible());
        QCOMPARE(c.playlist().size(), 2);
        QCOMPARE(c.playlist()[0].toMap().value("name").toString(), QStringLiteral("A"));
        // Next loads the next script.
        QVERIFY(c.playlistHasNext());
        QVERIFY(c.playlistNext());
        QCOMPARE(c.scriptPath(), d.filePath("B.funscript"));
        QVERIFY(!c.playlistHasNext());
        // Removing the active one loads the new active one.
        QVERIFY(c.removeFromPlaylist(1));
        QCOMPARE(c.scriptPath(), d.filePath("A.funscript"));
        // A drop of several scripts becomes the list; the first opens.
        QVERIFY(c.openPaths({QUrl::fromLocalFile(d.filePath("C.funscript")).toString(), d.filePath("A.funscript")}));
        QCOMPARE(c.scriptPath(), d.filePath("C.funscript"));
        QCOMPARE(c.playlist().size(), 2);
        // End of the video: Auto-advance (default on), then Repeat.
        QVERIFY(c.playlistAuto());
        QVERIFY(c.playlistFinished());
        QCOMPARE(c.scriptPath(), d.filePath("A.funscript"));
        QVERIFY(!c.playlistFinished());  // at the end, no Repeat
        c.setPlaylistRepeat(true);
        QVERIFY(c.playlistFinished());
        QCOMPARE(c.playlistIndex(), 0);
        c.setPlaylistAuto(false);
        QVERIFY(!c.playlistFinished());
        // Pick and the options survive a restart.
        QVERIFY(c.pickPlaylist(1));
        QCOMPARE(c.scriptPath(), d.filePath("A.funscript"));
        PlayerController again;
        QVERIFY(again.playlistRepeat());
        QVERIFY(!again.playlistAuto());
    }

    void projectAndHeatmapExport() {
        QTemporaryDir d;
        QByteArray js = zigzag();
        js.chop(1);
        js += ",\"metadata\":{\"chapters\":[{\"name\":\"A\",\"startTime\":2000,\"endTime\":0}]}}";
        writeFile(d.filePath("P.funscript"), js);
        writeFile(d.filePath("P.mp4"), "not a real video");
        QString projectPath;
        {
            PlayerController c;
            QVERIFY(c.openScript(d.filePath("P.funscript")));
            // Heatmap PNG beside the script, 960 x 56 (0.7).
            const QString png = c.exportHeatmapPng();
            QCOMPARE(png, d.filePath("P.heatmap.png"));
            const QImage img(png);
            QCOMPARE(img.size(), QSize(960, 56));
            // Project with video, offset, seek and the marked range.
            QVERIFY(c.attachVideo(d.filePath("P.mp4")));
            c.setOffsetMs(250);
            c.setMarker(1000, 4000);
            projectPath = c.saveProject(5500);
            QCOMPARE(projectPath, d.filePath("P.snp.json"));
        }
        // A fresh player restores everything from the project.
        PlayerController c2;
        QCOMPARE(c2.loadProject(projectPath), qint64(5500));
        QCOMPARE(c2.scriptPath(), d.filePath("P.funscript"));
        QCOMPARE(c2.videoPath(), d.filePath("P.mp4"));
        QCOMPARE(c2.offsetMs(), 250);
        QCOMPARE(c2.markerStartMs(), 1000.0);
        QCOMPARE(c2.markerEndMs(), 4000.0);
        // Errors as in 0.7.
        writeFile(d.filePath("bad.snp.json"), "{\"version\":1}");
        QCOMPARE(c2.loadProject(d.filePath("bad.snp.json")), qint64(-1));
        QCOMPARE(c2.lastError(), QStringLiteral("Project has no script path"));
        PlayerController empty;
        QVERIFY(empty.saveProject(0).isEmpty());
        QVERIFY(empty.exportHeatmapPng().isEmpty());
    }

    void scriptDoctor() {
        QTemporaryDir d;
        writeFile(d.filePath("Q.funscript"), zigzag());
        PlayerController c;
        QCOMPARE(c.scriptQuality().value("error").toString(), QStringLiteral("no script loaded"));
        QVERIFY(c.openScript(d.filePath("Q.funscript")));
        const auto r = c.scriptQuality();
        QCOMPARE(r.value("error").toString(), QString());
        QVERIFY(r.value("score").toDouble() >= 0 && r.value("score").toDouble() <= 1);
        QVERIFY(r.contains("passed"));
        QVERIFY(r.value("warnings").canConvert<QStringList>());
    }

    void sleepTimer() {
        PlayerController c;
        QSignalSpy reached(&c, &PlayerController::sleepReached);
        c.setSleepMinutes(0);
        c.setPlaying(true);
        QTest::qWait(1200);
        QCOMPARE(reached.count(), 0);  // off
        c.setPlaying(false);
        QCOMPARE(c.sleepMinutes(), 0);
        c.setSleepMinutes(15);
        QCOMPARE(c.sleepMinutes(), 15);
    }

    void chaptersBookmarksHeatmapLanes() {
        QTemporaryDir d;
        QByteArray s = zigzag();
        s.chop(1);
        s += ",\"metadata\":{\"bookmarks\":[{\"name\":\"b\",\"time\":2.5}],"
             "\"chapters\":[{\"name\":\"Intro\",\"startTime\":\"0:00\",\"endTime\":\"0:04\"},"
             "{\"name\":\"Main\",\"startTime\":4000,\"endTime\":10000}]}}";
        writeFile(d.filePath("C.funscript"), s);
        PlayerController c;
        CurveItem stroke, vib, suc;
        vib.setProperty("lane", "vibration");
        suc.setProperty("lane", "suction");
        HeatmapItem heat;
        c.attachCurve(&stroke);
        c.attachCurve(&vib);
        c.attachCurve(&suc);
        c.attachHeatmap(&heat);
        QVERIFY(c.openScript(d.filePath("C.funscript")));
        QCOMPARE(c.chapters().size(), 2);
        QCOMPARE(c.chapters()[1].toMap().value("name").toString(), QStringLiteral("Main"));
        c.setOffsetMs(200);
        QCOMPARE(c.chapterVideoMs(1), qint64(4200));
        const auto bands = c.chapterBands();
        QCOMPARE(bands.size(), 2);
        QCOMPARE(bands[0].toMap().value("endMs").toLongLong(), 4000LL);
        QCOMPARE(bands[1].toMap().value("index").toInt(), 1);
        QVERIFY(bands[1].toMap().value("tip").toString().startsWith(QStringLiteral("Main · 0:04–0:10")));
        // Scene marker at video 6.2 s = script 6.0 s: cuts "Main" (4-10 s).
        QVERIFY(c.addSceneMarker(6200));
        QCOMPARE(c.chapters().size(), 3);
        QCOMPARE(c.chapters()[1].toMap().value("endMs").toLongLong(), 6000LL);
        QCOMPARE(c.chapters()[2].toMap().value("name").toString(), QStringLiteral("Scene 3"));
        QVERIFY(!c.addSceneMarker(6300));  // within 250 ms
        QCOMPARE(c.lastError(), QStringLiteral("A scene marker is already here."));
        PlayerController again;  // saved into the script
        QVERIFY(again.openScript(d.filePath("C.funscript")));
        QCOMPARE(again.chapters().size(), 3);
        QVERIFY(!stroke.empty() && !vib.empty() && !suc.empty() && !heat.empty());
        for (float v : heat.values()) QVERIFY(v >= 0.0f && v <= 1.0f);
    }

    void brokenFilesAreReported() {
        QTemporaryDir d;
        writeFile(d.filePath("bad.funscript"), "{oops");
        writeFile(d.filePath("marks.funscript"), "{\"actions\":[{\"at\":0,\"pos\":0},{\"at\":1000,\"pos\":100}],"
                                                 "\"metadata\":{\"chapters\":\"nonsense\"}}");
        PlayerController c;
        QSignalSpy err(&c, &PlayerController::lastErrorChanged);
        QVERIFY(!c.openScript(d.filePath("bad.funscript")));
        QVERIFY(c.lastError().contains("bad.funscript"));
        QVERIFY(err.count() >= 1);
        QVERIFY(c.openScript(d.filePath("marks.funscript")));  // broken chapters do not block
        QVERIFY(c.chapters().isEmpty());
        QVERIFY(c.lastError().isEmpty());
        QVERIFY(!c.openScript(d.filePath("missing.funscript")));
    }

    // 0.7 0.5.41: a video decode failure names its class and the file, and
    // goes away once the video plays; other errors stay.
    // 0.7 ProbePlaybackVideo (light): warn on mkv/avi / codec name tags.
    void videoLinkWarningOnProbeContainers() {
        QTemporaryDir d;
        const QString script = d.filePath("probe.funscript");
        writeFile(script, zigzag(11, 100));
        writeFile(d.filePath("clip.mp4"), "x");
        writeFile(d.filePath("clip.mkv"), "x");
        writeFile(d.filePath("movie.hevc.mp4"), "x");
        PlayerController c;
        QVERIFY(c.openScript(script, d.filePath("clip.mp4")));
        QVERIFY(c.videoLinkWarning().isEmpty());
        QVERIFY(!c.videoLinkSuggestPlayable());

        QVERIFY(c.attachVideo(d.filePath("clip.mkv")));
        QVERIFY(c.videoLinkWarning().contains(QStringLiteral("Container")));
        QVERIFY(c.videoLinkSuggestPlayable());
        c.dismissVideoLinkWarning();
        QVERIFY(c.videoLinkWarning().isEmpty());

        QVERIFY(c.attachVideo(d.filePath("movie.hevc.mp4")));
        QVERIFY(c.videoLinkWarning().contains(QStringLiteral("Codec")));
        QVERIFY(c.videoLinkSuggestPlayable());
        c.reportFirstPicture(QStringLiteral("hevc"));
        QVERIFY(c.videoLinkWarning().isEmpty());  // first frame clears the soft hint
    }

    void mediaErrorsAreReportedAndClearOnPlay() {
        PlayerController c;
        c.reportMediaError(QMediaPlayer::NoError, QString());
        QVERIFY(c.lastError().isEmpty());
        QVERIFY(!c.mediaError());
        c.reportMediaError(QMediaPlayer::FormatError, QStringLiteral("no decoder"));
        QVERIFY(c.lastError().contains(QStringLiteral("format not supported")));
        QVERIFY(c.lastError().contains(QStringLiteral("no decoder")));
        QVERIFY(c.lastError().contains(QStringLiteral("cannot be played")));
        QVERIFY(c.lastError().contains(QStringLiteral("Make playable")));
        QVERIFY(c.mediaError());
        c.setPlaying(false);
        QVERIFY(!c.lastError().isEmpty());
        c.setPlaying(true);
        QVERIFY(c.lastError().isEmpty());
        QVERIFY(!c.mediaError());  // cleared with the error text on play
        c.setPlaying(false);
        // A script error is not cleared by playing.
        QTemporaryDir d;
        writeFile(d.filePath("bad.funscript"), "{oops");
        QVERIFY(!c.openScript(d.filePath("bad.funscript")));
        c.setPlaying(true);
        QVERIFY(c.lastError().contains("bad.funscript"));
        c.setPlaying(false);
    }

    // 0.7 PS-10: a drop never vanishes silently. Extra videos and
    // unsupported files are named; a drop with nothing usable says so.
    void dropsNeverVanishSilently() {
        QTemporaryDir d;
        writeFile(d.filePath("A.funscript"), "{\"actions\":[{\"at\":0,\"pos\":0},{\"at\":1000,\"pos\":100}]}");
        writeFile(d.filePath("one.mp4"), "x");
        writeFile(d.filePath("two.mp4"), "x");
        writeFile(d.filePath("notes.txt"), "x");
        PlayerController c;
        QVERIFY(!c.openPaths({d.filePath("notes.txt")}));
        QVERIFY(c.lastError().contains(QStringLiteral("Drop ignored")));
        QVERIFY(!c.hasScript());
        // Two videos and a text file: the first video opens, the rest is named.
        QVERIFY(c.openPaths({d.filePath("one.mp4"), d.filePath("two.mp4"), d.filePath("notes.txt")}));
        QCOMPARE(c.videoName(), QStringLiteral("one.mp4"));
        QVERIFY(c.lastError().contains(QStringLiteral("1 more video")));
        QVERIFY(c.lastError().contains(QStringLiteral("notes.txt")));
        // A script with a video: both open; a clean drop leaves no message.
        QVERIFY(c.openPaths({d.filePath("one.mp4"), d.filePath("A.funscript")}));
        QVERIFY(c.hasScript());
        QVERIFY(c.lastError().isEmpty());
        // A script and a stray file: the script opens, the file is named.
        QVERIFY(c.openPaths({d.filePath("A.funscript"), d.filePath("notes.txt")}));
        QVERIFY(c.hasScript());
        QVERIFY(c.lastError().contains(QStringLiteral("notes.txt")));
    }

    // 0.7 app_snapshot_test.go TestSaveVideoSnapshot: no frame is an error;
    // the suggested name is "<video stem>_MM-SS.png"; a name without an
    // extension gets ".png". (Cancel is the dialog's own; nothing is called.)
    void snapshotNameAndSave() {
        QTemporaryDir d;
        PlayerController c;
        QVERIFY(!c.saveSnapshot(nullptr, d.filePath("x.png")));
        QVERIFY(c.lastError().contains(QStringLiteral("no video frame")));
        writeFile(d.filePath("My Clip.mp4"), "x");
        QVERIFY(c.openPath(d.filePath("My Clip.mp4")));
        QCOMPARE(c.snapshotName(83500), QStringLiteral("My Clip_01-23.png"));
        QCOMPARE(c.snapshotName(-5), QStringLiteral("My Clip_00-00.png"));
        // A 4x4 test-pattern frame in a video sink is saved at its size.
        QVideoFrameFormat fmt(QSize(4, 4), QVideoFrameFormat::Format_RGBA8888);
        QVideoFrame frame(fmt);
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < 4; ++y) memset(frame.bits(0) + y * frame.bytesPerLine(0), y * 60, 16);
        frame.unmap();
        QVideoSink sink;
        sink.setVideoFrame(frame);
        QVERIFY(c.saveSnapshot(&sink, d.filePath("frame")));  // no extension on purpose
        const QImage saved(d.filePath("frame.png"));
        QCOMPARE(saved.size(), QSize(4, 4));
        QVERIFY(c.lastError().isEmpty());
    }

    // pb_chart_tooltip_test.py: hovering the heatmap gives time and value.
    void heatTooltipShowsTimeAndValue() {
        PlayerController c;
        QVERIFY(c.heatTooltip(500).isEmpty());  // no script, no tooltip
        QTemporaryDir d;
        writeFile(d.filePath("t.funscript"),
                  R"({"actions":[{"at":0,"pos":0},{"at":500,"pos":100},{"at":1000,"pos":0},{"at":1500,"pos":100}]})");
        QVERIFY(c.openPath(d.filePath("t.funscript")));
        const QString tip = c.heatTooltip(1000);
        QVERIFY2(tip.startsWith(QString::fromStdString(samn::FormatTooltipTime(1000)) + QStringLiteral(" · ")),
                 qPrintable(tip));
        QVERIFY2(QRegularExpression(QStringLiteral(" · \\d{1,3}%$")).match(tip).hasMatch(), qPrintable(tip));
        QVERIFY(!c.heatTooltip(1e9).isEmpty());  // beyond the end: clamped, not empty
    }

    // pb_clip7776_test.py: a real-shaped tj script (contact recipe in the
    // metadata, no video) loads and plays. Synthetic equivalent of the 0.7
    // fixture: 9 points, tj profile, device_recipe with contact vibration.
    // This test found that playback ignored the script's recipe.
    void tjScriptWithoutVideoPlays() {
        QTemporaryDir d;
        writeFile(d.filePath("tj.funscript"), R"({"actions":[
            {"at":0,"pos":84},{"at":208,"pos":84},{"at":666,"pos":90},{"at":1100,"pos":30},
            {"at":1500,"pos":88},{"at":1950,"pos":25},{"at":2400,"pos":86},{"at":2850,"pos":40},{"at":3291,"pos":23}],
            "metadata":{"profile":"tj","duration":3291,
              "device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,"max_speed":0.5,
                               "smoothing":0.22,"contact_vibration":true,"contact_vibration_span":0.75},
              "quality_score":1,"quality_passed":true}})");
        PlayerController c;
        QVERIFY(c.openPath(d.filePath("tj.funscript")));
        QVERIFY(c.lastError().isEmpty());
        QVERIFY(c.hasScript());
        QCOMPARE(c.pointCount(), 9);
        QCOMPARE(c.scriptDurationMs(), qint64(3291));
        QVERIFY(c.videoName().isEmpty());
        QVERIFY(c.showStrokeLane());
        QVERIFY(!c.showSuctionLane());  // D-132: without a device the lanes show what the file carries
        // The device gets the script's recipe (0.7 StartPlayback used
        // MapOptionsFromScript): tj suction follows the position, and
        // contact vibration buzzes only near the deepest points.
        c.setPlaying(true);
        c.setVideoPositionMs(666);  // pos 90, contact
        QVERIFY2(std::abs(c.suctionNow() - 90) <= 5, qPrintable(QString::number(c.suctionNow())));
        QVERIFY2(c.vibrationNow() > 50, qPrintable(QString::number(c.vibrationNow())));
        for (qint64 ms : {1100, 1950}) {  // pos 30 and 25: far from contact
            c.setVideoPositionMs(ms);
            QVERIFY2(c.vibrationNow() == 0, qPrintable(QStringLiteral("%1 ms: %2").arg(ms).arg(c.vibrationNow())));
        }
        c.setVideoPositionMs(1950);
        QVERIFY2(std::abs(c.suctionNow() - 25) <= 5, qPrintable(QString::number(c.suctionNow())));
        c.setPlaying(false);
        QVERIFY(c.lastError().isEmpty());
    }

    // 0.7 StartPlayback: the Player's sync mode, tick, max speed and
    // smoothing over the script's recipe; the lanes are what the device gets
    // and follow every change at once; every choice is saved.
    void mappingSettingsShapeTheLanes() {
        QTemporaryDir d;
        const QString path = d.filePath("plain.funscript");
        writeFile(path, QByteArray(R"({"actions":)") + kRefActions + "}");
        samn::PlaybackChoices ch;  // the defaults: independent, 50 ms, 0.6, 0.3
        {
            PlayerController c;
            QVERIFY(c.openPath(path));
            QSignalSpy lanes(&c, &PlayerController::lanesChanged);
            auto* m = c.mapping();
            QCOMPARE(m->syncMode(), QString("independent"));
            QCOMPARE(m->tickMs(), 50);
            QCOMPARE(m->maxSpeed(), 0.6);
            QCOMPARE(m->smoothing(), 0.3);
            const auto now = [&](qint64 ms) {
                c.setVideoPositionMs(ms);
                return std::pair<int, int>{c.vibrationNow(), c.suctionNow()};
            };
            QCOMPARE(now(600), expectedAt(path, ch, 600));
            m->setSyncMode("synchronized");
            ch.sync_mode = "synchronized";
            QVERIFY(lanes.count() > 0);
            for (qint64 ms : {300, 600, 1200}) QCOMPARE(now(ms), expectedAt(path, ch, ms));
            m->setSyncMode("vibration_only");
            QCOMPARE(now(600).second, 0);
            QVERIFY(now(600).first > 0);
            m->setSyncMode("synchronized");
            m->setTickMs(100);
            m->setMaxSpeed(1.2);
            m->setSmoothing(0);  // 0 is a value: no smoothing (PS-11)
            ch.tick_ms = 100;
            ch.max_speed = 1.2;
            ch.smoothing = 0;
            QCOMPARE(now(600), expectedAt(path, ch, 600));
            QCOMPARE(now(1200), expectedAt(path, ch, 1200));
            // A stroke script has no contact controls: contact strength does nothing.
            QVERIFY(!c.contactControls());
            const auto before = now(1200);
            m->setContactIntensity(0);
            QCOMPARE(now(1200), before);
            m->setSyncMode("sideways");  // unknown: ignored
            QCOMPARE(m->syncMode(), QString("synchronized"));
            m->setTickMs(5);
            QCOMPARE(m->tickMs(), 10);
            m->setMaxSpeed(9);
            QCOMPARE(m->maxSpeed(), 2.0);
        }
        // Saved and loaded again.
        PlayerController c;
        auto* m = c.mapping();
        QCOMPARE(m->syncMode(), QString("synchronized"));
        QCOMPARE(m->tickMs(), 10);
        QCOMPARE(m->maxSpeed(), 2.0);
        QCOMPARE(m->smoothing(), 0.0);
        QCOMPARE(m->contactIntensity(), 0.0);
    }

    // A broken stored value takes the default; a stored 0 stays 0 (PS-11).
    void brokenMappingSettingsTakeTheDefaults() {
        writeFile(QString::fromUtf8(qgetenv("SAMN_CONFIG_DIR")) + "/settings.json",
                  R"({"playback.sync_mode":"sideways","playback.tick_ms":"fast","playback.max_speed":null,)"
                  R"("playback.smoothing":0,"playback.contact_intensity":0,"playback.effect_presets":"[1,2]"})");
        PlayerController c;
        auto* m = c.mapping();
        QCOMPARE(m->syncMode(), QString("independent"));
        QCOMPARE(m->tickMs(), 50);
        QCOMPARE(m->maxSpeed(), 0.6);
        QCOMPARE(m->smoothing(), 0.0);
        QCOMPARE(m->contactIntensity(), 0.0);
        QVERIFY(m->effectPresets().isEmpty());
    }

    // 0.7 contact controls: strength 0-2 (0 mutes), off, sensitivity and
    // curve; the knobs act in recipe mode with contact vibration on.
    void contactControlsFollowTheScript() {
        QTemporaryDir d;
        const QString path = d.filePath("tj.funscript");
        writeFile(path, tjFunscript());
        samn::PlaybackChoices ch;
        ch.contact_span = 0.75;
        ch.contact_curve = std::string("linear");
        PlayerController c;
        QVERIFY(c.openPath(path));
        QVERIFY(c.contactControls());
        QVERIFY(c.contactKnobsLive());
        auto* m = c.mapping();
        QCOMPARE(m->contactSpan(), 0.75);  // from the recipe
        QCOMPARE(m->contactCurve(), QString("linear"));
        const auto now = [&](qint64 ms) {
            c.setVideoPositionMs(ms);
            return std::pair<int, int>{c.vibrationNow(), c.suctionNow()};
        };
        const auto full = now(600);
        QVERIFY(full.first > 30);
        QCOMPARE(full, expectedAt(path, ch, 600));
        m->setContactIntensity(0.5);
        ch.contact_intensity = 0.5;
        QCOMPARE(now(600), expectedAt(path, ch, 600));
        QVERIFY(now(600).first < full.first);
        m->setContactIntensity(0);  // mutes, suction stays
        QCOMPARE(now(600), std::make_pair(0, full.second));
        m->setContactIntensity(1);
        m->setContactOff(true);
        QCOMPARE(now(600).first, 0);
        m->setContactOff(false);
        QCOMPARE(now(600), full);
        m->setContactSpan(0.5);
        m->setContactCurve("peak");
        ch.contact_intensity = 1;
        ch.contact_span = 0.5;
        ch.contact_curve = std::string("peak");
        QCOMPARE(now(600), expectedAt(path, ch, 600));
        QVERIFY(now(600).first > full.first);
        // Feel probe follows the live knobs; Save to script persists them.
        const QVariantMap probe = c.contactFeelProbe();
        QVERIFY(probe.value("activePct").toDouble() > 0);
        QVERIFY(probe.value("stroke").toList().size() >= 2);
        QVERIFY(c.saveContactToScript());
        QVERIFY(c.toolNote().contains("Contact settings saved"));
        QCOMPARE(c.mapping()->contactSpan(), 0.5);
        QCOMPARE(c.mapping()->contactCurve(), QString("peak"));
        const samn::Document saved = samn::LoadDocument(std::filesystem::u8path(c.scriptPath().toStdString()));
        QVERIFY(saved.recipe.contact_vibration);
        QCOMPARE(saved.recipe.contact_vibration_span, 0.5);
        QCOMPARE(saved.recipe.contact_vibration_curve, std::string("peak"));
        m->setContactIntensity(0.25);
        // Reopen keeps the saved recipe; live strength is remembered.
        QVERIFY(c.openPath(c.scriptPath()));
        QCOMPARE(m->contactSpan(), 0.5);
        QCOMPARE(m->contactCurve(), QString("peak"));
        QCOMPARE(m->contactIntensity(), 0.25);
        // A plain script has none; an axes script has the controls but the
        // knobs do not act (baked curves).
        const QString plain = d.filePath("plain.funscript");
        writeFile(plain, QByteArray(R"({"actions":)") + kRefActions + "}");
        QVERIFY(c.openPath(plain));
        QVERIFY(!c.contactControls());
        QVERIFY(!c.contactKnobsLive());
        samn::Document axes = samn::LoadFunscriptFile(std::filesystem::u8path(path.toStdString()));
        axes.BakeNeoAxes();
        const QString axesPath = d.filePath("axes.samn");
        samn::SaveDocument(std::filesystem::u8path(axesPath.toStdString()), axes);
        QVERIFY(c.openPath(axesPath));
        QVERIFY(c.contactControls());
        QVERIFY(!c.contactKnobsLive());
    }

    // 0.7 contact overlay and spatial contact: the marks show on the video,
    // the tip box follows the recorded path, and the tip in a contact area
    // makes the toy vibrate even on a shallow stroke.
    void contactMarksShowAndDriveTheVibration() {
        QTemporaryDir d;
        samn::Document doc;
        doc.version = samn::SamnCurrentVersion;
        doc.kind = samn::SamnKind;
        doc.general = {{0, 20}, {1000, 30}, {2000, 20}, {3000, 30}};
        doc.duration_ms = 3000;
        doc.profile = "standard";
        doc.recipe.sync = "independent";
        doc.recipe.smoothing = 0.3;
        doc.recipe.contact_vibration = true;
        samn::ContactMarks cm;
        cm.tip_class = "glans";
        cm.tip = samn::ContactMarkBox{0, 0, 20, 20, "glans", false};
        cm.primary = samn::ContactMarkBox{40, 40, 40, 40, "nipples", false};
        cm.extras = {samn::ContactMarkBox{150, 10, 20, 20, "mouth", true}};
        doc.contact_marks = cm;
        samn::TrajectoryData path;
        path.width = 320;
        path.height = 240;
        path.tip = {{0, 0, 0}, {1000, 60, 60}, {2000, 0, 0}, {3000, 0, 0}};
        doc.trajectory = path;
        const QString file = d.filePath("marks.samn");
        samn::SaveDocument(std::filesystem::u8path(file.toStdString()), doc);

        PlayerController c;
        QVERIFY(c.openPath(file));
        const QVariantMap m = c.contactMarks();
        QCOMPARE(m.value("tipClass").toString(), QString("glans"));
        QCOMPARE(m.value("primary").toMap().value("cls").toString(), QString("nipples"));
        QCOMPARE(m.value("extras").toList().size(), 1);
        QCOMPARE(m.value("width").toInt(), 320);
        QCOMPARE(m.value("hasPartner").toBool(), false);
        QCOMPARE(m.value("hasExtras").toBool(), false);
        QCOMPARE(m.value("hint").toString(), QString("Tip: glans · 3 marked boxes · feel only (the tip tracker drives the stroke)"));
        QCOMPARE(c.tipAt(1100).value("x").toDouble(), 60.0);
        QVERIFY(c.tipAt(5000).isEmpty());  // past the path
        QVERIFY(c.partnerAt(1000).isEmpty());
        QVERIFY(c.extraAt(0, 1000).isEmpty());
        QVERIFY(c.hasTrajectory());
        QCOMPARE(c.tipPathPoints().size(), 4);
        QVERIFY(c.partnerPathPoints().isEmpty());
        QVERIFY(!c.trajectoryOverlay());  // MT-Debug off by default
        c.setTrajectoryOverlay(true);
        QVERIFY(c.trajectoryOverlay());
        c.setTrajectoryOverlay(false);
        QVERIFY(c.showContactMarks());
        c.setShowContactMarks(false);
        QVERIFY(!c.showContactMarks());
        c.setShowContactMarks(true);

        // Spatial contact: shallow everywhere, but the tip is in the area at 1 s.
        c.setVideoPositionMs(1000);
        const int inArea = c.vibrationNow();
        c.setVideoPositionMs(2500);
        const int away = c.vibrationNow();
        QVERIFY2(inArea > away + 20, qPrintable(QString("%1 vs %2").arg(inArea).arg(away)));

        // Partner path: non-fixed gold contact follows it in the overlay
        // (partnerAt); fixed extras stay put (hasPartner + fixed flag).
        path.partner = {{0, 20, 20}, {1000, 180, 120}, {2000, 20, 20}, {3000, 20, 20}};
        doc.trajectory = path;
        const QString withPartner = d.filePath("marks-partner.samn");
        samn::SaveDocument(std::filesystem::u8path(withPartner.toStdString()), doc);
        QVERIFY(c.openPath(withPartner));
        const QVariantMap mp = c.contactMarks();
        QVERIFY(mp.value("hasPartner").toBool());
        QVERIFY(mp.value("hint").toString().contains(QString("partner path")));
        QCOMPARE(c.partnerAt(1100).value("x").toDouble(), 180.0);
        QCOMPARE(c.partnerAt(1100).value("y").toDouble(), 120.0);
        QVERIFY(c.partnerAt(5000).isEmpty());
        QCOMPARE(c.partnerPathPoints().size(), 4);
        QCOMPARE(c.partnerPathPoints().at(1).toMap().value("x").toDouble(), 180.0);
        QVERIFY(!mp.value("primary").toMap().value("fixed").toBool());
        QVERIFY(mp.value("extras").toList().at(0).toMap().value("fixed").toBool());

        // Extras path (track_multi): non-fixed magenta follows its own series.
        cm.extras = {samn::ContactMarkBox{150, 10, 20, 20, "mouth", false}};
        doc.contact_marks = cm;
        path.extras = {{{0, 10, 10}, {1000, 200, 150}, {2000, 10, 10}, {3000, 10, 10}}};
        doc.trajectory = path;
        const QString withExtras = d.filePath("marks-extras.samn");
        samn::SaveDocument(std::filesystem::u8path(withExtras.toStdString()), doc);
        QVERIFY(c.openPath(withExtras));
        const QVariantMap me = c.contactMarks();
        QVERIFY(me.value("hasExtras").toBool());
        QVERIFY(me.value("hint").toString().contains(QString("extras path")));
        QCOMPARE(c.extraAt(0, 1100).value("x").toDouble(), 200.0);
        QCOMPARE(c.extraAt(0, 1100).value("y").toDouble(), 150.0);
        QVERIFY(c.extraAt(0, 5000).isEmpty());
        QVERIFY(c.extraAt(1, 1000).isEmpty());

        // A plain script has no marks.
        const QString plain = d.filePath("plain.funscript");
        writeFile(plain, QByteArray(R"({"actions":)") + kRefActions + "}");
        QVERIFY(c.openPath(plain));
        QVERIFY(c.contactMarks().isEmpty());
        QVERIFY(c.tipAt(1000).isEmpty());
        QVERIFY(c.partnerAt(1000).isEmpty());
        QVERIFY(c.extraAt(0, 1000).isEmpty());
    }

    // 0.7 Settings → Play "Honor inverted flag" (on by default) and the
    // script folders' Remove.
    void honorInvertedAndRemoveScriptFolder() {
        QTemporaryDir d;
        const QString path = d.filePath("inv.funscript");
        writeFile(path, R"({"inverted":true,"actions":[{"at":0,"pos":10},{"at":1000,"pos":80}]})");
        PlayerController c;
        QVERIFY(c.openPath(path));
        c.setVideoPositionMs(0);
        QCOMPARE(c.strokeNow(), 90);  // flipped: 100 - 10
        samn::Settings().set("playback.honor_inverted", false);
        QVERIFY(c.reloadScript());
        c.setVideoPositionMs(0);
        QCOMPARE(c.strokeNow(), 10);  // as written
        samn::Settings().set("playback.honor_inverted", true);
        QVERIFY(c.reloadScript());
        c.setVideoPositionMs(0);
        QCOMPARE(c.strokeNow(), 90);
        PlayerController none;
        QVERIFY(!none.reloadScript());

        c.addScriptFolder(d.path());
        QVERIFY(c.scriptFolders().contains(d.path()));
        QSignalSpy folders(&c, &PlayerController::scriptFoldersChanged);
        c.removeScriptFolder(d.path());
        QVERIFY(!c.scriptFolders().contains(d.path()));
        QCOMPARE(folders.count(), 1);
        c.removeScriptFolder(d.path());  // not there: nothing happens
        QCOMPARE(folders.count(), 1);
    }

    // 0.7 Strength (soft / normal / strong): an Emotion Script's preset
    // scales vibration and suction; the choice is saved in the script.
    void strengthPresetIsSavedInTheScript() {
        QTemporaryDir d;
        const QString path = d.filePath("feel.samn");
        writeTjSamn(path, "normal");
        {
            PlayerController c;
            QVERIFY(c.openPath(path));
            QCOMPARE(c.strengthPresets(), QStringList({"soft", "normal", "strong"}));
            QCOMPARE(c.activeStrength(), QString("normal"));
            c.setVideoPositionMs(600);
            const int normalSuction = c.suctionNow();
            c.setActiveStrength("soft");
            QCOMPARE(c.activeStrength(), QString("soft"));
            QCOMPARE(samn::LoadDocument(std::filesystem::u8path(path.toStdString())).active_strength, std::string("soft"));
            samn::PlaybackChoices ch;
            ch.contact_span = 0.75;
            ch.contact_curve = std::string("linear");
            const auto soft = expectedAt(path, ch, 600);
            QCOMPARE(std::make_pair(c.vibrationNow(), c.suctionNow()), soft);
            QVERIFY(c.suctionNow() < normalSuction);  // suction x 0.85
        }
        PlayerController c;
        QVERIFY(c.openPath(path));
        QCOMPARE(c.activeStrength(), QString("soft"));
        c.setActiveStrength("");
        QCOMPARE(samn::LoadDocument(std::filesystem::u8path(path.toStdString())).active_strength, std::string());
        // A .funscript has no strength presets.
        const QString plain = d.filePath("plain.funscript");
        writeFile(plain, QByteArray(R"({"actions":)") + kRefActions + "}");
        QVERIFY(c.openPath(plain));
        QVERIFY(c.strengthPresets().isEmpty());
        c.setActiveStrength("soft");
        QCOMPARE(c.activeStrength(), QString());
    }

    // 0.7 Effects presets (player_layout.js; app_effect_presets_test.go
    // TestEffectPresetsRoundTrip): save, apply, Default, Custom, delete,
    // saved as a JSON text under playback.effect_presets.
    void effectPresetsSaveApplyDelete() {
        const QString file = QString::fromUtf8(qgetenv("SAMN_CONFIG_DIR")) + "/settings.json";
        const auto stored = [&] {
            samn::Settings s(std::filesystem::u8path(file.toStdString()));
            return s.get(samn::kKeyEffectPresets, nullptr);
        };
        {
            PlayerController c;
            auto* m = c.mapping();
            auto* o = c.output();
            QVERIFY(m->effectPresets().isEmpty());  // a fresh install has none
            QVERIFY(stored().is_null());
            m->setSmoothing(0.8);
            m->setMaxSpeed(0.4);
            m->setTickMs(80);
            o->setIntensity(40);
            o->setSoftStartMs(1500);
            o->setReducePeaks(true);
            QVERIFY(!m->saveEffectPreset("  "));
            QVERIFY(!m->saveEffectPreset("__default"));
            QVERIFY(m->saveEffectPreset("  Night "));
            QCOMPARE(m->effectPresets(), QStringList({"Night"}));
            QCOMPARE(m->effectPreset(), QString("Night"));
            const auto text = stored();
            QVERIFY(text.is_string());
            const auto night = samn::ParseEffectPresets(text.get<std::string>()).at("Night");
            QCOMPARE(*night.intensity, 40.0);
            QCOMPARE(*night.smoothing, 0.8);
            QCOMPARE(*night.max_speed, 0.4);
            QCOMPARE(*night.tick_ms, 80.0);
            QCOMPARE(*night.soft_start_ms, 1500.0);
            QVERIFY(*night.reduce_peaks);
            // A change by hand makes the selection Custom.
            o->setIntensity(45);
            QCOMPARE(m->effectPreset(), QString());
            // Default: the 0.7 FX_DEFAULTS.
            QVERIFY(m->applyEffectPreset("__default"));
            QCOMPARE(m->effectPreset(), QString("__default"));
            QCOMPARE(m->smoothing(), 0.3);
            QCOMPARE(m->maxSpeed(), 0.6);
            QCOMPARE(m->tickMs(), 50);
            QCOMPARE(o->intensity(), 100);
            QCOMPARE(o->softStartMs(), 500);
            QVERIFY(!o->reducePeaks());
            QVERIFY(m->applyEffectPreset("Night"));
            QCOMPARE(m->effectPreset(), QString("Night"));
            QCOMPARE(m->smoothing(), 0.8);
            QCOMPARE(m->maxSpeed(), 0.4);
            QCOMPARE(m->tickMs(), 80);
            QCOMPARE(o->intensity(), 40);
            QCOMPARE(o->softStartMs(), 1500);
            QVERIFY(o->reducePeaks());
            QVERIFY(!m->applyEffectPreset("Nope"));
            m->setTickMs(90);
            QCOMPARE(m->effectPreset(), QString());
        }
        // A new start reads them back; the applied values were saved too.
        {
            PlayerController c;
            auto* m = c.mapping();
            QCOMPARE(m->effectPresets(), QStringList({"Night"}));
            QCOMPARE(m->smoothing(), 0.8);
            QCOMPARE(c.output()->intensity(), 40);
            QVERIFY(m->deleteEffectPreset("Night"));
            QVERIFY(!m->deleteEffectPreset("Night"));
            QVERIFY(m->effectPresets().isEmpty());
        }
        PlayerController c;
        QVERIFY(c.mapping()->effectPresets().isEmpty());
        // The text 0.7 wrote loads as it is (no field missing: all apply).
        {
            samn::Settings s(std::filesystem::u8path(file.toStdString()));
            s.set(samn::kKeyEffectPresets,
                  R"({"Night":{"intensity":40,"smoothing":0.8,"maxSpeed":0.4,"tickMs":80,"softStartMs":1500,"reducePeaks":true}})");
        }
        PlayerController c2;
        QCOMPARE(c2.mapping()->effectPresets(), QStringList({"Night"}));
        QVERIFY(c2.mapping()->applyEffectPreset("Night"));
        QCOMPARE(c2.mapping()->tickMs(), 80);
    }

    void zoomWindow() {
        QTemporaryDir dir;
        const QString script = dir.path() + "/z.funscript";
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"actions":[{"at":0,"pos":0},{"at":30000,"pos":100},{"at":60000,"pos":0}]})");
        f.close();
        PlayerController c;
        QSignalSpy view(&c, &PlayerController::viewChanged);
        QVERIFY(c.openPath(script));
        QCOMPARE(c.viewEndMs(), 60000.0);
        QCOMPARE(c.zoomLabel(), QString("Full"));
        c.zoomAt(2, 0.25);  // mouse at 15 s
        QCOMPARE(c.viewStartMs(), 7500.0);
        QCOMPARE(c.viewEndMs(), 37500.0);
        QVERIFY(c.zoomed());
        QCOMPARE(c.zoomLabel(), QString("7.5s–37.5s"));
        // A longer video widens the timeline but keeps the zoom.
        c.setVideoDurationMs(90000);
        QCOMPARE(c.viewEndMs(), 37500.0);
        c.resetZoom();
        QCOMPARE(c.viewEndMs(), 90000.0);
        // The offset shifts where the video ends in script time.
        c.setOffsetMs(1000);
        QCOMPARE(c.viewEndMs(), 89000.0);
        QVERIFY(view.count() >= 4);
    }

    void audioLevelFromSound() {
        QTemporaryDir dir;
        const QString wav = writeToneWav(dir.path());
        AudioLevelDecoder dec;
        QSignalSpy done(&dec, &AudioLevelDecoder::finished);
        QSignalSpy fail(&dec, &AudioLevelDecoder::failed);
        dec.start(wav);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + fail.count() > 0, 15000);
        QCOMPARE(fail.count(), 0);
        const auto env = done.first().first().value<std::vector<double>>();
        QVERIFY(env.size() >= 39 && env.size() <= 41);  // 2 s in 50 ms steps
        QCOMPARE(env[5], 0.0);                          // silence
        QVERIFY(env[30] > 0.9);                         // tone
        // Same unchanged file: served from the cache, synchronously.
        dec.start(wav);
        QCOMPARE(done.count(), 2);
        // A missing file fails cleanly.
        dec.start(dir.path() + "/missing.mp4");
        QCOMPARE(fail.count(), 1);
    }

    // Length cap: stop once the RMS windows cover maxMs, and don't reuse that
    // envelope for a full-file (or different-cap) request.
    void audioLevelCapsAtMaxMs() {
        QTemporaryDir dir;
        const QString wav = writeToneWav(dir.path());
        AudioLevelDecoder dec;
        QSignalSpy done(&dec, &AudioLevelDecoder::finished);
        QSignalSpy fail(&dec, &AudioLevelDecoder::failed);
        const int step = samn::kAudioLevelStepMs;
        const qint64 capMs = 500;

        dec.start(wav, step, capMs);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + fail.count() > 0, 15000);
        QCOMPARE(fail.count(), 0);
        QCOMPARE(done.first().first().value<std::vector<double>>().size(), size_t(capMs / step));
        QVERIFY(!dec.busy());

        // Same cap, same file: served from the cache, synchronously.
        dec.start(wav, step, capMs);
        QCOMPARE(done.count(), 2);
        QCOMPARE(done.at(1).first().value<std::vector<double>>().size(), size_t(capMs / step));

        // A full-file decode must miss the capped cache.
        dec.start(wav, step, -1);
        QCOMPARE(done.count(), 2);
        QVERIFY(dec.busy());
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + fail.count() >= 3, 15000);
        QCOMPARE(fail.count(), 0);
        const auto full = done.at(2).first().value<std::vector<double>>();
        QVERIFY(full.size() >= 39 && full.size() <= 41);
        QVERIFY(!dec.busy());

        dec.start(wav);
        QCOMPARE(done.count(), 4);
        QCOMPARE(done.at(3).first().value<std::vector<double>>().size(), full.size());

        // A different cap misses the full-file cache and stops at its own length.
        const qint64 otherMs = 1000;
        dec.start(wav, step, otherMs);
        QCOMPARE(done.count(), 4);
        QVERIFY(dec.busy());
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + fail.count() >= 5, 15000);
        QCOMPARE(fail.count(), 0);
        QCOMPARE(done.at(4).first().value<std::vector<double>>().size(), size_t(otherMs / step));
        QVERIFY(!dec.busy());
    }

    void curveModes() {
        CurveItem curve;
        curve.setWidth(500);
        std::vector<samn::Action> acts;
        for (int i = 0; i < 100000; ++i) acts.push_back({i * 50, (i * 37) % 101});
        curve.setActions(acts);
        // 100 000 strokes on 500 px: the stroke lane shows the tempo wave.
        QCOMPARE(curve.modeFor(500), CurveItem::Mode::Tempo);
        QCOMPARE(curve.property("mode").toString(), QString("tempo"));
        curve.setProperty("lane", "vibration");
        QCOMPARE(curve.modeFor(500), CurveItem::Mode::Fine);
        // Zoomed in to 10 points: the soft curve with dots.
        curve.setViewStartMs(0);
        curve.setViewEndMs(450);
        QCOMPARE(curve.modeFor(500), CurveItem::Mode::Smooth);
        // Painting a zoomed-out curve stays cheap and does not crash.
        curve.setViewEndMs(-1);
        curve.setHeight(120);
        QImage img(500, 120, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&img);
        curve.paint(&painter);
        QCOMPARE(curve.layerRenders(), 1);
        // Moving the playhead only blits the cached layer.
        for (int t = 0; t < 50; ++t) {
            curve.setPlayheadMs(t * 1000);
            curve.paint(&painter);
        }
        QCOMPARE(curve.layerRenders(), 1);
        // A new view redraws it once.
        curve.setViewEndMs(100000);
        curve.paint(&painter);
        QCOMPARE(curve.layerRenders(), 2);
    }
};

QTEST_MAIN(PlayerControllerTest)
#include "player_controller_test.moc"
