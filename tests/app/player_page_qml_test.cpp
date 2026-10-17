// The Player page: a seek asked for before the video is loaded (a project's
// saved position) lands once the video is there, not at 0. The clip is a
// synthetic 4 s test pattern (tests/app/data).
#include <QFile>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "icon_provider.h"
#include "player_controller.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

using samn::ui::PlayerController;

namespace {
QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // Only QML problems count (not audio or platform messages of the runner).
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
}
}  // namespace

class PlayerPageQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }

    void seekBeforeLoadLandsAfterLoad() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        run();
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // 0.7 "Feel without film": a script without a video plays on its own
    // clock; Play, Pause, seek and the playhead work as with a video.
    void scriptWithoutVideoPlaysOnItsClock() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        runScriptOnly();
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

private:
    void runScriptOnly() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                width: 1000; height: 700
                property alias page: pp
                DeviceController { id: dev }
                PlayerPage { id: pp; anchors.fill: parent; device: dev }
            })",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;  // declared first: it must outlive the root item
        window.resize(1000, 700);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* ctrl = page->property("controller").value<PlayerController*>();
        auto* clock = page->findChild<QObject*>(QStringLiteral("scriptClock"));
        QVERIFY(page && ctrl && clock);

        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("alone.funscript"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"actions":[{"at":0,"pos":0},{"at":2000,"pos":100},{"at":4000,"pos":0},{"at":6000,"pos":100}]})");
        f.close();
        QVERIFY(ctrl->openPath(script));
        QVERIFY(page->property("scriptOnly").toBool());
        QCOMPARE(clock->property("duration").toLongLong(), qint64(6000));

        QMetaObject::invokeMethod(page, "togglePlay");
        QTRY_VERIFY(page->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->playheadMs() >= 200, 3000);  // the playhead moves
        QMetaObject::invokeMethod(page, "togglePlay");
        QTRY_VERIFY(!page->property("playing").toBool());
        const qint64 held = clock->property("position").toLongLong();
        QTest::qWait(150);
        QCOMPARE(clock->property("position").toLongLong(), held);  // pause holds
        QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 4500));
        QCOMPARE(clock->property("position").toLongLong(), qint64(4500));
        QTRY_COMPARE(ctrl->playheadMs(), qint64(4500));
        // Stop: back to 0 and not playing; Previous without a list: to the start.
        QMetaObject::invokeMethod(page, "togglePlay");
        QTRY_VERIFY(page->property("playing").toBool());
        QMetaObject::invokeMethod(page, "stop");
        QTRY_VERIFY(!page->property("playing").toBool());
        QCOMPARE(clock->property("position").toLongLong(), qint64(0));
        QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 3000));
        QMetaObject::invokeMethod(page, "previous");
        QCOMPARE(clock->property("position").toLongLong(), qint64(0));
        // Previous in a list: the script before, playing on from its start.
        const QString second = dir.filePath(QStringLiteral("second.funscript"));
        QVERIFY(QFile::copy(script, second));
        QVERIFY(ctrl->openPaths({script, second}));
        QVERIFY(ctrl->pickPlaylist(1));
        QCOMPARE(ctrl->scriptName(), QStringLiteral("second.funscript"));
        QMetaObject::invokeMethod(page, "togglePlay");
        QTRY_VERIFY_WITH_TIMEOUT(clock->property("position").toLongLong() >= 200, 3000);
        QMetaObject::invokeMethod(page, "previous");
        QCOMPARE(ctrl->scriptName(), QStringLiteral("alone.funscript"));
        QTRY_VERIFY(page->property("playing").toBool());
        QVERIFY(clock->property("position").toLongLong() < 200);  // the new script from its start
        // The volume is remembered, in steps of 0.05 (0.7 pb.volume).
        auto* vol = page->findChild<QObject*>(QStringLiteral("volume"));
        QVERIFY(vol);
        QMetaObject::invokeMethod(page, "setVolume", Q_ARG(QVariant, 0.33));
        QCOMPARE(vol->property("value").toDouble(), 0.35);
        QCOMPARE(ctrl->uiValue(QStringLiteral("player.volume"), 0.8).toDouble(), 0.35);
        // A video makes the video the player again.
        QVERIFY(ctrl->openPath(QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4")));
        QVERIFY(!page->property("scriptOnly").toBool());
        QCOMPARE(clock->property("duration").toLongLong(), qint64(0));
    }

    void run() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));  // Qt 6.4 does not add it
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                width: 1000; height: 700
                property alias page: pp
                DeviceController { id: dev }
                PlayerPage { id: pp; anchors.fill: parent; device: dev }
            })",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;  // declared first: it must outlive the root item
        window.resize(1000, 700);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* page = root->property("page").value<QQuickItem*>();
        QVERIFY(page);
        auto* media = page->findChild<QMediaPlayer*>(QStringLiteral("media"));
        QVERIFY(media);
        auto* ctrl = page->property("controller").value<PlayerController*>();
        QVERIFY(ctrl);

        // Names come from files and scripts: a name like "a<b>c" must show
        // as written, so no text of the page may guess rich text (AutoText).
        // Only the Script Doctor card builds its own markup (StyledText).
        int texts = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            if (!o->inherits("QQuickText")) continue;
            ++texts;
            const int format = o->property("textFormat").toInt();  // Text.AutoText is 2
            QVERIFY2(format != 2, qPrintable(QStringLiteral("AutoText: \"%1\"").arg(o->property("text").toString())));
        }
        QVERIFY(texts > 20);

        // Nothing loaded: the seek waits instead of landing at 0.
        QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 2500));
        QCOMPARE(media->property("pendingSeekMs").toDouble(), 2500.0);
        QCOMPARE(media->position(), 0);

        // Opening a video drops a seek meant for the one before.
        QVERIFY(ctrl->openPath(QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4")));
        QCOMPARE(media->property("pendingSeekMs").toDouble(), -1.0);
        // The project case: the seek comes right after the video was set.
        // (FFmpeg may load the small clip at once; the wait above is what
        // fails without the queue. Here the end state must be right.)
        QMetaObject::invokeMethod(page, "seekTo", Q_ARG(QVariant, 2500));
        QTRY_VERIFY_WITH_TIMEOUT(media->mediaStatus() != QMediaPlayer::LoadingMedia, 10000);
        if (media->mediaStatus() == QMediaPlayer::InvalidMedia || media->mediaStatus() == QMediaPlayer::NoMedia)
            QSKIP("no media backend can decode the test clip here");
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(media->position() - 2500) <= 150, 5000);
        QCOMPARE(media->property("pendingSeekMs").toDouble(), -1.0);

        // A file that is no video: the error bar says so (0.7 0.5.41).
        QTemporaryDir dir;
        const QString broken = dir.filePath(QStringLiteral("broken.mp4"));
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();
        QVERIFY(ctrl->openPath(broken));
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->lastError().contains(QStringLiteral("cannot be played")), 10000);
        QVERIFY(ctrl->lastError().contains(QStringLiteral("broken.mp4")));
    }
};

QTEST_MAIN(PlayerPageQmlTest)
#include "player_page_qml_test.moc"
