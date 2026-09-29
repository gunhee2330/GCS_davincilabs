#include "PoliceRcLink.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/QJSEngine>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QtCore/QCoreApplication>
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <jni.h>
#endif

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(PoliceRcLinkLog, "PoliceDrone.RcLink")

namespace {

// ponytail: calibration knob. UniGCS 3.2.1 draws imgStrength as 0..100 (k/f.java b0, 20-wide bands);
// not yet measured on our handset. If the drawer's raw value lives elsewhere (dBm, 0..255), change these two.
constexpr int kStrengthMin = 0;
constexpr int kStrengthMax = 100;

#ifdef Q_OS_ANDROID
constexpr const char* kMonitorClass = "org/mavlink/qgroundcontrol/PoliceRcLinkMonitor";

// Binder threads: hop onto the object's thread.
void jniLinkInfo(JNIEnv*, jclass, jint strength, jint quality, jint validPercent)
{
    PoliceRcLink* const link = PoliceRcLink::instance();
    (void) QMetaObject::invokeMethod(link, [link, strength, quality, validPercent] {
        link->handleLinkInfo(strength, quality, validPercent);
    }, Qt::QueuedConnection);
}

void jniConnected(JNIEnv*, jclass, jboolean connected)
{
    PoliceRcLink* const link = PoliceRcLink::instance();
    const bool up = connected;
    (void) QMetaObject::invokeMethod(link, [link, up] { link->handleConnected(up); }, Qt::QueuedConnection);
}
#endif

}  // namespace

Q_APPLICATION_STATIC(PoliceRcLink, _policeRcLinkInstance, nullptr);

PoliceRcLink::PoliceRcLink(QObject* parent) : QObject(parent)
{
    _stale.setSingleShot(true);
    _stale.setInterval(kStaleMs);
    (void) connect(&_stale, &QTimer::timeout, this, [this] {
        if (_available) {
            _available = false;
            emit changed();
        }
    });
}

PoliceRcLink::~PoliceRcLink() = default;

PoliceRcLink* PoliceRcLink::instance()
{
    return _policeRcLinkInstance();
}

PoliceRcLink* PoliceRcLink::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    PoliceRcLink* const link = instance();
    QJSEngine::setObjectOwnership(link, QJSEngine::CppOwnership);
    return link;
}

int PoliceRcLink::percentFromStrength(int raw)
{
    if (raw < 0) {
        return -1;
    }
    return std::clamp((raw - kStrengthMin) * 100 / (kStrengthMax - kStrengthMin), 0, 100);
}

void PoliceRcLink::init()
{
#ifdef Q_OS_ANDROID
    const JNINativeMethod methods[]{
        {"nativeLinkInfo", "(III)V", reinterpret_cast<void*>(jniLinkInfo)},
        {"nativeConnected", "(Z)V", reinterpret_cast<void*>(jniConnected)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kMonitorClass, methods, std::size(methods))) {
        qCWarning(PoliceRcLinkLog) << "Failed to register native methods for" << kMonitorClass;
        return;
    }
    QJniObject::callStaticMethod<void>(kMonitorClass, "start", "(Landroid/content/Context;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
    if (env.checkAndClearExceptions()) {
        qCWarning(PoliceRcLinkLog) << "PoliceRcLinkMonitor.start threw";
    }
#endif
}

void PoliceRcLink::handleLinkInfo(int strength, int quality, int validPercent)
{
    _strength = strength;
    _quality = quality;
    _validPercent = validPercent;
    _available = strength >= 0;
    if (_available) {
        _stale.start();
    } else {
        _stale.stop();
    }
    emit changed();
}

void PoliceRcLink::handleConnected(bool connected)
{
    if (connected != _connected) {
        _connected = connected;
        emit changed();
    }
}
