#include "SiyiAiControllerTest.h"

#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "SettingsManager.h"
#include "SiyiAiController.h"
#include "SiyiAiProtocol.h"
#include "SiyiCameraSettings.h"
#include "SiyiProtocol.h"

namespace {

QByteArray uint16le(quint16 value)
{
    QByteArray out;
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    return out;
}

/// One CommandId::TargetStream frame as the module emits it: the box in the reference frame,
/// its class, then the tracking status this test varies.
QByteArray targetStreamFrame(SiyiAi::TrackingStatus status)
{
    QByteArray data;
    data.append(uint16le(640));    // centre x
    data.append(uint16le(360));    // centre y
    data.append(uint16le(80));     // width
    data.append(uint16le(160));    // height
    data.append(static_cast<char>(SiyiAi::TargetType::Person));
    data.append(static_cast<char>(status));
    return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiAi::CommandId::TargetStream), data);
}

} // namespace

/// A cancel from the hand controller or SIYI's own app reaches the GCS only as a target stream
/// carrying Track_Sta 3. It has to clear the tracking state at once, otherwise the dashboard
/// keeps reporting a target and drawing its stale box until the stream times out.
void SiyiAiControllerTest::_cancelledTargetStopsTracking_test()
{
    QUdpSocket module;
    QVERIFY(module.bind(QHostAddress::LocalHost, 0));

    SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
    QVERIFY(settings);
    settings->aiIpAddress()->setRawValue(QStringLiteral("127.0.0.1"));
    settings->aiPort()->setRawValue(module.localPort());

    // Not the singleton: it is only started on a normal app boot, and a private instance keeps
    // the fake module's loopback port to this test.
    SiyiAiController controller(nullptr);
    controller.start();

    // start() announces itself, and that datagram carries the ephemeral port replies go back to.
    QVERIFY(module.waitForReadyRead(TestTimeout::shortMs()));
    QHostAddress controllerAddress;
    quint16 controllerPort = 0;
    QByteArray probe;
    probe.resize(module.pendingDatagramSize());
    QVERIFY(module.readDatagram(probe.data(), probe.size(), &controllerAddress, &controllerPort) > 0);
    QVERIFY(controllerPort != 0);

    QSignalSpy targetSpy(&controller, &SiyiAiController::targetChanged);

    const QByteArray tracking = targetStreamFrame(SiyiAi::TrackingStatus::Tracking);
    QCOMPARE(module.writeDatagram(tracking, controllerAddress, controllerPort), tracking.size());
    QVERIFY_SIGNAL_COUNT_WAIT(targetSpy, 1, TestTimeout::shortMs());
    QVERIFY(controller.hasTarget());
    QVERIFY(!controller.targetLost());

    const QByteArray cancelled = targetStreamFrame(SiyiAi::TrackingStatus::CancelledByUser);
    QCOMPARE(module.writeDatagram(cancelled, controllerAddress, controllerPort), cancelled.size());
    QVERIFY_SIGNAL_COUNT_WAIT(targetSpy, 2, TestTimeout::shortMs());
    QVERIFY(!controller.hasTarget());
}

UT_REGISTER_TEST(SiyiAiControllerTest, TestLabel::Unit)
