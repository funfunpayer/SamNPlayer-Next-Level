// The AI Train label editor's controller (D-155 §2.2): tracks, keys, undo,
// saving beside the clip and the dataset export. Synthetic clips only.
#include <QDir>
#include <memory>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "label_controller.h"
#include "model_download.h"
#include "samn/vision/onnx_detector.h"
#include "fake_file_server.h"
#include "zip_builder.h"
#include "synthetic_clips.h"

using samn::ui::LabelController;

// A model server for the tests: /v1/models and /v1/chat/completions answer
// what the test sets; the last request body is kept.
class FakeModelServer : public QObject {
public:
    QTcpServer server;
    QByteArray answer;  // the chat completion's content
    QByteArray lastBody, lastAuth;
    explicit FakeModelServer() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* c = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(c, &QTcpSocket::readyRead, c, [this, c, buffer] {
                    *buffer += c->readAll();
                    const int head = int(buffer->indexOf("\r\n\r\n"));
                    if (head < 0) return;
                    const QList<QByteArray> lines = buffer->left(head).split('\n');
                    int length = 0;
                    for (const QByteArray& l : lines) {
                        if (l.toLower().startsWith("content-length:")) length = l.mid(15).trimmed().toInt();
                        if (l.toLower().startsWith("authorization:")) lastAuth = l.mid(14).trimmed();
                    }
                    if (buffer->size() < head + 4 + length) return;
                    lastBody = buffer->mid(head + 4, length);
                    QByteArray body;
                    if (lines.front().contains("/v1/models"))
                        body = R"({"data":[{"id":"vision-b"},{"id":"vision-a"}]})";
                    else {
                        QJsonObject msg{{"role", "assistant"}, {"content", QString::fromUtf8(answer)}};
                        body = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", msg}}}}}).toJson();
                    }
                    c->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                             QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    c->disconnectFromHost();
                });
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()); }
};

class LabelControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString clip_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        qputenv("SAMN_CONFIG_DIR", dir_.filePath("config").toUtf8());  // never the user's settings
        clip_ = dir_.filePath("clip.mp4");
        QVERIFY(samn::engine::testclips::WriteMoving(std::filesystem::path(clip_.toStdU16String()), 4, 80.0, 1.0));
    }

    void labelsKeysUndoAndSaving() {
        LabelController c;
        c.setRetrackAfterDrag(false);  // the keys here are counted by hand
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
        // Its own copy of the clip, so it does not lean on what another test saved.
        QVERIFY(QDir(dir_.path()).mkpath("export"));
        const QString clip = dir_.filePath("export/clip.mp4");
        QFile::remove(clip);
        QFile::remove(dir_.filePath("export/clip.labels.json"));
        QVERIFY(QFile::copy(clip_, clip));
        LabelController c;
        QVERIFY(c.openVideo(clip));
        QVERIFY(c.addLabel("glans"));
        QVERIFY(c.setBox(QRectF(10, 20, 40, 40)));
        c.seek(2000);
        QVERIFY(c.setBox(QRectF(110, 20, 40, 40)));
        c.seek(0);
        QVERIFY(c.addLabel("mouth"));
        QVERIFY(c.setBox(QRectF(20, 20, 30, 30)));
        c.seek(1000);
        QVERIFY(c.markAbsent());
        const QString out = dir_.filePath("export/dataset");
        const int n = c.exportDataset(QUrl::fromLocalFile(out).toString(), 500);
        QVERIFY(n >= 8);  // glans and mouth are reviewed over the clip
        QCOMPARE(QDir(out + "/images").entryList(QDir::Files).size(), n);
        QCOMPARE(QDir(out + "/labels").entryList(QDir::Files).size(), n);
        QFile classes(out + "/classes.txt");
        QVERIFY(classes.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(classes.readAll()), QString("glans\nmouth\n"));
        // Into the student's training set: replaced, not added to.
        QVERIFY(c.addToTrainingSet(500) == n);
        const QString set = dir_.filePath("config/ai/datasets/clip");
        QCOMPARE(QDir(set + "/images").entryList(QDir::Files).size(), n);
        QFile::copy(set + "/images/" + QDir(set + "/images").entryList(QDir::Files).value(0), set + "/images/stale.jpg");
        QVERIFY(c.addToTrainingSet(1000) > 0);
        QVERIFY(!QFile::exists(set + "/images/stale.jpg"));
        QVERIFY(c.status().contains("training set"));
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

    void theRulesSuggestPhases() {
        // 3 s black, then 12 s of a moving square: the start is "empty".
        const QString path = dir_.filePath("story.avi");
        {
            cv::VideoWriter w(path.toStdString(), cv::CAP_OPENCV_MJPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 10,
                              cv::Size(96, 64));
            QVERIFY(w.isOpened());
            for (int i = 0; i < 150; ++i) {
                cv::Mat f(64, 96, CV_8UC3, cv::Scalar(0, 0, 0));
                if (i >= 30) {
                    f.setTo(cv::Scalar(110, 110, 110));
                    cv::rectangle(f, cv::Rect((i * 7) % 70, 20, 24, 24), cv::Scalar(250, 250, 250), cv::FILLED);
                }
                w.write(f);
            }
        }
        LabelController c;
        QVERIFY(c.openVideo(path));
        c.setPhase(10000, 12000, "hand");  // the user's own stays
        QVERIFY(c.suggestPhases());
        QVERIFY(c.suggesting());
        QVERIFY(!c.suggestPhases());  // one run at a time
        QTRY_VERIFY_WITH_TIMEOUT(!c.suggesting(), 30000);
        QCOMPARE(c.suggestProgress(), 100);
        c.seek(1000);
        QCOMPARE(c.phaseAtPreview(), QString("empty"));
        const QVariantMap first = c.phases().value(0).toMap();
        QCOMPARE(first.value("source").toString(), QString("rules"));
        QVERIFY(!first.value("confirmed").toBool());
        c.seek(11000);
        QCOMPARE(c.phaseAtPreview(), QString("hand"));
        // Confirm, and undo takes the suggestions back in one step.
        QVERIFY(c.confirmPhases() > 0);
        QVERIFY(c.undo());
        QVERIFY(c.undo());
        c.seek(1000);
        QCOMPARE(c.phaseAtPreview(), QString());
        // A run can be canceled.
        QVERIFY(c.suggestPhases());
        c.cancelSuggest();
        QVERIFY(!c.suggesting());
    }

    void theTrackerFillsBetweenKeys() {
        // The moving disc (WriteMoving: x 160, y = 120 + 40 sin(2 pi t), r 35).
        const QString path = dir_.filePath("tracked.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        c.setRetrackAfterDrag(false);
        QVERIFY(c.openVideo(path));
        QVERIFY(c.addLabel("glans"));
        QVERIFY(!c.trackCurrent());  // one key is not enough
        const auto disc = [](double ms) { return QRectF(125, 120 + 40 * std::sin(2 * 3.14159265358979323846 * ms / 1000.0) - 35, 70, 70); };
        c.seek(0);
        QVERIFY(c.setBox(disc(0)));
        c.seek(1000);
        QVERIFY(c.setBox(disc(c.previewMs())));
        const int before = c.lanes()[0].toMap().value("keys").toList().size();
        QVERIFY(c.trackCurrent());
        QVERIFY(c.tracking());
        QTRY_VERIFY_WITH_TIMEOUT(!c.tracking(), 30000);
        const QVariantList keys = c.lanes()[0].toMap().value("keys").toList();
        QVERIFY2(keys.size() > before + 2, qPrintable(c.status()));
        int tracker = 0;
        for (const auto& k : keys)
            if (k.toMap().value("source").toString() == "tracker") {
                ++tracker;
                QVERIFY(!k.toMap().value("confirmed").toBool());
            }
        QVERIFY(tracker >= 3);
        // The box follows the disc halfway.
        c.seek(500);
        const QVariantMap box = c.boxes().value(0).toMap();
        QVERIFY(std::abs(box.value("y").toDouble() + 35 - (120 + 40 * std::sin(2 * 3.14159265358979323846 * c.previewMs() / 1000.0))) < 12);
        // One undo takes the tracked keys back.
        QVERIFY(c.undo());
        QCOMPARE(c.lanes()[0].toMap().value("keys").toList().size(), before);
        QVERIFY(!c.nextUncertain() || c.status().contains("Uncertain"));
    }

    // Owner 20 Oct: after a drag the stretches on both sides follow at once;
    // the switch turns it off and is remembered.
    void aDraggedBoxIsFollowedAtOnce() {
        const QString path = dir_.filePath("retrack.mp4");
        QVERIFY(QFile::copy(clip_, path));
        const auto disc = [](double ms) { return QRectF(125, 120 + 40 * std::sin(2 * 3.14159265358979323846 * ms / 1000.0) - 35, 70, 70); };
        LabelController c;
        c.setRetrackAfterDrag(true);
        QVERIFY(c.openVideo(path));
        QVERIFY(c.addLabel("glans"));
        c.seek(0);
        QVERIFY(c.setBox(disc(0)));
        QVERIFY(!c.tracking());  // nothing on either side yet
        c.seek(2000);
        QVERIFY(c.setBox(disc(c.previewMs())));
        QVERIFY(c.tracking());  // 0 to 2 s, on its own
        QTRY_VERIFY_WITH_TIMEOUT(!c.tracking(), 30000);
        const auto keyCount = [&] { return c.lanes()[0].toMap().value("keys").toList().size(); };
        const int tracked = keyCount();
        QVERIFY2(tracked > 4, qPrintable(c.status()));

        // A correction in the middle: both halves are tracked again, the
        // confirmed ends stay, one undo takes the new run back.
        c.seek(1000);
        QVERIFY(c.setBox(disc(c.previewMs())));
        QVERIFY(c.tracking());
        QTRY_VERIFY_WITH_TIMEOUT(!c.tracking(), 30000);
        int confirmed = 0;
        for (const auto& k : c.lanes()[0].toMap().value("keys").toList()) confirmed += k.toMap().value("confirmed").toBool();
        QCOMPARE(confirmed, 3);
        QVERIFY(c.undo());  // the run
        QVERIFY(c.undo());  // the box
        QCOMPARE(keyCount(), tracked);

        // A drag while a run goes replaces it; only the last run counts.
        c.seek(500);
        QVERIFY(c.setBox(disc(c.previewMs())));
        c.seek(1500);
        QVERIFY(c.setBox(disc(c.previewMs())));
        QTRY_VERIFY_WITH_TIMEOUT(!c.tracking(), 30000);

        // Undone while it tracks: the run's result does not come back.
        c.seek(2500);
        QVERIFY(c.setBox(disc(c.previewMs())));
        QVERIFY(c.tracking());
        const int beforeUndo = c.lanes()[0].toMap().value("keys").toList().size() - 1;
        QVERIFY(c.undo());
        QTRY_VERIFY_WITH_TIMEOUT(!c.tracking(), 30000);
        QCOMPARE(keyCount(), beforeUndo);

        c.setRetrackAfterDrag(false);
        c.seek(1200);
        QVERIFY(c.setBox(disc(c.previewMs())));
        QVERIFY(!c.tracking());
        LabelController again;
        QVERIFY(!again.retrackAfterDrag());  // remembered
        again.setRetrackAfterDrag(true);
    }

    void aTeacherOnAModelServerProposesBoxes() {
        FakeModelServer fake;
        const QString path = dir_.filePath("teacher.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        QVERIFY(!c.askTeacher());  // no model yet
        c.setTeacherUrl(fake.url());
        QCOMPARE(c.teacherUrl(), fake.url().chopped(1));  // without the trailing slash
        c.setTeacherKey("secret");
        QVERIFY(c.testTeacher());
        QVERIFY(c.teacherBusy());
        QTRY_VERIFY(!c.teacherBusy());
        QCOMPARE(c.teacherModels(), QStringList({"vision-a", "vision-b"}));
        QCOMPARE(c.teacherModel(), QString("vision-a"));  // the first one, when none was chosen
        QCOMPARE(fake.lastAuth, QByteArray("Bearer secret"));

        // The disc at frame 0 sits at 160 / 120 in a 320 x 240 picture.
        fake.answer = R"(Here you go: {"objects": [{"label": "glans", "box": [0.39, 0.354, 0.22, 0.292]}]})";
        c.seek(0);
        QVERIFY(c.askTeacher());
        QTRY_VERIFY(!c.teacherBusy());
        QVERIFY2(c.labels().contains("glans"), qPrintable(c.teacherStatus()));
        const QVariantMap key = c.lanes()[0].toMap().value("keys").toList().value(0).toMap();
        QCOMPARE(key.value("source").toString(), QString("teacher:vision-a"));
        QVERIFY(!key.value("confirmed").toBool());
        const QVariantMap box = c.boxes().value(0).toMap();
        QVERIFY(std::abs(box.value("x").toDouble() - 124.8) < 1);
        // The request asked for the classes and carried the frame.
        const QJsonObject req = QJsonDocument::fromJson(fake.lastBody).object();
        QCOMPARE(req.value("model").toString(), QString("vision-a"));
        const QString sent = QString::fromUtf8(fake.lastBody);
        QVERIFY(sent.contains("data:image/jpeg;base64,"));
        QVERIFY(sent.contains("glans"));
        QVERIFY(!sent.contains("exclude"));  // not asked of a teacher
        // Nothing seen: said, nothing changed; one undo takes the proposal back.
        fake.answer = R"({"objects": []})";
        QVERIFY(c.askTeacher());
        QTRY_VERIFY(!c.teacherBusy());
        QVERIFY(c.teacherStatus().contains("nothing"));
        QVERIFY(c.undo());
        QVERIFY(c.labels().isEmpty());  // the teacher had made the object
        // Remembered.
        LabelController again;
        QCOMPARE(again.teacherModel(), QString("vision-a"));
        QCOMPARE(again.teacherKey(), QString("secret"));
    }

    // NudeNet on this computer (D-155 step 4), with a tiny stand-in model of
    // NudeNet's shape (tests/vision/data/make_tiny_yolo.py): a face, and feet
    // that are no label of ours.
    void nudenetIsALocalTeacher() {
        const QString data = QStringLiteral(SAMN_VISION_TESTDATA);
        const QString path = dir_.filePath("nudenet.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        c.setTeacherKind("nudenet");
        QCOMPARE(c.teacherKind(), QString("nudenet"));
        if (!c.localTeacherAvailable()) {
            QVERIFY(!c.askTeacher());
            QVERIFY(c.teacherStatus().contains("ONNX Runtime"));
            return;
        }
        QVERIFY(!c.askTeacher());  // nothing fetched yet
        QVERIFY(c.teacherStatus().contains("Fetch NudeNet"));
        QVERIFY(!c.chooseNudeNet(data + "/tiny_yolo.onnx"));  // two classes: not NudeNet
        QVERIFY(c.teacherStatus().contains("not NudeNet"));
        QVERIFY(c.nudenetPath().isEmpty());
        QVERIFY(c.chooseNudeNet(QUrl::fromLocalFile(data + "/tiny_nudenet.onnx").toString()));
        QCOMPARE(c.nudenetPath(), data + "/tiny_nudenet.onnx");

        // 320 x 240 is padded to 320 x 320 and seen at 64: five pixels each.
        c.seek(0);
        QVERIFY(c.askTeacher());
        QVERIFY(c.teacherBusy());
        QTRY_VERIFY(!c.teacherBusy());
        QCOMPARE(c.labels(), QStringList{"face"});  // the feet have no label
        const QVariantMap key = c.lanes()[0].toMap().value("keys").toList().value(0).toMap();
        QCOMPARE(key.value("source").toString(), QString("teacher:nudenet"));
        QVERIFY(!key.value("confirmed").toBool());
        const QVariantMap box = c.boxes().value(0).toMap();
        QVERIFY(std::abs(box.value("x").toDouble() - 110) < 1);
        QVERIFY(std::abs(box.value("w").toDouble() - 100) < 1);
        // Asking for other classes only: the face is not proposed.
        QVERIFY(c.undo());
        QVERIFY(c.addLabel("hand"));
        QVERIFY(c.askTeacher());
        QTRY_VERIFY(!c.teacherBusy());
        QVERIFY(c.teacherStatus().contains("nothing"));

        LabelController again;  // remembered
        QCOMPARE(again.teacherKind(), QString("nudenet"));
        QCOMPARE(again.nudenetPath(), data + "/tiny_nudenet.onnx");
    }

    // Votes (D-155 step 4, Owner 20 Oct): what both teachers see is plain, what
    // only one of those that could see it found is kept as uncertain; the
    // same through the clip in one undo step.
    void teachersVoteOnAFrameAndThroughTheClip() {
        if (!samn::vision::OnnxDetector::Available()) return;  // one teacher only: nothing to vote
        FakeModelServer fake;
        const QString path = dir_.filePath("votes.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        c.setTeacherUrl(fake.url());
        c.setTeacherModel("vision-a");
        QVERIFY(c.chooseNudeNet(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_nudenet.onnx"));
        c.setTeacherKind("server");  // A asks one; the vote asks both

        // NudeNet's face is at 110, 110, 100 x 100 in the 320 x 240 picture.
        fake.answer = R"({"objects": [{"label": "face", "box": [0.35, 0.46, 0.31, 0.42]},
                                      {"label": "hand", "box": [0.8, 0.1, 0.1, 0.1]}]})";
        c.seek(0);
        QVERIFY(c.askAllTeachers());
        QVERIFY(c.teacherBusy());
        QTRY_VERIFY(!c.teacherBusy());
        QVERIFY2(c.labels().contains("face") && c.labels().contains("hand"), qPrintable(c.teacherStatus()));
        const auto keyOf = [&](const QString& label) {
            for (const QVariant& l : c.lanes())
                if (l.toMap().value("label").toString() == label) return l.toMap().value("keys").toList().value(0).toMap();
            return QVariantMap();
        };
        QVERIFY(keyOf("face").value("source").toString().startsWith("teachers:"));
        QVERIFY(!keyOf("face").value("uncertain").toBool());  // both saw it
        QVERIFY(!keyOf("hand").value("uncertain").toBool());  // NudeNet has no hands
        QVERIFY(c.teacherStatus().contains("0 uncertain"));

        // The server misses the face: NudeNet's face stays, uncertain.
        QVERIFY(c.undo());
        fake.answer = R"({"objects": []})";
        QVERIFY(c.askAllTeachers());
        QTRY_VERIFY(!c.teacherBusy());
        QCOMPARE(c.labels(), QStringList{"face"});
        QVERIFY(keyOf("face").value("uncertain").toBool());

        // Through the clip: one vote per second, one undo step.
        QVERIFY(c.undo());
        fake.answer = R"({"objects": [{"label": "face", "box": [0.35, 0.46, 0.31, 0.42]}]})";
        QVERIFY(c.teachClip(1000));
        QVERIFY(c.clipTeaching());
        QTRY_VERIFY_WITH_TIMEOUT(!c.clipTeaching(), 20000);
        QVERIFY2(keyOf("face").size() > 0, qPrintable(c.teacherStatus()));
        int keys = 0;
        for (const QVariant& l : c.lanes()) keys += l.toMap().value("keys").toList().size();
        QVERIFY2(keys >= 4, qPrintable(c.teacherStatus()));
        QVERIFY(c.teacherStatus().contains("frame"));
        QVERIFY(c.undo());
        QVERIFY(c.labels().isEmpty());

        // Stopped at once: said, and what was done is kept.
        QVERIFY(c.teachClip(500));
        c.cancelTeachClip();
        QTRY_VERIFY_WITH_TIMEOUT(!c.clipTeaching(), 20000);
        QVERIFY(c.teacherStatus().startsWith("Stopped"));

        // A server that does not answer sits out after its first failure;
        // NudeNet goes on alone through the clip.
        c.setTeacherUrl("http://127.0.0.1:1/v1");
        QElapsedTimer took;
        took.start();
        QVERIFY(c.teachClip(1000));
        QTRY_VERIFY_WITH_TIMEOUT(!c.clipTeaching(), 20000);
        QVERIFY(took.elapsed() < 15000);
        QVERIFY2(c.teacherStatus().contains("Not always answered"), qPrintable(c.teacherStatus()));
        QVERIFY(c.labels().contains("face"));
    }

    // Our student (D-155 step 5) is a teacher too: alone (A) and in the vote.
    // A tiny RF-DETR-shaped model stands in (tests/vision/data/tiny_detr.onnx):
    // its query 0 is class 1, which classes.json names "face".
    void theStudentIsATeacher() {
        if (!samn::vision::OnnxDetector::Available()) return;
        const QString models = dir_.filePath("config/models/student");
        QVERIFY(QDir().mkpath(models));
        QFile::remove(models + "/student.onnx");
        QVERIFY(QFile::copy(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_detr.onnx", models + "/student.onnx"));
        QFile json(models + "/classes.json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(R"({"classes": ["hand", "face"], "model": "nano"})");
        json.close();
        const QString path = dir_.filePath("student.mp4");
        QVERIFY(QFile::copy(clip_, path));
        LabelController c;
        QVERIFY(c.openVideo(path));
        QCOMPARE(c.studentClasses(), (QStringList{"hand", "face"}));
        QVERIFY(!c.studentPath().isEmpty());
        c.setTeacherKind("student");
        QCOMPARE(c.teacherKind(), QString("student"));
        c.seek(0);
        QVERIFY(c.askTeacher());
        QTRY_VERIFY(!c.teacherBusy());
        QVERIFY2(c.labels().contains("face"), qPrintable(c.teacherStatus()));
        const QVariantMap key = c.lanes()[0].toMap().value("keys").toList().value(0).toMap();
        QCOMPARE(key.value("source").toString(), QString("teacher:student"));
        const QVariantMap box = c.boxes().value(0).toMap();
        QVERIFY(std::abs(box.value("x").toDouble() - 48) < 1);  // (0.25 - 0.1) * 320
        QVERIFY(std::abs(box.value("h").toDouble() - 96) < 1);  // 0.4 * 240
        QVERIFY(c.undo());

        // The vote: NudeNet and the student, run one after the other; their
        // faces are in different places, so each is kept, uncertain.
        QVERIFY(c.chooseNudeNet(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_nudenet.onnx"));
        c.setTeacherUrl("");
        QVERIFY(c.askAllTeachers());
        QTRY_VERIFY(!c.teacherBusy());
        int faces = 0, unsure = 0;
        for (const QVariant& l : c.lanes()) {
            const QVariantMap k = l.toMap().value("keys").toList().value(0).toMap();
            faces += l.toMap().value("cls").toString() == "face";
            unsure += k.value("uncertain").toBool();
            QCOMPARE(k.value("source").toString(), QString("teachers:nudenet+student"));
        }
        QCOMPARE(faces, 2);
        QCOMPARE(unsure, 2);
        LabelController again;
        QCOMPARE(again.teacherKind(), QString("nudenet"));  // chooseNudeNet made NudeNet the teacher
        QFile::remove(models + "/student.onnx");
    }

    void nudenetDownloadChecksWhatItGets() {
        if (!samn::vision::OnnxDetector::Available()) return;  // nothing to fetch for
        QFile f(QStringLiteral(SAMN_VISION_TESTDATA) + "/tiny_nudenet.onnx");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray model = f.readAll();
        FakeFileServer server;
        server.body = testzip::Zip({{"nudenet/__init__.py", "", false}, {"nudenet/320n.onnx", model, true}});
        const auto sha = [](const QByteArray& b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); };
        samn::ui::ModelSource src{"NudeNet", server.url(), sha("not this"), "nudenet/320n.onnx", sha(model), "nudenet-test.onnx"};

        LabelController c;
        c.setTeacherKind("server");
        c.setNudeNetSource(src);
        QVERIFY(c.downloadNudeNet());
        QVERIFY(!c.downloadNudeNet());  // one at a time
        QVERIFY(c.modelDownload() >= 0);
        QTRY_COMPARE(c.modelDownload(), -1);
        QVERIFY2(c.teacherStatus().contains("cannot be used"), qPrintable(c.teacherStatus()));
        QVERIFY(!QFile::exists(dir_.filePath("config/models/nudenet-test.onnx")));

        src.archive_sha256 = sha(server.body);
        c.setNudeNetSource(src);
        QVERIFY(c.downloadNudeNet());
        QTRY_COMPARE(c.modelDownload(), -1);
        QVERIFY2(c.teacherStatus().contains("ready"), qPrintable(c.teacherStatus()));
        QCOMPARE(QFileInfo(c.nudenetPath()).fileName(), QString("nudenet-test.onnx"));
        QVERIFY(QFile::exists(dir_.filePath("config/models/nudenet-test.onnx")));
        QCOMPARE(c.teacherKind(), QString("nudenet"));  // the new model is the teacher
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
