// Error toasts (0.7 notify.js showToast): a reported failure becomes a toast
// and an error in the log; at most 3, clipped at 220 characters, gone after
// the show time or a click; a repeat is one toast. The player toasts its
// failures, not its hints.
#include <QTemporaryDir>
#include <QtTest>

#include "app_log.h"
#include "notices.h"
#include "player_controller.h"

using samn::ui::AppLog;
using samn::ui::Notices;

class NoticesTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    static QStringList texts(const Notices& n) {
        QStringList out;
        for (const QVariant& v : n.toasts()) out << v.toMap().value("text").toString();
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.path().toUtf8());
    }

    void aFailureIsAToastAndAnError() {
        Notices n;
        AppLog::instance().buffer().Clear();
        AppLog::instance().reportError("Could not save: disk full");
        QTRY_COMPARE(texts(n), QStringList{"Could not save: disk full"});
        const auto entries = AppLog::instance().buffer().Entries();
        QVERIFY(!entries.empty());
        QCOMPARE(entries.back().level, samn::LogSeverity::kError);
        QCOMPARE(QString::fromStdString(entries.back().text), QString("Could not save: disk full"));
        // Plain log lines, also errors, are no toast (0.7: only uiError).
        AppLog::instance().add(samn::LogSeverity::kError, "a device detail");
        QTest::qWait(50);
        QCOMPARE(n.toasts().size(), 1);
        // Nothing to say, nothing shown.
        n.error("   ");
        QTest::qWait(50);
        QCOMPARE(n.toasts().size(), 1);
    }

    void threeAtMostClippedAndARepeatIsOne() {
        Notices n;
        for (int i = 1; i <= 5; ++i) n.error(QStringLiteral("failure %1").arg(i));
        n.error("failure 5");
        QTRY_COMPARE(texts(n), (QStringList{"failure 3", "failure 4", "failure 5"}));
        n.error(QString(300, 'x'));
        QTRY_VERIFY(texts(n).last().startsWith('x'));
        QCOMPARE(n.toasts().size(), 3);
        const QString last = texts(n).last();
        QCOMPARE(last, QString(Notices::kMaxChars - 3, 'x') + QStringLiteral("…"));  // 0.7: slice(0, 217) + '…'
    }

    void aClickOrTheTimeDismisses() {
        Notices n;
        n.setShowMs(300);
        n.error("first");
        n.error("second");
        QTRY_COMPARE(n.toasts().size(), 2);
        n.dismiss(n.toasts().first().toMap().value("id").toInt());
        QCOMPARE(texts(n), QStringList{"second"});
        n.dismiss(9999);  // gone already: nothing happens
        QTRY_VERIFY_WITH_TIMEOUT(n.toasts().isEmpty(), 3000);
    }

    void thePlayerToastsFailuresNotHints() {
        Notices n;
        samn::ui::PlayerController c;
        QFile broken(dir_.filePath("broken.funscript"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ not json");
        broken.close();
        c.openPaths({broken.fileName()});
        QVERIFY2(c.lastError().startsWith("Could not open broken.funscript"), qPrintable(c.lastError()));
        QTRY_VERIFY(!n.toasts().isEmpty());
        QVERIFY2(texts(n).last().startsWith("Could not open broken.funscript"), qPrintable(texts(n).join(" | ")));
        const int before = n.toasts().size();
        // A hint: "Open a script first." stays in the status line.
        c.bakeOffset();
        QVERIFY2(c.lastError() == "Open a script first.", qPrintable(c.lastError()));
        QTest::qWait(50);
        QCOMPARE(n.toasts().size(), before);
    }
};

QTEST_GUILESS_MAIN(NoticesTest)
#include "notices_test.moc"
