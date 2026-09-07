#include "PersonDetectorTest.h"

#include <algorithm>
#include <chrono>
#include <optional>

#include <QtCore/QFile>
#include <QtCore/QMutexLocker>
#include <QtGui/QImage>
#include <QtTest/QSignalSpy>

#include "PersonDetector.h"
#include "PersonDetectorCore.h"
#include "PersonDetectorWorker.h"
#include "VideoReceiver.h"

using namespace std::chrono_literals;

UT_REGISTER_TEST_LIGHTWEIGHT(PersonDetectorTest, TestLabel::Unit)

namespace {

/// Four clearly visible people plus one partially occluded, on a bus.
QImage busFixture()
{
    return QImage(QStringLiteral(":/unittest/bus.jpg")).convertToFormat(QImage::Format_RGB888);
}

}  // namespace

void PersonDetectorTest::_testDetectBus()
{
    PersonDetectorWorker worker;
    if (!worker.load()) {
        QSKIP("Person detection model unavailable (built without ONNX Runtime)");
    }

    // Which recipe the model wants is data rather than code, so the core test cannot pin it: that
    // one fixes what each recipe does to a pixel, this one fixes which recipe ships. Flipping back
    // to the Ultralytics one barely moves the count on this fixture — it is the overhead crowd it
    // turns into thousands of boxes — so nothing else here would catch it.
    QFile descriptorFile(QStringLiteral(":/PersonDetection/rtmdet-n-person.json"));
    QVERIFY(descriptorFile.open(QIODevice::ReadOnly));
    const std::optional<PersonDetectorCore::ModelDescriptor> descriptor =
        PersonDetectorCore::parseModelDescriptor(descriptorFile.readAll());
    QVERIFY(descriptor.has_value());
    QVERIFY2(descriptor->preprocess == PersonDetectorCore::Preprocess::BgrMeanStd,
             "the mmdeploy export is not being fed the mmdeploy recipe");

    const QImage bus = busFixture();
    QVERIFY(!bus.isNull());

    int inferenceMs = 0;
    const PersonDetectorWorker::Detections result = worker.detect(bus, &inferenceMs);
    qCDebug(PersonDetectorLog) << "persons" << result.persons
                               << "vehicles" << result.vehicles
                               << "ms" << inferenceMs;

    QVERIFY2(result.persons.size() >= 3, qPrintable(QStringLiteral("only %1 people found").arg(result.persons.size())));
    QVERIFY2(result.persons.size() <= 8, qPrintable(QStringLiteral("%1 people found").arg(result.persons.size())));
    QVERIFY(inferenceMs > 0);

    for (const QRectF& box : result.persons) {
        QVERIFY(box.width() > 0);
        QVERIFY(box.height() > 0);
        QVERIFY(box.left() >= 0.0);
        QVERIFY(box.top() >= 0.0);
        QVERIFY(box.right() <= 1.0);
        QVERIFY(box.bottom() <= 1.0);
        // Standing people in a 810x1080 photo: every box is taller than it is wide.
        QVERIFY(box.height() * bus.height() > box.width() * bus.width());
    }

    // The shipped model is trained on people alone: the bus in the photo is not a miss, it is a
    // class the model does not have. Nothing may claim otherwise, here or in the panel.
    QVERIFY(!worker.detectsVehicles());
    QVERIFY2(result.vehicles.isEmpty(),
             qPrintable(QStringLiteral("%1 vehicles from a person-only model").arg(result.vehicles.size())));

    // One call covered region 0 alone. The number on the panel is the whole sweep: the rest of the
    // regions are tiles letterboxed up rather than down, mapped back into frame coordinates and
    // merged, so the sweep is driven to its end here rather than stopping at the first frame.
    const int regions = worker.regionCount();
    QVERIFY2(regions > 1, "the fixture is not being tiled, so only the whole frame pass is covered");
    PersonDetectorWorker::Detections swept;
    for (int i = 1; i < regions; ++i) {
        swept = worker.detect(bus, nullptr);
    }
    qCDebug(PersonDetectorLog) << "swept regions" << regions
                               << "persons" << swept.persons.size()
                               << "vehicles" << swept.vehicles.size();

    // Tiles see people the squeezed whole frame misses, and the merge keeps what region 0 found.
    QVERIFY2(swept.persons.size() > result.persons.size(),
             qPrintable(QStringLiteral("the tiles added nothing to the whole frame pass's %1")
                            .arg(result.persons.size())));
    QVERIFY2(swept.persons.size() <= 20,
             qPrintable(QStringLiteral("%1 people swept out of a photo of five").arg(swept.persons.size())));
    for (const QRectF& box : swept.persons) {
        QVERIFY(box.width() > 0);
        QVERIFY(box.height() > 0);
        QVERIFY(box.left() >= 0.0);
        QVERIFY(box.top() >= 0.0);
        QVERIFY(box.right() <= 1.0);
        QVERIFY(box.bottom() <= 1.0);
    }
    // The range check above cannot see a tile mapped back without its origin: that only ever pulls a
    // box towards the corner, which is still inside the picture. Tiles cover the whole width, so the
    // right half has to contribute boxes of its own beyond the one the whole frame pass found there.
    const auto inRightHalf = [](const QRectF& box) { return box.left() > 0.5; };
    QVERIFY2(std::ranges::count_if(swept.persons, inRightHalf)
                 > std::ranges::count_if(result.persons, inRightHalf),
             "no tile placed a box in the right half, so tile boxes are losing their tile's origin");
    // Same argument down the other axis: a tile frozen to the top strip also stays in range.
    const auto inBottomHalf = [](const QRectF& box) { return box.top() > 0.5; };
    QVERIFY2(std::ranges::count_if(swept.persons, inBottomHalf)
                 > std::ranges::count_if(result.persons, inBottomHalf),
             "no tile placed a box in the bottom half, so tile boxes are losing their tile's origin");
    QVERIFY2(swept.vehicles.isEmpty(),
             qPrintable(QStringLiteral("%1 vehicles from a person-only model").arg(swept.vehicles.size())));
}

void PersonDetectorTest::_testMailboxDropsStale()
{
    PersonDetector detector(nullptr);
    if (!detector._loadWorker()) {
        QSKIP("Person detection model unavailable (built without ONNX Runtime)");
    }

    TappedVideoFrame frame;
    frame.image = busFixture();
    QVERIFY(!frame.image.isNull());

    QSignalSpy spy(&detector, &PersonDetector::detectionsChanged);
    for (int i = 0; i < 5; ++i) {
        detector.submit(frame);
    }

    QTRY_VERIFY(spy.count() >= 1);
    // Settle: anything still queued is at most the one coalesced follow-up run.
    (void) UnitTest::waitForNoSignal(spy, 1s, u"detectionsChanged");
    QVERIFY2(spy.count() <= 2, qPrintable(QStringLiteral("5 frames produced %1 detections").arg(spy.count())));
}

void PersonDetectorTest::_testDisabledDropsFrames()
{
    PersonDetector detector(nullptr);
    detector._loaded = detector._loadWorker();
    if (!detector._loaded) {
        QSKIP("Person detection model unavailable (built without ONNX Runtime)");
    }
    QVERIFY(detector.enabled());  // QSettings default

    TappedVideoFrame frame;
    frame.image = busFixture();
    QVERIFY(!frame.image.isNull());

    detector.submit(frame);
    QTRY_VERIFY(!detector.boxes().isEmpty());
    QVERIFY(!detector.detectsVehicles());  // person-only model: no vehicle count to report at all

    QSignalSpy spy(&detector, &PersonDetector::detectionsChanged);
    QSignalSpy activeSpy(&detector, &PersonDetector::activeChanged);
    detector.setEnabled(false);
    QVERIFY(!detector.enabled());
    QCOMPARE(spy.count(), 1);
    QVERIFY(detector.boxes().isEmpty());

    {
        PersonDetector restored(nullptr);
        QVERIFY2(!restored.enabled(), "the switch was not persisted");
        // No model loaded in this one, so the switch alone must not activate it.
        restored.setEnabled(true);
        QVERIFY(!restored.active());
    }

    // A frame submitted while off must not reach the worker or repopulate the boxes. The literal
    // timeout has to outlast one inference of the bus frame, or an ungated submit() would go unseen.
    detector.submit(frame);
    {
        QMutexLocker locker(&detector._mutex);
        QVERIFY2(detector._pending.isNull() && !detector._runPending, "a frame reached the worker while off");
    }
    QVERIFY_NO_SIGNAL_WAIT(spy, 1000);
    QVERIFY(detector.boxes().isEmpty());

    detector.setEnabled(true);
    QVERIFY(detector.enabled());
    QVERIFY(detector.active());  // _loaded && _enabled
    QCOMPARE(activeSpy.count(), 1);

    // A frame already in flight at switch-off must not repopulate the boxes either. The worker
    // normally has it before the switch reaches the mailbox; if not, the cleared mailbox covers it.
    QSignalSpy inFlightSpy(&detector, &PersonDetector::detectionsChanged);
    detector.submit(frame);
    detector.setEnabled(false);
    QVERIFY_NO_SIGNAL_WAIT(inFlightSpy, 1000);
    QVERIFY(detector.boxes().isEmpty());

    detector.setEnabled(true);
}
