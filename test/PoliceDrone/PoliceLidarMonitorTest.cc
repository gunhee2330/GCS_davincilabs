#include "PoliceLidarMonitorTest.h"

#include <QtCore/QtNumeric>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "MAVLinkLib.h"
#include "MockLink.h"
#include "PoliceLidarMonitor.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

namespace {

constexpr int kForwardSector = 0;
constexpr int kYaw90Sector = 2;

/// Room for the serialise, parse and dispatch hop, and deliberately well short of the second the
/// distance sensor fact group coalesces into: a value that only turned up after that would fail.
constexpr int kDispatchMs = 400;

/// A stand-in for the 5 s default where the test has to sit through a whole timeout. Long enough
/// that the 250 ms staleness sweep and the dispatch hop both fit inside it with room over.
constexpr int kShortTimeoutMs = 1000;

/// A TF Mini's own declared range, in centimetres. Neither bound is consulted by the monitor;
/// they are here because a real frame carries them.
constexpr uint16_t kMinDistanceCm = 40;
constexpr uint16_t kMaxDistanceCm = 1200;

} // namespace

void PoliceLidarMonitorTest::_sendDistanceSensor(uint8_t orientation, uint16_t centimetres, uint8_t sysid)
{
    QVERIFY(mockLink());

    mavlink_message_t message{};
    const float quaternion[4]{};
    (void) mavlink_msg_distance_sensor_pack_chan(
        (sysid != 0) ? sysid : static_cast<uint8_t>(mockLink()->vehicleId()),
        MAV_COMP_ID_AUTOPILOT1,
        mockLink()->outgoingMavlinkChannel(),
        &message,
        0,                          // time_boot_ms
        kMinDistanceCm,
        kMaxDistanceCm,
        centimetres,
        MAV_DISTANCE_SENSOR_LASER,
        0,                          // id
        orientation,
        255,                        // covariance - unknown
        0.0f,                       // horizontal_fov - unknown
        0.0f,                       // vertical_fov - unknown
        quaternion,                 // valid only for MAV_SENSOR_ROTATION_CUSTOM
        0);                         // signal_quality - unknown, which is what PX4's tfmini sends

    mockLink()->respondWithMavlinkMessage(message);
}

void PoliceLidarMonitorTest::_readingsAreRawMetresPerSector_test()
{
    PoliceLidarMonitor monitor;
    QCOMPARE(monitor.staleTimeoutMs(), 5000);
    QVERIFY2(!monitor.fresh(), "A monitor that has heard nothing reported itself fresh");

    monitor.setVehicle(vehicle());

    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTRY_COMPARE_WITH_TIMEOUT(monitor.forwardDistance(), 3.3, kDispatchMs);
    QVERIFY(monitor.fresh());
    QCOMPARE(monitor.sectorDistances().at(kForwardSector).toDouble(), 3.3);

    // The seven sectors this airframe carries no sensor for stay blank rather than reading zero.
    for (int sector = 1; sector < 8; ++sector) {
        QVERIFY2(qIsNaN(monitor.sectorDistances().at(sector).toDouble()),
                 qPrintable(QStringLiteral("Sector %1 reported a distance no sensor sent").arg(sector)));
    }

    _sendDistanceSensor(MAV_SENSOR_ROTATION_YAW_90, 660);
    QTRY_COMPARE_WITH_TIMEOUT(monitor.sectorDistances().at(kYaw90Sector).toDouble(), 6.6, kDispatchMs);
    QCOMPARE(monitor.forwardDistance(), 3.3);

    // A changed distance is a change in QML too, and lands now rather than at some group's tick.
    QSignalSpy forwardSpy(&monitor, &PoliceLidarMonitor::forwardDistanceChanged);
    QVERIFY(forwardSpy.isValid());
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 250);
    QTRY_COMPARE_WITH_TIMEOUT(monitor.forwardDistance(), 2.5, kDispatchMs);
    QCOMPARE(forwardSpy.count(), 1);

    // A zero is the autopilot saying it has no reading, not an obstacle at the nose. The sensor
    // is still talking, so the displays stay up.
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 0);
    QTRY_VERIFY_WITH_TIMEOUT(qIsNaN(monitor.forwardDistance()), kDispatchMs);
    QVERIFY(monitor.fresh());
}

void PoliceLidarMonitorTest::_repeatedIdenticalFramesStayFresh_test()
{
    PoliceLidarMonitor monitor;
    monitor.setVehicle(vehicle());

    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTRY_COMPARE_WITH_TIMEOUT(monitor.forwardDistance(), 3.3, kDispatchMs);

    QSignalSpy forwardSpy(&monitor, &PoliceLidarMonitor::forwardDistanceChanged);
    QSignalSpy freshSpy(&monitor, &PoliceLidarMonitor::freshChanged);
    QVERIFY(forwardSpy.isValid());
    QVERIFY(freshSpy.isValid());

    // The same distance once a second for past the timeout: were an identical frame not a
    // receipt, the sector would have gone stale before the last pass. Every frame here carries a
    // value already held, which is the case an equal Fact value made invisible.
    for (int second = 0; second < 5; ++second) {
        QTest::qWait(1000);
        _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
        QTest::qWait(kDispatchMs);
        QVERIFY2(monitor.fresh(),
                 qPrintable(QStringLiteral("Went stale after %1 s of frames").arg(second + 1)));
        QCOMPARE(monitor.forwardDistance(), 3.3);
    }

    QCOMPARE(forwardSpy.count(), 0);
    QCOMPARE(freshSpy.count(), 0);
}

void PoliceLidarMonitorTest::_silenceGoesStale_test()
{
    PoliceLidarMonitor monitor;
    // Shortened so the test waits out silence in about a second instead of five. What is under
    // test is that silence past the timeout clears the sector, not the length of the timeout -
    // the 5 s default is pinned in _readingsAreRawMetresPerSector_test.
    monitor.setStaleTimeoutMs(kShortTimeoutMs);
    monitor.setVehicle(vehicle());

    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.fresh(), kDispatchMs);

    // Inside the timeout the reading stands: a 0.5 Hz profile must not blink between frames.
    QTest::qWait(monitor.staleTimeoutMs() / 2);
    QCOMPARE(monitor.forwardDistance(), 3.3);
    QVERIFY(monitor.fresh());

    // Past it, a sensor that died mid-sortie leaves nothing on screen.
    QTRY_VERIFY_WITH_TIMEOUT(!monitor.fresh(), monitor.staleTimeoutMs() + 1000);
    QVERIFY(qIsNaN(monitor.forwardDistance()));
    const QVariantList distances = monitor.sectorDistances();
    for (const QVariant &metres : distances) {
        QVERIFY(qIsNaN(metres.toDouble()));
    }
}

void PoliceLidarMonitorTest::_downwardOrientationIsIgnored_test()
{
    PoliceLidarMonitor monitor;
    monitor.setVehicle(vehicle());

    // Orientation 25, MAV_SENSOR_ROTATION_PITCH_270: a downward lidar, which is what the second
    // rangefinder on this airframe would be.
    QCOMPARE(static_cast<int>(MAV_SENSOR_ROTATION_PITCH_270), 25);
    _sendDistanceSensor(MAV_SENSOR_ROTATION_PITCH_270, 150);
    QTest::qWait(kDispatchMs);

    QVERIFY2(!monitor.fresh(), "A downward rangefinder kept the forward displays alive");
    const QVariantList distances = monitor.sectorDistances();
    for (const QVariant &metres : distances) {
        QVERIFY(qIsNaN(metres.toDouble()));
    }
}

void PoliceLidarMonitorTest::_foreignSysidAndNullVehicleLeaveNothing_test()
{
    PoliceLidarMonitor monitor;
    monitor.setVehicle(vehicle());

    // Vehicle drops anything from another system id before it signals, so a second aircraft on
    // the same link cannot draw on this one's ring.
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330,
                        static_cast<uint8_t>(mockLink()->vehicleId() + 1));
    QTest::qWait(kDispatchMs);
    QVERIFY2(!monitor.fresh(), "A frame from another system id reached the monitor");
    QVERIFY(qIsNaN(monitor.forwardDistance()));

    // The same frame from its own vehicle does arrive, so the check above was not a dead link.
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.fresh(), kDispatchMs);

    // The vehicle going away takes the reading with it rather than leaving the last one standing,
    // and nothing that arrives afterwards is anybody's.
    monitor.setVehicle(nullptr);
    QVERIFY(!monitor.fresh());
    QVERIFY(qIsNaN(monitor.forwardDistance()));
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTest::qWait(kDispatchMs);
    QVERIFY(!monitor.fresh());

    // Once more, then the link itself goes away. A destroyed vehicle leaves the monitor holding a
    // pointer that has nulled itself, so the null QML delivers next matches what is held: the
    // reading still has to go at once rather than standing until staleness notices.
    monitor.setVehicle(vehicle());
    _sendDistanceSensor(MAV_SENSOR_ROTATION_NONE, 330);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.fresh(), kDispatchMs);
    _disconnectMockLink();
    monitor.setVehicle(nullptr);
    QVERIFY2(!monitor.fresh(), "A departed airframe's reading stood after its vehicle went away");
    QVERIFY(qIsNaN(monitor.forwardDistance()));
}

void PoliceLidarMonitorTest::_fastRateRequestedOncePerVehicle_test()
{
    // The vehicle is past its initial connect sequence already, so the request goes out on
    // setVehicle; nothing from that sequence is counted here.
    QVERIFY(vehicle()->isInitialConnectComplete());
    mockLink()->clearReceivedMavCommandCounts();
    mockLink()->clearReceivedRequestMessageCounts();

    PoliceLidarMonitor ring;
    PoliceLidarMonitor glow;
    ring.setVehicle(vehicle());
    glow.setVehicle(vehicle());

    QTRY_COMPARE_WITH_TIMEOUT(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 1, kDispatchMs);

    mavlink_message_t message{};
    QVERIFY(mockLink()->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_LONG, message));
    mavlink_command_long_t command{};
    mavlink_msg_command_long_decode(&message, &command);
    QCOMPARE(static_cast<int>(command.command), static_cast<int>(MAV_CMD_SET_MESSAGE_INTERVAL));
    QCOMPARE(static_cast<int>(command.target_component), static_cast<int>(MAV_COMP_ID_AUTOPILOT1));
    QCOMPARE(command.param1, static_cast<float>(MAVLINK_MSG_ID_DISTANCE_SENSOR));
    QCOMPARE(command.param2, 200000.0f);

    // MockLink answers UNSUPPORTED for this id, as a firmware that will not stream it faster
    // would. Once that ack is in, no second request follows, the manager does not go on to ask
    // for MESSAGE_INTERVAL, and the timeout stays where it was.
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle()->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_SET_MESSAGE_INTERVAL), kDispatchMs);
    QTest::qWait(kDispatchMs);
    QCOMPARE(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 1);
    QCOMPARE(mockLink()->receivedRequestMessageCount(MAV_COMP_ID_AUTOPILOT1, MAVLINK_MSG_ID_MESSAGE_INTERVAL), 0);
    QCOMPARE(ring.staleTimeoutMs(), 5000);
    QCOMPARE(glow.staleTimeoutMs(), 5000);
}

void PoliceLidarMonitorTest::_confirmedFastRateShortensTimeout_test()
{
    PoliceLidarMonitor ring;
    PoliceLidarMonitor glow;
    ring.setVehicle(vehicle());
    glow.setVehicle(vehicle());

    // MockLink refuses SET_MESSAGE_INTERVAL for this id, so the vehicle's half of the accepted
    // path is played here: the MESSAGE_INTERVAL report MessageIntervalManager asks for after an
    // ACCEPTED ack.
    const auto sendInterval = [this](int32_t intervalUs) {
        mavlink_message_t message{};
        (void) mavlink_msg_message_interval_pack_chan(
            static_cast<uint8_t>(mockLink()->vehicleId()),
            MAV_COMP_ID_AUTOPILOT1,
            mockLink()->outgoingMavlinkChannel(),
            &message,
            MAVLINK_MSG_ID_DISTANCE_SENSOR,
            intervalUs);
        mockLink()->respondWithMavlinkMessage(message);
    };

    // Still on the radio profile's 0.5 Hz: nothing changes.
    sendInterval(2000000);
    QTest::qWait(kDispatchMs);
    QCOMPARE(ring.staleTimeoutMs(), 5000);
    QCOMPARE(glow.staleTimeoutMs(), 5000);

    sendInterval(200000);
    QTRY_COMPARE_WITH_TIMEOUT(ring.staleTimeoutMs(), 2000, kDispatchMs);
    QCOMPARE(glow.staleTimeoutMs(), 2000);

    PoliceLidarMonitor camera;
    camera.setVehicle(vehicle());
    QCOMPARE(camera.staleTimeoutMs(), 2000);

    // The timeout is the vehicle's, not the monitor's: leaving it goes back to 5 s, and coming
    // back to it, still confirmed, returns to 2 s.
    ring.setVehicle(nullptr);
    QCOMPARE(ring.staleTimeoutMs(), 5000);
    ring.setVehicle(vehicle());
    QCOMPARE(ring.staleTimeoutMs(), 2000);
}

void PoliceLidarMonitorTest::_lostLinkRequestsFastRateAgain_test()
{
    mockLink()->clearReceivedMavCommandCounts();

    PoliceLidarMonitor ring;
    PoliceLidarMonitor glow;
    ring.setVehicle(vehicle());
    glow.setVehicle(vehicle());
    QTRY_COMPARE_WITH_TIMEOUT(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 1, kDispatchMs);

    const auto sendFastInterval = [this]() {
        mavlink_message_t message{};
        (void) mavlink_msg_message_interval_pack_chan(
            static_cast<uint8_t>(mockLink()->vehicleId()),
            MAV_COMP_ID_AUTOPILOT1,
            mockLink()->outgoingMavlinkChannel(),
            &message,
            MAVLINK_MSG_ID_DISTANCE_SENSOR,
            200000);
        mockLink()->respondWithMavlinkMessage(message);
    };
    sendFastInterval();
    QTRY_COMPARE_WITH_TIMEOUT(ring.staleTimeoutMs(), 2000, kDispatchMs);
    QCOMPARE(glow.staleTimeoutMs(), 2000);

    // A rebooting autopilot goes quiet and comes back on its own rate: the confirmation goes with
    // the link, and a display that turns up meanwhile sends nothing into it.
    simulateCommLoss(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle()->vehicleLinkManager()->communicationLost(), VehicleLinkManager::kTestCommLostDetectionTimeoutMs);
    QCOMPARE(ring.staleTimeoutMs(), 5000);
    QCOMPARE(glow.staleTimeoutMs(), 5000);
    PoliceLidarMonitor camera;
    camera.setVehicle(vehicle());
    QCOMPARE(camera.staleTimeoutMs(), 5000);
    QTest::qWait(kDispatchMs);
    QCOMPARE(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 1);

    // Back again: one new request for the three displays, and the same 5 Hz confirmed again, a
    // rate MessageIntervalManager has already cached, shortens them all.
    simulateCommLoss(false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle()->vehicleLinkManager()->communicationLost(), VehicleLinkManager::kTestCommLostDetectionTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 2, kDispatchMs);
    QTest::qWait(kDispatchMs);
    QCOMPARE(mockLink()->receivedMavCommandCount(MAV_CMD_SET_MESSAGE_INTERVAL, MAV_COMP_ID_AUTOPILOT1), 2);
    QCOMPARE(ring.staleTimeoutMs(), 5000);

    sendFastInterval();
    QTRY_COMPARE_WITH_TIMEOUT(ring.staleTimeoutMs(), 2000, kDispatchMs);
    QCOMPARE(glow.staleTimeoutMs(), 2000);
    QCOMPARE(camera.staleTimeoutMs(), 2000);
}

UT_REGISTER_TEST(PoliceLidarMonitorTest, TestLabel::Integration, TestLabel::Vehicle)
