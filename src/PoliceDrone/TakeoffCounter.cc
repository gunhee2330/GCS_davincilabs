#include "TakeoffCounter.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(TakeoffCounterLog, "PoliceDrone.TakeoffCounter")

namespace {

constexpr const char* kSettingsGroup = "PoliceDrone/TakeoffCount";

/// Suffix on the airframe's own key, so the duration sits beside the count in the same group.
/// A suffix rather than a child key: QSettings would then have to hold "uid-abc" as both a
/// value and a group, which not every backend keeps.
constexpr const char* kLastFlightSuffix = "-lastFlightSeconds";

/// Relative altitude that counts as airborne when the autopilot never sends a landed state.
/// Two metres clears barometric drift on the pad without waiting for cruise height.
constexpr double kAirborneAltitudeM = 2.0;

}  // namespace

Q_APPLICATION_STATIC(TakeoffCounter, _takeoffCounterInstance, nullptr);

TakeoffCounter::TakeoffCounter(QObject* parent) : QObject(parent) {}

TakeoffCounter::~TakeoffCounter() = default;

TakeoffCounter* TakeoffCounter::instance()
{
    return _takeoffCounterInstance();
}

TakeoffCounter* TakeoffCounter::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    TakeoffCounter* const counter = instance();
    QJSEngine::setObjectOwnership(counter, QJSEngine::CppOwnership);
    return counter;
}

void TakeoffCounter::init()
{
    MultiVehicleManager* const manager = MultiVehicleManager::instance();
    (void) connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &TakeoffCounter::_activeVehicleChanged);
    _follow(manager->activeVehicle());
}

void TakeoffCounter::_activeVehicleChanged(Vehicle* vehicle)
{
    _follow(vehicle);
}

void TakeoffCounter::_follow(Vehicle* vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
        (void) disconnect(_vehicle->altitudeRelative(), nullptr, this, nullptr);
    }

    _vehicle = vehicle;
    if (!_vehicle) {
        if (_count != 0) {
            _count = 0;
            emit takeoffCountChanged();
        }
        if (_lastFlightSeconds != -1) {
            _lastFlightSeconds = -1;
            emit lastFlightSecondsChanged();
        }
        return;
    }

    (void) connect(_vehicle, &Vehicle::armedChanged, this, &TakeoffCounter::_armedChanged);
    (void) connect(_vehicle, &Vehicle::flyingChanged, this, &TakeoffCounter::_flyingChanged);
    (void) connect(_vehicle, &Vehicle::vehicleUIDChanged, this, &TakeoffCounter::_uidChanged);
    (void) connect(_vehicle->altitudeRelative(), &Fact::rawValueChanged, this, &TakeoffCounter::_altitudeChanged);

    // Joining a vehicle already in the air is not a takeoff this station saw.
    _airborneThisCycle = _vehicle->flying();
    // Nor is its duration known: the liftoff instant is behind us. Timing it from here would
    // report a flight far shorter than the one actually flown.
    _airborneSinceMs = 0;
    _load();
}

void TakeoffCounter::_armedChanged(bool armed)
{
    // Arming opens a cycle and disarming closes one; the latch drops either way so the next
    // liftoff counts. Disarming is also a landing for an airframe that never sent one.
    Q_UNUSED(armed);
    _markLanded();
}

void TakeoffCounter::_flyingChanged(bool flying)
{
    if (flying) {
        _markAirborne();
    } else {
        _markLanded();
    }
}

void TakeoffCounter::_altitudeChanged(const QVariant& value)
{
    if (_vehicle && _vehicle->armed() && (value.toDouble() > kAirborneAltitudeM)) {
        _markAirborne();
    }
}

void TakeoffCounter::_markAirborne()
{
    if (_airborneThisCycle) {
        return;
    }

    _airborneThisCycle = true;
    _airborneSinceMs = QDateTime::currentMSecsSinceEpoch();
    ++_count;
    _store();
    emit takeoffCountChanged();
    qCDebug(TakeoffCounterLog) << "takeoff" << _count << "for" << _settingsKey();
}

void TakeoffCounter::_markLanded()
{
    const bool wasAirborne = _airborneThisCycle;
    const qint64 sinceMs = _airborneSinceMs;
    _airborneThisCycle = false;
    _airborneSinceMs = 0;

    // Nothing to time for a cycle that never left the ground, or for a vehicle this station
    // joined mid-flight.
    if (!wasAirborne || (sinceMs == 0)) {
        return;
    }

    _lastFlightSeconds = static_cast<int>((QDateTime::currentMSecsSinceEpoch() - sinceMs) / 1000);
    _store();
    emit lastFlightSecondsChanged();
    qCDebug(TakeoffCounterLog) << "flight of" << _lastFlightSeconds << "s for" << _settingsKey();
}

void TakeoffCounter::_uidChanged()
{
    // AUTOPILOT_VERSION lands after the vehicle is already active, so the count may have been
    // read under the system-id fallback. Re-read under the airframe's own key.
    _load();
}

QString TakeoffCounter::_settingsKey() const
{
    if (!_vehicle) {
        return QString();
    }
    if (_vehicle->vehicleUID() != 0) {
        return QStringLiteral("uid-%1").arg(QString::number(_vehicle->vehicleUID(), 16));
    }
    return QStringLiteral("sysid-%1").arg(_vehicle->id());
}

void TakeoffCounter::_load()
{
    const QString key = _settingsKey();
    if (key.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const int count = settings.value(key, 0).toInt();
    if (count != _count) {
        _count = count;
        emit takeoffCountChanged();
    }
    const int lastFlight = settings.value(key + QLatin1String(kLastFlightSuffix), -1).toInt();
    if (lastFlight != _lastFlightSeconds) {
        _lastFlightSeconds = lastFlight;
        emit lastFlightSecondsChanged();
    }
}

void TakeoffCounter::_store() const
{
    const QString key = _settingsKey();
    if (key.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(key, _count);
    if (_lastFlightSeconds >= 0) {
        settings.setValue(key + QLatin1String(kLastFlightSuffix), _lastFlightSeconds);
    }
}
