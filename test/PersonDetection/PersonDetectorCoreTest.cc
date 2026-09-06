#include "PersonDetectorCoreTest.h"

#include <QtCore/QRect>
#include <QtGui/QColor>

#include <array>
#include <optional>
#include <vector>

#include "PersonDetectorCore.h"

using namespace PersonDetectorCore;

UT_REGISTER_TEST_LIGHTWEIGHT(PersonDetectorCoreTest, TestLabel::Unit)

namespace {

/// The bundled model's shape and mapping. The core reads these from the model and its descriptor
/// at load time, so the test states the ones it decodes against rather than importing them.
constexpr int kInputSize = 320;
constexpr int kNumClasses = 80;
constexpr int kNumAnchors = 2100;  ///< 40*40 + 20*20 + 10*10 for a 320 input

constexpr std::array<int, 1> kPersonClasses{0};         ///< COCO "person"
constexpr std::array<int, 3> kVehicleClasses{2, 5, 7};  ///< COCO "car", "bus", "truck"

}  // namespace

void PersonDetectorCoreTest::_testLetterboxKeepsAspect()
{
    QImage source(640, 360, QImage::Format_RGB32);
    source.fill(Qt::red);
    const Letterbox lb = letterbox(source, kInputSize);
    QCOMPARE(lb.image.size(), QSize(kInputSize, kInputSize));
    QCOMPARE(lb.image.format(), QImage::Format_RGB888);
    QCOMPARE(lb.scale, 0.5f);
    QCOMPARE(lb.padX, 0);
    QCOMPARE(lb.padY, 70);
    QCOMPARE(lb.image.pixelColor(160, 160), QColor(Qt::red));       // inside content
    QCOMPARE(lb.image.pixelColor(160, 10), QColor(114, 114, 114));  // top pad
}

/// The recipe is the one thing about a model that fails quietly: the wrong channel order, plane
/// order or scaling returns thousands of confident boxes over noise rather than an error, so the
/// floats a known pixel becomes are pinned here for both.
void PersonDetectorCoreTest::_testFillInputMatchesRecipe()
{
    constexpr int side = 2;
    constexpr size_t plane = side * side;
    Letterbox lb;
    lb.image = QImage(side, side, QImage::Format_RGB888);
    lb.image.fill(QColor(114, 114, 114));
    lb.image.setPixelColor(1, 0, QColor(200, 100, 50));  // pixel 1
    lb.image.setPixelColor(0, 1, QColor(30, 60, 90));    // pixel 2, first of the row below

    // An Ultralytics export: RGB in plane order, 0..1.
    std::vector<float> input(3 * plane, -1.f);
    fillInput(lb, Preprocess::Rgb01, input);
    QCOMPARE(input[(0 * plane) + 1], 200.f / 255);
    QCOMPARE(input[(1 * plane) + 1], 100.f / 255);
    QCOMPARE(input[(2 * plane) + 1], 50.f / 255);
    QCOMPARE(input[(0 * plane) + 2], 30.f / 255);
    QCOMPARE(input[(1 * plane) + 2], 60.f / 255);
    QCOMPARE(input[(2 * plane) + 2], 90.f / 255);

    // The same pixels for mmdeploy: blue leads, and each plane has its own mean and std.
    fillInput(lb, Preprocess::BgrMeanStd, input);
    QCOMPARE(input[(0 * plane) + 1], (50.f - 103.53f) / 57.375f);
    QCOMPARE(input[(1 * plane) + 1], (100.f - 116.28f) / 57.12f);
    QCOMPARE(input[(2 * plane) + 1], (200.f - 123.675f) / 58.395f);
    QCOMPARE(input[(0 * plane) + 2], (90.f - 103.53f) / 57.375f);
    QCOMPARE(input[(1 * plane) + 2], (60.f - 116.28f) / 57.12f);
    QCOMPARE(input[(2 * plane) + 2], (30.f - 123.675f) / 58.395f);

    // A buffer that is not the model's own input is left alone rather than filled halfway.
    std::vector<float> wrongSize((3 * plane) - 1, -1.f);
    fillInput(lb, Preprocess::BgrMeanStd, wrongSize);
    QCOMPARE(wrongSize.front(), -1.f);
}

void PersonDetectorCoreTest::_testDecodeUndoesLetterboxAndSuppressesOverlap()
{
    const QSize sourceSize(640, 360);
    const Letterbox lb = letterbox(QImage(sourceSize, QImage::Format_RGB888), kInputSize);
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
    const QList<QRectF> persons = decodeBoxes(out.data(), kNumAnchors, lb, sourceSize, kPersonClasses, conf);
    QCOMPARE(persons.size(), 1);
    // input px: cx 160, cy 160, w 80, h 160 -> x0 120, y0 80; undo pad/scale -> (240, 20, 160, 320) px
    QVERIFY(qAbs(persons[0].x() - 240.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].y() - 20.0 / 360) < 1e-3);
    QVERIFY(qAbs(persons[0].width() - 160.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].height() - 320.0 / 360) < 1e-3);

    // Same anchors, the vehicle class group: car and truck, no person.
    const QList<QRectF> vehicles = decodeBoxes(out.data(), kNumAnchors, lb, sourceSize, kVehicleClasses, conf);
    QCOMPARE(vehicles.size(), 2);
    // car, input px: cx 256, cy 160, w 32, h 32 -> undo pad/scale -> (480, 148, 64, 64) px
    QVERIFY(qAbs(vehicles[0].x() - 480.0 / 640) < 1e-3);
    QVERIFY(qAbs(vehicles[0].y() - 148.0 / 360) < 1e-3);
    QVERIFY(qAbs(vehicles[0].width() - 64.0 / 640) < 1e-3);
    QVERIFY(qAbs(vehicles[0].height() - 64.0 / 360) < 1e-3);
}

/// The model has already scored, suppressed and sorted these, so the decode is the mapping and the
/// letterbox: rows below the threshold and labels the descriptor does not name are not ours to count.
void PersonDetectorCoreTest::_testDecodeDetsLabelsMapsLabelsAndUndoesLetterbox()
{
    const QSize sourceSize(640, 360);
    const Letterbox lb = letterbox(QImage(sourceSize, QImage::Format_RGB888), kInputSize);
    // x1, y1, x2, y2 in letterboxed input pixels, then the score. Rows as the model emits them.
    const std::vector<float> dets{
        120.f, 80.f, 200.f, 240.f, 0.90f,   // person, the box the letterbox has to be undone from
        20.f,  90.f, 60.f,  130.f, 0.20f,   // person, but below the threshold
        260.f, 90.f, 300.f, 130.f, 0.80f,   // a class this descriptor does not name
        0.f,   0.f,  0.f,   0.f,   0.f,     // padding row, as a fixed-size export emits
    };
    const std::vector<int64_t> labels{0, 0, 4, 0};
    constexpr std::array<int, 1> personOnly{0};

    const QList<QRectF> persons =
        decodeDetsLabels(dets, labels, lb, sourceSize, personOnly, 0.4f);
    QCOMPARE(persons.size(), 1);
    // input px x0 120, y0 80, x1 200, y1 240; pad 70 rows, scale 0.5 -> (240, 20, 160, 320) px
    QVERIFY(qAbs(persons[0].x() - 240.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].y() - 20.0 / 360) < 1e-3);
    QVERIFY(qAbs(persons[0].width() - 160.0 / 640) < 1e-3);
    QVERIFY(qAbs(persons[0].height() - 320.0 / 360) < 1e-3);

    // Class 4 is the one the person list skipped, and a list naming it picks up that box alone.
    constexpr std::array<int, 1> classFour{4};
    const QList<QRectF> others = decodeDetsLabels(dets, labels, lb, sourceSize, classFour, 0.4f);
    QCOMPARE(others.size(), 1);
    QVERIFY(qAbs(others[0].x() - 520.0 / 640) < 1e-3);  // (260 - 0 pad) / 0.5

    // A model with no vehicle class at all: nothing to map, so nothing comes back.
    QVERIFY(decodeDetsLabels(dets, labels, lb, sourceSize, {}, 0.4f).isEmpty());
    // A dets tensor that does not hold one row per label is not decoded halfway.
    QVERIFY(decodeDetsLabels(std::span(dets).first(10), labels, lb, sourceSize, personOnly, 0.4f).isEmpty());
}

/// Class ids belong to the model, not to COCO: a descriptor written for a model trained on its own
/// classes has to decode those, and one written for a different model has to be refused.
void PersonDetectorCoreTest::_testDescriptorMapsClassIds()
{
    QString error;
    const std::optional<ModelDescriptor> coco = parseModelDescriptor(
        R"({"name": "yolov8n-320-coco", "layout": "yolo", "preprocess": "rgb01", )"
        R"("person": [0], "vehicle": [2, 5, 7], "conf": 0.3})", &error);
    QVERIFY2(coco.has_value(), qPrintable(error));
    QVERIFY(classIdsWithin(*coco, kNumClasses));
    QCOMPARE(coco->name, QStringLiteral("yolov8n-320-coco"));
    QCOMPARE(coco->personClasses, QList<int>({0}));
    QCOMPARE(coco->vehicleClasses, QList<int>({2, 5, 7}));
    QCOMPARE(coco->confThreshold, 0.3f);

    // Four classes of its own, none of them COCO's numbering.
    constexpr int aerialClasses = 4;
    const std::optional<ModelDescriptor> aerial = parseModelDescriptor(
        R"({"name": "aerial", "layout": "yolo", "preprocess": "rgb01", )"
        R"("person": [2], "vehicle": [0, 3], "conf": 0.25})", &error);
    QVERIFY2(aerial.has_value(), qPrintable(error));
    QVERIFY(classIdsWithin(*aerial, aerialClasses));
    QCOMPARE(aerial->personClasses, QList<int>({2}));
    QCOMPARE(aerial->vehicleClasses, QList<int>({0, 3}));
    QCOMPARE(aerial->confThreshold, 0.25f);

    // Those ids reach the decode: on this model class 2 is the person and class 0 is a vehicle,
    // so a hardcoded COCO mapping would swap the two.
    constexpr int anchors = 6;
    const QSize sourceSize(kInputSize, kInputSize);  // square, so a box decodes to the fractions set here
    const Letterbox lb = letterbox(QImage(sourceSize, QImage::Format_RGB888), kInputSize);
    std::vector<float> out((4 + aerialClasses) * anchors, 0.f);
    auto set = [&](int anchor, int classId, float cx, float cy, float w, float h, float score) {
        out[0 * anchors + anchor] = cx * kInputSize;
        out[1 * anchors + anchor] = cy * kInputSize;
        out[2 * anchors + anchor] = w * kInputSize;
        out[3 * anchors + anchor] = h * kInputSize;
        out[(4 + classId) * anchors + anchor] = score;
    };
    set(1, 2, 0.5f, 0.5f, 0.2f, 0.4f, 0.9f);
    set(3, 0, 0.2f, 0.2f, 0.2f, 0.2f, 0.9f);

    const QList<QRectF> persons =
        decodeBoxes(out.data(), anchors, lb, sourceSize, aerial->personClasses, aerial->confThreshold);
    const QList<QRectF> vehicles =
        decodeBoxes(out.data(), anchors, lb, sourceSize, aerial->vehicleClasses, aerial->confThreshold);
    QCOMPARE(persons.size(), 1);
    QCOMPARE(vehicles.size(), 1);
    QVERIFY(qAbs(persons[0].x() - 0.4) < 1e-3);   // cx 0.5 - w/2
    QVERIFY(qAbs(vehicles[0].x() - 0.1) < 1e-3);  // cx 0.2 - w/2

    // A descriptor and a model that disagree are a failed load, not a guess at the nearest class.
    // The parser cannot see the model, so it is the loader that asks, once it knows the class count.
    const std::optional<ModelDescriptor> pastTheEnd =
        parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", )"
                             R"("person": [0], "vehicle": [80], "conf": 0.3})", &error);
    QVERIFY(pastTheEnd.has_value());
    QVERIFY(!classIdsWithin(*pastTheEnd, kNumClasses));
    QVERIFY(classIdsWithin(*pastTheEnd, 81));

    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", "vehicle": [2], "conf": 0.3})", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", )"
                                  R"("person": [], "vehicle": [2], "conf": 0.3})"));
    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", )"
                                  R"("person": [0], "vehicle": [2], "conf": 0})"));
    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", )"
                                  R"("person": [0.5], "vehicle": [2], "conf": 0.3})"));
    QVERIFY(!parseModelDescriptor("not json at all"));

    // A model with nothing but people is a descriptor with an empty vehicle list, not a broken one.
    const std::optional<ModelDescriptor> personOnly =
        parseModelDescriptor(R"({"layout": "detsLabels", "preprocess": "rgb01", )"
                             R"("person": [0], "vehicle": [], "conf": 0.4})", &error);
    QVERIFY2(personOnly.has_value(), qPrintable(error));
    QVERIFY(personOnly->vehicleClasses.isEmpty());
    QVERIFY(classIdsWithin(*personOnly, 1));
}

/// The layout says how to read the model's numbers and the preprocess says what it has to be fed,
/// so both are declared rather than sniffed: a missing or unknown one is a failed load, not a
/// fallback to whichever came first. Feeding RTMDet the Ultralytics recipe does not fail loudly, it
/// returns thousands of confident boxes over noise, so there is nothing downstream to catch it.
void PersonDetectorCoreTest::_testDescriptorStatesLayoutAndPreprocess()
{
    QString error;
    const std::optional<ModelDescriptor> yolo =
        parseModelDescriptor(R"({"layout": "yolo", "preprocess": "rgb01", )"
                             R"("person": [0], "vehicle": [2], "conf": 0.3})", &error);
    QVERIFY2(yolo.has_value(), qPrintable(error));
    QVERIFY(yolo->layout == OutputLayout::Yolo);

    const std::optional<ModelDescriptor> detsLabels =
        parseModelDescriptor(R"({"layout": "detsLabels", "preprocess": "rgb01", )"
                             R"("person": [0], "vehicle": [], "conf": 0.3})", &error);
    QVERIFY2(detsLabels.has_value(), qPrintable(error));
    QVERIFY(detsLabels->layout == OutputLayout::DetsLabels);

    QVERIFY(!parseModelDescriptor(R"({"layout": "yolov9", "preprocess": "rgb01", )"
                                  R"("person": [0], "vehicle": [], "conf": 0.3})", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseModelDescriptor(R"({"person": [0], "vehicle": [], "conf": 0.3})"));
    QVERIFY(!parseModelDescriptor(R"({"layout": 2, "preprocess": "rgb01", )"
                                  R"("person": [0], "vehicle": [], "conf": 0.3})"));

    const std::optional<ModelDescriptor> bgr =
        parseModelDescriptor(R"({"layout": "detsLabels", "preprocess": "bgrMeanStd", )"
                             R"("person": [0], "vehicle": [], "conf": 0.3})", &error);
    QVERIFY2(bgr.has_value(), qPrintable(error));
    QVERIFY(bgr->preprocess == Preprocess::BgrMeanStd);
    QVERIFY(yolo->preprocess == Preprocess::Rgb01);

    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "preprocess": "imagenet", )"
                                  R"("person": [0], "vehicle": [], "conf": 0.3})", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseModelDescriptor(R"({"layout": "yolo", "person": [0], "vehicle": [], "conf": 0.3})"));
}

/// The other half of the tile path that fails quietly: a tile's boxes come back in its own crop's
/// coordinates, and losing the origin on the way out leaves them inside the frame and inside 0..1,
/// just stacked in its corner. Where a known box lands is pinned here, off a tile whose origin,
/// width and height all differ, so a dropped origin or a swapped axis is caught.
void PersonDetectorCoreTest::_testTileBoxesMapIntoFrame()
{
    const QRect tile(432, 576, 192, 192);
    const QSize frameSize(810, 1080);
    QList<QRectF> boxes{QRectF(0.25, 0.5, 0.5, 0.25)};
    tileBoxesToFrame(boxes, tile, frameSize);
    QCOMPARE(boxes.size(), 1);
    // 432 + 0.25 * 192 = 480 px across, 576 + 0.5 * 192 = 672 px down, 96 px wide, 48 px tall
    QCOMPARE(boxes[0].x(), 480.0 / 810);
    QCOMPARE(boxes[0].y(), 672.0 / 1080);
    QCOMPARE(boxes[0].width(), 96.0 / 810);
    QCOMPARE(boxes[0].height(), 48.0 / 1080);
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

    // A tile that caught only the head and shoulders: it sits inside the whole frame's box and
    // shares almost none of its union, so IoU alone would report two people.
    const QRectF shouldersOnly(0.405, 0.405, 0.05, 0.05);
    QCOMPARE(PersonDetectorCore::mergeBoxes({person, shouldersOnly}).size(), 1);

    // Small is not the same as swallowed: a child beside them is half the size but mostly
    // outside the box, and both rules have to hold before a box is dropped.
    const QRectF child(0.45, 0.46, 0.03, 0.07);
    QCOMPARE(PersonDetectorCore::mergeBoxes({person, child}).size(), 2);
}
