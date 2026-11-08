#include <QtTest>

#include "mode_registry.h"

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
private slots:
    void cleanup() {
        for (const char* id : {"x", "y"}) ModeRegistry::instance().unregisterMode(id);
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
        QVERIFY(r.registerMode(extra("x")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ids(r.modes()), (QStringList{"player", "generator", "training", "x"}));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+4"));
        QCOMPARE(r.visibleCount(), 4);
    }

    void hiddenModeTakesNoKeyAndNoTab() {
        ModeRegistry& r = ModeRegistry::instance();
        r.setVisible("aitrain", false);
        QVERIFY(r.registerMode(extra("x")));
        QVERIFY(r.registerMode(extra("y")));
        QVERIFY(r.setVisible("x", false));
        QCOMPARE(ids(r.modes()), (QStringList{"player", "generator", "training", "y"}));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+4"));
        QVERIFY(r.setVisible("x", true));
        QCOMPARE(ids(r.modes()).last(), QStringLiteral("y"));  // order of registration, not of showing
    }

    void unregisterRemovesAndRefusesBuiltins() {
        ModeRegistry& r = ModeRegistry::instance();
        QVERIFY(r.registerMode(extra("x")));
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
        QVERIFY(r.registerMode(extra("x", "Ctrl+Shift+X")));
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QStringLiteral("Ctrl+Shift+X"));
        QStringList added;
        for (int i = 0; i < 6; ++i) {
            added << QStringLiteral("t%1").arg(i);
            QVERIFY(r.registerMode(extra(added.last())));
        }
        // Positions 1..9 get Ctrl+N; the 10th visible mode has no key.
        QCOMPARE(r.visibleCount(), 10);
        QCOMPARE(r.modes().last().toMap().value("shortcut").toString(), QString());
        for (const QString& id : added) r.unregisterMode(id);
    }

    void disabledModeStaysListed() {
        ModeRegistry& r = ModeRegistry::instance();
        QVERIFY(r.registerMode(extra("x")));
        QVERIFY(r.setEnabled("x", false));
        QVERIFY(!r.modes().last().toMap().value("enabled").toBool());
        QVERIFY(!r.setEnabled("nope", false));
    }
};

QTEST_MAIN(ModeRegistryTest)
#include "mode_registry_test.moc"
