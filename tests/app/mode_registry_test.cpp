#include <QtTest>

#include <QTemporaryDir>

#include "mode_registry.h"
#include "theme.h"

using samn::ui::ModeDescriptor;
using samn::ui::ModeRegistry;

namespace {
QStringList ids(const QVariantList& modes) {
    QStringList out;
    for (const QVariant& m : modes) out << m.toMap().value("id").toString();
    return out;
}
ModeDescriptor extra(const QString& id, const QString& shortcut = {}) {
    ModeDescriptor d;
    d.id = id;
    d.title = id.toUpper();
    d.icon = "layers";
    d.hue = QColor("#123456");
    d.page = "X.qml";
    d.shortcut = shortcut;
    return d;
}
}  // namespace

class ModeRegistryTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;
    QStringList registered_;

    // Registers through the registry and remembers the id, so cleanup() removes it even when a check fails.
    bool add(const ModeDescriptor& d) {
        registered_ << d.id;
        return ModeRegistry::instance().registerMode(d);
    }

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        // Before the registry exists: it reads the plugin switch from the settings; never the developer's.
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }

    void cleanup() {
        for (const QString& id : registered_) ModeRegistry::instance().unregisterMode(id);
        registered_.clear();
    }

    // The shipped titles, icons and hues are pinned: a change is deliberate. The hues are the Theme's.
    void builtinsMatchTheThemeAndTheShippedLook() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", true);
        samn::ui::Theme theme;
        const QVariantList m = r.modes();
        QCOMPARE(m.size(), 4);
        const struct { const char* title; const char* icon; QColor hue; } want[] = {
            {"Player", "player", theme.cyan()}, {"Generator", "create", theme.orange()},
            {"Training", "training", theme.blue()}, {"AI Train", "layers", theme.orange()}};
        for (int i = 0; i < 4; ++i) {
            const QVariantMap v = m[i].toMap();
            QCOMPARE(v.value("title").toString(), QString::fromLatin1(want[i].title));
            QCOMPARE(v.value("icon").toString(), QString::fromLatin1(want[i].icon));
            QCOMPARE(v.value("hue").value<QColor>(), want[i].hue);
        }
        r.setVisible("aitrain", false);
    }

    void builtinsComeFirstInTheShippedOrder() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QCOMPARE(ids(r.modes()), (QStringList{"player", "generator", "training"}));
        const QVariantList m = r.modes();
        QCOMPARE(m[0].toMap().value("title").toString(), QStringLiteral("Player"));
        QCOMPARE(m[1].toMap().value("icon").toString(), QStringLiteral("create"));
        QCOMPARE(m[2].toMap().value("hue").value<QColor>(), QColor("#00b4ff"));
        QCOMPARE(m[0].toMap().value("shortcut").toString(), QStringLiteral("Ctrl+1"));
        QCOMPARE(m[2].toMap().value("shortcut").toString(), QStringLiteral("Ctrl+3"));
    }

    void registeredModeAppearsAndGetsTheNextKey() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QSignalSpy spy(&r, &ModeRegistry::changed);
        QVERIFY(add(extra("x")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ids(r.modes()), (QStringList{"player", "generator", "training", "x"}));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+4"));
        QCOMPARE(r.visibleCount(), 4);
    }

    void hiddenModeTakesNoKeyAndNoTab() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QVERIFY(add(extra("x")));
        QVERIFY(add(extra("y")));
        QVERIFY(r.setVisible("x", false));
        QCOMPARE(ids(r.modes()), (QStringList{"player", "generator", "training", "y"}));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+4"));
        QVERIFY(r.setVisible("x", true));
        QCOMPARE(ids(r.modes()).last(), QStringLiteral("y"));  // order of registration, not of showing
    }

    void unregisterRemovesAndRefusesBuiltins() {
        ModeRegistry& r = ModeRegistry::instance();
        QVERIFY(add(extra("x")));
        QVERIFY(r.unregisterMode("x"));
        QVERIFY(!r.contains("x"));
        QVERIFY(!r.unregisterMode("x"));
        QVERIFY(!r.unregisterMode("player"));
        QVERIFY(!r.setVisible("player", false));
        QVERIFY(r.contains("player"));
    }

    void rejectsEmptyAndDuplicateIds() {
        ModeRegistry& r = ModeRegistry::instance();
        QVERIFY(!r.registerMode(extra("")));
        QVERIFY(!r.registerMode(extra("player")));
        ModeDescriptor untitled = extra("x");
        untitled.title.clear();
        QVERIFY(!r.registerMode(untitled));
        QVERIFY(!r.contains("x"));
    }

    void explicitShortcutIsKeptAndTenthModeHasNone() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QVERIFY(add(extra("x", "Ctrl+Shift+X")));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+Shift+X"));
        QStringList added;
        for (int i = 0; i < 6; ++i) {
            added << QStringLiteral("t%1").arg(i);
            QVERIFY(add(extra(added.last())));
        }
        // Positions 1..9 get Ctrl+N; the 10th visible mode has no key.
        QCOMPARE(r.visibleCount(), 10);
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QString());
    }

    void builtinFlagFromOutsideIsRefused() {
        ModeRegistry& r = ModeRegistry::instance();
        ModeDescriptor d = extra("x");
        d.builtin = true;
        QVERIFY(!r.registerMode(d));
        QVERIFY(!r.contains("x"));
    }

    void builtinsCannotBeDisabled() {
        ModeRegistry& r = ModeRegistry::instance();
        for (const char* id : {"player", "generator", "training"}) QVERIFY(!r.setEnabled(id, false));
        for (const QVariant& m : r.modes()) QVERIFY(m.toMap().value("enabled").toBool());
    }

    void pagesChangeOnlyWithTheVisibleExtraModes() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QSignalSpy pages(&r, &ModeRegistry::pagesChanged);
        QVERIFY(add(extra("x")));
        QCOMPARE(pages.count(), 1);
        QVERIFY(r.setEnabled("x", false));  // the page stays
        QVERIFY(r.setEnabled("x", true));
        QCOMPARE(pages.count(), 1);
        QVERIFY(r.setVisible("x", false));
        QCOMPARE(pages.count(), 2);
        QVERIFY(r.pages().isEmpty());
    }

    void disabledModeStaysListed() {
        ModeRegistry& r = ModeRegistry::instance();
        QVERIFY(add(extra("x")));
        QVERIFY(r.setEnabled("x", false));
        QVERIFY(!r.modes().last().toMap().value("enabled").toBool());
        QVERIFY(!r.setEnabled("nope", false));
    }
};

QTEST_MAIN(ModeRegistryTest)
#include "mode_registry_test.moc"
