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

public:
    explicit TakeoffCounter(QObject* parent = nullptr);
    ~TakeoffCounter() override;

    static TakeoffCounter* instance();
    static TakeoffCounter* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Starts following the active vehicle. Call once, after MultiVehicleManager::init().
    void init();

    [[nodiscard]] int takeoffCount() const { return _count; }

signals:
    void takeoffCountChanged();

private slots:
    void _activeVehicleChanged(Vehicle* vehicle);
    void _armedChanged(bool armed);
    void _flyingChanged(bool flying);
    void _altitudeChanged(const QVariant& value);
    void _uidChanged();

private:
    void _follow(Vehicle* vehicle);
    void _markAirborne();
    void _load();
    void _store() const;
    [[nodiscard]] QString _settingsKey() const;

    QPointer<Vehicle> _vehicle;
    int _count = 0;
    /// Latched per arm cycle so a landed state that flickers, or an altitude that hovers
    /// around the threshold, still counts one takeoff.
    bool _airborneThisCycle = false;
};
