#include "PoliceLidarMonitor.h"

#include <QtCore/QtNumeric>

#include "MAVLinkLib.h"
#include "Vehicle.h"

namespace {

/// MAV_SENSOR_ORIENTATION puts the eight yaw rotations first, NONE=0 through YAW_315=7, and
/// everything above them is a roll or pitch mounting the ring has no sector for.
constexpr uint8_t kLastYawOrientation = MAV_SENSOR_ROTATION_YAW_315;

/// Staleness re-check interval. Well inside the shortest timeout worth setting, and cheap: it
/// walks eight numbers.
constexpr int kStaleCheckMs = 250;

/// NaN never compares equal to itself, so a plain != would report a change on every stale
/// sector, every tick.
bool differs(double a, double b)
{
    if (qIsNaN(a) || qIsNaN(b)) {
        return qIsNaN(a) != qIsNaN(b);
    }
    return a != b;
}

}  // namespace

PoliceLidarMonitor::PoliceLidarMonitor(QObject* parent) : QObject(parent)
{
    for (int sector = 0; sector < kSectorCount; ++sector) {
        _metres[sector] = qQNaN();
        _lastSeenMs[sector] = -1;
    }

    _clock.start();
    _staleTimer.setInterval(kStaleCheckMs);
    (void) connect(&_staleTimer, &QTimer::timeout, this, &PoliceLidarMonitor::_dropStaleSectors);
    _staleTimer.start();
}

void PoliceLidarMonitor::setVehicle(Vehicle* vehicle)
{
    // A QPointer nulls itself the moment the vehicle is destroyed, so the null QML delivers next
    // can match what is held while that airframe's readings still stand: fall through and clear
    // them rather than waiting for staleness to notice.
    if ((_vehicle == vehicle) && (vehicle || !_fresh)) {
        return;
    }

    if (_vehicle) {
        (void) disconnect(_vehicle, &Vehicle::mavlinkMessageReceived,
                          this, &PoliceLidarMonitor::_mavlinkMessageReceived);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::mavlinkMessageReceived,
                       this, &PoliceLidarMonitor::_mavlinkMessageReceived);
    }

    // Another airframe's readings are not this one's, and a link that went away leaves no sensor
    // behind: everything goes back to unseen rather than standing as the new vehicle's numbers.
    for (int sector = 0; sector < kSectorCount; ++sector) {
        _lastSeenMs[sector] = -1;
        _setSectorMetres(sector, qQNaN());
    }
    _updateFresh();

    emit vehicleChanged();
}

QVariantList PoliceLidarMonitor::sectorDistances() const
{
    QVariantList distances;
    distances.reserve(kSectorCount);
    for (const double metres : _metres) {
        distances.append(metres);
    }
    return distances;
}

void PoliceLidarMonitor::setStaleTimeoutMs(int timeoutMs)
{
    if (_staleTimeoutMs == timeoutMs) {
        return;
    }
    _staleTimeoutMs = timeoutMs;
    emit staleTimeoutMsChanged();

    // A shortened timeout takes effect now rather than at the next tick.
    _dropStaleSectors();
}

void PoliceLidarMonitor::_mavlinkMessageReceived(const mavlink_message_t& message)
{
    if (message.msgid != MAVLINK_MSG_ID_DISTANCE_SENSOR) {
        return;
    }

    mavlink_distance_sensor_t distanceSensor{};
    mavlink_msg_distance_sensor_decode(&message, &distanceSensor);

    // A downward or custom-mounted rangefinder is not one of the eight sectors and must not stand
    // in for one: a live ground sensor would otherwise keep the ring alive on a forward reading
    // that stopped arriving.
    if (distanceSensor.orientation > kLastYawOrientation) {
        return;
    }

    const int sector = distanceSensor.orientation;

    // The frame itself is the receipt, whatever it carries: a sensor holding steady at one
    // distance is alive, and that is the case the fact group could not report.
    _lastSeenMs[sector] = _clock.elapsed();

    // min_distance and max_distance are deliberately not consulted. A reading below the minimum
    // is still a close object and has to draw, and one above the maximum is past the warn band
    // anyway. signal_quality is ignored too - PX4's tfmini reports 0, meaning unknown, always.
    // A zero distance is the autopilot saying it has no reading, not an obstacle at the nose.
    _setSectorMetres(sector, (distanceSensor.current_distance == 0)
                                 ? qQNaN()
                                 : (distanceSensor.current_distance / 100.0));

    _dropStaleSectors();
}

void PoliceLidarMonitor::_dropStaleSectors()
{
    const qint64 now = _clock.elapsed();
    for (int sector = 0; sector < kSectorCount; ++sector) {
        if ((_lastSeenMs[sector] >= 0) && ((now - _lastSeenMs[sector]) > _staleTimeoutMs)) {
            _lastSeenMs[sector] = -1;
            _setSectorMetres(sector, qQNaN());
        }
    }
    _updateFresh();
}

void PoliceLidarMonitor::_setSectorMetres(int sector, double metres)
{
    if (!differs(_metres[sector], metres)) {
        return;
    }

    _metres[sector] = metres;
    emit sectorDistancesChanged();
    if (sector == 0) {
        emit forwardDistanceChanged();
    }
}

void PoliceLidarMonitor::_updateFresh()
{
    bool fresh = false;
    for (const qint64 lastSeenMs : _lastSeenMs) {
        if (lastSeenMs >= 0) {
            fresh = true;
            break;
        }
    }

    if (fresh != _fresh) {
        _fresh = fresh;
        emit freshChanged();
    }
}
