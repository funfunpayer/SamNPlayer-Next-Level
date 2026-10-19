// The QML shell (Main.qml with the top bar, the mode deck and the Player page)
// and the bundled vector assets. Ports of the 0.7 UI tests flow_assets_test,
// flow_wiring_test, pb_tools_toggle_test and tools_nav_test (TEST_PARITY 52-55).
// Synthetic data only; no video is opened.
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "device_controller.h"
#include <memory>

#include "frame_provider.h"
#include "icon_provider.h"
#include "player_controller.h"
#include "update_checker.h"

Q_IMPORT_QML_PLUGIN(SamNPlugin)

namespace {

constexpr int kWait = 10000;

QStringList g_messages;
QtMessageHandler g_previous = nullptr;
void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // Only QML problems count: platform and audio messages differ per machine.
    const bool fromQml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:")) ||
                         (ctx.category && QLatin1String(ctx.category).startsWith(QLatin1String("qt.qml")));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && fromQml) g_messages << msg;
    if (g_previous) g_previous(type, ctx, msg);
}

bool isType(const QObject* o, const char* qmlType) {
    return QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String(qmlType) + "_QMLTYPE");
}

// Every item below `root` by the visual parent (QObject children are not the visual tree in QML).
QList<QQuickItem*> descendants(QQuickItem* root) {
    QList<QQuickItem*> out;
    for (QQuickItem* c : root->childItems()) out << c << descendants(c);
    return out;
}

QList<QQuickItem*> itemsOfType(QQuickItem* root, const char* qmlType) {
    QList<QQuickItem*> out;
    for (QQuickItem* i : descendants(root))
        if (isType(i, qmlType)) out << i;
    return out;
}

// Every bundled vector file: the resource path and its bytes.
QMap<QString, QByteArray> bundledSvgs() {
    QMap<QString, QByteArray> out;
    QDirIterator it(QStringLiteral(":/qt/qml/SamN/qml"), {"*.svg"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) out.insert(path, f.readAll());
    }
    return out;
}

// What is wrong with one SVG ("" = nothing). Same rules as the 0.7 asset test.
QString svgProblem(const QByteArray& bytes) {
    QXmlStreamReader xml(bytes);
    bool sawRoot = false;
    static const QRegularExpression handler(QStringLiteral("^on[a-z]+$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression remoteUrl(QStringLiteral("url\\(\\s*['\"]?\\s*(https?:)?//|url\\(\\s*['\"]?\\s*https?:"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression remoteRef(QStringLiteral("^\\s*(https?:)?//"), QRegularExpression::CaseInsensitiveOption);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const QString tag = xml.name().toString().toLower();
        if (!sawRoot) {
            sawRoot = true;
            if (tag != "svg") return "the root element is <" + tag + ">, not <svg>";
            if (xml.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg")) return "the root is not in the SVG namespace";
            if (!xml.attributes().hasAttribute("viewBox")) return "the root has no viewBox";
        }
        if (tag == "script") return "contains <script>";
        if (tag == "foreignobject") return "contains <foreignObject>";
        for (const QXmlStreamAttribute& a : xml.attributes()) {
            const QString name = a.name().toString();
            const QString value = a.value().toString();
            if (handler.match(name).hasMatch()) return "event handler attribute " + name;
            if (name == "href" && remoteRef.match(value).hasMatch()) return "href to the network: " + value;
            if (remoteUrl.match(value).hasMatch()) return "url() to the network in " + name;
        }
    }
    if (xml.hasError()) return "not well-formed: " + xml.errorString();
    if (!sawRoot) return "empty document";
    // <style> text and anything else that holds url(http...) or @import.
    const QString text = QString::fromUtf8(bytes);
    if (remoteUrl.match(text).hasMatch()) return "url() to the network";
    if (text.contains("@import", Qt::CaseInsensitive)) return "@import";
    return {};
}

// A running shell: the engine is declared first, so it outlives the window.
struct Shell {
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow* window = nullptr;
    QQuickItem* root() const { return window->contentItem(); }
    QQuickItem* one(const char* qmlType) const {
        const auto all = itemsOfType(root(), qmlType);
        return all.isEmpty() ? nullptr : all.first();
    }
    int mode() const { return window->property("mode").toInt(); }
    bool deckOpen() const { return one("ModeDeck")->property("open").toBool(); }
    QString pillName() const { return one("TopBar")->property("modeName").toString(); }
};

void loadShell(Shell& s, int startMode = 0) {
    s.engine.addImportPath(QStringLiteral("qrc:/qt/qml"));  // Qt 6.4 does not add it
    s.engine.addImageProvider(QStringLiteral("icon"), new samn::ui::IconProvider);
    s.engine.addImageProvider(QStringLiteral("frames"), new samn::ui::FrameProvider);
    s.engine.rootContext()->setContextProperty("startupPaths", QStringList());
    s.engine.rootContext()->setContextProperty("startMode", startMode);
    s.engine.rootContext()->setContextProperty("trainingStart", QString());
    QQmlComponent comp(&s.engine, QUrl(QStringLiteral("qrc:/qt/qml/SamN/qml/Main.qml")));
    QVERIFY2(comp.status() == QQmlComponent::Ready, qPrintable(comp.errorString()));
    s.object.reset(comp.create());
    QVERIFY2(s.object, qPrintable(comp.errorString()));
    s.window = qobject_cast<QQuickWindow*>(s.object.get());
    QVERIFY(s.window);
    s.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(s.window));
    s.window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(s.window));
}

}  // namespace

class ShellQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

    // The mode pill of the top bar, in window coordinates.
    static QPoint pillCentre(const Shell& s) { return QPoint(s.window->width() / 2, 43); }
    // The four cards of the open deck, left to right (window coordinates).
    static QList<QPoint> cardCentres(const Shell& s) {
        QList<QPoint> out;
        QQuickItem* deck = s.one("ModeDeck");
        for (QQuickItem* i : descendants(deck))
            if (QString(i->metaObject()->className()).startsWith("QQuickMouseArea") && i->cursor().shape() == Qt::PointingHandCursor)
                out << i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint();
        std::sort(out.begin(), out.end(), [](const QPoint& a, const QPoint& b) { return a.x() < b.x(); });
        return out;
    }
    // Opens the deck with the pill and waits until it is shown.
    static void openDeckWithPill(Shell& s) {
        // A deck still fading out covers the pill: wait until it is gone.
        QTRY_VERIFY_WITH_TIMEOUT(!s.one("ModeDeck")->isVisible(), kWait);
        QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, pillCentre(s));
        QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(s.one("ModeDeck")->isVisible(), kWait);
    }

    void serveRelease(const char* tag) {
        QFile f(config_.path() + "/latest.json");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QByteArray("{\"tag_name\":\"") + tag + "\",\"body\":\"Faster start\"}");
        f.close();
        qputenv("SAMN_UPDATE_URL", QUrl::fromLocalFile(f.fileName()).toString().toUtf8());
    }

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
        // The startup update check reads a local answer, never the network;
        // a patch tag is no release, so no test sees the banner by chance.
        serveRelease("patch-v1.0.0-p1");
        g_previous = qInstallMessageHandler(capture);
    }
    void cleanupTestCase() { qInstallMessageHandler(g_previous); }
    // Every test starts from the default settings (panels expanded).
    void init() {
        QFile::remove(config_.path() + "/settings.json");
        g_messages.clear();
    }

    // --- item 52: flow_assets_test ---
    void bundledVectorAssetsAreSafe() {
        const auto svgs = bundledSvgs();
        QVERIFY2(svgs.size() >= 70, qPrintable(QString::number(svgs.size())));  // 71 icons and 9 assets at the time of writing
        for (auto it = svgs.cbegin(); it != svgs.cend(); ++it) {
            const QString problem = svgProblem(it.value());
            QVERIFY2(problem.isEmpty(), qPrintable(it.key() + ": " + problem));
        }
        // Every SVG in the source tree is bundled (an asset missing from the
        // CMake list would show as a broken image).
        QDirIterator disk(QStringLiteral(SAMN_APP_QML_DIR), {"*.svg"}, QDir::Files, QDirIterator::Subdirectories);
        QStringList onDisk;
        while (disk.hasNext()) onDisk << QFileInfo(disk.next()).fileName();
        QStringList bundled;
        for (auto it = svgs.cbegin(); it != svgs.cend(); ++it) bundled << QFileInfo(it.key()).fileName();
        onDisk.sort();
        bundled.sort();
        QCOMPARE(bundled, onDisk);
        // The checker itself: each kind of problem is found.
        const char* head = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"";
        QVERIFY(svgProblem(QByteArray(head) + "><path d=\"M0 0\"/></svg>").isEmpty());
        QVERIFY(!svgProblem(QByteArray(head) + "><path d=\"M0 0\"></svg>").isEmpty());                   // not well-formed
        QVERIFY(!svgProblem(QByteArray(head) + "><script>x()</script></svg>").isEmpty());                // script
        QVERIFY(!svgProblem(QByteArray(head) + " onload=\"x()\"></svg>").isEmpty());                     // handler
        QVERIFY(!svgProblem(QByteArray(head) + "><image href=\"https://example.invalid/a.png\"/></svg>").isEmpty());
        QVERIFY(!svgProblem(QByteArray(head) + "><rect fill=\"url(http://example.invalid/g)\"/></svg>").isEmpty());
        QVERIFY(!svgProblem("<svg xmlns=\"http://www.w3.org/2000/svg\"/>").isEmpty());                   // no viewBox
    }

    // Every icon name written in a QML file is one of the bundled icons.
    void iconNamesInTheQmlSourcesExist() {
        QStringList icons;
        const auto svgs = bundledSvgs();
        for (auto it = svgs.cbegin(); it != svgs.cend(); ++it)
            if (it.key().contains("/icons/")) icons << QFileInfo(it.key()).completeBaseName();
        QVERIFY(icons.size() >= 60);
        static const QRegularExpression use(QStringLiteral("\\b(?:icon|modeIcon|name)\\s*:\\s*\"(\\w+)\""));
        int found = 0;
        QDirIterator qml(QStringLiteral(SAMN_APP_QML_DIR), {"*.qml"}, QDir::Files);
        while (qml.hasNext()) {
            QFile f(qml.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            auto m = use.globalMatch(QString::fromUtf8(f.readAll()));
            while (m.hasNext()) {
                const QString name = m.next().captured(1);
                ++found;
                QVERIFY2(icons.contains(name), qPrintable(QFileInfo(f).fileName() + ": no icon \"" + name + "\""));
            }
        }
        QVERIFY(found > 30);
    }

    // --- item 53: flow_wiring_test ---
    void shellLoadsWithoutWarningsAndShowsItsIcons() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QTest::qWait(300);
            QCOMPARE(s.window->title(), QString("SamNPlayer 2"));
            QCOMPARE(s.mode(), 0);
            QVERIFY(!s.deckOpen());
            QCOMPARE(s.pillName(), QString("Player"));
            QCOMPARE(s.one("TopBar")->property("modeIcon").toString(), QString("player"));
            QVERIFY(s.one("PlayerPage"));
            QVERIFY(s.one("LibraryPanel"));
            QVERIFY(s.one("InspectorPanel"));

            // Every icon the shell shows has a name that exists and has loaded.
            int shown = 0;
            QMap<QString, int> perPart;
            for (QQuickItem* icon : itemsOfType(s.root(), "Icon")) {
                const QString name = icon->property("name").toString();
                const QString source = icon->property("source").toUrl().toString();
                if (name.isEmpty()) {
                    // An empty name is allowed only on an icon that is not shown (IconButton without icon).
                    QVERIFY2(!icon->isVisible() || icon->width() <= 0, "a visible icon without a name");
                    QVERIFY(source.isEmpty());
                    continue;
                }
                QVERIFY2(!samn::ui::IconProvider::svgFor(name, Qt::white).isEmpty(), qPrintable("no icon file for \"" + name + "\""));
                if (!icon->isVisible()) continue;
                ++shown;
                QTRY_COMPARE_WITH_TIMEOUT(icon->property("status").toInt(), 1, kWait);  // Image.Ready
                for (QQuickItem* p = icon; p; p = p->parentItem())
                    for (const char* part : {"TopBar", "ModeDeck", "LibraryPanel", "InspectorPanel", "PlayerPage"})
                        if (isType(p, part)) ++perPart[part];
            }
            QVERIFY(shown > 10);
            // The top bar (pill icon and chevron), the library and the player show icons.
            QVERIFY2(perPart["TopBar"] >= 2, qPrintable(QString::number(perPart["TopBar"])));
            QVERIFY2(perPart["LibraryPanel"] >= 5, qPrintable(QString::number(perPart["LibraryPanel"])));
            QVERIFY2(perPart["PlayerPage"] >= 5, qPrintable(QString::number(perPart["PlayerPage"])));

            // The three mode cards each have their own icon: the ones of the modes list.
            openDeckWithPill(s);
            const QVariantList modes = s.one("ModeDeck")->property("modes").toList();
            QCOMPARE(modes.size(), 3);
            QStringList deckIcons;
            for (QQuickItem* icon : itemsOfType(s.one("ModeDeck"), "Icon")) deckIcons << icon->property("name").toString();
            QStringList expected;
            for (const QVariant& m : modes) {
                QVERIFY(!m.toMap().value("icon").toString().isEmpty());
                expected << m.toMap().value("icon").toString();
            }
            QCOMPARE(deckIcons, expected);
            QCOMPARE(expected, (QStringList{"player", "create", "training"}));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void everyModeLoadsWithoutWarnings() {
        for (int mode = 0; mode < 3; ++mode) {
            {
                Shell s;
                loadShell(s, mode);  // the SAMN_MODE test hook
                QVERIFY(s.window);
                QCOMPARE(s.mode(), mode);
                QCOMPARE(s.pillName(), s.window->property("modeNames").toStringList().value(mode));
                QTest::qWait(150);
            }
            QVERIFY2(g_messages.isEmpty(), qPrintable(QString::number(mode) + ": " + g_messages.join("\n")));
        }
    }

    void modeDeckOpensWithMAndPicksWithNumbers() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            const QStringList names = s.window->property("modeNames").toStringList();
            QCOMPARE(names, (QStringList{"Player", "Generator", "Training"}));
            QVERIFY(!s.deckOpen());
            // M opens, M again closes (it is a toggle).
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QTRY_VERIFY_WITH_TIMEOUT(s.one("ModeDeck")->isVisible(), kWait);
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
            // Esc closes an open deck; with no deck it does nothing.
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
            QTRY_VERIFY_WITH_TIMEOUT(!s.one("ModeDeck")->isVisible(), kWait);  // faded out
            QTest::keyClick(s.window, Qt::Key_Escape);
            QVERIFY(!s.deckOpen());
            QCOMPARE(s.mode(), 0);

            // The number keys pick a mode only while the deck is open: they close it.
            QTest::keyClick(s.window, Qt::Key_3);
            QTest::qWait(150);
            QCOMPARE(s.mode(), 0);  // closed deck: the key belongs to the Player
            for (int key : {Qt::Key_3, Qt::Key_2, Qt::Key_1}) {
                QTest::keyClick(s.window, Qt::Key_M);
                QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
                QTest::keyClick(s.window, Qt::Key(key));
                const int want = key - Qt::Key_1;
                QTRY_COMPARE_WITH_TIMEOUT(s.mode(), want, kWait);
                QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
                QCOMPARE(s.pillName(), names[want]);
                QCOMPARE(s.one("TopBar")->property("modeIcon").toString(),
                         s.one("ModeDeck")->property("modes").toList()[want].toMap().value("icon").toString());
            }
            // There is no fourth mode (the 0.7 Editor is the Generator, the Scene Map a
            // view in it, D-145): key 4 leaves the deck open.
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QTest::keyClick(s.window, Qt::Key_4);
            QTest::qWait(150);
            QVERIFY(s.deckOpen());
            QCOMPARE(s.mode(), 0);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // --- item 55: tools_nav_test, the parts the mode deck has ---
    // 0.7 tools_nav_test.py (roving tabs) for the mode deck: arrows and
    // Home/End move the focus ring, Tab stays inside, Enter or Space picks,
    // and focus returns where it was. While the deck is open the Player's
    // keys (Space, arrows) do not act behind it.
    void modeDeckKeyboard() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* deck = s.one("ModeDeck");
            QVERIFY(deck);
            auto ringAt = [&]() {
                int at = -1, n = 0;
                for (QQuickItem* i : descendants(deck))
                    if (i->objectName() == QLatin1String("focusRing")) {
                        if (i->isVisible()) at = n;
                        ++n;
                    }
                return at;
            };
            QQuickItem* before = s.window->activeFocusItem();
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QTRY_VERIFY_WITH_TIMEOUT(deck->hasActiveFocus(), kWait);
            QCOMPARE(deck->property("focusIndex").toInt(), 0);  // starts on the current mode
            QTRY_COMPARE_WITH_TIMEOUT(ringAt(), 0, kWait);
            QTest::keyClick(s.window, Qt::Key_Right);
            QTest::keyClick(s.window, Qt::Key_Right);
            QCOMPARE(deck->property("focusIndex").toInt(), 2);
            QCOMPARE(ringAt(), 2);
            QTest::keyClick(s.window, Qt::Key_Left);
            QCOMPARE(deck->property("focusIndex").toInt(), 1);
            QTest::keyClick(s.window, Qt::Key_End);
            QCOMPARE(deck->property("focusIndex").toInt(), 2);  // the last of three
            QTest::keyClick(s.window, Qt::Key_Right);  // wraps
            QCOMPARE(deck->property("focusIndex").toInt(), 0);
            QTest::keyClick(s.window, Qt::Key_Home);
            QTest::keyClick(s.window, Qt::Key_Tab);  // Tab stays inside the deck
            QCOMPARE(deck->property("focusIndex").toInt(), 1);
            QVERIFY(deck->hasActiveFocus());
            QTest::keyClick(s.window, Qt::Key_Backtab);
            QCOMPARE(deck->property("focusIndex").toInt(), 0);
            // Space picks while the deck is open (it does not toggle play behind it).
            QTest::keyClick(s.window, Qt::Key_Down);
            QTest::keyClick(s.window, Qt::Key_Down);
            QTest::keyClick(s.window, Qt::Key_Space);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 2, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
            QCOMPARE(s.window->activeFocusItem(), before);  // focus came back
            // Enter picks too, starting on the current mode (2).
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QCOMPARE(deck->property("focusIndex").toInt(), 2);
            QTest::keyClick(s.window, Qt::Key_Left);
            QTest::keyClick(s.window, Qt::Key_Left);
            QTest::keyClick(s.window, Qt::Key_Return);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // 0.7 top bar: the device chip (LED, name, battery, Connect /
    // Disconnect; a click opens the device controls) and the version.
    void topBarDeviceChipAndVersion() {
        QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7-dev"));
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name)) return i;
                return nullptr;
            };
            QQuickItem* label = byName("deviceChipLabel");
            QQuickItem* button = byName("deviceChipButton");
            QVERIFY(label && button && byName("versionLabel"));
            QCOMPARE(byName("versionLabel")->property("text").toString(), QStringLiteral("v9.8.7-dev"));
            QCOMPARE(label->property("text").toString(), QStringLiteral("Not connected"));
            QCOMPARE(button->property("text").toString(), QStringLiteral("Connect"));

            auto* dev = s.window->findChild<samn::ui::DeviceController*>();
            QVERIFY(dev);
            dev->connectMock();
            QTRY_VERIFY_WITH_TIMEOUT(label->property("text").toString().startsWith(QStringLiteral("Mock device")), kWait);
            QCOMPARE(button->property("text").toString(), QStringLiteral("Disconnect"));

            // A click on the chip opens the Player with the Inspector on Devices.
            QQuickItem* inspector = s.one("InspectorPanel");
            inspector->setProperty("collapsed", true);
            inspector->setProperty("tab", 2);
            s.window->setProperty("mode", 2);
            QTRY_COMPARE(s.mode(), 2);
            QQuickItem* chip = byName("deviceChip");
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, chip->mapToScene(QPointF(14, chip->height() / 2)).toPoint());
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QVERIFY(!inspector->property("collapsed").toBool());
            QCOMPARE(inspector->property("tab").toInt(), 0);

            // The button disconnects.
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier,
                              button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_COMPARE_WITH_TIMEOUT(label->property("text").toString(), QStringLiteral("Not connected"), kWait);
        }
        QCoreApplication::setApplicationVersion(QString());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void modeDeckPillCardsAndCurrentMark() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QVERIFY(!s.deckOpen());
            QCOMPARE(s.pillName(), QString("Player"));
            openDeckWithPill(s);
            // Four cards; the current mode is marked (a thicker, coloured frame), the others are not.
            auto cards = [&] {
                QList<QQuickItem*> out;
                for (QQuickItem* i : descendants(s.one("ModeDeck")))
                    if (i->property("modelData").isValid() && i->property("index").isValid() && i->width() > 100) out << i;
                std::sort(out.begin(), out.end(), [](QQuickItem* a, QQuickItem* b) { return a->property("index").toInt() < b->property("index").toInt(); });
                return out;
            };
            auto borderWidth = [](QQuickItem* card) {
                return qvariant_cast<QObject*>(card->property("border"))->property("width").toDouble();
            };
            QCOMPARE(cards().size(), 3);
            QCOMPARE(s.one("ModeDeck")->property("current").toInt(), 0);
            QTRY_VERIFY_WITH_TIMEOUT(borderWidth(cards()[0]) > borderWidth(cards()[1]), kWait);
            for (int i = 1; i < 3; ++i) QCOMPARE(borderWidth(cards()[i]), borderWidth(cards()[1]));
            QCOMPARE(cardCentres(s).size(), 3);

            // A click outside the card closes the deck and changes nothing.
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, QPoint(40, s.window->height() - 40));
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
            QCOMPARE(s.mode(), 0);

            // A click on a card picks that mode, closes the deck and marks the card.
            openDeckWithPill(s);
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, cardCentres(s)[2]);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 2, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(!s.deckOpen(), kWait);
            QCOMPARE(s.pillName(), QString("Training"));
            QCOMPARE(s.one("ModeDeck")->property("current").toInt(), 2);
            QTRY_VERIFY_WITH_TIMEOUT(borderWidth(cards()[2]) > borderWidth(cards()[0]), kWait);
            // Back to the Player: its page is shown again.
            openDeckWithPill(s);
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, cardCentres(s)[0]);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QCOMPARE(s.pillName(), QString("Player"));
            QTRY_VERIFY_WITH_TIMEOUT(s.one("PlayerPage")->isVisible(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // 0.7: the pages of the other modes are hidden while one mode shows.
    // D-146: the gear opens Settings over the current mode (0.7 Tools →
    // Settings / Log / About); tabs switch, Esc and × close, the mode stays.
    void gearOpensSettingsOverTheMode() {
        {
            Shell s;
            loadShell(s, 2);  // Training
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name)) return i;
                return nullptr;
            };
            QQuickItem* page = s.one("SettingsPage");
            QVERIFY(page);
            QVERIFY(!page->isVisible());
            QQuickItem* gear = byName("topSettingsButton");
            QVERIFY(gear && gear->isVisible());
            QTest::mouseClick(s.window, Qt::LeftButton, {}, gear->mapToScene(QPointF(gear->width() / 2, gear->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(page->isVisible() && page->property("opacity").toDouble() > 0.99, kWait);
            QVERIFY(byName("generalTab")->isVisible());
            QVERIFY(!byName("logTab")->isVisible());
            page->setProperty("tab", 1);
            QTRY_VERIFY_WITH_TIMEOUT(byName("deviceTab")->isVisible(), kWait);
            page->setProperty("tab", 2);
            QTRY_VERIFY_WITH_TIMEOUT(byName("logTab")->isVisible(), kWait);
            page->setProperty("tab", 3);
            QTRY_VERIFY_WITH_TIMEOUT(byName("aboutTab")->isVisible(), kWait);
            QVERIFY(byName("legalText")->property("text").toString().contains("GNU GENERAL PUBLIC LICENSE"));
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_SETTINGS"); !shot.isEmpty()) {
                page->setProperty("tab", 0);
                QTest::qWait(300);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
            // Esc closes; the mode underneath is unchanged, and M is not taken by the page.
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!page->isVisible(), kWait);
            QCOMPARE(s.mode(), 2);
            // Ctrl+, opens it again; × closes.
            QTest::keyClick(s.window, Qt::Key_Comma, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), kWait);
            QQuickItem* close = byName("settingsClose");
            QTest::mouseClick(s.window, Qt::LeftButton, {}, close->mapToScene(QPointF(close->width() / 2, close->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(!page->isVisible(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void settingsDeviceTabTestsTheMockDevice() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name)) return i;
                return nullptr;
            };
            QQuickItem* page = s.one("SettingsPage");
            page->setProperty("open", true);
            page->setProperty("tab", 1);
            QQuickItem* tab = byName("deviceTab");
            QTRY_VERIFY_WITH_TIMEOUT(tab->isVisible(), kWait);
            QQuickItem* vib = byName("testVibration");
            QQuickItem* pulse = byName("testPulse");
            QVERIFY(vib && pulse);
            QVERIFY(byName("deviceStatus")->property("text").toString().startsWith(QStringLiteral("Not connected")));
            QVERIFY(!vib->isEnabled());  // nothing to test without a device
            QVERIFY(!pulse->isEnabled());

            auto* dev = s.window->findChild<samn::ui::DeviceController*>();
            QVERIFY(dev);
            QSignalSpy log(dev, &samn::ui::DeviceController::logMessage);
            dev->connectMock();
            QTRY_VERIFY_WITH_TIMEOUT(byName("deviceStatus")->property("text").toString().startsWith(QStringLiteral("Connected · Mock device")), kWait);
            QTRY_VERIFY(vib->isEnabled());
            auto lines = [&] {
                QStringList out;
                for (const auto& a : log) out << a.at(0).toString();
                return out;
            };

            // The slider drives the device; the label shows 0.7's level.
            vib->setProperty("value", 50.0);
            QVERIFY(QMetaObject::invokeMethod(vib, "moved", Q_ARG(qreal, 50.0)));
            QTRY_VERIFY_WITH_TIMEOUT(lines().join("\n").contains("[mock] vibration=0.50"), kWait);
            QCOMPARE(byName("testVibrationLabel")->property("text").toString(), QStringLiteral("50 % (level 5)"));

            // Raw value: above the documented maximum is flagged, and sent as is.
            byName("rawValue")->setProperty("text", "20");
            QCOMPARE(byName("rawHint")->property("text").toString(), QStringLiteral("above documented maximum (10)"));
            QQuickItem* send = byName("rawSend");
            QTest::mouseClick(s.window, Qt::LeftButton, {}, send->mapToScene(QPointF(send->width() / 2, send->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(lines().join("\n").contains("[mock] vibration_raw=20"), kWait);
            byName("rawValue")->setProperty("text", "4");
            QCOMPARE(byName("rawHint")->property("text").toString(), QString());

            // A session blocks the test; All off still works.
            dev->setSessionActive(true);
            QTRY_VERIFY(!vib->isEnabled());
            QVERIFY(byName("testAllOff")->isEnabled());
            dev->setSessionActive(false);

            // The diagnostics report becomes 0.7's table.
            QQuickItem* table = byName("diagTable");
            QVERIFY(!table->isVisible());
            const QString report = QStringLiteral(
                R"({"phases":[{"phase":"raw_sweep_vibration","commands":12,"errors":1,"meanLatencyMs":4.25,"maxLatencyMs":9.5}],)"
                R"("notes":["Raw values above 10 were accepted."],"interrupted":true})");
            QVERIFY(QMetaObject::invokeMethod(tab, "showReport", Q_ARG(QVariant, report)));
            QTRY_VERIFY(table->isVisible());
            QStringList cells;
            for (QQuickItem* c : table->childItems())
                if (c->isVisible() && c->property("text").isValid()) cells << c->property("text").toString();
            QCOMPARE(cells.mid(5), QStringList({"raw sweep vibration", "12", "1", "4.3 ms", "9.5 ms"}));
            QCOMPARE(byName("diagStatus")->property("text").toString(), QStringLiteral("Done."));
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_DEVICE"); !shot.isEmpty()) {
                QTest::qWait(300);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
            dev->disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!vib->isEnabled(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void updateBannerAndSettingsCard() {
        QCoreApplication::setApplicationVersion("2.0.0-dev");
        serveRelease("v1.0.0");
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name)) return i;
                return nullptr;
            };
            auto* updates = s.window->findChild<samn::ui::UpdateChecker*>();
            QVERIFY(updates);
            QQuickItem* banner = byName("updateBanner");
            QVERIFY(banner);
            // The startup check found v1.0.0: nothing newer, no banner.
            QTRY_COMPARE_WITH_TIMEOUT(updates->state(), QStringLiteral("current"), kWait);
            QVERIFY(!banner->isVisible());

            serveRelease("v99.0.0");
            updates->check(true);
            QTRY_VERIFY_WITH_TIMEOUT(banner->isVisible() && banner->property("opacity").toDouble() > 0.99, kWait);
            QVERIFY(byName("updateBannerText")->property("text").toString().startsWith(QStringLiteral("SamNPlayer v99.0.0 is available")));
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_BANNER"); !shot.isEmpty()) s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            QQuickItem* close = byName("updateBannerClose");
            QTest::mouseClick(s.window, Qt::LeftButton, {}, close->mapToScene(QPointF(close->width() / 2, close->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(!banner->isVisible(), kWait);

            // Settings → General: the toggle, "Check now", the status and notes.
            QQuickItem* page = s.one("SettingsPage");
            page->setProperty("open", true);
            QTRY_VERIFY_WITH_TIMEOUT(page->property("opacity").toDouble() > 0.99 && byName("updateCheckNow")->isVisible(), kWait);
            // The Updates card is the last one: scroll it into view.
            QQuickItem* general = byName("generalTab");
            general->setProperty("contentY", qMax(0.0, general->property("contentHeight").toDouble() - general->height()));
            QTest::qWait(50);
            QVERIFY(byName("updateCheckOnStartup")->property("checked").toBool());
            serveRelease("v1.0.0");
            QQuickItem* now = byName("updateCheckNow");
            QVERIFY(now->isEnabled());
            QVERIFY(now->mapToScene(QPointF(0, now->height())).y() < general->mapToScene(QPointF(0, general->height())).y());
            QTest::mouseClick(s.window, Qt::LeftButton, {}, now->mapToScene(QPointF(now->width() / 2, now->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(byName("updateStatus")->property("text").toString().startsWith(QStringLiteral("No update available")), kWait);
            QVERIFY(!byName("updateOpenPage")->isVisible());
            serveRelease("v99.0.0");
            updates->check();
            QTRY_VERIFY_WITH_TIMEOUT(byName("updateOpenPage")->isVisible(), kWait);
            QCOMPARE(byName("updateNotes")->property("text").toString(), QStringLiteral("Faster start"));
            QVERIFY(!banner->isVisible());  // a manual check does not bring the banner back
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_UPDATE"); !shot.isEmpty()) {
                QTest::qWait(300);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
        }
        serveRelease("patch-v1.0.0-p1");
        QCoreApplication::setApplicationVersion(QString());
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void createScriptOpensTheGenerator() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* player = s.one("PlayerPage");
            QQuickItem* generator = s.one("GeneratorPage");
            QTemporaryDir dir;
            const QString video = dir.filePath("clip.mp4");
            QVERIFY(QFile::copy(QStringLiteral(SAMN_APP_QML_DIR "/../../tests/app/data/testpattern_4s.mp4"), video));
            QVERIFY(QMetaObject::invokeMethod(player, "createRequested", Q_ARG(QUrl, QUrl::fromLocalFile(video)), Q_ARG(bool, false)));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 1, kWait);
            auto* gen = generator->property("controller").value<QObject*>();
            QVERIFY(gen);
            QTRY_VERIFY_WITH_TIMEOUT(gen->property("hasVideo").toBool(), kWait);
            QCOMPARE(QFileInfo(gen->property("videoPath").toString()).fileName(), QStringLiteral("clip.mp4"));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void onlyTheChosenModesPageShows() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* player = s.one("PlayerPage");
            QQuickItem* training = s.one("TrainingPage");
            QVERIFY(player && training);
            QVERIFY(player->isVisible());
            QVERIFY(!training->isVisible());
            QTest::keyClick(s.window, Qt::Key_M);
            QTRY_VERIFY_WITH_TIMEOUT(s.deckOpen(), kWait);
            QTest::keyClick(s.window, Qt::Key_3);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 2, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(training->isVisible() && !player->isVisible(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // --- item 54: pb_tools_toggle_test ---
    void libraryAndInspectorCollapseAndTheVideoGrows() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* library = s.one("LibraryPanel");
            QQuickItem* inspector = s.one("InspectorPanel");
            QQuickItem* video = nullptr;
            for (QQuickItem* i : descendants(s.root()))
                if (i->inherits("QQuickVideoOutput")) video = i;
            QVERIFY(library && inspector && video);
            // The collapse button: the 28 px round control at the top of each column.
            auto toggleOf = [](QQuickItem* panel) {
                for (QQuickItem* i : descendants(panel))
                    if (QString(i->metaObject()->className()).startsWith("QQuickMouseArea") && i->parentItem() &&
                        i->parentItem()->width() == 28 && i->parentItem()->height() == 28 && i->parentItem()->y() == 10)
                        return i;
                return static_cast<QQuickItem*>(nullptr);
            };
            auto click = [&](QQuickItem* area) {
                QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier,
                                  area->mapToScene(QPointF(area->width() / 2, area->height() / 2)).toPoint());
            };
            QVERIFY(toggleOf(library) && toggleOf(inspector));

            QTRY_COMPARE_WITH_TIMEOUT(library->width(), 248.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(inspector->width(), 320.0, kWait);
            QVERIFY(!library->property("collapsed").toBool());
            QVERIFY(!inspector->property("collapsed").toBool());
            const qreal wide = video->width();
            QVERIFY(wide > 100);

            click(toggleOf(library));
            QTRY_COMPARE_WITH_TIMEOUT(library->width(), 44.0, kWait);
            QVERIFY(library->property("collapsed").toBool());
            QTRY_COMPARE_WITH_TIMEOUT(video->width(), wide + (248 - 44), kWait);
            QCOMPARE(inspector->width(), 320.0);  // the other column stays
            click(toggleOf(inspector));
            QTRY_COMPARE_WITH_TIMEOUT(inspector->width(), 44.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(video->width(), wide + (248 - 44) + (320 - 44), kWait);
            // The button stays reachable when collapsed: it expands again.
            click(toggleOf(library));
            QTRY_COMPARE_WITH_TIMEOUT(library->width(), 248.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(video->width(), wide + (320 - 44), kWait);
            click(toggleOf(inspector));
            QTRY_COMPARE_WITH_TIMEOUT(inspector->width(), 320.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(video->width(), wide, kWait);
            QVERIFY(!library->property("collapsed").toBool());
            QVERIFY(!inspector->property("collapsed").toBool());

            // The choice is remembered: collapsed now, a new shell starts collapsed.
            click(toggleOf(library));
            click(toggleOf(inspector));
            QTRY_COMPARE_WITH_TIMEOUT(library->width(), 44.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(inspector->width(), 44.0, kWait);
        }
        {
            Shell again;
            loadShell(again);
            QVERIFY(again.window);
            QTRY_COMPARE_WITH_TIMEOUT(again.one("LibraryPanel")->width(), 44.0, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(again.one("InspectorPanel")->width(), 44.0, kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }
};

QTEST_MAIN(ShellQmlTest)
#include "shell_qml_test.moc"
