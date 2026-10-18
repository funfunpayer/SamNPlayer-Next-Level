// EditorController (D-137, D-144): editing a script's curves with the mouse
// and the range tools, undo, and the save after every finished change (0.7).
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <fstream>
#include <sstream>

#include "curve_item.h"
#include "editor_controller.h"
#include "samn/funscript.h"
#include "samn/samn_file.h"

using samn::Action;
using samn::ui::CurveItem;
using samn::ui::EditorController;

namespace {

std::filesystem::path Fs(const QString& p) { return std::filesystem::path(p.toStdU16String()); }

std::string ReadText(const QString& p) {
    std::ifstream in(Fs(p), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<Action> Actions(const QString& p) { return samn::Parse(ReadText(p)).actions; }

// A 400 x 100 lane over 0..4000 ms: x = ms / 10, y = 6 + (1 - pos / 100) * 88.
double X(double ms) { return ms / 10.0; }
double Y(double pos) { return 6 + (1 - pos / 100.0) * 88; }

// 16-bit mono WAV, 4 s: a 440 Hz tone for 2 s, then silence.
QString WriteToneThenSilence(const QString& path) {
    const int rate = 8000;
    QByteArray pcm;
    for (int i = 0; i < 4 * rate; ++i) {
        const qint16 v = i < 2 * rate ? qint16(16000 * std::sin(2 * 3.14159265358979 * 440 * i / rate)) : 0;
        pcm.append(reinterpret_cast<const char*>(&v), 2);
    }
    QByteArray h;
    auto u32 = [&h](quint32 v) { h.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&h](quint16 v) { h.append(reinterpret_cast<const char*>(&v), 2); };
    h.append("RIFF"); u32(36 + pcm.size()); h.append("WAVEfmt "); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); h.append("data"); u32(pcm.size());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(h + pcm);
    return path;
}

int LaneAt(const std::vector<Action>& lane, int64_t ms) {
    Action best = lane.front();
    for (const Action& p : lane)
        if (p.at <= ms) best = p;
    return best.pos;
}

}  // namespace

class EditorControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    QString script(const char* name) {
        const QString p = dir_.filePath(QString::fromLatin1(name));
        std::ofstream(Fs(p)) << R"({"version":"1.0","inverted":false,"range":100,)"
                                R"("actions":[{"at":0,"pos":10},{"at":1000,"pos":90},{"at":2000,"pos":10},)"
                                R"({"at":3000,"pos":90},{"at":4000,"pos":10}],)"
                                R"("metadata":{"creator":"SamNPlayer","native_pipeline":{"backend":"csrt"}}})";
        return p;
    }

    static void lane(CurveItem& c, const char* name) {
        c.setWidth(400);
        c.setHeight(100);
        c.setProperty("lane", QString::fromLatin1(name));
    }

private slots:
    void dragAddDeleteAndSaveAtOnce() {
        const QString path = script("a.funscript");
        EditorController e;
        QVERIFY(!e.open(dir_.filePath("missing.funscript")));
        QVERIFY(e.status().contains("Cannot open"));
        QVERIFY(e.open(QUrl::fromLocalFile(path).toString()));
        QCOMPARE(e.name(), QStringLiteral("a.funscript"));
        QCOMPARE(e.lanes(), QStringList{"stroke"});
        QCOMPARE(e.pointCount(), 5);
        e.setDurationMs(4000);
        CurveItem c;
        lane(c, "stroke");
        e.attachCurve(&c);
        c.setViewStartMs(e.viewStartMs());
        c.setViewEndMs(e.viewEndMs());

        // Grab the point at 1 s and drag it: the video follows (returned time).
        QCOMPARE(e.press(&c, X(1000), Y(90)), 1000.0);
        QVERIFY(e.dragging());
        QCOMPARE(c.activeIndex(), 1);
        QCOMPARE(e.dragTo(X(1200), Y(50)), 1200.0);
        e.release();
        QVERIFY(!e.dragging());
        QCOMPARE(c.activeIndex(), -1);
        auto saved = Actions(path);
        QCOMPARE(saved[1].at, 1200);
        QCOMPARE(saved[1].pos, 50);
        QVERIFY(e.canUndo());
        QCOMPARE(e.undoText(), QStringLiteral("Undo: move point"));
        QVERIFY(!e.dirty());
        // The rest of the file is kept.
        QVERIFY(ReadText(path).find("\"backend\": \"csrt\"") != std::string::npos);

        // A click on a point that moves nothing records nothing.
        e.press(&c, X(1200), Y(50));
        e.release();
        QCOMPARE(int(e.session().UndoCount()), 1);

        // Click on empty space: a new point there, ready to drag.
        QCOMPARE(e.press(&c, X(2500), Y(30)), 2500.0);
        e.dragTo(X(2600), Y(40));
        e.release();
        saved = Actions(path);
        QCOMPARE(int(saved.size()), 6);
        QCOMPARE(saved[3].at, 2600);
        QCOMPARE(saved[3].pos, 40);

        // Double-click removes; at least 2 points stay.
        QVERIFY(e.deleteAt(&c, X(2600), Y(40)));
        QCOMPARE(int(Actions(path).size()), 5);
        QVERIFY(!e.deleteAt(&c, X(3500), Y(50)));  // nothing there
        while (e.pointCount() > 2) {
            const auto& a = e.session().Axis("stroke");
            QVERIFY(e.deleteAt(&c, X(double(a[1].at)), Y(a[1].pos)));
        }
        const auto& two = e.session().Axis("stroke");
        QVERIFY(!e.deleteAt(&c, X(double(two[0].at)), Y(two[0].pos)));
        QVERIFY(e.status().contains("2 points"));

        // Undo everything: the file is the original again.
        while (e.undo()) {
        }
        saved = Actions(path);
        QCOMPARE(int(saved.size()), 5);
        QCOMPARE(saved[1].at, 1000);
        QCOMPARE(saved[1].pos, 90);
        QVERIFY(e.redo());
        QCOMPARE(Actions(path)[1].at, 1200);
    }

    void rangeToolsWorkInsideTheMarker() {
        const QString path = script("b.funscript");
        EditorController e;
        QVERIFY(e.open(path));
        e.setDurationMs(4000);
        QSignalSpy history(&e, &EditorController::historyChanged);

        // Without a marker the range tools ask for one; invert takes all.
        QVERIFY(!e.deleteRange());
        QVERIFY(e.status().contains("Mark a range"));
        QVERIFY(e.invert());
        QCOMPARE(Actions(path)[0].pos, 90);
        QCOMPARE(Actions(path)[4].pos, 90);
        QVERIFY(e.undo());

        e.setMarker(2100, 900);  // swapped on purpose
        QVERIFY(e.hasMarker());
        QCOMPARE(e.markerStartMs(), 900.0);
        QVERIFY(e.invert());
        auto a = Actions(path);
        QCOMPARE(a[0].pos, 10);
        QCOMPARE(a[1].pos, 10);  // 90 inverted
        QCOMPARE(a[2].pos, 90);  // 10 inverted
        QCOMPARE(a[3].pos, 90);  // outside

        QVERIFY(e.scaleRange(0.5, false));
        a = Actions(path);
        QCOMPARE(a[1].pos, 30);  // 10 -> 50 + (10 - 50) * 0.5
        QCOMPARE(a[2].pos, 70);

        QVERIFY(e.deleteRange());
        QCOMPARE(int(Actions(path).size()), 3);
        QVERIFY(e.undo());

        e.setMarker(0, 4000);
        QVERIFY(e.capSpeed(20));  // 80 positions in 1 s: 500 * 80 / 1000 = 40 > 20, stretched
        QVERIFY(Actions(path).back().at > 4000);
        QVERIFY(e.undo());

        e.clearMarker();
        QVERIFY(!e.hasMarker());
        QVERIFY(history.count() >= 8);
        QCOMPARE(e.undoText(), QStringLiteral("Undo: Scale range"));
    }

    void simplifyKeepsTheShape() {
        const QString path = dir_.filePath("dense.funscript");
        {
            std::string acts;
            for (int i = 0; i <= 100; ++i) acts += (i ? "," : "") + std::string("{\"at\":") + std::to_string(i * 40) + ",\"pos\":" + "50}";
            std::ofstream(Fs(path)) << "{\"actions\":[" << acts << "]}";
        }
        EditorController e;
        QVERIFY(e.open(path));
        e.setMarker(0, 4000);
        QVERIFY(e.simplifyRange(1));
        QCOMPARE(int(Actions(path).size()), 2);  // a flat line needs only its ends
        QVERIFY(!e.simplifyRange(1));             // nothing left to change
        QVERIFY(e.status().contains("nothing to change"));
    }

    void framesSnapNewPoints() {
        const QString path = script("c.funscript");
        EditorController e;
        QVERIFY(e.open(path));
        e.setDurationMs(4000);
        e.setFpsSnap(25);
        CurveItem c;
        lane(c, "stroke");
        e.attachCurve(&c);
        c.setViewStartMs(0);
        c.setViewEndMs(4000);
        QCOMPARE(e.press(&c, X(2513), Y(50)), 2520.0);  // 25 fps: 40 ms frames
        e.release();
    }

    void samnLanesAreEditedAndSaved() {
        const QString path = script("d.funscript");
        const QString samnPath = dir_.filePath("d.samn");
        {
            samn::Document d;
            d.version = samn::SamnCurrentVersion;
            d.kind = samn::SamnKind;
            d.general = Actions(path);
            d.vibration = {{0, 20}, {2000, 80}, {4000, 20}};
            try {
                samn::SaveDocument(Fs(samnPath), d);
            } catch (const std::exception& ex) {
                QFAIL(ex.what());
            }
        }
        EditorController e;
        QVERIFY(e.open(path));
        QCOMPARE(e.lanes(), (QStringList{"stroke", "vibration"}));
        e.setDurationMs(4000);
        CurveItem vib;
        lane(vib, "vibration");
        e.attachCurve(&vib);
        vib.setViewStartMs(0);
        vib.setViewEndMs(4000);
        QCOMPARE(e.press(&vib, X(2000), Y(80)), 2000.0);
        QCOMPARE(e.activeLane(), QStringLiteral("vibration"));
        e.dragTo(X(2000), Y(60));
        e.release();
        const samn::Document back = samn::LoadDocument(Fs(samnPath));
        QCOMPARE(int(back.vibration.size()), 3);
        QCOMPARE(back.vibration[1].pos, 60);
        QCOMPARE(Actions(path)[1].pos, 90);  // the stroke is untouched
        QCOMPARE(int(back.general.size()), 5);
    }

    void aFailedSaveKeepsTheEditAndSaysSo() {
        const QString path = script("e.funscript");
        EditorController e;
        QVERIFY(e.open(path));
        QVERIFY(QFile::remove(path));
        QVERIFY(e.invert());
        QVERIFY(e.status().startsWith("Not saved"));
        QVERIFY(e.dirty());
        QCOMPARE(e.session().Axis("stroke")[0].pos, 90);
    }

    void axesFromTheStrokeMakeTheSamnAndUndoBack() {
        const QString path = script("g.funscript");
        const QString samnPath = dir_.filePath("g.samn");
        EditorController e;
        QVERIFY(e.open(path));
        QCOMPARE(e.lanes(), QStringList{"stroke"});
        QCOMPARE(e.playPath(), path);
        QSignalSpy lanes(&e, &EditorController::lanesChanged);
        QVERIFY(e.axesFromStroke());
        QCOMPARE(e.lanes(), (QStringList{"stroke", "vibration", "suction"}));
        QCOMPARE(lanes.count(), 1);
        QCOMPARE(e.playPath(), samnPath);  // the Player now gets the .samn
        QCOMPARE(e.undoText(), QStringLiteral("Undo: vibration and suction from the stroke"));
        samn::Document doc = samn::LoadDocument(Fs(samnPath));
        QVERIFY(!doc.vibration.empty());
        QVERIFY(!doc.suction.empty());
        QCOMPARE(int(doc.general.size()), 5);
        QCOMPARE(QString::fromStdString(doc.playback_source), QStringLiteral("axes"));
        // Same stroke again: nothing to change.
        QVERIFY(!e.axesFromStroke());

        // One undo removes both lanes; the recipe plays again.
        QVERIFY(e.undo());
        QCOMPARE(e.lanes(), QStringList{"stroke"});
        doc = samn::LoadDocument(Fs(samnPath));
        QVERIFY(doc.vibration.empty());
        QVERIFY(doc.suction.empty());
        QCOMPARE(QString::fromStdString(doc.playback_source), QStringLiteral("recipe"));
    }

    void vibrationFollowsTheSound() {
        const QString path = script("h.funscript");
        EditorController e;
        QVERIFY(e.open(path));
        QVERIFY(!e.vibrationFromSound(false));
        QVERIFY(e.status().contains("No video"));
        e.setVideoPath(WriteToneThenSilence(dir_.filePath("h.wav")));
        QVERIFY(e.vibrationFromSound(false));
        QVERIFY(e.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!e.busy(), 20000);
        QVERIFY2(e.status().startsWith("Vibration from the sound written"), qPrintable(e.status()));
        const auto& vib = e.session().Axis("vibration");
        QVERIFY(!vib.empty());
        QVERIFY(LaneAt(vib, 500) >= 80);   // the tone
        QVERIFY(LaneAt(vib, 3000) <= 10);  // silence
        // Suction was written first, so the script plays as before.
        QVERIFY(!e.session().Axis("suction").empty());
        QCOMPARE(e.undoText(), QStringLiteral("Undo: vibration from the sound"));
        const samn::Document doc = samn::LoadDocument(Fs(dir_.filePath("h.samn")));
        QCOMPARE(doc.vibration.size(), vib.size());

        // With contact: the stroke's own vibration where the sound is silent.
        QVERIFY(e.vibrationFromSound(true));
        QTRY_VERIFY_WITH_TIMEOUT(!e.busy(), 20000);
        QVERIFY(LaneAt(e.session().Axis("vibration"), 3000) > 10);
        QVERIFY(e.undo());
        QVERIFY(LaneAt(e.session().Axis("vibration"), 3000) <= 10);
    }

    void zoomFollowsTheMarker() {
        EditorController e;
        QVERIFY(e.open(script("f.funscript")));
        e.setDurationMs(4000);
        QVERIFY(!e.zoomed());
        e.setMarker(1000, 2000);
        e.zoomToMarker();
        QVERIFY(e.zoomed());
        QVERIFY(e.viewStartMs() < 1000 && e.viewEndMs() > 2000);
        e.showAll();
        QVERIFY(!e.zoomed());
        e.zoomAt(2, 0.5);
        QVERIFY(e.zoomed());
    }
};

QTEST_MAIN(EditorControllerTest)
#include "editor_controller_test.moc"
