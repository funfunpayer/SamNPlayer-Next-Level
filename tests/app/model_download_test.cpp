// Models fetched with one click (D-155 step 4): the zip reader and the hash
// checks. The archives are built here, in memory.
#include <QCryptographicHash>
#include <QtTest>

#include "model_download.h"
#include "zip_builder.h"

using samn::ui::ModelSource;

namespace {
using testzip::Zip;
QByteArray Sha(const QByteArray& b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); }
}  // namespace

class ModelDownloadTest : public QObject {
    Q_OBJECT
private slots:
    void readsStoredAndDeflatedEntries() {
        QByteArray model(300000, Qt::Uninitialized);
        for (int i = 0; i < model.size(); ++i) model[i] = char((i * 7) % 251);
        const QByteArray zip = Zip({{"pkg/__init__.py", "x = 1\n", false}, {"pkg/320n.onnx", model, true}});
        QString error;
        auto got = samn::ui::ReadZipEntry(zip, "pkg/320n.onnx", &error);
        QVERIFY2(got.has_value(), qPrintable(error));
        QCOMPARE(*got, model);
        got = samn::ui::ReadZipEntry(zip, "pkg/__init__.py", &error);
        QVERIFY(got.has_value());
        QCOMPARE(*got, QByteArray("x = 1\n"));
        QVERIFY(!samn::ui::ReadZipEntry(zip, "pkg/missing.onnx", &error));
        QVERIFY(error.contains("not in the archive"));
    }

    void brokenArchivesAreRefused() {
        const QByteArray model(5000, 'm');
        QByteArray zip = Zip({{"m.onnx", model, true}});
        QString error;
        QVERIFY(!samn::ui::ReadZipEntry("not a zip at all", "m.onnx", &error));
        QVERIFY(error.contains("not a zip"));
        QVERIFY(!samn::ui::ReadZipEntry(zip.left(zip.size() / 2) + zip.right(22), "m.onnx", &error));  // cut short
        // A flipped byte in the stored data: the CRC catches it.
        QByteArray stored = Zip({{"m.onnx", model, false}});
        stored[30 + 6 + 100] = char(stored[30 + 6 + 100] ^ 0x55);
        QVERIFY(!samn::ui::ReadZipEntry(stored, "m.onnx", &error));
        QVERIFY(error.contains("damaged"));
    }

    void bothHashesAreChecked() {
        const QByteArray model(4000, 'n');
        const QByteArray zip = Zip({{"nudenet/320n.onnx", model, true}});
        ModelSource src{"test", QUrl(), Sha(zip), "nudenet/320n.onnx", Sha(model), "m.onnx"};
        QString error;
        auto got = samn::ui::ExtractModel(zip, src, &error);
        QVERIFY2(got.has_value(), qPrintable(error));
        QCOMPARE(*got, model);
        ModelSource wrong = src;
        wrong.archive_sha256 = Sha("other");
        QVERIFY(!samn::ui::ExtractModel(zip, wrong, &error));
        QVERIFY(error.contains("download"));
        wrong = src;
        wrong.model_sha256 = Sha("other");
        QVERIFY(!samn::ui::ExtractModel(zip, wrong, &error));
        QVERIFY(error.contains("model"));
        // The real source is pinned.
        const ModelSource nn = samn::ui::NudeNetSource();
        QCOMPARE(nn.url.scheme(), QString("https"));
        QCOMPARE(nn.archive_sha256.size(), 64);
        QCOMPARE(nn.model_sha256.size(), 64);
        QCOMPARE(nn.entry, QString("nudenet/320n.onnx"));
    }
};

QTEST_GUILESS_MAIN(ModelDownloadTest)
#include "model_download_test.moc"
