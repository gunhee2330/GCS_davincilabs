#include "PoliceDroneTargetFollow.h"

#include <cmath>

#include <QtCore/QApplicationStatic>
#include <QtCore/QtMath>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "FirmwarePlugin.h"
#include "FollowMe.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SiyiAiController.h"
#include "SiyiCameraController.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(PoliceDroneTargetFollowLog, "FlyView.PoliceDroneTargetFollow")

namespace {

/// The rate FollowMe already publishes follow targets at, so the stacks see nothing new.
constexpr int kPublishIntervalMs = 250;

/// Follow is abandoned once the tracker has gone this long without a target.
constexpr qint64 kTargetLostTimeoutMs = 2000;

/// Below this the ground plane is inside the aircraft's own noise and the intersection is
/// meaningless, so no coordinate is produced.
constexpr double kMinHeightAboveGroundMeters = 1.0;

/// A ray this close to the horizon puts the intersection kilometres away, where a degree of
/// attitude error moves the result further than the target ever is. Treated as "no ground hit".
constexpr double kMinDepressionDeg = 5.0;

/// The pod delivers 16:9, which fixes the vertical field of view given the horizontal one.
constexpr double kFrameAspect = 16.0 / 9.0;

} // namespace

Q_APPLICATION_STATIC(PoliceDroneTargetFollow, _policeDroneTargetFollowInstance);

PoliceDroneTargetFollow::PoliceDroneTargetFollow(QObject *parent)
    : QObject(parent)
{
    _publishTimer.setInterval(kPublishIntervalMs);
    (void) connect(&_publishTimer, &QTimer::timeout, this, &PoliceDroneTargetFollow::_publish);
}

PoliceDroneTargetFollow *PoliceDroneTargetFollow::instance()
{
    return _policeDroneTargetFollowInstance();
}

PoliceDroneTargetFollow *PoliceDroneTargetFollow::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    PoliceDroneTargetFollow *const follow = instance();
    QJSEngine::setObjectOwnership(follow, QJSEngine::CppOwnership);
    return follow;
}

void PoliceDroneTargetFollow::setEnabled(bool enabled)
{
    if (_enabled == enabled) {
        return;
    }

    _enabled = enabled;

    if (_enabled) {
        // Started here rather than on the first sighting so that enabling with nothing tracked
        // also runs out and switches itself back off.
        _targetSeenTimer.start();
        _publishTimer.start();
        _setStatus(false, Block::NoTarget, QGeoCoordinate());
    } else {
        _publishTimer.stop();
        _setStatus(false, Block::Disabled, QGeoCoordinate());
    }

    emit enabledChanged();
}

void PoliceDroneTargetFollow::setCameraHFovDeg(double fovDeg)
{
    if ((fovDeg <= 0.0) || (fovDeg >= 180.0)) {
        qCWarning(PoliceDroneTargetFollowLog) << "ignoring out of range field of view:" << fovDeg;
        return;
    }

    if (!qFuzzyCompare(_cameraHFovDeg, fovDeg)) {
        _cameraHFovDeg = fovDeg;
        emit cameraHFovDegChanged();
    }
}

PoliceDroneTargetFollow::Block PoliceDroneTargetFollow::evaluate(const Conditions &conditions)
{
    if (!conditions.enabled) {
        return Block::Disabled;
    }

    // Ahead of the vehicle checks on purpose: a target lost while the link is also down must
    // still abort rather than wait for the vehicle to come back.
    if (conditions.msSinceTargetSeen > kTargetLostTimeoutMs) {
        return Block::TargetLost;
    }

    if (!conditions.vehicleValid) {
        return Block::NoVehicle;
    }
    if (!conditions.armed) {
        return Block::NotArmed;
    }
    if (!conditions.flying) {
        return Block::NotFlying;
    }
    if (!conditions.targetTracked) {
        return Block::NoTarget;
    }
    if (!conditions.geolocationValid) {
        return Block::NoGeolocation;
    }

    return Block::None;
}

QString PoliceDroneTargetFollow::blockText(Block block)
{
    switch (block) {
    case Block::None:
        return QString();
    case Block::Disabled:
        return tr("Target follow is off");
    case Block::TargetLost:
        return tr("Target lost, follow stopped");
    case Block::NoVehicle:
        return tr("No vehicle connected");
    case Block::NotArmed:
        return tr("Vehicle is not armed");
    case Block::NotFlying:
        return tr("Vehicle is not flying");
    case Block::NoTarget:
        return tr("Nothing is being tracked");
    case Block::NoGeolocation:
        return tr("Target position cannot be computed");
    }

    return QString();
}

PoliceDroneTargetFollow::Geolocation PoliceDroneTargetFollow::geolocateTarget(const SightLine &sight)
{
    Geolocation result;

    // An AMSL altitude is required: it is what carries into the follow target's altitude field.
    if (sight.vehicleCoordinate.type() != QGeoCoordinate::Coordinate3D) {
        return result;
    }
    if ((sight.hFovDeg <= 0.0) || (sight.hFovDeg >= 180.0) || (sight.vFovDeg <= 0.0) || (sight.vFovDeg >= 180.0)) {
        return result;
    }

    // Pinhole model: half a frame away from the centre is tan(fov / 2) in camera units.
    const double imageRight = (2.0 * sight.frameX - 1.0) * std::tan(qDegreesToRadians(sight.hFovDeg) / 2.0);
    const double imageDown = (2.0 * sight.frameY - 1.0) * std::tan(qDegreesToRadians(sight.vFovDeg) / 2.0);

    const double azimuth = qDegreesToRadians(sight.vehicleHeadingDeg + sight.gimbalYawDeg);
    const double elevation = qDegreesToRadians(sight.gimbalPitchDeg);
    const double sinAz = std::sin(azimuth);
    const double cosAz = std::cos(azimuth);
    const double sinEl = std::sin(elevation);
    const double cosEl = std::cos(elevation);

    // Camera basis in east/north/up: boresight, image right, image down. Image down is the
    // cross product of the other two, which for a level camera comes out as straight down.
    const double boresightE = sinAz * cosEl;
    const double boresightN = cosAz * cosEl;
    const double boresightU = sinEl;
    const double rightE = cosAz;
    const double rightN = -sinAz;
    const double downE = sinEl * sinAz;
    const double downN = sinEl * cosAz;
    const double downU = -cosEl;

    const double rayE = boresightE + (imageRight * rightE) + (imageDown * downE);
    const double rayN = boresightN + (imageRight * rightN) + (imageDown * downN);
    const double rayU = boresightU + (imageDown * downU);

    // The laser measures along the boresight, so it fixes the height of the ground under the
    // frame centre. Using it as the plane height rather than as a slant range to the target
    // keeps off-centre targets honest; without a reading the plane falls back to takeoff level.
    double heightAboveGround = sight.heightAboveGroundMeters;
    if (std::isfinite(sight.laserRangeMeters) && (sight.laserRangeMeters > 0.0)) {
        const double laserHeight = -sight.laserRangeMeters * boresightU;
        if (laserHeight > 0.0) {
            heightAboveGround = laserHeight;
        }
    }
    if (!std::isfinite(heightAboveGround) || (heightAboveGround < kMinHeightAboveGroundMeters)) {
        return result;
    }

    const double rayLength = std::sqrt((rayE * rayE) + (rayN * rayN) + (rayU * rayU));
    if (rayLength <= 0.0) {
        return result;
    }
    const double depressionDeg = qRadiansToDegrees(std::asin(-rayU / rayLength));
    if (depressionDeg < kMinDepressionDeg) {
        return result;
    }

    const double scale = heightAboveGround / -rayU;
    const double east = scale * rayE;
    const double north = scale * rayN;

    result.groundDistanceMeters = std::hypot(east, north);
    result.slantRangeMeters = scale * rayLength;
    result.coordinate = sight.vehicleCoordinate.atDistanceAndAzimuth(result.groundDistanceMeters,
                                                                    qRadiansToDegrees(std::atan2(east, north)),
                                                                    -heightAboveGround);
    result.valid = result.coordinate.isValid();

    return result;
}

PoliceDroneTargetFollow::SightLine PoliceDroneTargetFollow::_sightLine(Vehicle *vehicle,
                                                                      const SiyiAiController *tracker,
                                                                      const SiyiCameraController *camera) const
{
    SightLine sight;

    sight.frameX = tracker->targetCentreX();
    sight.frameY = tracker->targetCentreY();

    // Optical zoom divides the tangent of the half angle, and 16:9 with square pixels gives the
    // vertical half angle from the horizontal one.
    const double halfTan = std::tan(qDegreesToRadians(_cameraHFovDeg) / 2.0) / qMax(1.0, camera->zoomMultiple());
    sight.hFovDeg = qRadiansToDegrees(2.0 * std::atan(halfTan));
    sight.vFovDeg = qRadiansToDegrees(2.0 * std::atan(halfTan / kFrameAspect));

    sight.gimbalYawDeg = camera->yawDeg();
    sight.gimbalPitchDeg = camera->pitchDeg();
    sight.vehicleHeadingDeg = vehicle->heading()->rawValue().toDouble();
    sight.heightAboveGroundMeters = vehicle->altitudeRelative()->rawValue().toDouble();
    sight.laserRangeMeters = camera->rangefinderAvailable() ? camera->rangefinderDistance() : 0.0;
    sight.vehicleCoordinate = vehicle->coordinate();

    return sight;
}

void PoliceDroneTargetFollow::_publish()
{
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    const SiyiAiController *const tracker = SiyiAiController::instance();
    const SiyiCameraController *const camera = SiyiCameraController::instance();

    Conditions conditions;
    conditions.enabled = _enabled;
    conditions.vehicleValid = (vehicle != nullptr);
    conditions.armed = vehicle && vehicle->armed();
    conditions.flying = vehicle && vehicle->flying();
    conditions.targetTracked = tracker->hasTarget() && !tracker->targetLost();

    if (conditions.targetTracked) {
        _targetSeenTimer.restart();
    }
    conditions.msSinceTargetSeen = _targetSeenTimer.isValid() ? _targetSeenTimer.elapsed() : 0;

    Geolocation fix;
    if (vehicle && conditions.targetTracked) {
        fix = geolocateTarget(_sightLine(vehicle, tracker, camera));
        conditions.geolocationValid = fix.valid;
    }

    const Block block = evaluate(conditions);

    if (block == Block::TargetLost) {
        qCWarning(PoliceDroneTargetFollowLog) << "no target for" << conditions.msSinceTargetSeen
                                              << "ms, disabling follow";
        setEnabled(false);
        _setStatus(false, Block::TargetLost, QGeoCoordinate());
        return;
    }

    if (block != Block::None) {
        _setStatus(false, block, QGeoCoordinate());
        return;
    }

    FirmwarePlugin *const plugin = vehicle->firmwarePlugin();
    if (!plugin) {
        _setStatus(false, Block::NoVehicle, QGeoCoordinate());
        return;
    }

    FollowMe::GCSMotionReport report{};
    report.lat_int = static_cast<int>(fix.coordinate.latitude() * 1e7);
    report.lon_int = static_cast<int>(fix.coordinate.longitude() * 1e7);
    report.altMetersAMSL = fix.coordinate.altitude();

    // Position only: the target's velocity is not estimated, and its heading is unknown. The
    // remaining report fields stay zeroed so no stack reads a velocity we never measured.
    const uint8_t estimationCapabilities = (1 << FollowMe::POS);

    qCDebug(PoliceDroneTargetFollowLog) << "coordinate:" << fix.coordinate
                                        << "groundDistance:" << fix.groundDistanceMeters
                                        << "slantRange:" << fix.slantRangeMeters;

    plugin->sendGCSMotionReport(vehicle, report, estimationCapabilities);

    _setStatus(true, Block::None, fix.coordinate);
}

void PoliceDroneTargetFollow::_setStatus(bool following, Block block, const QGeoCoordinate &coordinate)
{
    if ((_following == following) && (_block == block) && (_targetCoordinate == coordinate)) {
        return;
    }

    _following = following;
    _block = block;
    _targetCoordinate = coordinate;

    emit statusChanged();
}
