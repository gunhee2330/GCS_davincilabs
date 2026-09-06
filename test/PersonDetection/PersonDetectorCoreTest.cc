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
    // Box rows are in input pixels; the helper takes fractions of the input for readability
    auto set = [&](int anchor, int classId, float cx, float cy, float w, float h, float score) {
        out[0 * kNumAnchors + anchor] = cx * kInputSize;
        out[1 * kNumAnchors + anchor] = cy * kInputSize;
        out[2 * kNumAnchors + anchor] = w * kInputSize;
        out[3 * kNumAnchors + anchor] = h * kInputSize;
        out[(4 + classId) * kNumAnchors + anchor] = score;
    };
    set(8, 0, 0.5f, 0.5f, 0.25f, 0.5f, 0.9f);   // keep
    set(7, 0, 0.52f, 0.5f, 0.25f, 0.5f, 0.8f);  // overlaps 8, iterated first -> only the sort keeps 8
    set(9, 0, 0.1f, 0.5f, 0.1f, 0.3f, 0.3f);    // below conf
    set(11, 2, 0.8f, 0.5f, 0.1f, 0.1f, 0.95f);  // car
    set(12, 7, 0.2f, 0.3f, 0.1f, 0.1f, 0.9f);   // truck
    // Two weak vehicle votes on one anchor: the group scores as the best of them, so it stays below
    // the threshold. Summing the group instead would let it through.
    set(13, 2, 0.6f, 0.7f, 0.1f, 0.1f, 0.25f);
    set(13, 7, 0.6f, 0.7f, 0.1f, 0.1f, 0.25f);

    // Thresholds are passed rather than defaulted: this test is about the decode, not about
    // whichever confidence the detector happens to run at.
    constexpr float conf = 0.4f;
    const QList<QRectF> persons = decodeBoxes(out.data(), lb, sourceSize, kPersonClasses, conf);
    QCOMPARE(persons.size(), 1);
    // input px: cx 160, cy 160, w 80, h 160 -> x0 120, y0 80; undo pad/scale -> (240, 20, 160, 320) px
    QVERIFY(qAbs(persons[0].x() - 240.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].y() - 20.0 / 360) < 1e-3);
    QVERIFY(qAbs(persons[0].width() - 160.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].height() - 320.0 / 360) < 1e-3);

    // Same anchors, the vehicle class group: car and truck, no person.
    const QList<QRectF> vehicles = decodeBoxes(out.data(), lb, sourceSize, kVehicleClasses, conf);
    QCOMPARE(vehicles.size(), 2);
    // car, input px: cx 256, cy 160, w 32, h 32 -> undo pad/scale -> (480, 148, 64, 64) px
    QVERIFY(qAbs(vehicles[0].x() - 480.0 / 640) < 1e-3);
    QVERIFY(qAbs(vehicles[0].y() - 148.0 / 360) < 1e-3);
    QVERIFY(qAbs(vehicles[0].width() - 64.0 / 640) < 1e-3);
    QVERIFY(qAbs(vehicles[0].height() - 64.0 / 360) < 1e-3);
}

/// The sweep runs the same person past several tiles, and their overlap is what says they are
/// one person; a neighbour standing close by is not.
void PersonDetectorCoreTest::_testMergeDropsTheSamePersonSeenTwice()
{
    const QRectF person(0.40, 0.40, 0.06, 0.14);
    const QRectF sameAgain(0.405, 0.405, 0.06, 0.14);  // the tile next door, a pixel off
    const QRectF neighbour(0.47, 0.40, 0.06, 0.14);    // shoulder to shoulder, still someone else

    const QList<QRectF> merged = PersonDetectorCore::mergeBoxes({person, sameAgain, neighbour});

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.first(), person);  // the earlier list wins, so the whole frame pass does
    QCOMPARE(merged.last(), neighbour);
    QVERIFY(PersonDetectorCore::mergeBoxes({person, QRectF()}).size() == 1);
}
