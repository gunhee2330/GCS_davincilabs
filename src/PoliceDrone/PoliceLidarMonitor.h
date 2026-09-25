#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "MAVLinkMessageType.h"

class Vehicle;

/// \brief The rangefinder's own readings in raw metres, with a clock on each one.
///
/// The proximity displays used to read Vehicle's distance sensor fact group, which cannot answer
/// the two questions a single forward lidar makes urgent. Nothing there ever clears a fact or
/// drops telemetryAvailable, so a sensor that died in flight keeps its last reading on screen for
/// the rest of the sortie; and Fact::setRawValue emits nothing for an equal value, so a sensor
/// holding steady at 3.3 m is indistinguishable in QML from one that stopped talking. The fact
/// group also coalesces at 1 Hz and hands QML a value already converted to the app's distance
/// unit, while the warn bands are metres.
///
/// So this keeps its own copy: raw metres, never unit-converted, and a monotonic timestamp per
/// sector that a repeated identical frame refreshes. A sector silent for longer than
/// staleTimeoutMs goes back to NaN and stops counting towards fresh, which is what the displays
/// gate on.
class PoliceLidarMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("Vehicle.h")

    /// The vehicle whose DISTANCE_SENSOR traffic is watched. QML hands over
    /// multiVehicleManager.activeVehicle; a change or a null clears everything held. Setting one
    /// also asks the autopilot for DISTANCE_SENSOR at 5 Hz, once per Vehicle however many
    /// monitors watch it, and not before its initial connect sequence has finished. A lost link
    /// takes that request with it, and it goes out again when the link comes back.
    Q_PROPERTY(Vehicle* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)

    /// Eight raw metres, sector 0 the nose and 1..7 the yaw rotations in 45 degree steps. NaN
    /// where the sector has never reported or has gone stale.
    Q_PROPERTY(QVariantList sectorDistances READ sectorDistances NOTIFY sectorDistancesChanged)

    /// Sector 0, the only one this airframe carries a sensor for.
    Q_PROPERTY(double forwardDistance READ forwardDistance NOTIFY forwardDistanceChanged)

    /// True while any sector has been heard from inside staleTimeoutMs. A live sensor with
    /// nothing in range is fresh: it is the sensor being alive that this reports, not an obstacle.
    Q_PROPERTY(bool fresh READ fresh NOTIFY freshChanged)

    /// How long a sector's reading stands without a new frame. PX4 sends DISTANCE_SENSOR at
    /// 0.5 Hz on the radio link, so 3 s blanked the displays on a single dropped frame; 5 s rides
    /// out one drop and blanks only after two in a row, while a dead sensor still clears within
    /// 5 s. Raise it if the field link drops frames. Drops to 2 s, ten frames, once the vehicle
    /// reports DISTANCE_SENSOR at 5 Hz or faster; a vehicle that refused the request never says
    /// so and the 5 s stands. Back to 5 s on a lost link until the vehicle confirms again, and on
    /// a move to a vehicle that has not confirmed.
    Q_PROPERTY(int staleTimeoutMs READ staleTimeoutMs WRITE setStaleTimeoutMs NOTIFY staleTimeoutMsChanged)

public:
    explicit PoliceLidarMonitor(QObject* parent = nullptr);

    [[nodiscard]] Vehicle* vehicle() const { return _vehicle; }
    void setVehicle(Vehicle* vehicle);

    [[nodiscard]] QVariantList sectorDistances() const;
    [[nodiscard]] double forwardDistance() const { return _metres[0]; }
    [[nodiscard]] bool fresh() const { return _fresh; }
    [[nodiscard]] int staleTimeoutMs() const { return _staleTimeoutMs; }
    void setStaleTimeoutMs(int timeoutMs);

signals:
    void vehicleChanged();
    void sectorDistancesChanged();
    void forwardDistanceChanged();
    void freshChanged();
    void staleTimeoutMsChanged();

private slots:
    void _mavlinkMessageReceived(const mavlink_message_t& message);
    /// Drops whatever has fallen outside staleTimeoutMs. Runs on a timer and on every frame.
    void _dropStaleSectors();
    void _requestFastRate();
    void _communicationLostChanged(bool lost);

private:
    /// Stores \a metres and signals only if it is not what the sector already held, NaN included.
    void _setSectorMetres(int sector, double metres);
    void _updateFresh();

    static constexpr int kSectorCount = 8;
    static constexpr int kSlowStaleTimeoutMs = 5000;

    QPointer<Vehicle> _vehicle;
    double _metres[kSectorCount];
    /// Monotonic ms at the sector's last frame, -1 when never seen or dropped as stale.
    qint64 _lastSeenMs[kSectorCount];
    QElapsedTimer _clock;
    QTimer _staleTimer;
    int _staleTimeoutMs = kSlowStaleTimeoutMs;
    bool _fresh = false;
};
