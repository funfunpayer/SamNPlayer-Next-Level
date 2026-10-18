// EditorController (D-137, D-144): editing a script's curves with the mouse
// and the range tools, undo, and the save after every finished change (0.7).
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
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
