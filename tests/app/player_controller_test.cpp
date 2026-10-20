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
#include "heatmap_item.h"
#include "player_controller.h"
#include "samn/playback_mapping.h"
#include "samn/contact_marks.h"
#include "samn/samn_file.h"
#include "samn/settings.h"

using samn::ui::AudioLevelDecoder;
using samn::ui::CurveItem;
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
        c.removeScriptFolder(d.filePath("scripts"));
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
    void mediaErrorsAreReportedAndClearOnPlay() {
        PlayerController c;
        c.reportMediaError(QMediaPlayer::NoError, QString());
        QVERIFY(c.lastError().isEmpty());
        c.reportMediaError(QMediaPlayer::FormatError, QStringLiteral("no decoder"));
        QVERIFY(c.lastError().contains(QStringLiteral("format not supported")));
        QVERIFY(c.lastError().contains(QStringLiteral("no decoder")));
        c.setPlaying(false);
        QVERIFY(!c.lastError().isEmpty());
        c.setPlaying(true);
        QVERIFY(c.lastError().isEmpty());
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
        m->setContactIntensity(0.25);
        // A new script starts from its own recipe; the strength is kept.
        QVERIFY(c.openPath(path));
        QCOMPARE(m->contactSpan(), 0.75);
        QCOMPARE(m->contactCurve(), QString("linear"));
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
        QCOMPARE(m.value("hint").toString(), QString("Tip: glans · 3 marked boxes · feel only (the tip tracker drives the stroke)"));
        QCOMPARE(c.tipAt(1100).value("x").toDouble(), 60.0);
        QVERIFY(c.tipAt(5000).isEmpty());  // past the path
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

        // A plain script has no marks.
        const QString plain = d.filePath("plain.funscript");
        writeFile(plain, QByteArray(R"({"actions":)") + kRefActions + "}");
        QVERIFY(c.openPath(plain));
        QVERIFY(c.contactMarks().isEmpty());
        QVERIFY(c.tipAt(1000).isEmpty());
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
