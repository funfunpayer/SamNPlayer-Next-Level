// Clip prep against the real thing, run by hand (not in ctest: it needs the
// downloads): SAMN_FFMPEG_WHEEL names this platform's imageio-ffmpeg 0.6.0
// wheel, SAMN_REAL_FFMPEG an ffmpeg (the one out of that wheel).
//   SAMN_FFMPEG_WHEEL=… SAMN_REAL_FFMPEG=… clip_prep_real_check
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "clip_prep_controller.h"
#include "model_download.h"

using samn::ui::ClipPrepController;

class ClipPrepRealCheck : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8());
    }

    // With SAMN_FFMPEG_WHEEL set to this platform's downloaded wheel: the
    // pinned hashes and the unpacking hold for the real file.
    void theRealWheelUnpacks() {
        const QString wheel = qEnvironmentVariable("SAMN_FFMPEG_WHEEL");
        QVERIFY2(!wheel.isEmpty(), "set SAMN_FFMPEG_WHEEL to the imageio-ffmpeg wheel");
        QFile f(wheel);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString error;
        const auto bin = samn::ui::ExtractModel(f.readAll(), samn::ui::FfmpegSource(), &error);
        QVERIFY2(bin, qPrintable(error));
        QVERIFY(bin->size() > 50'000'000);
    }

    // With SAMN_REAL_FFMPEG set to an ffmpeg: a real cut of a made-up video,
    // 1920 wide in, 1280 wide and the cut's length out.
    void aRealCutWithARealFfmpeg() {
        const QString ffmpeg = qEnvironmentVariable("SAMN_REAL_FFMPEG");
        QVERIFY2(!ffmpeg.isEmpty(), "set SAMN_REAL_FFMPEG to an ffmpeg");
        const QString src = dir_.filePath("real/source.mp4");
        QDir().mkpath(dir_.filePath("real"));
        QProcess make;
        make.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=30", "-f", "lavfi",
                            "-i", "sine=frequency=440", "-t", "6", "-c:v", "libx264", "-preset", "ultrafast", "-c:a", "aac", src});
        QVERIFY(make.waitForFinished(120000) && make.exitCode() == 0);
        ClipPrepController c;
        QVERIFY2(c.chooseFfmpeg(ffmpeg), qPrintable(c.toolStatus()));
        c.setPreset("720p");
        QVERIFY(c.cut(src, "1", "0:03.5"));
        QTRY_VERIFY_WITH_TIMEOUT(!c.cutting(), 120000);
        QVERIFY2(c.status().startsWith("Done"), qPrintable(c.status()));
        QProcess probe;
        probe.start(ffmpeg, {"-hide_banner", "-i", c.lastOutput()});
        QVERIFY(probe.waitForFinished(30000));
        const QString info = QString::fromUtf8(probe.readAllStandardError());
        QVERIFY2(info.contains("Duration: 00:00:02.5"), qPrintable(info));
        QVERIFY2(info.contains("1280x720"), qPrintable(info));
        QVERIFY2(info.contains("Audio: aac"), qPrintable(info));
    }
};

QTEST_GUILESS_MAIN(ClipPrepRealCheck)
#include "clip_prep_real_check.moc"
