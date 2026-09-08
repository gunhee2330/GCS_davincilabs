#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtPositioning/QGeoCoordinate>

Q_DECLARE_LOGGING_CATEGORY(PoliceDroneTargetFollowLog)

class SiyiAiController;
class SiyiCameraController;
class Vehicle;

/// \brief Publishes the tracked target's ground position so the aircraft can follow it.
///
/// Route B for aircraft follow: the tracked box is turned into a ground coordinate and handed to
/// the firmware plugin's follow-target sender, which both supported stacks consume in their follow
/// flight mode. Route A, which is what ships, does not go through here at all — the gimbal has an
/// undocumented follow command after all (0xC3, one payload byte, on the gimbal's own short-frame
/// link) and the SIYI air unit flies it, leaving the GCS only the flight mode to set. So the claim
/// this file used to make, that the SDK carries no command for follow, is simply false.
///
/// The safety gate lives in this class rather than in the UI: nothing is published unless the
/// operator enabled it, the vehicle is armed and flying, and the tracker reported the target
/// within the last two seconds. Losing the target for longer disables the publisher outright,
/// so re-following is always a deliberate operator action. The class never changes flight mode;
/// putting the aircraft into its follow mode stays an operator action too.
/// NOT SHIPPED. Deliberately unreachable: no QML registration, and no create()/instance() pair
/// either, so registering it is a deliberate act rather than one forgotten line. The geolocation
/// and its tests are kept because they are the part worth keeping, but they are not flight-ready:
/// the gimbal yaw sign has never been checked against a real pod, the ray is intersected with a
/// horizontal plane through whatever the laser last measured at frame centre (wrong on a slope,
/// on a building, or for a target near the frame edge), and the sender it would call reports
/// nothing back about whether the aircraft accepted anything. Before this may command an
/// aircraft, verify the pod's own target coordinate (SIYI 0x17) against a surveyed point and
/// make that the source instead.
class PoliceDroneTargetFollow : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool             enabled             READ enabled        WRITE setEnabled    NOTIFY enabledChanged)
    Q_PROPERTY(bool             following           READ following                          NOTIFY statusChanged)
    Q_PROPERTY(QString          notFollowingReason  READ notFollowingReason                 NOTIFY statusChanged)
    Q_PROPERTY(QGeoCoordinate   targetCoordinate    READ targetCoordinate                   NOTIFY statusChanged)

    /// Horizontal field of view of the pod's video at 1x zoom, in degrees. Writable because the
    /// geolocation scales with it and the datasheet figure is only a starting point: lens
    /// tolerance and whichever sensor the AI module is fed both move it.
    Q_PROPERTY(double cameraHFovDeg READ cameraHFovDeg WRITE setCameraHFovDeg NOTIFY cameraHFovDegChanged)

public:
    explicit PoliceDroneTargetFollow(QObject *parent = nullptr);

    /// Everything geolocation needs, gathered so it can be computed without a vehicle.
    ///
    /// Frame coordinates are normalised 0..1 with the origin at the top left, matching what the
    /// AI controller reports. Angles are degrees. Two conventions are assumed and must hold for
    /// the result to mean anything:
    /// - \a gimbalYawDeg is measured from the airframe nose (SIYI reports the pod's own frame),
    ///   so the vehicle heading is added to it here.
    /// - \a gimbalPitchDeg is positive up, so looking down at the ground is negative.
    /// Camera roll is taken as zero: the pod stabilises roll, and so is the airframe's own roll
    /// and pitch, which the stabilised gimbal absorbs.
    struct SightLine {
        double frameX = 0.5;
        double frameY = 0.5;
        double hFovDeg = 0.0;
        double vFovDeg = 0.0;
        double gimbalYawDeg = 0.0;
        double gimbalPitchDeg = 0.0;
        double vehicleHeadingDeg = 0.0;
        double heightAboveGroundMeters = 0.0;   ///< AGL, i.e. above the takeoff altitude plane
        double laserRangeMeters = 0.0;          ///< <= 0 or non-finite when the LRF has no reading
        QGeoCoordinate vehicleCoordinate;       ///< must carry an AMSL altitude
    };

    struct Geolocation {
        QGeoCoordinate coordinate;              ///< AMSL altitude of the assumed ground plane
        double groundDistanceMeters = 0.0;
        double slantRangeMeters = 0.0;
        bool valid = false;
    };

    /// What is stopping the publisher, in the order the gate tests them.
    enum class Block {
        None,
        Disabled,
        TargetLost,     ///< aborting: no target for longer than the timeout
        NoVehicle,
        NotArmed,
        NotFlying,
        NoTarget,       ///< nothing tracked right now, but still inside the timeout
        NoGeolocation
    };
    Q_ENUM(Block)

    /// The gate's whole input, so it can be tested without a vehicle or a tracker.
    struct Conditions {
        bool enabled = false;
        bool vehicleValid = false;
        bool armed = false;
        bool flying = false;
        bool targetTracked = false;
        bool geolocationValid = false;
        qint64 msSinceTargetSeen = 0;
    };

    /// Intersects the camera ray with the ground. With a laser range the ground plane is put at
    /// the height that range implies under the boresight; without one it sits at the aircraft's
    /// takeoff altitude. Returns an invalid result rather than a nonsense coordinate when the ray
    /// runs at or above the horizon, when the aircraft is too low for a plane to mean anything,
    /// or when the inputs are out of range.
    [[nodiscard]] static Geolocation geolocateTarget(const SightLine &sight);

    [[nodiscard]] static Block evaluate(const Conditions &conditions);
    [[nodiscard]] static QString blockText(Block block);

    [[nodiscard]] bool enabled() const { return _enabled; }
    [[nodiscard]] bool following() const { return _following; }
    [[nodiscard]] QString notFollowingReason() const { return blockText(_block); }
    [[nodiscard]] QGeoCoordinate targetCoordinate() const { return _targetCoordinate; }
    [[nodiscard]] double cameraHFovDeg() const { return _cameraHFovDeg; }

    void setEnabled(bool enabled);
    void setCameraHFovDeg(double fovDeg);

signals:
    void enabledChanged();
    void statusChanged();
    void cameraHFovDegChanged();

private slots:
    void _publish();

private:
    [[nodiscard]] SightLine _sightLine(Vehicle *vehicle, const SiyiAiController *tracker,
                                       const SiyiCameraController *camera) const;
    void _setStatus(bool following, Block block, const QGeoCoordinate &coordinate);

    QTimer _publishTimer;
    QElapsedTimer _targetSeenTimer;

    bool _enabled = false;
    bool _following = false;
    Block _block = Block::Disabled;
    QGeoCoordinate _targetCoordinate;

    /// ZT30 zoom camera at 1x, from the pod datasheet. See cameraHFovDeg.
    double _cameraHFovDeg = 71.4;
};
