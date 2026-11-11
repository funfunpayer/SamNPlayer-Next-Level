// The QML shell (Main.qml with the top bar, the mode tabs and the Player page)
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
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

#include "device_controller.h"
#include "samn/settings.h"
#include <memory>

#include "frame_provider.h"
#include "app_log.h"
#include "icon_provider.h"
#include "player_controller.h"
#include "system_check.h"
#include "mode_registry.h"
#include "plugins.h"
#include "theme.h"
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

// True while a visual descendant has this objectName.
bool hasNamedItem(QQuickItem* root, const char* name) {
    for (QQuickItem* i : descendants(root))
        if (i->objectName() == QLatin1String(name)) return true;
    return false;
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
    // Hidden until ready (main.cpp shows it): Generator and Training are
    // built in the background, the ribbons are loaded.
    QVERIFY(!s.window->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(s.window->property("ready").toBool(), 15000);
    s.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(s.window));
    s.window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(s.window));
}

}  // namespace

class ShellQmlTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

    // The centre of mode tab i in the top bar (window coordinates).
    static QPoint tabCentre(const Shell& s, int i) {
        QQuickItem* tab = nullptr;
        for (QQuickItem* t : descendants(s.one("TopBar")))
            if (t->objectName() == QStringLiteral("modeTab_%1").arg(i)) tab = t;
        return tab ? tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint() : QPoint();
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
        QVERIFY2(svgs.size() >= 70, qPrintable(QString::number(svgs.size())));  // 72 icons and 4 assets at the time of writing
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
    // Every "?" names a help text that exists (HelpTexts.js). The catalogue
    // holds 0.7's 189 data-help texts (ci/help_texts.py); SamN2 may add more
    // (e.g. st-reduced-motion for D-092).
    void helpKeysInTheQmlSourcesExist() {
        QFile cat(QStringLiteral(SAMN_APP_QML_DIR) + "/HelpTexts.js");
        QVERIFY(cat.open(QIODevice::ReadOnly));
        // A Windows checkout has CRLF line ends; the entries are matched per line.
        const QString js = QString::fromUtf8(cat.readAll()).remove(QLatin1Char('\r'));
        QSet<QString> keys;
        static const QRegularExpression entry(QStringLiteral("^    \"([^\"]+)\": \"(.+)\",$"), QRegularExpression::MultilineOption);
        auto e = entry.globalMatch(js);
        while (e.hasNext()) keys.insert(e.next().captured(1));
        QVERIFY2(keys.size() >= 189, qPrintable(QString("help catalogue has %1 keys, need ≥ 189").arg(keys.size())));
        static const QRegularExpression use(QStringLiteral("(?:HelpChip\\s*\\{[^}]*?\\bkey|\\bhelp)\\s*:\\s*\"([\\w.\\-]+)\""));
        int found = 0;
        QDirIterator qml(QStringLiteral(SAMN_APP_QML_DIR), {"*.qml"}, QDir::Files);
        while (qml.hasNext()) {
            QFile f(qml.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            auto m = use.globalMatch(QString::fromUtf8(f.readAll()));
            while (m.hasNext()) {
                const QString key = m.next().captured(1);
                ++found;
                QVERIFY2(keys.contains(key), qPrintable(QFileInfo(f).fileName() + ": no help text \"" + key + "\""));
            }
        }
        QVERIFY(found >= 20);
    }

    // A "?" opens its text below it; Esc closes it.
    void aHelpChipOpensItsText() {
        {
            Shell s;
            loadShell(s, 2);  // Training: the session settings have chips
            QQuickItem* chip = nullptr;
            for (QQuickItem* c : itemsOfType(s.root(), "HelpChip"))
                if (c->property("key").toString() == "training.cycles" && c->isVisible()) chip = c;
            QVERIFY(chip);
            QVERIFY(!chip->property("open").toBool());
            QTest::mouseClick(s.window, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(chip->property("open").toBool(), kWait);
            QCOMPARE(chip->property("text").toString(),
                     QString("Number of training cycles (1–20). Default 5. Training feel only — Create CSRT unchanged."));
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_HELP"); !shot.isEmpty()) {
                QTest::qWait(300);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!chip->property("open").toBool(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // PARITY item 21: Library HelpChips on Local Files / Recent / Favorites /
    // Scripts / Analysis resolve and a click opens the popover.
    void libraryHelpChipsResolve() {
        {
            Shell s;
            loadShell(s, 0);
            QVERIFY(s.one("LibraryPanel"));
            struct Row { const char* key; const char* snippet; };
            const Row rows[] = {
                {"library.local-files", "Opens a video or script"},
                {"library.recent", "Newest scripts you opened"},
                {"library.favorites", "Scripts you starred"},
                {"library.scripts", "Imported = opened from disk"},
                {"library.analysis", "Audio Analysis opens the waveform"},
            };
            QQuickItem* local = nullptr;
            for (const Row& row : rows) {
                QQuickItem* chip = nullptr;
                for (QQuickItem* c : itemsOfType(s.root(), "HelpChip"))
                    if (c->property("key").toString() == QLatin1String(row.key) && c->isVisible()) chip = c;
                QVERIFY2(chip, row.key);
                QVERIFY2(chip->property("text").toString().contains(QLatin1String(row.snippet)),
                         qPrintable(QString("%1: %2").arg(row.key, chip->property("text").toString())));
                if (QLatin1String(row.key) == QLatin1String("library.local-files")) local = chip;
            }
            QVERIFY(local);
            QVERIFY(!local->property("open").toBool());
            QTest::mouseClick(s.window, Qt::LeftButton, {},
                              local->mapToScene(QPointF(local->width() / 2, local->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(local->property("open").toBool(), kWait);
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!local->property("open").toBool(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    void shellLoadsWithoutWarningsAndShowsItsIcons() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QTest::qWait(300);
            QCOMPARE(s.window->title(), QString("SamNPlayer Next"));
            // The two-colour wordmark (D-151) is painted at a real size.
            auto* mark = s.window->findChild<QQuickItem*>("wordmark");
            QVERIFY(mark);
            QCOMPARE(mark->property("text").toString(), QString("SamNPlayer Next"));
            QVERIFY(mark->width() > 200 && mark->height() > 30);
            QCOMPARE(s.mode(), 0);
            QCOMPARE(s.pillName(), QString("Player"));
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
                    for (const char* part : {"TopBar", "LibraryPanel", "InspectorPanel", "PlayerPage"})
                        if (isType(p, part)) ++perPart[part];
            }
            QVERIFY(shown > 10);
            // The top bar (the three mode tabs and more), the library and the player show icons.
            QVERIFY2(perPart["TopBar"] >= 3, qPrintable(QString::number(perPart["TopBar"])));
            QVERIFY2(perPart["LibraryPanel"] >= 5, qPrintable(QString::number(perPart["LibraryPanel"])));
            QVERIFY2(perPart["PlayerPage"] >= 5, qPrintable(QString::number(perPart["PlayerPage"])));

            // The three mode tabs each have their own icon: the ones of the modes list.
            const QVariantList modes = s.one("TopBar")->property("modes").toList();
            QCOMPARE(modes.size(), 3);
            QStringList tabIcons;
            for (QQuickItem* icon : itemsOfType(s.one("TopBar"), "Icon"))
                for (QQuickItem* p = icon; p; p = p->parentItem())
                    if (p->objectName().startsWith(QLatin1String("modeTab_"))) { tabIcons << icon->property("name").toString(); break; }
            QStringList expected;
            for (const QVariant& m : modes) expected << m.toMap().value("icon").toString();
            QCOMPARE(tabIcons, expected);
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

    // D-150: the modes are a tab row (the target images); Ctrl+1…3 switch.
    void modeTabsSwitchWithCtrlNumbers() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            const QStringList names = s.window->property("modeNames").toStringList();
            QCOMPARE(names, (QStringList{"Player", "Generator", "Training"}));
            // A plain number belongs to the page, not to the mode switch.
            QTest::keyClick(s.window, Qt::Key_3);
            QTest::qWait(150);
            QCOMPARE(s.mode(), 0);
            for (int key : {Qt::Key_3, Qt::Key_2, Qt::Key_1}) {
                QTest::keyClick(s.window, Qt::Key(key), Qt::ControlModifier);
                const int want = key - Qt::Key_1;
                QTRY_COMPARE_WITH_TIMEOUT(s.mode(), want, kWait);
                QCOMPARE(s.pillName(), names[want]);
                QCOMPARE(s.one("TopBar")->property("currentMode").toInt(), want);
            }
            // No fourth mode while no plugin is on (D-145, D-155): Ctrl+4 changes nothing.
            QTest::keyClick(s.window, Qt::Key_4, Qt::ControlModifier);
            QTest::qWait(150);
            QCOMPARE(s.mode(), 0);
            // The Player's keys stay with the Player: L in the Generator
            // does not switch the Player's loop (and an "A" or "T" there
            // never clashes with the label editor's keys).
            QQuickItem* player = s.one("PlayerPage");
            QObject* ctrl = player->property("controller").value<QObject*>();
            QVERIFY(ctrl);
            const bool loop = ctrl->property("loopEnabled").toBool();
            QTest::keyClick(s.window, Qt::Key_2, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 1, kWait);
            QTest::keyClick(s.window, Qt::Key_L);
            QTest::qWait(100);
            QCOMPARE(ctrl->property("loopEnabled").toBool(), loop);
            QTest::keyClick(s.window, Qt::Key_1, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QTest::keyClick(s.window, Qt::Key_L);
            QTRY_COMPARE(ctrl->property("loopEnabled").toBool(), !loop);  // in the Player it does
            // While Settings is open the Player's keys do not act behind it.
            QQuickItem* settings = s.one("SettingsPage");
            QVERIFY(!player->property("overlayOpen").toBool());
            settings->setProperty("open", true);
            QVERIFY(player->property("overlayOpen").toBool());
            settings->setProperty("open", false);
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

    void modeTabsClickAndCurrentMark() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QCOMPARE(s.pillName(), QString("Player"));
            auto tab = [&](int i) {
                for (QQuickItem* t : descendants(s.one("TopBar")))
                    if (t->objectName() == QStringLiteral("modeTab_%1").arg(i)) return t;
                return static_cast<QQuickItem*>(nullptr);
            };
            for (int i = 0; i < 3; ++i) QVERIFY(tab(i) && tab(i)->isVisible());
            QVERIFY(tab(0)->property("current").toBool());
            QVERIFY(!tab(1)->property("current").toBool());
            // A click on a tab picks that mode and marks the tab.
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, tabCentre(s, 2));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 2, kWait);
            QCOMPARE(s.pillName(), QString("Training"));
            QVERIFY(tab(2)->property("current").toBool());
            QVERIFY(!tab(0)->property("current").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(s.one("TrainingPage")->isVisible(), kWait);
            // Back to the Player: its page is shown again.
            QTest::mouseClick(s.window, Qt::LeftButton, Qt::NoModifier, tabCentre(s, 0));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(s.one("PlayerPage")->isVisible(), kWait);
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_TABS"); !shot.isEmpty()) s.window->grabWindow().save(QString::fromLocal8Bit(shot));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // 0.7: the pages of the other modes are hidden while one mode shows.
    // D-146: the gear opens Settings over the current mode (0.7 Tools →
    // Settings / Log / About); tabs switch, Esc and × close, the mode stays.
    // Error toasts (0.7 notify.js): a failure shows at the bottom right;
    // Open Log opens Settings on the Log tab.
    void anErrorToastOpensTheLog() {
        {
            Shell s;
            loadShell(s, 0);
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name) && i->isVisible()) return i;
                return nullptr;
            };
            QVERIFY(!byName("toast"));
            samn::ui::AppLog::instance().reportError("Could not save: the disk is full");
            QTRY_VERIFY_WITH_TIMEOUT(byName("toast"), kWait);
            QCOMPARE(byName("toastText")->property("text").toString(), QString("Could not save: the disk is full"));
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_TOAST"); !shot.isEmpty()) {
                QTest::qWait(300);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
            QQuickItem* open = byName("toastOpenLog");
            QVERIFY(open);
            QTest::mouseClick(s.window, Qt::LeftButton, {}, open->mapToScene(QPointF(open->width() / 2, open->height() / 2)).toPoint());
            QQuickItem* page = s.one("SettingsPage");
            QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), kWait);
            QCOMPARE(page->property("tab").toInt(), 3);  // Log
            QTRY_VERIFY_WITH_TIMEOUT(!byName("toast"), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

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
            // Support card: appeal, GitHub, and the donate placeholder (D-022 / D-153).
            QVERIFY(byName("supportSection") && byName("supportSection")->isVisible());
            QVERIFY(byName("supportOpenGithub"));
            QQuickItem* donate = byName("donateComingSoon");
            QVERIFY(donate && donate->isVisible());
            QVERIFY(donate->property("text").toString().contains(QStringLiteral("Donation page coming soon")));
            page->setProperty("tab", 1);
            QTRY_VERIFY_WITH_TIMEOUT(byName("deviceTab")->isVisible(), kWait);
            page->setProperty("tab", 2);
            QTRY_VERIFY_WITH_TIMEOUT(byName("systemTab")->isVisible(), kWait);
            QVERIFY(byName("systemCheckAgain") && byName("systemCopyReport"));
            QVERIFY(byName("reportPathField") && byName("reportPathChoose") && byName("reportPathDefault"));
            QCOMPARE(byName("reportPathStatus")->property("text").toString(),
                     QStringLiteral("No metrics recorded yet (path empty)."));
            {
                QObject* settings = page->property("settings").value<QObject*>();
                QVERIFY(settings);
                QCOMPARE(settings->property("reportPath").toString(), QString());
                settings->setProperty("reportPath", QStringLiteral("/tmp/metrics.jsonl"));
                QCOMPARE(byName("reportPathField")->property("text").toString(), QStringLiteral("/tmp/metrics.jsonl"));
                QCOMPARE(byName("reportPathStatus")->property("text").toString(), QStringLiteral("/tmp/metrics.jsonl"));
                settings->setProperty("reportPath", QString());
                QCOMPARE(byName("reportPathStatus")->property("text").toString(),
                         QStringLiteral("No metrics recorded yet (path empty)."));
            }
            QMetaObject::invokeMethod(page->property("system").value<QObject*>(), "refresh");
            QTRY_VERIFY_WITH_TIMEOUT(byName("systemItem_settings"), kWait);
            QVERIFY(byName("systemItem_gpu") && byName("systemItem_ai-gpu"));
            QTRY_VERIFY_WITH_TIMEOUT(!qobject_cast<samn::ui::SystemCheckController*>(page->property("system").value<QObject*>())->running(), kWait);
            {
                // A fake PC (D-167): a card plus an iGPU, ffmpeg and Python missing (optional, "off").
                samn::ui::CheckInputs fake;
                samn::ui::GpuInfo igpu, card;
                igpu.name = QStringLiteral("Intel UHD Graphics 770");
                igpu.vendor = QStringLiteral("Intel");
                igpu.dedicated_mb = 128;
                card.name = QStringLiteral("NVIDIA GeForce RTX 3060");
                card.vendor = QStringLiteral("NVIDIA");
                card.dedicated_mb = 12288;
                fake.gpus = {igpu, card};
                samn::ui::ClassifyGpus(fake.gpus);
                fake.config_dir = QStringLiteral("/fake");
                fake.free_bytes = qint64(50) * 1024 * 1024 * 1024;
                fake.ffmpeg_path.clear();
                fake.python_path.clear();
                fake.ram_mb = 6000;
                qobject_cast<samn::ui::SystemCheckController*>(page->property("system").value<QObject*>())->setInputs(fake);
            }
            QTRY_VERIFY_WITH_TIMEOUT(byName("systemItem_ffmpeg") && byName("systemItem_ai-provider") && byName("systemItem_video-path"), kWait);
            QVERIFY(byName("systemItem_python") && byName("systemItem_ram"));
            QCOMPARE(byName("systemSummary")->property("text").toString(), QStringLiteral("1 warning"));  // RAM below 8 GB
            QCOMPARE(byName("systemDot_ffmpeg")->property("color").value<QColor>(), byName("systemDot_python")->property("color").value<QColor>());
            QVERIFY(byName("systemDot_ffmpeg")->property("color").value<QColor>() != byName("systemDot_ram")->property("color").value<QColor>());
            QVERIFY(byName("systemDot_ai-provider")->property("color").value<QColor>() != byName("systemDot_ram")->property("color").value<QColor>());
            page->setProperty("tab", 3);
            QTRY_VERIFY_WITH_TIMEOUT(byName("logTab")->isVisible(), kWait);
            page->setProperty("tab", 4);
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
            QVERIFY(!byName("diagHistoryHeading") || !byName("diagHistoryHeading")->isVisible());
            {
                QFile hist(dev->diagnosticsHistoryPath());
                QVERIFY(hist.open(QIODevice::WriteOnly | QIODevice::Truncate));
                hist.write("{\"startedAt\":\"2025-01-01T00:00:00.000Z\",\"name\":\"Old\",\"kind\":\"ble\",\"commands\":3,\"errors\":0,\"interrupted\":false}\n");
                hist.write("{\"startedAt\":\"2025-06-02T12:00:00.000Z\",\"name\":\"Mock device\",\"kind\":\"mock\",\"commands\":12,\"errors\":1,\"interrupted\":true}\n");
            }
            QVERIFY(QMetaObject::invokeMethod(tab, "refreshHistory"));
            QTRY_VERIFY(byName("diagHistoryHeading") && byName("diagHistoryHeading")->isVisible());
            QStringList histLines;
            for (QQuickItem* i : descendants(tab))
                if (i->objectName() == QLatin1String("diagHistoryLine") && i->isVisible())
                    histLines << i->property("text").toString();
            QCOMPARE(histLines.size(), 2);
            QVERIFY(histLines.at(0).contains(QStringLiteral("2025-06-02 (Mock)")));
            QVERIFY(histLines.at(0).contains(QStringLiteral("12 commands, 1 errors, interrupted")));
            QVERIFY(histLines.at(1).contains(QStringLiteral("2025-01-01 (Old)")));
            QVERIFY(histLines.at(1).contains(QStringLiteral("3 commands, 0 errors")));
            QVERIFY(!histLines.at(1).contains(QStringLiteral("interrupted")));
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

    // D-153: the support link bottom left, and the card once at start; the
    // card closes and stays off at the next start when Settings turns it off.
    void supportNoteShowsAtStartAndCanBeTurnedOff() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto* updates = s.window->findChild<samn::ui::UpdateChecker*>();
            QVERIFY(updates && updates->supportNoteAtStart());
            QCOMPARE(updates->projectPageUrl(), QStringLiteral("https://github.com/funfunpayer/SamNPlayer-Next-Level"));
            QQuickItem* card = nullptr;
            QQuickItem* link = nullptr;
            for (QQuickItem* i : descendants(s.root())) {
                if (i->objectName() == QLatin1String("supportCard")) card = i;
                if (i->objectName() == QLatin1String("supportLink")) link = i;
            }
            QVERIFY(card && link);
            QVERIFY(link->isVisible());
            QTRY_VERIFY_WITH_TIMEOUT(card->isVisible() && card->property("opacity").toDouble() > 0.99, kWait);
            QQuickItem* close = nullptr;
            for (QQuickItem* i : descendants(card))
                if (i->objectName() == QLatin1String("supportCardClose")) close = i;
            QVERIFY(close);
            QTest::mouseClick(s.window, Qt::LeftButton, {}, close->mapToScene(QPointF(close->width() / 2, close->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(!card->isVisible(), kWait);
            updates->setSupportNoteAtStart(false);
        }
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* card = nullptr;
            for (QQuickItem* i : descendants(s.root()))
                if (i->objectName() == QLatin1String("supportCard")) card = i;
            QVERIFY(card);
            QTest::qWait(2500);
            QVERIFY(!card->isVisible());
            s.window->findChild<samn::ui::UpdateChecker*>()->setSupportNoteAtStart(true);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // D-092 / UI-07: Settings → General "Reduce motion" stops decorative
    // loops and zeros Theme.animFast / animNormal; the choice is kept.
    // Running endless animations (loops: Animation.Infinite) in the window.
    static int RunningLoops(QObject* root) {
        int n = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            bool isAnimation = false;
            for (const QMetaObject* m = o->metaObject(); m; m = m->superClass())
                if (qstrcmp(m->className(), "QQuickAbstractAnimation") == 0) isAnimation = true;
            if (isAnimation && o->property("running").toBool() && o->property("loops").toInt() < 0) ++n;  // Animation.Infinite
        }
        return n;
    }

    void reducedMotionStopsDecorativeAnimations() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto* theme = s.engine.singletonInstance<samn::ui::Theme*>(qmlTypeId("SamN", 1, 0, "Theme"));
            QVERIFY(theme);
            QVERIFY(!theme->reducedMotion());
            QCOMPARE(theme->animFast(), 140);
            QCOMPARE(theme->animNormal(), 220);
            QCOMPARE(theme->animSlow(), 420);
            QVERIFY(RunningLoops(s.window) > 0);  // the Flow look moves
            QTest::keyClick(s.window, Qt::Key_Comma, Qt::ControlModifier);
            QQuickItem* page = s.one("SettingsPage");
            QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), kWait);
            QQuickItem* toggle = nullptr;
            for (QQuickItem* i : descendants(s.root()))
                if (i->objectName() == QLatin1String("reducedMotion")) toggle = i;
            QVERIFY(toggle && toggle->isVisible());
            QTest::mouseClick(s.window, Qt::LeftButton, {},
                              toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
            QTRY_VERIFY(theme->reducedMotion());
            QCOMPARE(theme->animFast(), 0);
            QCOMPARE(theme->animNormal(), 0);
            QCOMPARE(theme->animSlow(), 0);
            QCOMPARE(theme->animAmbient(), 0);
            // No endless loop keeps running (a 0 ms loop would also spin).
            QTRY_COMPARE(RunningLoops(s.window), 0);
            if (const QByteArray shot = qgetenv("SAMN_TEST_SHOT_REDUCED"); !shot.isEmpty()) {
                QTest::qWait(200);
                s.window->grabWindow().save(QString::fromLocal8Bit(shot));
            }
        }
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto* theme = s.engine.singletonInstance<samn::ui::Theme*>(qmlTypeId("SamN", 1, 0, "Theme"));
            QVERIFY(theme && theme->reducedMotion());
            theme->setReducedMotion(false);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // D-166: a mode registered at start gets a tab, a Ctrl+N key and a page; removing it hides all three.
    void registeredModeGetsTabKeyAndPage() {
        auto& registry = samn::ui::ModeRegistry::instance();
        samn::ui::ModeDescriptor extra;
        extra.id = "extratest";
        extra.title = "Extra";
        extra.icon = "layers";
        extra.hue = QColor("#ff8a23");
        extra.page = "AiTrainPage.qml";
        {
            QVERIFY(registry.registerMode(extra));  // before the window: "registered at startup"
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QCOMPARE(s.window->property("modeNames").toStringList(), (QStringList{"Player", "Generator", "Training", "Extra"}));
            QVERIFY(hasNamedItem(s.one("TopBar"), "modeTab_3"));
            QTest::keyClick(s.window, Qt::Key_4, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 3, kWait);
            QCOMPARE(s.pillName(), QStringLiteral("Extra"));
            QTRY_VERIFY_WITH_TIMEOUT(s.one("AiTrainPage") && s.one("AiTrainPage")->isVisible(), kWait);
            // Removed while shown: the tab, the page and the key go, the shell is back in the Player.
            QVERIFY(registry.unregisterMode("extratest"));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QCOMPARE(s.window->property("modeNames").toStringList(), (QStringList{"Player", "Generator", "Training"}));
            QVERIFY(!s.one("AiTrainPage"));
            QVERIFY(!hasNamedItem(s.one("TopBar"), "modeTab_3"));
            QTest::keyClick(s.window, Qt::Key_4, Qt::ControlModifier);
            QTest::qWait(150);
            QCOMPARE(s.mode(), 0);
            // Registered while running: it appears without a restart.
            QVERIFY(registry.registerMode(extra));
            QTRY_COMPARE_WITH_TIMEOUT(s.window->property("modeNames").toStringList().size(), 4, kWait);
            QVERIFY(registry.unregisterMode("extratest"));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // The shell follows the current mode by id: removing or disabling an earlier extra mode keeps the shown one;
    // removing or disabling the shown one goes back to the Player.
    void currentModeSurvivesOtherModesLeaving() {
        auto& registry = samn::ui::ModeRegistry::instance();
        samn::ui::ModeDescriptor a, b;
        a.id = "extra_a"; a.title = "ExtraA"; a.icon = "layers"; a.hue = QColor("#ff8a23"); a.page = "AiTrainPage.qml";
        b = a; b.id = "extra_b"; b.title = "ExtraB";
        QVERIFY(registry.registerMode(a));
        QVERIFY(registry.registerMode(b));
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QCOMPARE(s.window->property("modeNames").toStringList().size(), 5);
            QTest::keyClick(s.window, Qt::Key_5, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 4, kWait);
            QCOMPARE(s.pillName(), QStringLiteral("ExtraB"));
            // An earlier extra mode goes: ExtraB moves to index 3 and stays shown.
            QVERIFY(registry.unregisterMode("extra_a"));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 3, kWait);
            QCOMPARE(s.pillName(), QStringLiteral("ExtraB"));
            // Re-added before it: still ExtraB.
            QVERIFY(registry.registerMode(a));
            QTRY_COMPARE_WITH_TIMEOUT(s.window->property("modeNames").toStringList().size(), 5, kWait);
            QCOMPARE(s.pillName(), QStringLiteral("ExtraB"));
            // Disabled while shown: back to the Player; the tab stays listed.
            QVERIFY(registry.setEnabled("extra_b", false));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QCOMPARE(s.window->property("modeNames").toStringList().size(), 5);
            QVERIFY(registry.unregisterMode("extra_a"));
            QVERIFY(registry.unregisterMode("extra_b"));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // D-155: the AI Train plugin mode is off by default; the Settings switch
    // adds the fourth tab, Ctrl+4 opens it, and switching it off while it is
    // shown goes back to the Player. The choice is kept.
    void aiTrainPluginModeComesAndGoes() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            auto* plugins = s.engine.singletonInstance<samn::ui::Plugins*>(qmlTypeId("SamN", 1, 0, "Plugins"));
            QVERIFY(plugins);
            QVERIFY(!plugins->aiTrainEnabled());
            QCOMPARE(s.window->property("modeNames").toStringList().size(), 3);
            plugins->setAiTrainEnabled(true);
            QTRY_COMPARE_WITH_TIMEOUT(s.window->property("modeNames").toStringList(),
                                      (QStringList{"Player", "Generator", "Training", "AI Train"}), kWait);
            QTest::keyClick(s.window, Qt::Key_4, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 3, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(s.one("AiTrainPage") && s.one("AiTrainPage")->isVisible(), kWait);
            QCOMPARE(s.pillName(), QStringLiteral("AI Train"));
            plugins->setAiTrainEnabled(false);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QCOMPARE(s.window->property("modeNames").toStringList().size(), 3);
            QVERIFY(!s.one("AiTrainPage"));
            plugins->setAiTrainEnabled(true);  // kept for the next start
        }
        {
            Shell s;
            loadShell(s, 3);
            QVERIFY(s.window);
            QCOMPARE(s.mode(), 3);
            QTRY_VERIFY_WITH_TIMEOUT(s.one("AiTrainPage") && s.one("AiTrainPage")->isVisible(), kWait);
            s.engine.singletonInstance<samn::ui::Plugins*>(qmlTypeId("SamN", 1, 0, "Plugins"))->setAiTrainEnabled(false);
        }
        {
            Shell s;
            loadShell(s, 3);  // off: a start in the plugin mode falls back to the Player
            QVERIFY(s.window);
            QCOMPARE(s.mode(), 0);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // Review A1: each page built its own LibraryController and `changed` only
    // fires on the instance that wrote, so a script the Generator created never
    // reached the Player's Generated list. Main.qml hands one instance to both.
    void thePlayerAndTheGeneratorShareOneLibrary() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* player = s.one("PlayerPage");
            QQuickItem* generator = s.one("GeneratorPage");
            QVERIFY(player && generator);
            auto* playerLib = player->property("library").value<QObject*>();
            auto* generatorLib = generator->property("library").value<QObject*>();
            QVERIFY(playerLib && generatorLib);
            QCOMPARE(playerLib, generatorLib);
            QTemporaryDir dir;
            const QString script = dir.filePath("made.funscript");
            QVERIFY(QFile(script).open(QIODevice::WriteOnly));
            QSignalSpy changed(playerLib, SIGNAL(changed()));
            QVERIFY(QMetaObject::invokeMethod(generatorLib, "noteScriptGenerated", Q_ARG(QString, script)));
            QVERIFY(changed.count() >= 1);
            bool listed = false;
            for (const QVariant& v : playerLib->property("generated").toList())
                listed = listed || v.toMap().value("name").toString() == QStringLiteral("made.funscript");
            QVERIFY(listed);
        }
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

    // "Edit this script" opens the Editor. editRequested(scenes=true) still
    // selects Generator → Scenes (D-145). The Player Scene Map tab that only
    // redirected there is gone.
    void editFromThePlayerOpensTheEditor() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* player = s.one("PlayerPage");
            QQuickItem* generator = s.one("GeneratorPage");
            QTemporaryDir dir;
            const QString video = dir.filePath("clip.mp4");
            QVERIFY(QFile::copy(QStringLiteral(SAMN_APP_QML_DIR "/../../tests/app/data/testpattern_4s.mp4"), video));
            const QString script = dir.filePath("clip.funscript");
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"version":"1.0","actions":[{"at":0,"pos":0},{"at":500,"pos":100},{"at":1000,"pos":0}]})");
            f.close();
            QVERIFY(QMetaObject::invokeMethod(player, "editRequested", Q_ARG(QString, script),
                                              Q_ARG(QUrl, QUrl::fromLocalFile(video)), Q_ARG(bool, true)));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 1, kWait);
            QCOMPARE(generator->property("sidePanel").toInt(), 1);  // the scenes
            auto* edit = generator->property("editor").value<QObject*>();
            QVERIFY(edit);
            QVERIFY(edit->property("hasScript").toBool());
            auto* gen = generator->property("controller").value<QObject*>();
            QVERIFY(gen);
            QTRY_VERIFY_WITH_TIMEOUT(gen->property("hasVideo").toBool(), kWait);
            QCOMPARE(QFileInfo(gen->property("videoPath").toString()).fileName(), QStringLiteral("clip.mp4"));
            // Without the scenes: the edit panel.
            QTest::keyClick(s.window, Qt::Key_1, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QVERIFY(QMetaObject::invokeMethod(player, "editRequested", Q_ARG(QString, script),
                                              Q_ARG(QUrl, QUrl::fromLocalFile(video)), Q_ARG(bool, false)));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 1, kWait);
            QCOMPARE(generator->property("sidePanel").toInt(), 0);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // Bottom tabs: Timeline, Chapters, Bookmarks, Audio Analysis. A saved 4
    // (old Audio Analysis) opens index 3; a saved 3 (old Scene Map) opens Timeline.
    void playerBottomTabsDropSceneMap() {
        auto bottomTabsOf = [](QQuickItem* timeline) -> QQuickItem* {
            for (QQuickItem* i : descendants(timeline))
                if (isType(i, "FlowTabs")) return i;
            return nullptr;
        };
        auto labelsOf = [](QQuickItem* tabs) {
            QStringList labels;
            for (const QVariant& v : tabs->property("model").toList()) labels << v.toString();
            return labels;
        };
        // The BPM label sits in the Audio Analysis panel, a direct child of the
        // timeline card. The card stays hidden until a script is open, and
        // QQuickItem::visible reports that effective state.
        auto audioPanelOf = [](QQuickItem* timeline, QQuickItem* bpm) -> QQuickItem* {
            QQuickItem* panel = bpm;
            while (panel && panel->parentItem() && panel->parentItem() != timeline) panel = panel->parentItem();
            return panel && panel->parentItem() == timeline ? panel : nullptr;
        };
        const QStringList expect{QStringLiteral("Timeline"), QStringLiteral("Chapters"),
                                 QStringLiteral("Bookmarks"), QStringLiteral("Audio Analysis")};

        samn::Settings().set("ui.player.bottomTab", 4);
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* timeline = s.one("PlayerTimeline");
            QVERIFY(timeline);
            QVERIFY(!timeline->findChild<QObject*>(QStringLiteral("openScenes")));
            QVERIFY(timeline->findChild<QQuickItem*>(QStringLiteral("chapterStrip")));
            QQuickItem* tabs = bottomTabsOf(timeline);
            QVERIFY(tabs);
            QCOMPARE(labelsOf(tabs), expect);
            QCOMPARE(tabs->property("current").toInt(), 3);
            QTemporaryDir dir;
            const QString script = dir.filePath(QStringLiteral("clip.funscript"));
            QFile clip(script);
            QVERIFY(clip.open(QIODevice::WriteOnly));
            clip.write(R"({"version":"1.0","actions":[{"at":0,"pos":20},{"at":1000,"pos":80}]})");
            clip.close();
            auto* ctrl = s.one("PlayerPage")->property("controller").value<samn::ui::PlayerController*>();
            QVERIFY(ctrl);
            QVERIFY(ctrl->openScript(script));
            QTRY_VERIFY_WITH_TIMEOUT(timeline->isVisible(), kWait);
            auto* bpm = timeline->findChild<QQuickItem*>(QStringLiteral("audioAnalysisBpm"));
            QQuickItem* panel = audioPanelOf(timeline, bpm);
            QVERIFY(panel);
            QVERIFY(panel->isVisible());
            QVERIFY(QMetaObject::invokeMethod(timeline, "showAudioAnalysis"));
            QCOMPARE(tabs->property("current").toInt(), 3);
            QCOMPARE(ctrl->uiValue(QStringLiteral("player.bottomTab"), -1).toInt(), 3);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
        g_messages.clear();

        samn::Settings().set("ui.player.bottomTab", 3);
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* timeline = s.one("PlayerTimeline");
            QQuickItem* tabs = bottomTabsOf(timeline);
            QVERIFY(timeline && tabs);
            QCOMPARE(labelsOf(tabs), expect);
            QCOMPARE(tabs->property("current").toInt(), 0);
            QTemporaryDir dir;
            const QString script = dir.filePath(QStringLiteral("clip.funscript"));
            QFile clip(script);
            QVERIFY(clip.open(QIODevice::WriteOnly));
            clip.write(R"({"version":"1.0","actions":[{"at":0,"pos":20},{"at":1000,"pos":80}]})");
            clip.close();
            auto* ctrl = s.one("PlayerPage")->property("controller").value<samn::ui::PlayerController*>();
            QVERIFY(ctrl);
            QVERIFY(ctrl->openScript(script));
            QTRY_VERIFY_WITH_TIMEOUT(timeline->isVisible(), kWait);
            auto* bpm = timeline->findChild<QQuickItem*>(QStringLiteral("audioAnalysisBpm"));
            QQuickItem* panel = audioPanelOf(timeline, bpm);
            QVERIFY(panel);
            QVERIFY(!panel->isVisible());
            auto* stroke = timeline->findChild<QQuickItem*>(QStringLiteral("laneName_stroke"));
            QVERIFY(stroke);
            QVERIFY(stroke->isVisible());
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
            QTest::keyClick(s.window, Qt::Key_3, Qt::ControlModifier);
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
            for (QQuickItem* i : descendants(s.one("PlayerPage")))  // Training has a film stage too
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

    // In-app user handbook (PARITY §12): Settings and Create open it; Esc /
    // × / Close / backdrop dismiss; section nav jumps the body.
    void userHandbookOpensFromSettingsAndCreate() {
        {
            Shell s;
            loadShell(s, 0);
            QVERIFY(s.window);
            auto byName = [&](const char* name) -> QQuickItem* {
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == QLatin1String(name) && i->isVisible()) return i;
                return nullptr;
            };
            auto click = [&](QQuickItem* item) {
                QVERIFY(item);
                QTest::mouseClick(s.window, Qt::LeftButton, {},
                                  item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            };

            QQuickItem* handbook = s.one("Handbook");
            QVERIFY(handbook);
            QVERIFY(!handbook->isVisible());

            // Settings → General → Open user handbook.
            QQuickItem* settings = s.one("SettingsPage");
            QVERIFY(settings);
            settings->setProperty("open", true);
            QTRY_VERIFY_WITH_TIMEOUT(settings->isVisible(), kWait);
            QQuickItem* openHb = byName("openHandbook");
            QVERIFY(openHb);
            click(openHb);
            QTRY_VERIFY_WITH_TIMEOUT(handbook->isVisible() && handbook->property("opacity").toDouble() > 0.99, kWait);
            QCOMPARE(byName("handbookTitle")->property("text").toString(), QStringLiteral("User handbook"));
            QVERIFY(byName("handbookFooter")->property("text").toString().contains(QStringLiteral("USER_HANDBOOK.md")));
            QVERIFY(byName("handbookNav"));
            QVERIFY(byName("handbookBody"));
            QCOMPARE(handbook->property("activeSection").toInt(), 0);

            // Esc closes the handbook first; Settings stays open underneath.
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!handbook->isVisible(), kWait);
            QVERIFY(settings->isVisible());
            QTest::keyClick(s.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!settings->isVisible(), kWait);

            // Create header button opens it too.
            s.window->setProperty("mode", 1);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 1, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(byName("openHandbookCreate"), kWait);
            // TEMP diagnostic (CI, Qt 6.7): where the button is when it is clicked.
            {
                auto* b = byName("openHandbookCreate");
                qInfo() << "DIAG win" << s.window->width() << s.window->height() << "btn" << b->mapToScene(QPointF(0, 0)) << b->width();
                for (QQuickItem* i : descendants(s.root()))
                    if (i->objectName() == "stageSplit" || i->objectName() == "projectPanel" || i->objectName() == "createScroll")
                        qInfo() << "DIAG" << i->objectName() << i->mapToScene(QPointF(0, 0)) << i->width() << i->height();
            }
            click(byName("openHandbookCreate"));
            QTRY_VERIFY_WITH_TIMEOUT(handbook->isVisible(), kWait);
            click(byName("handbookClose"));
            QTRY_VERIFY_WITH_TIMEOUT(!handbook->isVisible(), kWait);

            // Project nav entry.
            QTRY_VERIFY_WITH_TIMEOUT(byName("projectOpenHandbook"), kWait);
            click(byName("projectOpenHandbook"));
            QTRY_VERIFY_WITH_TIMEOUT(handbook->isVisible(), kWait);
            {
                const QVariant secs = handbook->property("sections");
                QVERIFY(secs.isValid());
                // JS arrays arrive as QVariantList or QJSValue; count either way.
                int n = secs.toList().size();
                if (n == 0) n = secs.value<QVariantList>().size();
                QCOMPARE(n, 11);
            }
            click(byName("handbookCloseButton"));
            QTRY_VERIFY_WITH_TIMEOUT(!handbook->isVisible(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // PARITY item 12: several videos dropped on the Generator join the queue;
    // one video still opens there; a script drop goes to the Player.
    void generatorDropQueuesSeveralVideos() {
        {
            Shell s;
            loadShell(s, 1);
            QVERIFY(s.window);
            QCOMPARE(s.mode(), 1);
            QQuickItem* generator = s.one("GeneratorPage");
            QVERIFY(generator);
            auto* gen = generator->property("controller").value<QObject*>();
            QVERIFY(gen);
            QTemporaryDir dir;
            const QString a = dir.filePath("a.mp4");
            const QString b = dir.filePath("b.mp4");
            QVERIFY(QFile::copy(QStringLiteral(SAMN_APP_QML_DIR "/../../tests/app/data/testpattern_4s.mp4"), a));
            QVERIFY(QFile::copy(QStringLiteral(SAMN_APP_QML_DIR "/../../tests/app/data/testpattern_4s.mp4"), b));
            const QString script = dir.filePath("clip.funscript");
            {
                QFile f(script);
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(R"({"version":"1.0","actions":[{"at":0,"pos":0},{"at":500,"pos":100}]})");
            }

            QVariant ok;
            QVERIFY(QMetaObject::invokeMethod(
                s.window, "handleUrlsDropped", Q_RETURN_ARG(QVariant, ok),
                Q_ARG(QVariant, QVariant::fromValue(QStringList{
                    QUrl::fromLocalFile(a).toString(), QUrl::fromLocalFile(b).toString()}))));
            QCOMPARE(s.mode(), 1);
            QCOMPARE(gen->property("queue").toList().size(), 2);
            QVERIFY(!gen->property("hasVideo").toBool());

            QVERIFY(QMetaObject::invokeMethod(
                s.window, "handleUrlsDropped", Q_RETURN_ARG(QVariant, ok),
                Q_ARG(QVariant, QVariant::fromValue(QStringList{QUrl::fromLocalFile(a).toString()}))));
            QCOMPARE(s.mode(), 1);
            QTRY_VERIFY_WITH_TIMEOUT(gen->property("hasVideo").toBool(), kWait);

            QVERIFY(QMetaObject::invokeMethod(
                s.window, "handleUrlsDropped", Q_RETURN_ARG(QVariant, ok),
                Q_ARG(QVariant, QVariant::fromValue(QStringList{QUrl::fromLocalFile(script).toString()}))));
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            auto* player = s.one("PlayerPage")->property("controller").value<QObject*>();
            QVERIFY(player);
            QTRY_VERIFY_WITH_TIMEOUT(player->property("hasScript").toBool(), kWait);
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }

    // Top bar hide-panels and Alt+B are the Player theatre toggle.
    void hidePanelsButtonAndAltBToggleTheatre() {
        {
            Shell s;
            loadShell(s);
            QVERIFY(s.window);
            QQuickItem* player = s.one("PlayerPage");
            QQuickItem* topBar = s.one("TopBar");
            QVERIFY(player && topBar);
            QVERIFY(!player->property("theatre").toBool());
            QVERIFY(!topBar->property("panelsHidden").toBool());
            QQuickItem* button = nullptr;
            for (QQuickItem* i : descendants(topBar))
                if (i->objectName() == QLatin1String("topHidePanelsButton")) button = i;
            QVERIFY(button && button->isVisible());
            QCOMPARE(button->property("icon").toString(), QStringLiteral("video"));
            QCOMPARE(button->property("tip").toString(), QStringLiteral("Hide side panels (Alt+B / T)"));
            QCOMPARE(button->opacity(), 0.45);
            QQuickItem* library = s.one("LibraryPanel");
            QQuickItem* inspector = s.one("InspectorPanel");
            QVERIFY(library && inspector);
            QVERIFY(library->isVisible() && inspector->isVisible());

            QTest::mouseClick(s.window, Qt::LeftButton, {},
                              button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(player->property("theatre").toBool(), kWait);
            QVERIFY(topBar->property("panelsHidden").toBool());
            QCOMPARE(button->property("icon").toString(), QStringLiteral("minimize"));
            QCOMPARE(button->property("tip").toString(), QStringLiteral("Show side panels (Alt+B / T)"));
            QCOMPARE(button->opacity(), 1.0);
            QVERIFY(!library->isVisible() && !inspector->isVisible());

            QTest::keyClick(s.window, Qt::Key_B, Qt::AltModifier);
            QTRY_VERIFY_WITH_TIMEOUT(!player->property("theatre").toBool(), kWait);
            QVERIFY(!topBar->property("panelsHidden").toBool());
            QVERIFY(library->isVisible() && inspector->isVisible());
            QCOMPARE(button->property("tip").toString(), QStringLiteral("Hide side panels (Alt+B / T)"));

            // From another mode, Alt+B returns to the Player and hides the panels.
            s.window->setProperty("mode", 2);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 2, kWait);
            QTest::keyClick(s.window, Qt::Key_B, Qt::AltModifier);
            QTRY_COMPARE_WITH_TIMEOUT(s.mode(), 0, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(player->isVisible(), kWait);
            QVERIFY(player->property("theatre").toBool());
            QVERIFY(!library->isVisible() && !inspector->isVisible());

            // Settings (or the handbook) blocks the shortcut.
            QQuickItem* settings = s.one("SettingsPage");
            QVERIFY(settings);
            settings->setProperty("open", true);
            QTRY_VERIFY_WITH_TIMEOUT(s.window->property("overlayOpen").toBool(), kWait);
            QTest::keyClick(s.window, Qt::Key_B, Qt::AltModifier);
            QTest::qWait(150);
            QVERIFY(player->property("theatre").toBool());
            QVERIFY(s.window->property("overlayOpen").toBool());
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join("\n")));
    }
};

QTEST_MAIN(ShellQmlTest)
#include "shell_qml_test.moc"
