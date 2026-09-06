#include "PoliceDroneTargetFollowTest.h"

#include <cmath>

#include <QtCore/QRegularExpression>

#include "PoliceDroneTargetFollow.h"

using Block = PoliceDroneTargetFollow::Block;
using Conditions = PoliceDroneTargetFollow::Conditions;
using Geolocation = PoliceDroneTargetFollow::Geolocation;
using SightLine = PoliceDroneTargetFollow::SightLine;

namespace {

/// 150 m AMSL, 100 m of it above the takeoff plane, so the ground sits at 50 m AMSL.
const QGeoCoordinate kVehicleCoordinate(37.5, 127.0, 150.0);

/// A 90 degree horizontal field of view makes the frame edge exactly one boresight length
/// off the optical axis, so the expected offsets can be worked out by hand.
SightLine nadirSightLine()
{
    SightLine sight;
    sight.hFovDeg = 90.0;
    sight.vFovDeg = 60.0;
    sight.gimbalPitchDeg = -90.0;
    sight.heightAboveGroundMeters = 100.0;
    sight.vehicleCoordinate = kVehicleCoordinate;
    return sight;
}

Conditions followingConditions()
{
    Conditions conditions;
    conditions.enabled = true;
    conditions.vehicleValid = true;
    conditions.armed = true;
    conditions.flying = true;
    conditions.targetTracked = true;
    conditions.geolocationValid = true;
    conditions.msSinceTargetSeen = 0;
    return conditions;
}

void compareWithin(double actual, double expected, double tolerance)
{
    QVERIFY2(qAbs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("expected %1, got %2").arg(expected).arg(actual)));
}

/// Compares bearings the short way round, so a due north result may come back as 359.99.
void compareAzimuth(double actual, double expected, double tolerance)
{
    const double difference = std::fmod(actual - expected + 540.0, 360.0) - 180.0;
    QVERIFY2(qAbs(difference) <= tolerance,
             qPrintable(QStringLiteral("expected azimuth %1, got %2").arg(expected).arg(actual)));
}

} // namespace

void PoliceDroneTargetFollowTest::_nadirCentreOfFrameIsBelowVehicle_test()
{
    const Geolocation fix = PoliceDroneTargetFollow::geolocateTarget(nadirSightLine());

    QVERIFY(fix.valid);
    compareWithin(fix.groundDistanceMeters, 0.0, 0.01);
    compareWithin(fix.slantRangeMeters, 100.0, 0.01);
    compareWithin(fix.coordinate.altitude(), 50.0, 0.01);
    compareWithin(fix.coordinate.distanceTo(kVehicleCoordinate), 0.0, 0.01);
}

void PoliceDroneTargetFollowTest::_nadirOffsetInFrameMapsToGroundOffset_test()
{
    // Three quarters across a 90 degree frame is half a frame off the axis, so the ray leaves
    // the camera at 100 m out for 100 m down: a 50 m ground offset to the image right.
    SightLine sight = nadirSightLine();
    sight.frameX = 0.75;

    const Geolocation east = PoliceDroneTargetFollow::geolocateTarget(sight);
    QVERIFY(east.valid);
    compareWithin(east.groundDistanceMeters, 50.0, 0.05);
    compareWithin(east.slantRangeMeters, 111.803, 0.05);
    compareAzimuth(kVehicleCoordinate.azimuthTo(east.coordinate), 90.0, 0.1);

    // Image right is relative to where the aircraft is pointing, so the same pixel with the
    // nose east must land south of the aircraft.
    sight.vehicleHeadingDeg = 90.0;
    const Geolocation south = PoliceDroneTargetFollow::geolocateTarget(sight);
    QVERIFY(south.valid);
    compareWithin(south.groundDistanceMeters, 50.0, 0.05);
    compareAzimuth(kVehicleCoordinate.azimuthTo(south.coordinate), 180.0, 0.1);
}

void PoliceDroneTargetFollowTest::_obliqueWithLaserRangeUsesMeasuredHeight_test()
{
    // 100 m of slant range at 30 degrees down puts the ground 50 m below and the target
    // 100 * cos(30) north. The stale AGL below must be ignored in favour of the laser.
    SightLine sight;
    sight.hFovDeg = 90.0;
    sight.vFovDeg = 60.0;
    sight.gimbalPitchDeg = -30.0;
    sight.heightAboveGroundMeters = 500.0;
    sight.laserRangeMeters = 100.0;
    sight.vehicleCoordinate = kVehicleCoordinate;

    const Geolocation fix = PoliceDroneTargetFollow::geolocateTarget(sight);

    QVERIFY(fix.valid);
    compareWithin(fix.groundDistanceMeters, 86.603, 0.05);
    compareWithin(fix.slantRangeMeters, 100.0, 0.05);
    compareWithin(fix.coordinate.altitude(), 100.0, 0.05);
    compareAzimuth(kVehicleCoordinate.azimuthTo(fix.coordinate), 0.0, 0.1);

    // Without the laser the ray hits the takeoff plane instead, five times further down and
    // so five times further out.
    sight.laserRangeMeters = 0.0;
    const Geolocation fallback = PoliceDroneTargetFollow::geolocateTarget(sight);
    QVERIFY(fallback.valid);
    compareWithin(fallback.groundDistanceMeters, 866.025, 0.5);
}

void PoliceDroneTargetFollowTest::_rayAtOrAboveHorizonIsRejected_test()
{
    SightLine sight;
    sight.hFovDeg = 90.0;
    sight.vFovDeg = 60.0;
    sight.heightAboveGroundMeters = 100.0;
    sight.vehicleCoordinate = kVehicleCoordinate;

    sight.gimbalPitchDeg = 10.0;
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(sight).valid);

    // Below the horizon but grazing it: the intersection would be kilometres out.
    sight.gimbalPitchDeg = -2.0;
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(sight).valid);

    // Boresight below the horizon, but the target sits at the top of a 60 degree frame, which
    // puts its own ray above it.
    sight.gimbalPitchDeg = -20.0;
    sight.frameY = 0.0;
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(sight).valid);
}

void PoliceDroneTargetFollowTest::_degenerateInputsAreRejected_test()
{
    SightLine noAltitude = nadirSightLine();
    noAltitude.vehicleCoordinate = QGeoCoordinate(37.5, 127.0);
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(noAltitude).valid);

    SightLine noFov = nadirSightLine();
    noFov.hFovDeg = 0.0;
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(noFov).valid);

    SightLine onTheGround = nadirSightLine();
    onTheGround.heightAboveGroundMeters = 0.0;
    QVERIFY(!PoliceDroneTargetFollow::geolocateTarget(onTheGround).valid);

    // A laser reading taken with the boresight above the horizon places no ground plane, so
    // the takeoff plane is used instead: the target still resolves, at takeoff altitude.
    SightLine upwardBoresight = nadirSightLine();
    upwardBoresight.gimbalPitchDeg = 10.0;
    upwardBoresight.frameY = 1.0;
    upwardBoresight.laserRangeMeters = 100.0;
    const Geolocation belowAnUpwardBoresight = PoliceDroneTargetFollow::geolocateTarget(upwardBoresight);
    QVERIFY(belowAnUpwardBoresight.valid);
    compareWithin(belowAnUpwardBoresight.coordinate.altitude(), 50.0, 0.01);
}

void PoliceDroneTargetFollowTest::_gateOrdersItsChecks_test()
{
    QCOMPARE(PoliceDroneTargetFollow::evaluate(followingConditions()), Block::None);

    Conditions conditions = followingConditions();
    conditions.enabled = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::Disabled);

    conditions = followingConditions();
    conditions.vehicleValid = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NoVehicle);

    conditions = followingConditions();
    conditions.armed = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NotArmed);

    conditions = followingConditions();
    conditions.flying = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NotFlying);

    conditions = followingConditions();
    conditions.targetTracked = false;
    conditions.msSinceTargetSeen = 1500;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NoTarget);

    conditions = followingConditions();
    conditions.geolocationValid = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NoGeolocation);

    QVERIFY(PoliceDroneTargetFollow::blockText(Block::None).isEmpty());
    QVERIFY(!PoliceDroneTargetFollow::blockText(Block::NoGeolocation).isEmpty());
}

void PoliceDroneTargetFollowTest::_gateAbortsOnLostTargetBeforeVehicleChecks_test()
{
    Conditions conditions = followingConditions();
    conditions.targetTracked = false;
    conditions.geolocationValid = false;

    conditions.msSinceTargetSeen = 2000;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::NoTarget);

    conditions.msSinceTargetSeen = 2001;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::TargetLost);

    // Losing the vehicle at the same moment must not hide the abort.
    conditions.vehicleValid = false;
    conditions.armed = false;
    conditions.flying = false;
    QCOMPARE(PoliceDroneTargetFollow::evaluate(conditions), Block::TargetLost);
}

void PoliceDroneTargetFollowTest::_enableDefaultsOffAndReportsWhy_test()
{
    PoliceDroneTargetFollow follow;

    QVERIFY(!follow.enabled());
    QVERIFY(!follow.following());
    QCOMPARE(follow.notFollowingReason(), PoliceDroneTargetFollow::blockText(Block::Disabled));

    follow.setEnabled(true);
    QVERIFY(follow.enabled());
    QVERIFY(!follow.following());
    QCOMPARE(follow.notFollowingReason(), PoliceDroneTargetFollow::blockText(Block::NoTarget));

    follow.setEnabled(false);
    QVERIFY(!follow.enabled());
    QCOMPARE(follow.notFollowingReason(), PoliceDroneTargetFollow::blockText(Block::Disabled));

    const double fov = follow.cameraHFovDeg();
    expectLogMessage("FlyView.PoliceDroneTargetFollow", QtWarningMsg,
                     QRegularExpression(QStringLiteral("ignoring out of range field of view")));
    follow.setCameraHFovDeg(0.0);
    verifyExpectedLogMessage();
    QCOMPARE(follow.cameraHFovDeg(), fov);
    follow.setCameraHFovDeg(45.0);
    QCOMPARE(follow.cameraHFovDeg(), 45.0);
}

UT_REGISTER_TEST_LIGHTWEIGHT(PoliceDroneTargetFollowTest, TestLabel::Unit)
