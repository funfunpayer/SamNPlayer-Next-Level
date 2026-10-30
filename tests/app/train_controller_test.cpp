// The student's training (D-155 step 5) with stand-ins: a fake uv that makes
// a real virtual environment with tiny torch and rfdetr modules, and a fake
// training script with the real one's JSON lines (tests/app/data). Nothing
// is downloaded and nothing learns; the controller's flow is what is tested.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "train_controller.h"

using samn::ui::TrainController;

namespace {
const QString kData = QStringLiteral(SAMN_TEST_DATA);
QString Python() {
    for (const char* name : {"python3", "python"}) {
        const QString p = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!p.isEmpty()) return p;
    }
    return {};
}
void AddClip(const QString& name, int frames) {
    const QString d = TrainController::datasetsDir() + "/" + name;
    QVERIFY(QDir().mkpath(d + "/images"));
    QVERIFY(QDir().mkpath(d + "/labels"));
    QFile c(d + "/classes.txt");
    QVERIFY(c.open(QIODevice::WriteOnly));
    c.write("glans\n");
    c.close();
    for (int i = 0; i < frames; ++i) {
        QFile f(d + QStringLiteral("/images/%1_%2.jpg").arg(name).arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
}
}  // namespace

class TrainControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString python_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8());
        python_ = Python();
        QVERIFY2(!python_.isEmpty(), "the tests need a Python on the PATH for the stand-ins");
        qputenv("FAKE_TRAIN_MODEL", (QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_detr.onnx").toUtf8());
    }

    void uvIsPinnedForThisPlatform() {
        const auto uv = samn::ui::UvSource();
#if (defined(Q_OS_WIN) || defined(Q_OS_LINUX)) && defined(Q_PROCESSOR_X86_64)
        QCOMPARE(uv.url.scheme(), QString("https"));
        QCOMPARE(uv.archive_sha256.size(), 64);
        QCOMPARE(uv.model_sha256.size(), 64);
        QVERIFY(uv.entry.startsWith("uv-0.13.4.data/scripts/uv"));
#else
        QVERIFY(uv.url.isEmpty());
#endif
    }

    void aFailedStepSaysWhyAndLeavesItNotReady() {
        TrainController c;
        c.setUvCommand(python_, {kData + "/fake_uv.py"});
        qputenv("FAKE_UV_FAIL", "venv");
        QVERIFY(c.setUpEnvironment());
        QVERIFY(c.settingUp());
        QTRY_VERIFY_WITH_TIMEOUT(!c.settingUp(), 60000);
        qunsetenv("FAKE_UV_FAIL");
        QVERIFY(!c.envReady());
        QVERIFY2(c.envStatus().contains("failed") && c.envStatus().contains("no network"), qPrintable(c.envStatus()));
        QVERIFY(!c.train());
        QVERIFY(c.trainStatus().contains("Set up"));
    }

    void theEnvironmentIsSetUpInTheAiFolder() {
        TrainController c;
        c.setDevice("cpu");
        c.setUvCommand(python_, {kData + "/fake_uv.py"});
        QVERIFY(c.setUpEnvironment());
        QVERIFY(!c.setUpEnvironment());  // one at a time
        QTRY_VERIFY_WITH_TIMEOUT(!c.settingUp(), 120000);
        QVERIFY2(c.envReady(), qPrintable(c.envStatus()));
        QVERIFY2(c.envStatus().contains("Ready: PyTorch 9.9.9 on the processor"), qPrintable(c.envStatus()));
        QFile log(TrainController::aiDir() + "/cache/fake_uv.log");
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QList<QByteArray> calls = log.readAll().trimmed().split('\n');
        QCOMPARE(calls.size(), 5);  // the failed run's install and venv, then install, venv, pip
        const auto call = [&](int i) { return QJsonDocument::fromJson(calls[i]).object(); };
        QCOMPARE(call(2).value("args").toVariant().toStringList(), (QStringList{"python", "install", "3.12"}));
        const QStringList pip = call(4).value("args").toVariant().toStringList();
        QCOMPARE(pip.mid(0, 2), (QStringList{"pip", "install"}));
        QCOMPARE(pip.value(pip.indexOf("--torch-backend") + 1), QString("cpu"));
        QCOMPARE(pip.last(), QString("rfdetr[train,onnx]==1.11.2"));
        QCOMPARE(pip.value(pip.indexOf("--python") + 1), c.envPython());
        // Python goes into the AI folder, not the system.
        QCOMPARE(QDir::cleanPath(call(2).value("python_dir").toString()), QDir::cleanPath(TrainController::aiDir() + "/python"));
        // Remembered.
        TrainController again;
        QCOMPARE(again.device(), QString("cpu"));
        QVERIFY(again.envReady());
    }

    void trainingNeedsSamples() {
        TrainController c;
        c.setTrainScript(kData + "/fake_train.py");
        QVERIFY(c.datasets().isEmpty());
        AddClip("few", 4);
        QCOMPARE(c.datasets().size(), 1);
        QVERIFY(!c.train());
        QVERIFY(c.trainStatus().contains("at least 10"));
        QVERIFY(c.removeDataset("few"));
        QVERIFY(!c.removeDataset("../config"));
        QVERIFY(c.datasets().isEmpty());
    }

    void aRunTrainsAndKeepsTheStudent() {
        AddClip("clipA", 8);
        AddClip("clipB", 6);
        TrainController c;
        c.setEpochs(2);
        c.setTrainScript(kData + "/fake_train.py");
        QVERIFY(c.studentPath().isEmpty());
        QVERIFY(c.train());
        QVERIFY(c.training());
        QVERIFY(!c.train());  // one at a time
        QTRY_VERIFY_WITH_TIMEOUT(!c.training(), 60000);
        QVERIFY2(c.trainStatus().startsWith("Done"), qPrintable(c.trainStatus()));
        QCOMPARE(c.trainProgress(), 100);
        QVERIFY(QFileInfo::exists(c.studentPath()));
        QCOMPARE(c.studentClasses(), QStringList{"glans"});
        // The run folder keeps only its log and the classes.
        const QStringList runs = QDir(TrainController::aiDir() + "/runs").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(runs.size(), 1);
        QVERIFY(!QFile::exists(TrainController::aiDir() + "/runs/" + runs[0] + "/student.onnx"));
        QCOMPARE(TrainController().epochs(), 2);
    }

    void stopKeepsWhatWasLearned() {
        TrainController c;
        c.setTrainScript(kData + "/fake_train.py");
        qputenv("FAKE_TRAIN_SLOW", "1");
        QVERIFY(c.train());
        QTRY_VERIFY_WITH_TIMEOUT(c.trainStatus().startsWith("Epoch"), 30000);
        c.stop();
        QVERIFY(c.trainStatus().startsWith("Stopping"));
        QTRY_VERIFY_WITH_TIMEOUT(!c.training(), 30000);
        qunsetenv("FAKE_TRAIN_SLOW");
        QVERIFY2(c.trainStatus().contains("Stopped early"), qPrintable(c.trainStatus()));
        QVERIFY(!c.studentPath().isEmpty());
    }

    void anErrorIsShown() {
        TrainController c;
        c.setTrainScript(kData + "/fake_train.py");
        qputenv("FAKE_TRAIN_ERROR", "1");
        QVERIFY(c.train());
        QTRY_VERIFY_WITH_TIMEOUT(!c.training(), 30000);
        qunsetenv("FAKE_TRAIN_ERROR");
        QVERIFY2(c.trainStatus().contains("Error: ValueError: too few samples"), qPrintable(c.trainStatus()));
        TrainController missing;
        missing.setTrainScript(kData + "/no_such_script.py");
        QVERIFY(!missing.train());
        QVERIFY(missing.trainStatus().contains("script is missing"));
    }

    // A Python that cannot start ends the run with a reason; a new one can start.
    void aPythonThatCannotStartEndsTheRun() {
        TrainController c;
        c.setTrainScript(kData + "/fake_train.py");
        QVERIFY(c.envReady());
        const QString python = c.envPython(), aside = python + ".aside";
        QVERIFY(QFile::rename(python, aside));
        {
            QFile stub(python);
            QVERIFY(stub.open(QIODevice::WriteOnly));
            stub.write("not a program\n");
        }
        QVERIFY(QFile::setPermissions(python, QFile::ReadOwner | QFile::WriteOwner));
        c.train();
        QTRY_VERIFY_WITH_TIMEOUT(!c.training(), 10000);
        QVERIFY2(c.trainStatus().contains("cannot start"), qPrintable(c.trainStatus()));
        QVERIFY(QFile::remove(python));
        QVERIFY(QFile::rename(aside, python));
        QVERIFY(c.train());  // not stuck
        QTRY_VERIFY_WITH_TIMEOUT(!c.training(), 60000);
    }
};

QTEST_GUILESS_MAIN(TrainControllerTest)
#include "train_controller_test.moc"
