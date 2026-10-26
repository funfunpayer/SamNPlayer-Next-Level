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

#include "frame_provider.h"
#include "icon_provider.h"
#include "player_controller.h"
#include "samn/samn_file.h"

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

    void noScriptCardCreatesAndGoesBack() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        runNoScript();
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void noPictureSaysSoAndOffersACopy() {
        g_messages.clear();
        g_previous = qInstallMessageHandler(capture);
        runNoPicture();
        qInstallMessageHandler(g_previous);
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

private:
    // Repeater delegates and Flow children: walk childItems() too.
    static QQuickItem* named(QQuickItem* root, const char* name) {
        for (QQuickItem* child : root->childItems()) {
            if (child->objectName() == QLatin1String(name)) return child;
            if (auto* hit = named(child, name)) return hit;
        }
        return nullptr;
    }

    void runNoScript() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick
            import SamN
            Item {
                width: 1000; height: 700
                property alias page: pp
                property var asked: []
                DeviceController { id: dev }
                PlayerPage {
                    id: pp; anchors.fill: parent; device: dev
                    onCreateRequested: (video, start) => asked = asked.concat([video.toString() + "|" + start])
                }
            })",
                     QUrl("qrc:/test/Wrapper.qml"));
        QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1000, 700);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* ctrl = page->property("controller").value<PlayerController*>();
        QVERIFY(page && ctrl);
        auto asked = [&] { return root->property("asked").toStringList(); };

        QTemporaryDir dir;
        const QString video = dir.filePath("New clip.mp4");
        QVERIFY(QFile::copy(QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4"), video));
        const QString other = dir.filePath("Other.funscript");
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}]})");
        f.close();

        // A script first, then a video without one: the card, with "Back".
        QVERIFY(ctrl->openPath(other));
        QVERIFY(ctrl->openPath(video));
        QQuickItem* card = named(page, "noScriptCard");
        QVERIFY(card);
        QTRY_VERIFY(card->isVisible());
        QVERIFY(named(page, "noScriptBack")->isVisible());
        QQuickItem* still = named(page, "noScriptStill");
        QVERIFY(still->property("source").toString().startsWith(QStringLiteral("image://frames/3000/")));
        QTRY_COMPARE_WITH_TIMEOUT(still->property("status").toInt(), 1, 10000);  // Image.Ready
        if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_NOSCRIPT"); !shot.isEmpty()) {
            QTest::qWait(200);
            window.grabWindow().save(QString::fromLocal8Bit(shot));
        }

        // "Create script" asks Main to open the Generator with the video.
        QMetaObject::invokeMethod(named(page, "noScriptCreate"), "clicked");
        // Compared as URLs: Qt versions differ in how a space is written.
        auto same = [&](const QString& entry, const char* start) {
            const QString url = entry.section('|', 0, -2);
            return QUrl(url).toLocalFile() == video && entry.section('|', -1) == QLatin1String(start);
        };
        QCOMPARE(asked().size(), 1);
        QVERIFY2(same(asked().at(0), "false"), qPrintable(asked().at(0)));

        // "Back to the script" opens the last one again.
        QMetaObject::invokeMethod(named(page, "noScriptBack"), "clicked");
        QTRY_COMPARE(ctrl->scriptName(), QStringLiteral("Other.funscript"));
        QVERIFY(!card->isVisible());

        // "Always create right away": the next video without a script starts it.
        QMetaObject::invokeMethod(named(page, "noScriptAutoCreate"), "toggled", Q_ARG(bool, true));
        QVERIFY(ctrl->autoCreate());
        QVERIFY(ctrl->openPath(video));
        QTRY_COMPARE(asked().size(), 2);
        QVERIFY2(same(asked().at(1), "true"), qPrintable(asked().at(1)));
        ctrl->setAutoCreate(false);
    }

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
        // A 0.7 companion axis file beside it shows as its own lane.
        QFile twist(dir.filePath(QStringLiteral("alone.twist.funscript")));
        QVERIFY(twist.open(QIODevice::WriteOnly));
        twist.write(R"({"actions":[{"at":0,"pos":50},{"at":3000,"pos":90},{"at":6000,"pos":10}]})");
        twist.close();
        QVERIFY(ctrl->openPath(script));
        QTRY_VERIFY(named(page, "axisLane_twist"));
        QVERIFY(named(page, "axisLane_twist")->isVisible());
        QVERIFY(named(page, "axisLane_twist")->height() >= 34);
        // 0.7 legend chips: a click on a lane's name shows it in the main curve;
        // Up / Down move on (two lanes: stroke and twist, wrapping).
        auto* twistName = named(page, "laneName_twist");
        QVERIFY(twistName && twistName->isVisible());
        QTest::mouseClick(page->window(), Qt::LeftButton, {},
                          twistName->mapToScene(QPointF(twistName->width() / 2, twistName->height() / 2)).toPoint());
        QTRY_COMPARE(ctrl->activeLane(), QStringLiteral("twist"));
        QCOMPARE(named(page, "mainCurve")->property("lane").toString(), QStringLiteral("twist"));
        QCOMPARE(named(page, "mainCurveLane")->property("text").toString(), QStringLiteral("Twist"));
        QVERIFY(twistName->property("active").toBool());
        QTest::keyClick(page->window(), Qt::Key_Down);
        QTRY_COMPARE(ctrl->activeLane(), QStringLiteral("stroke"));
        QCOMPARE(named(page, "mainCurve")->property("lane").toString(), QStringLiteral("stroke"));
        // A click on the lane itself picks it too (0.7).
        auto* twistLane = named(page, "axisLane_twist");
        QTest::mouseClick(page->window(), Qt::LeftButton, {},
                          twistLane->mapToScene(QPointF(twistLane->width() / 2, twistLane->height() / 2)).toPoint());
        QTRY_COMPARE(ctrl->activeLane(), QStringLiteral("twist"));
        QVERIFY(page->property("scriptOnly").toBool());
        QCOMPARE(clock->property("duration").toLongLong(), qint64(6000));

        // 0.7 transport: the Extended-O trigger shows while it is on and
        // works only while playing.
        auto* eo = named(page, "extendedOButton");
        QVERIFY(eo && eo->isVisible());
        QVERIFY(!eo->isEnabled());
        QMetaObject::invokeMethod(page, "togglePlay");
        QTRY_VERIFY(page->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->playheadMs() >= 200, 3000);  // the playhead moves
        QTRY_VERIFY(eo->isEnabled());
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
        // Theatre (D-144): panels away, the video larger, the curve strip
        // stays and can be switched off; both choices are remembered.
        auto* lib = page->findChild<QQuickItem*>(QStringLiteral("libraryPanel"));
        auto* insp = page->findChild<QQuickItem*>(QStringLiteral("inspector"));
        auto* card = page->findChild<QQuickItem*>(QStringLiteral("videoCard"));
        auto* timeline = page->findChild<QQuickItem*>(QStringLiteral("timeline"));
        QVERIFY(lib && insp && card && timeline);
        QVERIFY(lib->isVisible() && insp->isVisible() && timeline->isVisible());
        const qreal normalW = card->width(), normalH = card->height();
        page->setProperty("theatre", true);
        QTRY_VERIFY(!lib->isVisible() && !insp->isVisible());
        QVERIFY(timeline->isVisible());
        QTRY_VERIFY(card->width() > normalW && card->height() > normalH);
        const qreal withLanes = card->height();
        page->setProperty("theatreLanes", false);
        QTRY_VERIFY(!timeline->isVisible());
        QTRY_VERIFY(card->height() > withLanes);
        QVERIFY(ctrl->uiValue(QStringLiteral("player.theatre"), false).toBool());
        QVERIFY(!ctrl->uiValue(QStringLiteral("player.theatreLanes"), true).toBool());
        page->setProperty("theatreLanes", true);
        page->setProperty("theatre", false);
        QTRY_VERIFY(lib->isVisible() && insp->isVisible() && timeline->isVisible());
        QTRY_COMPARE(card->width(), normalW);
        // A video makes the video the player again.
        QVERIFY(ctrl->openPath(QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4")));
        QVERIFY(!page->property("scriptOnly").toBool());
        QCOMPARE(clock->property("duration").toLongLong(), qint64(0));
    }

    // Owner 26 Oct: a video whose picture never comes (AV1 on a GPU without
    // an AV1 decoder) says so and offers an H.264 copy; a video that shows
    // frames never does.
    void runNoPicture() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
        engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
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
        QQuickWindow window;
        window.resize(1000, 700);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY(root);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = root->property("page").value<QQuickItem*>();
        auto* media = page->findChild<QMediaPlayer*>(QStringLiteral("media"));
        auto* ctrl = page->property("controller").value<PlayerController*>();
        QVERIFY(page && media && ctrl);
        // The empty Play button grows with the stage, within 96..140 px.
        auto* emptyPlay = page->findChild<QQuickItem*>(QStringLiteral("emptyPlay"));
        QVERIFY(emptyPlay);
        QVERIFY(emptyPlay->width() >= 96 && emptyPlay->width() <= 140);

        QVERIFY(ctrl->openPath(QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(media->mediaStatus() != QMediaPlayer::LoadingMedia, 10000);
        if (media->mediaStatus() == QMediaPlayer::InvalidMedia || media->mediaStatus() == QMediaPlayer::NoMedia)
            QSKIP("no media backend can decode the test clip here");
        auto* card = page->findChild<QQuickItem*>(QStringLiteral("noPictureCard"));
        QVERIFY(card);
        media->play();
        QTRY_VERIFY_WITH_TIMEOUT(media->position() >= 2000, 10000);
        if (!page->property("frameSeen").toBool()) QSKIP("no frames reach the video sink in this environment");
        QVERIFY(!page->property("noPicture").toBool());  // frames came: no alarm
        QVERIFY(!card->isVisible());
        media->pause();

        // No frame reaches the sink: after 1.5 s of playing the stage says so.
        page->findChild<QObject*>(QStringLiteral("frameWatch"))->setProperty("enabled", false);
        page->setProperty("frameSeen", false);
        media->setPosition(0);
        media->play();
        QTRY_VERIFY_WITH_TIMEOUT(page->property("noPicture").toBool(), 10000);
        QVERIFY(media->position() >= 1500);
        QTRY_VERIFY(card->isVisible());
        QVERIFY2(ctrl->lastError().startsWith("No picture"), qPrintable(ctrl->lastError()));
        QVERIFY(ctrl->lastError().contains("Convert to H.264"));
        auto* convert = page->findChild<QQuickItem*>(QStringLiteral("convertVideo"));
        QVERIFY(convert && convert->isVisible());
        media->stop();
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

        // A script with contact marks draws them on the picture (0.7 contact
        // overlay): tip, contact area and extra; the switch hides them.
        {
            QTemporaryDir md;
            samn::Document doc;
            doc.version = samn::SamnCurrentVersion;
            doc.kind = samn::SamnKind;
            doc.general = {{0, 10}, {1000, 90}, {2000, 10}, {3000, 90}};
            doc.duration_ms = 3000;
            samn::ContactMarks cm;
            cm.tip = samn::ContactMarkBox{10, 10, 30, 30, "glans", false};
            cm.primary = samn::ContactMarkBox{60, 40, 40, 40, "nipples", false};
            cm.extras = {samn::ContactMarkBox{120, 40, 30, 30, "mouth", true}};
            doc.contact_marks = cm;
            const QString marks = md.filePath(QStringLiteral("marks.samn"));
            samn::SaveDocument(std::filesystem::u8path(marks.toStdString()), doc);
            QVERIFY(ctrl->openScript(marks, QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4")));
            auto* layer = page->findChild<QQuickItem*>(QStringLiteral("contactMarksLayer"));
            QVERIFY(layer);
            QTRY_VERIFY(layer->isVisible());
            // Repeater items hang in the item tree, not under the page as QObjects.
            const auto mark = [layer](const char* name) -> QQuickItem* {
                for (QQuickItem* i : layer->childItems())
                    if (i->objectName() == QLatin1String(name)) return i;
                return nullptr;
            };
            QTRY_VERIFY(mark("contactMark_2"));
            auto* primary = mark("contactMark_1");
            QVERIFY(primary->width() > 5 && primary->height() > 5);
            ctrl->setShowContactMarks(false);
            QTRY_VERIFY(!layer->isVisible());
            ctrl->setShowContactMarks(true);
        }

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
