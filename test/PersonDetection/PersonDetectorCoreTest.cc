#include "PersonDetectorCoreTest.h"

#include <QtGui/QColor>

#include <vector>

#include "PersonDetectorCore.h"

using namespace PersonDetectorCore;

UT_REGISTER_TEST_LIGHTWEIGHT(PersonDetectorCoreTest, TestLabel::Unit)

void PersonDetectorCoreTest::_testLetterboxKeepsAspect()
{
    QImage source(640, 360, QImage::Format_RGB32);
    source.fill(Qt::red);
    const Letterbox lb = letterbox(source);
    QCOMPARE(lb.image.size(), QSize(kInputSize, kInputSize));
    QCOMPARE(lb.image.format(), QImage::Format_RGB888);
    QCOMPARE(lb.scale, 0.5f);
    QCOMPARE(lb.padX, 0);
    QCOMPARE(lb.padY, 70);
    QCOMPARE(lb.image.pixelColor(160, 160), QColor(Qt::red));       // inside content
    QCOMPARE(lb.image.pixelColor(160, 10), QColor(114, 114, 114));  // top pad
}

void PersonDetectorCoreTest::_testDecodeUndoesLetterboxAndSuppressesOverlap()
{
    const QSize sourceSize(640, 360);
    const Letterbox lb = letterbox(QImage(sourceSize, QImage::Format_RGB888));
    std::vector<float> out((4 + kNumClasses) * kNumAnchors, 0.f);
    auto set = [&](int anchor, float cx, float cy, float w, float h, float score) {
        out[0 * kNumAnchors + anchor] = cx;
        out[1 * kNumAnchors + anchor] = cy;
        out[2 * kNumAnchors + anchor] = w;
        out[3 * kNumAnchors + anchor] = h;
        out[(4 + kPersonClass) * kNumAnchors + anchor] = score;
    };
    set(8, 0.5f, 0.5f, 0.25f, 0.5f, 0.9f);    // keep
    set(7, 0.52f, 0.5f, 0.25f, 0.5f, 0.8f);   // overlaps 8, iterated first -> only the sort keeps 8
    set(9, 0.1f, 0.5f, 0.1f, 0.3f, 0.3f);     // below conf
    out[(4 + 2) * kNumAnchors + 11] = 0.95f;  // class 2 (car) -> ignored
    out[0 * kNumAnchors + 11] = 0.8f;
    out[1 * kNumAnchors + 11] = 0.5f;
    out[2 * kNumAnchors + 11] = 0.1f;
    out[3 * kNumAnchors + 11] = 0.1f;

    const QList<QRectF> boxes = decodePersons(out.data(), lb, sourceSize);
    QCOMPARE(boxes.size(), 1);
    // input px: cx 160, cy 160, w 80, h 160 -> x0 120, y0 80; undo pad/scale -> (240, 20, 160, 320) px
    QVERIFY(qAbs(boxes[0].x() - 240.0 / 640) < 1e-3);
    QVERIFY(qAbs(boxes[0].y() - 20.0 / 360) < 1e-3);
    QVERIFY(qAbs(boxes[0].width() - 160.0 / 640) < 1e-3);
    QVERIFY(qAbs(boxes[0].height() - 320.0 / 360) < 1e-3);
}
