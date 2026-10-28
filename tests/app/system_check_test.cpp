// The health check (D-162): the adapter rules, the checks on any PC handed in,
// the report and the controller. The real adapter list is only run, not judged.
#include <QClipboard>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

#include "system_check.h"

using namespace samn::ui;

namespace {
GpuInfo Gpu(const char* name, const char* vendor, quint64 mb) {
    GpuInfo g;
    g.name = QString::fromLatin1(name);
    g.vendor = QString::fromLatin1(vendor);
    g.dedicated_mb = mb;
    return g;
}
QList<GpuInfo> Classified(QList<GpuInfo> g) {
    ClassifyGpus(g);
    return g;
}
const CheckItem* Find(const QList<CheckItem>& items, const char* id) {
    for (const CheckItem& i : items)
        if (i.id == QLatin1String(id)) return &i;
    return nullptr;
}
bool AnyImportantFailure(const QList<CheckItem>& items) {
    for (const CheckItem& i : items)
        if (i.important && i.state == QLatin1String("fail")) return true;
    return false;
}
CheckInputs GoodPc() {
    CheckInputs in;
    in.gpus = Classified({Gpu("Intel UHD Graphics 770", "Intel", 128), Gpu("NVIDIA GeForce RTX 3060", "NVIDIA", 12288)});
    in.config_dir = QStringLiteral("/home/u/.config/samn");
    in.free_bytes = qint64(50) * 1024 * 1024 * 1024;
    return in;
}
}  // namespace

class SystemCheckTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8());
    }

    void vendorIds() {
        QCOMPARE(GpuVendorName(0x10DE), QString("NVIDIA"));
        QCOMPARE(GpuVendorName(0x1002), QString("AMD"));
        QCOMPARE(GpuVendorName(0x1022), QString("AMD"));
        QCOMPARE(GpuVendorName(0x8086), QString("Intel"));
        QCOMPARE(GpuVendorName(0x1414), QString("Microsoft"));
        QCOMPARE(GpuVendorName(0x1234), QString("Other"));
    }

    void anIntelIgpuListedFirstNeverWinsTheTraining() {
        const auto gpus = Classified({Gpu("Intel UHD Graphics 770", "Intel", 128), Gpu("NVIDIA GeForce RTX 3060", "NVIDIA", 12288)});
        QVERIFY(gpus[0].integrated);
        QVERIFY(!gpus[1].integrated);
        const auto pick = PickTrainingGpu(gpus);
        QVERIFY(pick);
        QCOMPARE(pick->name, QString("NVIDIA GeForce RTX 3060"));
    }

    void theNvidiaCardWithTheMostMemoryTrains() {
        const auto pick = PickTrainingGpu(Classified({Gpu("NVIDIA GeForce GTX 1050", "NVIDIA", 4096), Gpu("NVIDIA GeForce RTX 4090", "NVIDIA", 24576)}));
        QVERIFY(pick);
        QCOMPARE(pick->name, QString("NVIDIA GeForce RTX 4090"));
    }

    void classification() {
        const auto g = Classified({Gpu("Intel Arc A750", "Intel", 8192), Gpu("AMD Radeon Graphics", "AMD", 512), Gpu("AMD Radeon RX 6700 XT", "AMD", 12288),
                                   Gpu("Microsoft Basic Render Driver", "Microsoft", 0)});
        QVERIFY(!g[0].integrated);
        QVERIFY(g[1].integrated);
        QVERIFY(!g[2].integrated);
        QVERIFY(g[3].software);
        QVERIFY(!g[0].software);
    }

    void withoutNvidiaThereIsNoTrainingCard() {
        QVERIFY(!PickTrainingGpu(Classified({Gpu("Intel UHD Graphics", "Intel", 128), Gpu("AMD Radeon RX 6700 XT", "AMD", 12288)})));
        QVERIFY(!PickTrainingGpu({}));
        // A software NVIDIA entry does not count.
        GpuInfo soft = Gpu("NVIDIA", "NVIDIA", 100);
        soft.software = true;
        QVERIFY(!PickTrainingGpu({soft}));
    }

    void listingTheRealAdaptersDoesNotCrash() {
        const auto gpus = ListGpus();
        for (const GpuInfo& g : gpus) QVERIFY(!g.name.isEmpty() && !g.vendor.isEmpty());
        qInfo() << "adapters found:" << gpus.size();
    }

    void aGoodPcHasNoImportantProblem() {
        const auto items = RunChecks(GoodPc());
        QStringList ids;
        for (const CheckItem& i : items) ids << i.id;
        QCOMPARE(ids, (QStringList{"settings", "disk", "gpu", "ai-gpu", "video", "onnx", "ai-env", "nudenet"}));
        QVERIFY(!AnyImportantFailure(items));
        for (const CheckItem& i : items) {
            QCOMPARE(i.state, QString("ok"));
            QVERIFY(i.fix.isEmpty());
        }
    }

    void theGpuLineListsEveryAdapter() {
        const auto items = RunChecks(GoodPc());
        const QString d = Find(items, "gpu")->detail;
        QVERIFY2(d.contains("Intel UHD Graphics 770 · integrated"), qPrintable(d));
        QVERIFY2(d.contains("NVIDIA GeForce RTX 3060 · 12 GB · dedicated"), qPrintable(d));
        QCOMPARE(d.count('\n'), 1);
    }

    void trainingNamesTheNvidiaCardAndSaysTheIgpuIsIgnored() {
        const auto ai_items = RunChecks(GoodPc());
        const CheckItem* ai = Find(ai_items, "ai-gpu");
        QCOMPARE(ai->state, QString("ok"));
        QVERIFY2(ai->detail.contains("Training uses NVIDIA GeForce RTX 3060 (CUDA)"), qPrintable(ai->detail));
        QVERIFY2(ai->detail.contains("Intel UHD Graphics 770"), qPrintable(ai->detail));
    }

    void noNvidiaTrainsOnTheProcessor() {
        CheckInputs in = GoodPc();
        in.gpus = Classified({Gpu("Intel UHD Graphics 770", "Intel", 128)});
        const auto ai_items = RunChecks(in);
        const CheckItem* ai = Find(ai_items, "ai-gpu");
        QCOMPARE(ai->state, QString("warn"));
        QVERIFY(ai->detail.contains("training runs on the processor"));
        QVERIFY(!ai->fix.isEmpty());
        QVERIFY(!ai->important);
    }

    void onlySoftwareOrNoAdapterIsAWarning() {
        CheckInputs in = GoodPc();
        in.gpus = Classified({Gpu("Microsoft Basic Render Driver", "Microsoft", 0)});
        QCOMPARE(Find(RunChecks(in), "gpu")->state, QString("warn"));
        in.gpus = {};
        QCOMPARE(Find(RunChecks(in), "gpu")->state, QString("warn"));
    }

    void littleDiskSpaceIsAnImportantFailure() {
        CheckInputs in = GoodPc();
        in.free_bytes = qint64(100) * 1024 * 1024;
        const auto d_items = RunChecks(in);
        const CheckItem* d = Find(d_items, "disk");
        QCOMPARE(d->state, QString("fail"));
        QVERIFY(d->important);
        QVERIFY(!d->fix.isEmpty());
        QVERIFY(AnyImportantFailure(RunChecks(in)));
        in.free_bytes = qint64(1) * 1024 * 1024 * 1024;
        const auto w_items = RunChecks(in);
        const CheckItem* w = Find(w_items, "disk");
        QCOMPARE(w->state, QString("warn"));
        QVERIFY(!AnyImportantFailure(RunChecks(in)));
    }

    void aSettingsFolderThatCannotBeWrittenIsImportant() {
        CheckInputs in = GoodPc();
        in.config_writable = false;
        const auto s_items = RunChecks(in);
        const CheckItem* s = Find(s_items, "settings");
        QCOMPARE(s->state, QString("fail"));
        QVERIFY(s->important);
    }

    void missingH264IsImportantMissingAv1IsAWarning() {
        CheckInputs in = GoodPc();
        in.decodes_h264 = false;
        const auto v_items = RunChecks(in);
        const CheckItem* v = Find(v_items, "video");
        QCOMPARE(v->state, QString("fail"));
        QVERIFY(v->important);
        in = GoodPc();
        in.decodes_av1 = false;
        const auto items = RunChecks(in);
        QCOMPARE(Find(items, "video")->state, QString("warn"));
        QVERIFY(Find(items, "video")->fix.contains("Convert to H.264"));
        QVERIFY(!AnyImportantFailure(items));
    }

    void theOptionalThingsOnlyWarn() {
        CheckInputs in = GoodPc();
        in.onnx_available = false;
        in.nudenet_present = false;
        in.train_env = TrainEnvState::kNotSetUp;
        auto items = RunChecks(in);
        QCOMPARE(Find(items, "onnx")->state, QString("warn"));
        QCOMPARE(Find(items, "nudenet")->state, QString("warn"));
        QCOMPARE(Find(items, "ai-env")->state, QString("warn"));
        QVERIFY(Find(items, "ai-env")->fix.contains("AI Train"));
        QVERIFY(!AnyImportantFailure(items));
        in.train_env = TrainEnvState::kFailed;
        items = RunChecks(in);
        QCOMPARE(Find(items, "ai-env")->state, QString("fail"));
        QVERIFY(!Find(items, "ai-env")->important);
        QVERIFY(!AnyImportantFailure(items));
    }

    void theReportHasEveryTitleAndState() {
        CheckInputs in = GoodPc();
        in.decodes_av1 = false;
        in.free_bytes = qint64(100) * 1024 * 1024;
        const auto items = RunChecks(in);
        const QString r = Report(items);
        for (const CheckItem& i : items) QVERIFY2(r.contains(i.title), qPrintable(i.title));
        QVERIFY(r.contains("[OK] Settings folder"));
        QVERIFY(r.contains("[WARN] Video decoding"));
        QVERIFY(r.contains("[FAIL] Free disk space"));
        QVERIFY(r.contains("SamNPlayer Next"));
    }

    void theControllerSummarisesAndCopies() {
        SystemCheckController c;
        QVERIFY(c.items().isEmpty());
        QVERIFY(!c.running());
        CheckInputs in = GoodPc();
        c.setInputs(in);
        QCOMPARE(c.items().size(), 8);
        QCOMPARE(c.summary(), QString("All checks passed"));
        QVERIFY(!c.importantProblem());
        in.decodes_av1 = false;
        in.nudenet_present = false;
        c.setInputs(in);
        QCOMPARE(c.summary(), QString("2 warnings"));
        in.decodes_h264 = false;
        c.setInputs(in);
        QCOMPARE(c.summary(), QString("1 problem, 1 warning"));  // the video row is now the failure
        QVERIFY(c.importantProblem());
        QCOMPARE(c.items().first().toMap().value("id").toString(), QString("settings"));
        c.copyReport();
        if (auto* clip = QGuiApplication::clipboard()) QVERIFY(clip->text().contains("[FAIL] Video decoding"));
    }

    void refreshRunsOffThreadAndFinishes() {
        SystemCheckController c;
        QSignalSpy spy(&c, &SystemCheckController::changed);
        c.refresh();
        QVERIFY(c.running());
        QTRY_VERIFY_WITH_TIMEOUT(!c.running(), 15000);
        QCOMPARE(c.items().size(), 8);
        QVERIFY(c.items().first().toMap().value("state").toString() == "ok");  // the isolated config folder is writable
    }

    void destroyingWhileRunningIsSafe() {
        auto* c = new SystemCheckController;
        c->refresh();
        delete c;
        QTest::qWait(50);
    }
};

QTEST_MAIN(SystemCheckTest)
#include "system_check_test.moc"
