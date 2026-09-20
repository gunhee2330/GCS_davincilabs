#pragma once

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
class TakeoffCounter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int takeoffCount READ takeoffCount NOTIFY takeoffCountChanged)
    /// Duration of the last completed flight in seconds, -1 before this airframe has flown one
    /// under this station. Written on the landing edge of the same arm cycle the count uses.
    Q_PROPERTY(int lastFlightSeconds READ lastFlightSeconds NOTIFY lastFlightSecondsChanged)

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

signals:
    void takeoffCountChanged();
    void lastFlightSecondsChanged();

private slots:
    void _activeVehicleChanged(Vehicle* vehicle);
    void _armedChanged(bool armed);
    void _flyingChanged(bool flying);
    void _altitudeChanged(const QVariant& value);
    void _uidChanged();

private:
    void _follow(Vehicle* vehicle);
    void _markAirborne();
    /// Closes the airborne cycle and records how long it lasted. Landing and disarming both
    /// end a flight, so both call it.
    void _markLanded();
    void _load();
    void _store() const;
    [[nodiscard]] QString _settingsKey() const;

    QPointer<Vehicle> _vehicle;
    int _count = 0;
    int _lastFlightSeconds = -1;
    /// Latched per arm cycle so a landed state that flickers, or an altitude that hovers
    /// around the threshold, still counts one takeoff.
    bool _airborneThisCycle = false;
    /// Wall clock at the takeoff this cycle, 0 when not airborne. The station's own clock
    /// rather than a vehicle fact, for the same lifetime reason the dashboard times flights
    /// off its own: a Fact handed out by getFact() is destructible from QML.
    qint64 _airborneSinceMs = 0;
};
