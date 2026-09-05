#include "PersonDetectorTest.h"

#include <chrono>

#include <QtCore/QMutexLocker>
#include <QtGui/QImage>
#include <QtTest/QSignalSpy>

#include "PersonDetector.h"
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

    const QImage bus = busFixture();
    QVERIFY(!bus.isNull());

    int inferenceMs = 0;
    const PersonDetectorWorker::Detections result = worker.detect(bus, &inferenceMs);
    qCDebug(PersonDetectorLog) << "persons" << result.persons
                               << "vehicles" << result.vehicles
                               << "ms" << inferenceMs;

    QVERIFY2(result.persons.size() >= 3, qPrintable(QStringLiteral("only %1 people found").arg(result.persons.size())));
    QVERIFY2(result.persons.size() <= 6, qPrintable(QStringLiteral("%1 people found").arg(result.persons.size())));
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

    QVERIFY2(!result.vehicles.isEmpty(), "no vehicle found");
    // The bus fills the width of the photo, so the best-scoring vehicle box is wider than it is tall.
    const QRectF vehicle = result.vehicles.first();
    QVERIFY(vehicle.width() * bus.width() > vehicle.height() * bus.height());
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
    QVERIFY(!detector.vehicleBoxes().isEmpty());

    QSignalSpy spy(&detector, &PersonDetector::detectionsChanged);
    QSignalSpy activeSpy(&detector, &PersonDetector::activeChanged);
    detector.setEnabled(false);
    QVERIFY(!detector.enabled());
    QCOMPARE(spy.count(), 1);
    QVERIFY(detector.boxes().isEmpty());
    QVERIFY(detector.vehicleBoxes().isEmpty());

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
    QVERIFY(detector.vehicleBoxes().isEmpty());

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
