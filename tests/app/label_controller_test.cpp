// The AI Train label editor's controller (D-155 §2.2): tracks, keys, undo,
// saving beside the clip and the dataset export. Synthetic clips only.
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "label_controller.h"
#include "synthetic_clips.h"

using samn::ui::LabelController;

class LabelControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString clip_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        clip_ = dir_.filePath("clip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(clip_.toStdU16String()), 4, 80.0, 1.0));
    }

    void labelsKeysUndoAndSaving() {
        LabelController c;
        QVERIFY(!c.openVideo(dir_.filePath("missing.mp4")));
        QVERIFY(c.openVideo(QUrl::fromLocalFile(clip_).toString()));
        QVERIFY(c.hasVideo());
        QVERIFY(c.durationMs() > 3000);
        QVERIFY(!c.frame().isNull());
        QVERIFY(c.labels().isEmpty());
        QVERIFY(!c.setBox(QRectF(10, 10, 40, 40)));  // no label yet
        QVERIFY(c.presetLabels().contains("glans"));

        QVERIFY(c.addLabel("Glans"));
        QCOMPARE(c.currentLabel(), QString("glans"));
        QVERIFY(c.setBox(QRectF(10, 20, 40, 40)));
        c.seek(2000);
        const double t = c.previewMs();
        QVERIFY(c.setBox(QRectF(110, 20, 40, 40)));
        c.seek(t / 2);
        const QVariantMap b = c.boxes().value(0).toMap();
        QVERIFY(b.value("present").toBool());
        QVERIFY(b.value("reviewed").toBool());
        QVERIFY(qAbs(b.value("x").toDouble() - 60) < 3);  // halfway
        QVERIFY(!b.value("keyed").toBool());
        QVERIFY(!c.setBox(QRectF(0, 0, 2, 2)));  // too small

        c.seek(3000);
        QVERIFY(c.markAbsent());
        QVERIFY(!c.boxes().value(0).toMap().value("present").toBool());
        const QVariantList lanes = c.lanes();
        QCOMPARE(lanes.size(), 1);
        QCOMPARE(lanes[0].toMap().value("keys").toList().size(), 3);
        QCOMPARE(lanes[0].toMap().value("spans").toList().size(), 3);

        // Undo and redo, one step each; every step is saved.
        QVERIFY(c.undo());
        QVERIFY(c.boxes().value(0).toMap().value("present").toBool());
        QVERIFY(c.redo());
        QVERIFY(!c.boxes().value(0).toMap().value("present").toBool());
        QVERIFY(c.removeKey());
        QVERIFY(c.boxes().value(0).toMap().value("present").toBool());
        QVERIFY(c.markAbsent(true));  // covered
        QVERIFY(c.lanes()[0].toMap().value("keys").toList().last().toMap().value("covered").toBool());
        QVERIFY(c.undo());
        QVERIFY(QFile::exists(dir_.filePath("clip.labels.json")));

        LabelController again;
        QVERIFY(again.openVideo(clip_));
        QCOMPARE(again.labels(), QStringList{"glans"});
        QCOMPARE(again.currentLabel(), QString("glans"));
        QCOMPARE(again.lanes()[0].toMap().value("keys").toList().size(), 2);
        QVERIFY(!again.canUndo());  // a new session starts with its own history
    }

    void exportWritesImagesLabelsAndClasses() {
        LabelController c;
        QVERIFY(c.openVideo(clip_));
        QVERIFY(c.addLabel("mouth"));
        QVERIFY(c.setBox(QRectF(20, 20, 30, 30)));
        c.seek(1000);
        QVERIFY(c.markAbsent());
        const QString out = dir_.filePath("dataset");
        const int n = c.exportDataset(QUrl::fromLocalFile(out).toString(), 500);
        QVERIFY(n >= 8);  // glans and mouth are reviewed over the clip
        QCOMPARE(QDir(out + "/images").entryList(QDir::Files).size(), n);
        QCOMPARE(QDir(out + "/labels").entryList(QDir::Files).size(), n);
        QFile classes(out + "/classes.txt");
        QVERIFY(classes.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(classes.readAll()), QString("glans\nmouth\n"));
        QFile first(out + "/labels/clip_00000000.txt");
        QVERIFY(first.open(QIODevice::ReadOnly));
        const QString lines = QString::fromUtf8(first.readAll());
        QVERIFY2(lines.contains(QRegularExpression("^0 [0-9.]+ [0-9.]+ [0-9.]+ [0-9.]+\n1 ")), qPrintable(lines));
    }

    // A class has several objects: two hands, two breasts; the model learns the class.
    void aClassHasSeveralObjects() {
        const QString hands = dir_.filePath("hands.mp4");
        QVERIFY(QFile::copy(clip_, hands));  // its own, empty labels file
        LabelController c;
        QVERIFY(c.openVideo(hands));
        QVERIFY(c.presetLabels().contains("hand"));
        QVERIFY(c.presetLabels().contains("contact"));
        QVERIFY(c.presetLabels().contains("exclude"));
        QVERIFY(c.addLabel("hand"));
        QVERIFY(c.setBox(QRectF(10, 10, 30, 30)));
        QVERIFY(c.addLabel("hand"));
        QCOMPARE(c.currentLabel(), QString("hand 2"));
        QVERIFY(c.setBox(QRectF(100, 10, 30, 30)));
        // A thigh the tracker must not follow: an exclusion zone.
        QVERIFY(c.addLabel("exclude"));
        QVERIFY(c.setBox(QRectF(0, 40, 60, 20)));
        const QVariantList lanes = c.lanes();
        QCOMPARE(lanes.size(), 3);
        QCOMPARE(lanes[1].toMap().value("cls").toString(), QString("hand"));
        QCOMPARE(lanes[1].toMap().value("classIndex").toInt(), 0);
        QCOMPARE(lanes[2].toMap().value("cls").toString(), QString("exclude"));
        QCOMPARE(lanes[2].toMap().value("classIndex").toInt(), 1);
        const QString out = dir_.filePath("hands_ds");
        QVERIFY(c.exportDataset(out, 1000) > 0);
        QFile classes(out + "/classes.txt");
        QVERIFY(classes.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(classes.readAll()), QString("hand\nexclude\n"));
        QFile first(out + "/labels/hands_00000000.txt");
        QVERIFY(first.open(QIODevice::ReadOnly));
        const QString lines = QString::fromUtf8(first.readAll());
        QCOMPARE(lines.count(QRegularExpression("^0 ", QRegularExpression::MultilineOption)), 2);
        QCOMPARE(lines.count(QRegularExpression("^1 ", QRegularExpression::MultilineOption)), 1);
    }

    void phasesAreALaneOfTheirOwn() {
        const QString path = dir_.filePath("phases.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        QVERIFY(c.phaseNames().contains("climax"));
        QVERIFY(c.phaseNames().contains("cool-down"));
        QVERIFY(c.setPhase(0, 1000, "intro"));
        QVERIFY(c.setPhase(1000, 99999, "oral"));  // clamped to the clip
        QCOMPARE(c.phases().size(), 2);
        QCOMPARE(c.phases()[1].toMap().value("endMs").toDouble(), double(qRound64(c.durationMs())));  // whole ms
        c.seek(500);
        QCOMPARE(c.phaseAtPreview(), QString("intro"));
        QVERIFY(c.undo());
        QCOMPARE(c.phases().size(), 1);
        QVERIFY(c.redo());
        // Saved beside the clip and back on the next open.
        LabelController again;
        QVERIFY(again.openVideo(path));
        QCOMPARE(again.phases().size(), 2);
        // Exported for the scene model.
        QVERIFY(again.setPhase(0, 0, "intro") == false);
        const QString out = dir_.filePath("phases_ds");
        again.exportDataset(out, 1000);
        QFile csv(out + "/phases.csv");
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(csv.readAll());
        QVERIFY(text.startsWith("clip,start_ms,end_ms,phase\n"));
        QVERIFY(text.contains("phases.mp4,0,1000,intro\n"));
    }

    void anImageIsAClipOfOneFrame() {
        QImage img(64, 48, QImage::Format_RGB888);
        img.fill(Qt::darkCyan);
        const QString path = dir_.filePath("still.png");
        QVERIFY(img.save(path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        QCOMPARE(c.durationMs(), 0.0);
        QCOMPARE(c.videoSize(), QSize(64, 48));
        QVERIFY(c.addLabel("hand_1"));
        QVERIFY(c.setBox(QRectF(8, 8, 20, 20)));
        QCOMPARE(c.exportDataset(dir_.filePath("still_ds"), 500), 1);
    }
};

QTEST_MAIN(LabelControllerTest)
#include "label_controller_test.moc"
