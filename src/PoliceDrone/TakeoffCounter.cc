#include "TakeoffCounter.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QtEndian>
#include <QtQml/QJSEngine>

#include <cmath>

#include "AppMessages.h"
#include "AppSettings.h"
#include "Fact.h"
#include "MAVLinkLib.h"
#include "MAVLinkProtocol.h"
#include "MAVLinkSigning.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(TakeoffCounterLog, "PoliceDrone.TakeoffCounter")

namespace {

constexpr const char* kSettingsGroup = "PoliceDrone/TakeoffCount";

/// Suffix on the airframe's own key, so the duration sits beside the count in the same group.
/// A suffix rather than a child key: QSettings would then have to hold "uid-abc" as both a
/// value and a group, which not every backend keeps.
constexpr const char* kLastFlightSuffix = "-lastFlightSeconds";

/// Where the flight log sat beside them, as a JSON array, before it moved to its own file.
constexpr const char* kFlightsSuffix = "-flights";

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
    _migrateSettings();

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
    _closeTlog();
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
        (void) disconnect(_vehicle->altitudeRelative(), nullptr, this, nullptr);
    }

    _vehicle = vehicle;
    if (!_takeoffTime.isEmpty()) {
        _takeoffTime.clear();
        emit takeoffTimeChanged();
    }
    if (!_vehicle) {
        if (_count != 0) {
            _count = 0;
            emit takeoffCountChanged();
        }
        if (_lastFlightSeconds != -1) {
            _lastFlightSeconds = -1;
            emit lastFlightSecondsChanged();
        }
        if (!_flights.isEmpty()) {
            _flights.clear();
            emit flightsChanged();
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
    _recordedThisCycle = false;
    _load();
}

void TakeoffCounter::_armedChanged(bool armed)
{
    // Arming opens a cycle and disarming closes one; the latch drops either way so the next
    // liftoff counts. Disarming in the air is also a landing, for an airframe that never sends one.
    _markLanded();
    _closeTlog();
    _airborneThisCycle = false;
    _airborneSinceMs = 0;
    _recordedThisCycle = false;
    if (armed && !_takeoffTime.isEmpty()) {
        _takeoffTime.clear();
        emit takeoffTimeChanged();
    }
}

void TakeoffCounter::_flyingChanged(bool flying)
{
    if (flying) {
        _inAir = true;
        _markAirborne();
    } else {
        _markLanded();
    }
}

void TakeoffCounter::_altitudeChanged(const QVariant& value)
{
    const double metres = value.toDouble();
    if (_vehicle && _vehicle->armed() && (metres > kAirborneAltitudeM)) {
        _markAirborne();
    }
    if (_airborneThisCycle && std::isfinite(metres)) {
        _maxAltitudeM = qMax(_maxAltitudeM, metres);
    }
}

void TakeoffCounter::_markAirborne()
{
    if (_airborneThisCycle) {
        return;
    }

    _inAir = true;
    _airborneThisCycle = true;
    _airborneSinceMs = QDateTime::currentMSecsSinceEpoch();
    _takeoffTime = QDateTime::fromMSecsSinceEpoch(_airborneSinceMs).toString(Qt::ISODate);
    // The altitude at the liftoff counts: a hover that holds one height sends no change after it.
    const double metres = _vehicle->altitudeRelative()->rawValue().toDouble();
    _maxAltitudeM = std::isfinite(metres) ? qMax(metres, 0.0) : 0.0;
    _openTlog();
    ++_count;
    _store();
    emit takeoffCountChanged();
    emit takeoffTimeChanged();
    qCDebug(TakeoffCounterLog) << "takeoff" << _count << "for" << _settingsKey();
}

void TakeoffCounter::_markLanded()
{
    const bool wasInAir = _inAir;
    _inAir = false;

    // Nothing to time for a vehicle already on the ground, or for one this station joined
    // mid-flight. The latch and the liftoff time stay: a landing that flickers is not the end
    // of the cycle, so the next one is timed from the same liftoff.
    if (!wasInAir || (_airborneSinceMs == 0)) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    _lastFlightSeconds = static_cast<int>((now.toMSecsSinceEpoch() - _airborneSinceMs) / 1000);

    // flightDistance is the vehicle's own count since it armed; it is zeroed by the next arm, not
    // by this landing, so a flicker's later landing reads the whole cycle's distance.
    QVariantMap flight{
        { QStringLiteral("takeoff"),     _takeoffTime },
        { QStringLiteral("seconds"),     _lastFlightSeconds },
        { QStringLiteral("metres"),      _vehicle->flightDistance()->rawValue().toDouble() },
        { QStringLiteral("landing"),     now.toString(Qt::ISODate) },
        { QStringLiteral("maxAltitude"), _maxAltitudeM },
        { QStringLiteral("vehicle"),     tr("%1호기").arg(_vehicle->id()) },
    };
    if (!_tlogName.isEmpty()) {
        flight.insert(QStringLiteral("tlog"), _tlogName);
    }

    // Read back rather than taken from memory: the records page writes the vehicle log it fetches
    // into this file, and a list held since the load would drop it.
    const QString key = _settingsKey();
    bool read = false;
    const QVariantList stored = readFlights(key, &read);
    if (read) {
        _flights = stored;
    } else {
        // Never written over: the file holds every earlier flight. This one is kept in memory only.
        // ponytail: a file that never parses again keeps this airframe's new flights off disk until
        // it is moved away; set it aside and start a new one if that is ever seen.
        qCWarning(TakeoffCounterLog) << "could not read the flight records of" << key << "so this flight is not saved to them";
    }
    if (_recordedThisCycle && !_flights.isEmpty()) {
        QVariantMap merged = _flights.first().toMap();
        merged.insert(flight);
        _flights[0] = merged;
    } else {
        _flights.prepend(flight);
        _recordedThisCycle = true;
    }
    if (read && !writeFlights(key, _flights)) {
        qCWarning(TakeoffCounterLog) << "could not write the flight records of" << key;
    }

    _store();
    emit lastFlightSecondsChanged();
    emit flightsChanged();
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
    return airframeKey(_vehicle);
}

QString TakeoffCounter::airframeKey(const Vehicle* vehicle)
{
    if (!vehicle) {
        return QString();
    }
    if (vehicle->vehicleUID() != 0) {
        return QStringLiteral("uid-%1").arg(QString::number(vehicle->vehicleUID(), 16));
    }
    return QStringLiteral("sysid-%1").arg(vehicle->id());
}

QString TakeoffCounter::recordsDirectory()
{
    // Test runs keep them in their own save folder, which the run removes when it exits
    const QString root = QGC::runningUnitTests() ? SettingsManager::instance()->appSettings()->savePath()->rawValue().toString()
                                                 : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(root).filePath(QStringLiteral("FlightRecords"));
}

QVariantList TakeoffCounter::readFlights(const QString& airframe, bool* ok)
{
    QFile file(QDir(recordsDirectory()).filePath(airframe + QStringLiteral(".json")));
    // No file is an airframe with no records yet; a file that will not open or parse is not.
    bool read = airframe.isEmpty() || !file.exists();
    QVariantList flights;
    if (!read && file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        read = (error.error == QJsonParseError::NoError) && document.isArray();
        flights = document.array().toVariantList();
    }
    if (ok) {
        *ok = read;
    }
    return flights;
}

bool TakeoffCounter::writeFlights(const QString& airframe, const QVariantList& flights)
{
    if (airframe.isEmpty() || !QDir().mkpath(recordsDirectory())) {
        return false;
    }
    // Written aside and renamed over the old file, so a crash mid-write leaves the last good list.
    QSaveFile file(QDir(recordsDirectory()).filePath(airframe + QStringLiteral(".json")));
    return file.open(QIODevice::WriteOnly) &&
           (file.write(QJsonDocument(QJsonArray::fromVariantList(flights)).toJson(QJsonDocument::Compact)) >= 0) &&
           file.commit();
}

bool TakeoffCounter::updateFlight(const QString& airframe, const QString& takeoff, const QVariantMap& fields)
{
    QVariantList flights = readFlights(airframe);
    for (QVariant& entry : flights) {
        QVariantMap flight = entry.toMap();
        if (flight.value(QStringLiteral("takeoff")).toString() == takeoff) {
            flight.insert(fields);
            entry = flight;
            return writeFlights(airframe, flights);
        }
    }
    return false;
}

void TakeoffCounter::_migrateSettings()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        if (!key.endsWith(QLatin1String(kFlightsSuffix))) {
            continue;
        }
        const QString airframe = key.chopped(static_cast<int>(qstrlen(kFlightsSuffix)));
        if (QFile::exists(QDir(recordsDirectory()).filePath(airframe + QStringLiteral(".json")))) {
            continue;
        }
        const QVariantList flights = QJsonDocument::fromJson(settings.value(key).toByteArray()).array().toVariantList();
        if (writeFlights(airframe, flights)) {
            settings.remove(key);
            qCDebug(TakeoffCounterLog) << "moved" << flights.size() << "flight records of" << airframe << "to their file";
        }
    }
}

void TakeoffCounter::_openTlog()
{
    _closeTlog();
    _tlogName.clear();

    const QString dir = SettingsManager::instance()->appSettings()->telemetrySavePath();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        return;
    }
    const QString name = QDateTime::fromMSecsSinceEpoch(_airborneSinceMs).toString(QStringLiteral("yyyyMMdd_hhmmss")) + QStringLiteral(".tlog");
    _tlog.setFileName(QDir(dir).filePath(name));
    // NewOnly: a file of that name is some other flight's, and is left alone.
    if (!_tlog.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qCWarning(TakeoffCounterLog) << "could not open the flight's telemetry log" << _tlog.fileName() << _tlog.errorString();
        return;
    }
    _tlogName = name;

    // The stock connection log's record, which every tlog reader expects: a big-endian microsecond
    // time, then the packet with its signature stripped. SETUP_SIGNING carries the key and is skipped.
    _tlogConnection = connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived, this,
                              [this](LinkInterface*, const mavlink_message_t& message) {
        if (message.msgid == MAVLINK_MSG_ID_SETUP_SIGNING) {
            return;
        }
        QByteArray record(sizeof(quint64), Qt::Uninitialized);
        qToBigEndian(static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000, record.data());
        record.append(MAVLinkSigning::serializeUnsignedCopy(message));
        (void) _tlog.write(record);
    });
}

void TakeoffCounter::_closeTlog()
{
    (void) disconnect(_tlogConnection);
    if (_tlog.isOpen()) {
        _tlog.close();
    }
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
    const QVariantList flights = readFlights(key);
    if (flights != _flights) {
        _flights = flights;
        emit flightsChanged();
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
