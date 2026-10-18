// FrameCache and FrameProvider: the Editor's preview frame and frame strip
// (D-142: the video stays open, frames live in a cache with a hard limit).
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "frame_cache.h"
#include "frame_provider.h"
#include "synthetic_clips.h"

using samn::ui::FrameCache;
using samn::ui::FrameProvider;
namespace clips = samn::engine::testclips;

class FrameCacheTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString clip_;

private slots:
    void initTestCase() {
        // A space and a percent sign in the name: the provider id must survive both.
        clip_ = dir_.filePath(QStringLiteral("my clip 100%.mp4"));
        QVERIFY(clips::WriteMoving(std::filesystem::path(clip_.toStdU16String()), 4, 80.0, 1.0));
    }

    void framesAreCachedAndScaled() {
        FrameCache cache;
        const auto f = cache.At(clip_, 1000);
        QVERIFY(!f.image.isNull());
        QCOMPARE(f.image.size(), QSize(320, 240));
        QVERIFY(qAbs(f.ms - 1000) <= 40);
        QCOMPARE(int(cache.misses()), 1);
        const auto again = cache.At(clip_, 1000);
        QCOMPARE(int(cache.hits()), 1);
        QCOMPARE(again.image, f.image);
        const auto small = cache.At(clip_, 1000, 60);
        QCOMPARE(small.image.height(), 60);
        QVERIFY(cache.At(dir_.filePath("missing.mp4"), 0).image.isNull());
    }

    void stripOrderReadsOnInsteadOfSeeking() {
        FrameCache cache;
        for (int ms = 0; ms < 3000; ms += 200) QVERIFY(!cache.At(clip_, ms, 54).image.isNull());
        // One seek for the first frame; the rest read on.
        QCOMPARE(cache.seeks(), 1);
        // Backwards needs a seek again, and lands on the right frame.
        const auto back = cache.At(clip_, 500);
        QVERIFY(qAbs(back.ms - 500) <= 40);
        QCOMPARE(cache.seeks(), 2);
    }

    void theBudgetHolds() {
        FrameCache cache(400 * 1024);  // about one and a half full frames
        for (int ms = 0; ms < 2000; ms += 100) cache.At(clip_, ms);
        QVERIFY(cache.usedBytes() <= 400 * 1024);
        QVERIFY(cache.usedBytes() > 0);
    }

    void providerDecodesOffTheUiThread() {
        FrameProvider provider;
        QQuickImageResponse* r = provider.requestImageResponse(FrameProvider::idFor(clip_, 2000, 54), QSize());
        QSignalSpy done(r, &QQuickImageResponse::finished);
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(r->errorString().isEmpty());
        QQuickTextureFactory* tf = r->textureFactory();
        QVERIFY(tf);
        QCOMPARE(tf->image().height(), 54);
        delete tf;
        delete r;

        QQuickImageResponse* bad = provider.requestImageResponse(FrameProvider::idFor(dir_.filePath("nope.mp4"), 0, 54), QSize());
        QSignalSpy badDone(bad, &QQuickImageResponse::finished);
        QTRY_COMPARE(badDone.count(), 1);
        QVERIFY(!bad->errorString().isEmpty());
        delete bad;
    }
};

QTEST_MAIN(FrameCacheTest)
#include "frame_cache_test.moc"
