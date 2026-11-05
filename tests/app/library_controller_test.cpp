#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "library_controller.h"

using samn::ui::LibraryController;

class LibraryControllerTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> cfg_;

private slots:
    void init() {
        cfg_ = std::make_unique<QTemporaryDir>();
        qputenv("SAMN_CONFIG_DIR", cfg_->path().toUtf8());
    }

    // Review A1: `changed` only fires on the instance that wrote. Two pages with
    // a controller each go stale; the app hands one shared instance to both.
    void changedFiresOnTheWritingInstanceOnly() {
        QTemporaryDir d;
        const QString a = d.filePath("a.funscript");
        QFile(a).open(QIODevice::WriteOnly);
        LibraryController writer, other;
        QSignalSpy writerSpy(&writer, &LibraryController::changed);
        QSignalSpy otherSpy(&other, &LibraryController::changed);
        writer.noteScriptGenerated(a);
        QCOMPARE(writerSpy.count(), 1);
        QCOMPARE(otherSpy.count(), 0);  // the other page's bindings never re-read: why one instance is shared
    }

    void recentFavoritesAndScripts() {
        QTemporaryDir d;
        const QString a = d.filePath("a.funscript"), b = d.filePath("b.funscript");
        QFile(a).open(QIODevice::WriteOnly);
        {
            LibraryController lib;
            QSignalSpy changed(&lib, &LibraryController::changed);
            lib.noteScriptOpened(a);
            lib.noteScriptOpened(b);
            lib.noteScriptOpened(a);
            QCOMPARE(lib.recent().size(), 2);
            QCOMPARE(lib.recent()[0].toMap()["name"].toString(), QString("a.funscript"));
            QVERIFY(lib.recent()[0].toMap()["exists"].toBool());
            QVERIFY(!lib.recent()[1].toMap()["exists"].toBool());  // b was never created
            QCOMPARE(lib.imported().size(), 2);
            QVERIFY(lib.toggleFavorite(b));
            QVERIFY(lib.isFavorite(b));
            lib.noteScriptEdited(a);
            lib.noteVideoOpened(d.filePath("clip.mp4"));
            QVERIFY(changed.count() >= 6);
        }
        // Kept across a restart.
        LibraryController lib;
        QCOMPARE(lib.recent().size(), 2);
        QCOMPARE(lib.favorites().size(), 1);
        QCOMPARE(lib.edited().size(), 1);
        QCOMPARE(lib.recentVideos()[0].toMap()["name"].toString(), QString("clip.mp4"));
        QVERIFY(!lib.toggleFavorite(b));
        QCOMPARE(lib.favorites().size(), 0);
    }

    void recentIsCapped() {
        LibraryController lib;
        for (int i = 0; i < 12; ++i) lib.noteScriptOpened(QStringLiteral("/tmp/s%1.funscript").arg(i));
        QCOMPARE(lib.recent().size(), 8);
        QCOMPARE(lib.imported().size(), 12);
    }
};

QTEST_MAIN(LibraryControllerTest)
#include "library_controller_test.moc"
