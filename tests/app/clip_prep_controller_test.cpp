// Clip prep (D-155 step 6) with a stand-in for ffmpeg (tests/app/data/
// fake_ffmpeg.py) that notes its arguments and prints -progress lines. The
// real ffmpeg and the real wheel: clip_prep_real_check (not in ctest). The
// one-click download runs against a local server.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

#include "clip_prep_controller.h"
#include "fake_file_server.h"
#include "zip_builder.h"

using samn::ui::ClipPrepController;

namespace {
const QString kData = QStringLiteral(SAMN_TEST_DATA);
QString Python() {
    for (const char* name : {"python3", "python"}) {
        const QString p = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!p.isEmpty()) return p;
    }
    return {};
}
void Write(const QString& path, const QByteArray& body) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(body);
}
}  // namespace

class ClipPrepControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString python_, video_, log_;

    // The stand-in's calls so far, each its arguments.
    QList<QStringList> calls() const {
        QList<QStringList> out;
        QFile f(log_);
        if (!f.open(QIODevice::ReadOnly)) return out;
        for (const QByteArray& line : f.readAll().split('\n'))
            if (!line.trimmed().isEmpty()) out << QJsonDocument::fromJson(line).toVariant().toStringList();
        return out;
    }
    static QString after(const QStringList& args, const QString& flag) { return args.value(args.indexOf(flag) + 1); }
    void useFake(ClipPrepController& c) { c.setFfmpegCommand(python_, {kData + "/fake_ffmpeg.py"}); }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8());
        python_ = Python();
        QVERIFY2(!python_.isEmpty(), "the tests need a Python on the PATH for the stand-in");
        log_ = dir_.filePath("ffmpeg.log");
        qputenv("FAKE_FFMPEG_LOG", log_.toUtf8());
        QDir().mkpath(dir_.filePath("videos"));
        video_ = dir_.filePath("videos/long.mp4");
        Write(video_, "not a real video; the stand-in does not read it");
        Write(dir_.filePath("videos/long.funscript"),
              R"({"version":"1.0","actions":[{"at":0,"pos":0},{"at":1000,"pos":100},{"at":2000,"pos":0},{"at":3000,"pos":100},{"at":4000,"pos":0}]})");
    }

    void ffmpegIsPinnedForThisPlatform() {
        const auto f = samn::ui::FfmpegSource();
#if (defined(Q_OS_WIN) || defined(Q_OS_LINUX)) && defined(Q_PROCESSOR_X86_64)
        QCOMPARE(f.url.scheme(), QString("https"));
        QCOMPARE(f.archive_sha256.size(), 64);
        QCOMPARE(f.model_sha256.size(), 64);
        QVERIFY(f.entry.startsWith("imageio_ffmpeg/binaries/ffmpeg-"));
#else
        QVERIFY(f.url.isEmpty());
#endif
    }

    void withoutFfmpegItAsksForIt() {
        ClipPrepController c;
        c.setFfmpegSource({});
        QVERIFY(!c.ffmpegReady());
        QVERIFY(!c.cut(video_, "1", "3"));
        QVERIFY2(c.status().contains("Fetch ffmpeg first"), qPrintable(c.status()));
        QVERIFY(!c.fetchFfmpeg());
        QVERIFY(c.toolStatus().contains("choose one of your own"));
    }

    void badTimesSayWhy() {
        ClipPrepController c;
        useFake(c);
        QVERIFY(!c.cut(video_, "abc", "3"));
        QVERIFY(c.status().contains("MM:SS"));
        QVERIFY(!c.cut(video_, "0:05", "0:03"));
        QVERIFY(c.status().contains("after the in point"));
        QVERIFY(!c.cut(dir_.filePath("videos/none.mp4"), "1", "3"));
        QVERIFY(c.status().contains("Open a video"));
        QVERIFY(!c.cut(video_, "1", "3", video_));
        QVERIFY2(c.status().contains("cannot replace its own video"), qPrintable(c.status()));
        QCOMPARE(c.suggestOutput(video_, "x", "3"), QString());
    }

    void theCommandToCopy() {
        ClipPrepController c;
        useFake(c);
        c.setPreset("720p");
        c.setNoAudio(false);
        const QString cmd = c.commandLine(video_, "1:25", "2:05", dir_.filePath("videos/my cut.mp4"));
        QVERIFY2(cmd.startsWith("ffmpeg -y -hide_banner -loglevel error -ss 85 -i "), qPrintable(cmd));
        QVERIFY2(cmd.contains(" -t 40 -vf \"scale='min(1280,iw)':-2:flags=lanczos\" -c:v libx264"), qPrintable(cmd));
        QVERIFY2(cmd.endsWith(" -movflags +faststart \"" + QDir::toNativeSeparators(dir_.filePath("videos/my cut.mp4")) + "\""), qPrintable(cmd));
        QVERIFY(!cmd.contains("-progress"));
        QVERIFY(c.commandLine(video_, "2:05", "1:25").isEmpty());
    }

    void aCutUsesTheSettingsAndCutsTheScript() {
        QFile::remove(log_);
        ClipPrepController c;
        useFake(c);
        c.setPreset("960w");
        c.setNoAudio(true);
        const QString out = dir_.filePath("videos/long_0-01.5-0-03.mp4");
        QCOMPARE(c.suggestOutput(QUrl::fromLocalFile(video_).toString(), "0:01.5", "0:03"), out);
        QVERIFY(c.cut(QUrl::fromLocalFile(video_).toString(), "0:01.5", "0:03"));
        QVERIFY(c.cutting());
        QVERIFY(!c.cut(video_, "1", "2"));  // one at a time
        QTRY_VERIFY_WITH_TIMEOUT(!c.cutting(), 30000);
        QVERIFY2(c.status().startsWith("Done"), qPrintable(c.status()));
        QVERIFY2(c.status().contains("1 script(s) cut along"), qPrintable(c.status()));
        QCOMPARE(c.progress(), 100);
        QCOMPARE(c.lastOutput(), out);
        QVERIFY(QFile::exists(out));

        const auto all = calls();
        QCOMPARE(all.size(), 1);
        const QStringList& a = all[0];
        QCOMPARE(after(a, "-ss"), QString("1.5"));
        QCOMPARE(after(a, "-t"), QString("1.5"));
        QCOMPARE(after(a, "-i"), video_);
        QCOMPARE(after(a, "-vf"), QString("scale='min(960,iw)':-2:flags=lanczos"));
        QVERIFY(a.contains("-an"));
        QCOMPARE(a.last(), out);

        // The script beside the clip: the same window, from 0.
        QFile s(dir_.filePath("videos/long_0-01.5-0-03.funscript"));
        QVERIFY(s.open(QIODevice::ReadOnly));
        const QJsonArray acts = QJsonDocument::fromJson(s.readAll()).object().value("actions").toArray();
        QCOMPARE(acts.size(), 3);
        QCOMPARE(acts[0].toObject().value("at").toInt(), 0);
        QCOMPARE(acts[0].toObject().value("pos").toInt(), 50);
        QCOMPARE(acts[1].toObject().value("at").toInt(), 500);
        QCOMPARE(acts[2].toObject().value("at").toInt(), 1500);

        // Remembered.
        ClipPrepController again;
        QCOMPARE(again.preset(), QString("960w"));
        QVERIFY(again.noAudio());
        again.setNoAudio(false);
        again.setPreset("720p");
    }

    // Owner 26 Oct: a video the player cannot show (AV1 without GPU support)
    // becomes an H.264 copy beside it at its own size; the scripts come along.
    void aWholeVideoConvertsForPlaybackWithItsScripts() {
        QFile::remove(log_);
        const QString samn = dir_.filePath("videos/long.samn");
        Write(samn, "{\"version\":1}");
        ClipPrepController c;
        useFake(c);
        const QString out = dir_.filePath("videos/long-h264.mp4");
        QCOMPARE(c.convertedName(video_), out);
        QSignalSpy converted(&c, &ClipPrepController::converted);
        QVERIFY(!c.convertForPlayback(video_, 0));  // no length
        QVERIFY(c.convertForPlayback(QUrl::fromLocalFile(video_).toString(), 4.0));
        QVERIFY(c.status().startsWith("Converting"));
        QTRY_COMPARE_WITH_TIMEOUT(converted.count(), 1, 30000);
        QCOMPARE(converted.first().first().toString(), out);
        QVERIFY2(c.status().contains("copied along"), qPrintable(c.status()));
        const QStringList a = calls().value(0);
        QCOMPARE(after(a, "-t"), QString("4"));
        QCOMPARE(after(a, "-vf"), QString("scale='min(7680,iw)':-2:flags=lanczos"));
        QVERIFY(!a.contains("-an"));
        QCOMPARE(a.last(), out);
        // The same scripts, byte for byte, under the new name.
        auto bytes = [](const QString& p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
        QCOMPARE(bytes(dir_.filePath("videos/long-h264.funscript")), bytes(dir_.filePath("videos/long.funscript")));
        QCOMPARE(bytes(dir_.filePath("videos/long-h264.samn")), bytes(samn));
        QFile::remove(samn);
    }

    void aBatchFromAMarksFile() {
        QFile::remove(log_);
        const QString marks = dir_.filePath("videos/marks.json");
        Write(marks, R"({"source":"long.mp4","max_width":1920,"clips":[
            {"name":"intro","start":"0:00","end":"0:02"},
            {"name":"a/b:c","start_sec":2,"duration_sec":1.5}]})");
        ClipPrepController c;
        useFake(c);
        QVERIFY(c.cutMarks(QUrl::fromLocalFile(marks).toString()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.cutting(), 30000);
        QVERIFY2(c.status().contains("Done: 2 clip(s)"), qPrintable(c.status()));
        QVERIFY(QFile::exists(dir_.filePath("videos/intro.mp4")));
        QVERIFY(QFile::exists(dir_.filePath("videos/a_b_c.mp4")));
        QVERIFY(QFile::exists(dir_.filePath("videos/a_b_c.funscript")));
        const auto all = calls();
        QCOMPARE(all.size(), 2);
        QCOMPARE(after(all[1], "-ss"), QString("2"));
        QCOMPARE(after(all[1], "-t"), QString("1.5"));
        QCOMPARE(after(all[1], "-vf"), QString("scale='min(1920,iw)':-2:flags=lanczos"));
        QVERIFY(all[1].contains("aac"));

        Write(marks, R"({"source":"gone.mp4","clips":[{"start":0,"end":1}]})");
        QVERIFY(!c.cutMarks(marks));
        QVERIFY2(c.status().contains("not there"), qPrintable(c.status()));
        Write(marks, R"({"clips":"no"})");
        QVERIFY(!c.cutMarks(marks));
        QVERIFY(c.status().contains("cannot be used"));
    }

    void aFailureSaysWhyAndLeavesNoHalfFile() {
        ClipPrepController c;
        useFake(c);
        qputenv("FAKE_FFMPEG_FAIL", "1");
        QVERIFY(c.cut(video_, "1", "2", dir_.filePath("videos/broken.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(!c.cutting(), 30000);
        qunsetenv("FAKE_FFMPEG_FAIL");
        QVERIFY2(c.status().contains("ffmpeg failed on broken.mp4") && c.status().contains("Invalid data"), qPrintable(c.status()));
        QVERIFY(!QFile::exists(dir_.filePath("videos/broken.mp4")));
    }

    void stopRemovesTheUnfinishedClip() {
        ClipPrepController c;
        useFake(c);
        qputenv("FAKE_FFMPEG_SLOW", "1");
        const QString out = dir_.filePath("videos/stopped.mp4");
        QVERIFY(c.cut(video_, "1", "9", out));
        QTRY_VERIFY_WITH_TIMEOUT(c.progress() > 0, 30000);
        QVERIFY(c.progress() < 100);
        c.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!c.cutting(), 30000);
        qunsetenv("FAKE_FFMPEG_SLOW");
        QVERIFY2(c.status().startsWith("Stopped"), qPrintable(c.status()));
        QVERIFY(!QFile::exists(out));
    }

    void ffmpegIsFetchedWithOneClick() {
        const QByteArray bin = "#!/bin/sh\necho ffmpeg version test\n";
        FakeFileServer server;
        server.body = testzip::Zip({{"imageio_ffmpeg/__init__.py", "", false}, {"imageio_ffmpeg/binaries/ffmpeg-test", bin, true}});
        const auto sha = [](const QByteArray& b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); };
        samn::ui::ModelSource src{"ffmpeg test", server.url(), sha("not this"), "imageio_ffmpeg/binaries/ffmpeg-test", sha(bin), "ffmpeg-test"};

        ClipPrepController c;
        c.setFfmpegSource(src);
        QVERIFY(!c.ffmpegReady());
        QVERIFY(c.fetchFfmpeg());
        QVERIFY(c.fetching());
        QVERIFY(!c.fetchFfmpeg());  // one at a time
        QTRY_VERIFY(!c.fetching());
        QVERIFY2(c.toolStatus().contains("cannot be used"), qPrintable(c.toolStatus()));
        QVERIFY(!c.ffmpegReady());

        src.archive_sha256 = sha(server.body);
        c.setFfmpegSource(src);
        QVERIFY(c.fetchFfmpeg());
        QTRY_VERIFY(!c.fetching());
        QVERIFY2(c.toolStatus().contains("ready"), qPrintable(c.toolStatus()));
        QVERIFY(c.ffmpegReady());
        QCOMPARE(c.ffmpegPath(), ClipPrepController::toolsDir() + "/ffmpeg-test");
        QFile f(c.ffmpegPath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bin);
#ifdef Q_OS_UNIX
        QVERIFY(f.permissions() & QFileDevice::ExeOwner);
#endif
    }

    void anFfmpegOfTheUsersOwn() {
        ClipPrepController c;
        c.setFfmpegSource({});
        QVERIFY(!c.chooseFfmpeg(dir_.filePath("videos/nothing")));
        QVERIFY(c.toolStatus().contains("no file"));
        QVERIFY(!c.chooseFfmpeg(dir_.filePath("videos/long.funscript")));
        QVERIFY(c.toolStatus().contains("not an ffmpeg"));
#ifdef Q_OS_UNIX
        const QString own = dir_.filePath("my-ffmpeg");
        Write(own, "#!/bin/sh\necho 'ffmpeg version 9.9 test'\n");
        QFile::setPermissions(own, QFile::permissions(own) | QFileDevice::ExeOwner);
        QVERIFY2(c.chooseFfmpeg(QUrl::fromLocalFile(own).toString()), qPrintable(c.toolStatus()));
        QVERIFY(c.ffmpegReady());
        QCOMPARE(ClipPrepController().ffmpegPath(), own);  // remembered
#endif
    }

};

QTEST_GUILESS_MAIN(ClipPrepControllerTest)
#include "clip_prep_controller_test.moc"
