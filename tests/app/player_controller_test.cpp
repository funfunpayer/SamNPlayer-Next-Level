// PlayerController: the Player logic behind the QML page (ported from the
// Python play page tests and 0.7 behaviour).
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <memory>

#include "audio_level.h"
#include "curve_item.h"
#include "heatmap_item.h"
#include "player_controller.h"

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
