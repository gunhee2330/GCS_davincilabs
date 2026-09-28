#pragma once

#include <QtCore/QFile>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(TakeoffCounterLog)

class QQmlEngine;
class QJSEngine;
class Vehicle;

/// \brief Lifetime takeoff count of the active vehicle, kept by the ground station.
///
/// The procurement spec wants the number of takeoffs readable in flight. Neither PX4 nor
/// ArduPilot reports a lifetime flight count over MAVLink, so the station keeps its own:
/// one entry per airframe in QSettings, keyed by the autopilot UID, incremented the first
/// time the vehicle is airborne in each arm cycle. It therefore counts flights this station
/// has watched, not flights the airframe has made under another controller.
///
/// The flight records themselves are kept without a limit in a JSON file per airframe under the
/// app's data location (recordsDirectory()), and each flight writes its own telemetry log, named
/// by its takeoff, from the first liftoff to the disarm.
class TakeoffCounter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int takeoffCount READ takeoffCount NOTIFY takeoffCountChanged)
    /// Duration of the last completed flight in seconds, -1 before this airframe has flown one
    /// under this station. Timed from the first liftoff of the arm cycle the count uses to its
    /// last landing, or to the disarm when that comes in the air.
    Q_PROPERTY(int lastFlightSeconds READ lastFlightSeconds NOTIFY lastFlightSecondsChanged)
    /// Completed flights of this airframe under this station, newest first, every one of them.
    /// Each is a map: takeoff (local ISO 8601 date and time of the first liftoff), seconds (as
    /// lastFlightSeconds) and metres (the vehicle's flightDistance when the flight was timed).
    /// Flights recorded since the records left the settings file also carry landing (local ISO
    /// 8601, the landing that timed it), maxAltitude (metres above home), vehicle (a label from the
    /// system id) and tlog (the file name of the flight's telemetry log in the telemetry folder),
    /// and vehicleLog once the records page has fetched the aircraft's own log.
    Q_PROPERTY(QVariantList flights READ flights NOTIFY flightsChanged)
    /// Local ISO 8601 date and time of this arm cycle's first liftoff, the very string its flight
    /// record keeps as takeoff. Empty until the liftoff, and again from the next arm.
    Q_PROPERTY(QString takeoffTime READ takeoffTime NOTIFY takeoffTimeChanged)

public:
    /// No default argument: a default-constructible QML_SINGLETON is default-constructed by the
    /// engine instead of going through create(), which hands QML a second counter that init()
    /// never followed a vehicle with — it reads zero for ever while the real one counts.
    explicit TakeoffCounter(QObject* parent);
    ~TakeoffCounter() override;

    static TakeoffCounter* instance();
    static TakeoffCounter* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Starts following the active vehicle. Call once, after MultiVehicleManager::init().
    void init();

    [[nodiscard]] int takeoffCount() const { return _count; }
    [[nodiscard]] int lastFlightSeconds() const { return _lastFlightSeconds; }
    [[nodiscard]] QVariantList flights() const { return _flights; }
    [[nodiscard]] QString takeoffTime() const { return _takeoffTime; }

    /// Where the per-airframe record files live: FlightRecords under the app's data location.
    static QString recordsDirectory();
    /// The records file of \a airframe (an airframeKey()), newest first; empty if there is none.
    /// \a ok, when given, is false when the file is there but could not be opened or parsed, which
    /// an empty list alone does not tell apart from no file.
    static QVariantList readFlights(const QString& airframe, bool* ok = nullptr);
    static bool writeFlights(const QString& airframe, const QVariantList& flights);
    /// Merges \a fields into the record of \a airframe that took off at \a takeoff, on disk.
    static bool updateFlight(const QString& airframe, const QString& takeoff, const QVariantMap& fields);
    /// The key an airframe's count and records are kept under: its autopilot UID, or its system
    /// id until the UID is known.
    static QString airframeKey(const Vehicle* vehicle);

signals:
    void takeoffCountChanged();
    void lastFlightSecondsChanged();
    void flightsChanged();
    void takeoffTimeChanged();

private slots:
    void _activeVehicleChanged(Vehicle* vehicle);
    void _armedChanged(bool armed);
    void _flyingChanged(bool flying);
    void _altitudeChanged(const QVariant& value);
    void _uidChanged();

private:
    void _follow(Vehicle* vehicle);
    void _markAirborne();
    /// Records how long and how far the arm cycle has flown, from its first liftoff to now. Landing
    /// and disarming both call it; a later landing in the same cycle overwrites an earlier one, in
    /// lastFlightSeconds and in the cycle's flight record alike, so a landed state that flickers
    /// still leaves the whole flight as one record. The latch is left to the arm edge.
    void _markLanded();
    void _load();
    void _store() const;
    [[nodiscard]] QString _settingsKey() const;
    /// Moves every airframe's flight list out of the settings file into its records file, once:
    /// a list is written to its file only while there is none, and dropped from the settings file
    /// only once written.
    static void _migrateSettings();
    /// The flight's telemetry log, opened at the first liftoff under the takeoff's name.
    void _openTlog();
    void _closeTlog();

    QPointer<Vehicle> _vehicle;
    int _count = 0;
    int _lastFlightSeconds = -1;
    QVariantList _flights;
    /// This arm cycle's flight is already the first record, so a later landing replaces it.
    bool _recordedThisCycle = false;
    /// Latched per arm cycle so a landed state that flickers, or an altitude that hovers
    /// around the threshold, still counts one takeoff.
    bool _airborneThisCycle = false;
    /// Wall clock at the first takeoff this arm cycle, 0 before it or when joined mid-flight.
    /// The station's own clock rather than a vehicle fact, for the same lifetime reason the
    /// dashboard times flights off its own: a Fact handed out by getFact() is destructible from QML.
    qint64 _airborneSinceMs = 0;
    /// _airborneSinceMs as takeoffTime shows it. Unlike that, kept past the disarm.
    QString _takeoffTime;
    /// In the air right now: set on the first liftoff and on every flying edge, cleared on landing.
    /// Not on altitude alone after that, since ground above home reads over 2 m. What keeps a
    /// disarm after the landing from timing the wait on the pad as flight.
    bool _inAir = false;
    /// Highest relative altitude since this arm cycle's first liftoff, in metres.
    double _maxAltitudeM = 0;
    /// This cycle's telemetry log: every MAVLink message received, stamped the way the stock
    /// .tlog is, from the first liftoff to the disarm.
    QFile _tlog;
    QMetaObject::Connection _tlogConnection;
    /// _tlog's file name while this cycle has one, for the flight record.
    QString _tlogName;
};
