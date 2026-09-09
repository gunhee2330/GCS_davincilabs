#include "ControllerBattery.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/QJSEngine>

#ifdef Q_OS_ANDROID
#include <QtCore/QTimer>

#include "AndroidInterface.h"
#endif

namespace {

#ifdef Q_OS_ANDROID
/// Polled, not subscribed. Android does deliver ACTION_BATTERY_CHANGED to a registered
/// receiver, but that wakes the app on every one percent and on every charger event for a
/// number that is only ever read off a bar - and it costs a receiver to register, hold and
/// unregister across the activity's lifetime. A sticky query on a timer costs one call.
/// Sixty seconds because a tablet does not move a percent faster than that, and a minute of
/// staleness on a controller battery changes no decision.
constexpr int kPollIntervalMsecs = 60 * 1000;
#endif

}  // namespace

Q_APPLICATION_STATIC(ControllerBattery, _controllerBatteryInstance, nullptr);

ControllerBattery::ControllerBattery(QObject* parent) : QObject(parent) {}

ControllerBattery::~ControllerBattery() = default;

ControllerBattery* ControllerBattery::instance()
{
    return _controllerBatteryInstance();
}

ControllerBattery* ControllerBattery::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    ControllerBattery* const battery = instance();
    QJSEngine::setObjectOwnership(battery, QJSEngine::CppOwnership);
    return battery;
}

void ControllerBattery::init()
{
#ifdef Q_OS_ANDROID
    QTimer* const timer = new QTimer(this);
    timer->setInterval(kPollIntervalMsecs);
    (void) connect(timer, &QTimer::timeout, this, &ControllerBattery::_refresh);
    timer->start();
    _refresh();
#endif
}

void ControllerBattery::_refresh()
{
#ifdef Q_OS_ANDROID
    const int percent = AndroidInterface::getBatteryPercent();
    if (percent != _percent) {
        _percent = percent;
        emit percentChanged();
    }
#endif
}
