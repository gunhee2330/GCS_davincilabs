#include "DeveloperPin.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/QJSEngine>

// Set per build in src/PoliceDrone/CMakeLists.txt; this is only what a build that set nothing gets.
#ifndef QGC_DEVELOPER_PIN
#define QGC_DEVELOPER_PIN "000000"
#endif

namespace {

constexpr const char *kSettingsGroup = "PoliceDrone/DeveloperPin";

}  // namespace

Q_APPLICATION_STATIC(DeveloperPin, _developerPinInstance, nullptr);

DeveloperPin::DeveloperPin(QObject *parent)
    : PinGate(QLatin1String(kSettingsGroup), QLatin1String(QGC_DEVELOPER_PIN), parent)
{
}

DeveloperPin::~DeveloperPin() = default;

DeveloperPin *DeveloperPin::instance()
{
    return _developerPinInstance();
}

DeveloperPin *DeveloperPin::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    DeveloperPin *const pin = instance();
    QJSEngine::setObjectOwnership(pin, QJSEngine::CppOwnership);
    return pin;
}

bool DeveloperPin::verify(const QString &pin)
{
    return _verify(pin);
}
