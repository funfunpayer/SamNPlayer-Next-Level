#include <QImage>
#include <QtTest>

#include "icon_provider.h"

using samn::ui::IconProvider;

// The icon SVGs are compiled into samn_app; this links it.
class IconProviderTest : public QObject {
    Q_OBJECT
private slots:
    void colorsAndRenders() {
        const QByteArray svg = IconProvider::svgFor("play", QColor("#00d9f4"));
        QVERIFY(svg.contains("#00d9f4"));
        QVERIFY(!svg.contains("currentColor"));
        IconProvider p;
        QSize size;
        const QImage img = p.requestImage("play?#ff8a23", &size, QSize(48, 48));
        QCOMPARE(size, QSize(48, 48));
        bool painted = false;
        for (int y = 0; y < img.height() && !painted; ++y)
            for (int x = 0; x < img.width() && !painted; ++x) painted = qAlpha(img.pixel(x, y)) > 0;
        QVERIFY(painted);
    }
    void unknownOrUnsafeNamesGiveNothing() {
        QVERIFY(IconProvider::svgFor("nope", Qt::white).isEmpty());
        QVERIFY(IconProvider::svgFor("../main", Qt::white).isEmpty());
        IconProvider p;
        const QImage img = p.requestImage("nope", nullptr, QSize(16, 16));
        QCOMPARE(qAlpha(img.pixel(8, 8)), 0);
    }
};

QTEST_MAIN(IconProviderTest)
#include "icon_provider_test.moc"
