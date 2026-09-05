#include "PersonDetectorTest.h"

#include <chrono>

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
