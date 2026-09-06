#include "PoliceGimbalJoystick.h"

#include <QtCore/QApplicationStatic>

#include "Joystick.h"
#include "JoystickManager.h"
#include "QGCLoggingCategory.h"
#include "SiyiAiController.h"
#include "SiyiCameraController.h"
#include "SiyiProtocol.h"

QGC_LOGGING_CATEGORY(PoliceGimbalJoystickLog, "PoliceDrone.GimbalJoystick")

namespace {

/// Percentage of the pod's maximum slew rate. A button is on or off, so one rate has to serve
/// both a nudge and a sweep; half speed is fast enough to follow a moving subject and slow
/// enough to stop on one.
constexpr int kSlewPercent = 50;

Q_APPLICATION_STATIC(PoliceGimbalJoystick, _policeGimbalJoystickInstance);

}  // namespace

PoliceGimbalJoystick::PoliceGimbalJoystick(QObject *parent)
    : QObject(parent)
{
}

PoliceGimbalJoystick *PoliceGimbalJoystick::instance()
{
    return _policeGimbalJoystickInstance();
}

void PoliceGimbalJoystick::init()
{
    if (_initialized) {
        return;
    }

    JoystickManager *const manager = JoystickManager::instance();
    if (!manager) {
        qCWarning(PoliceGimbalJoystickLog) << "joystick manager unavailable";
        return;
    }

    (void) connect(manager, &JoystickManager::activeJoystickChanged,
                   this, &PoliceGimbalJoystick::_activeJoystickChanged);

    _initialized = true;
    _activeJoystickChanged();
}

void PoliceGimbalJoystick::_activeJoystickChanged()
{
    Joystick *const joystick = JoystickManager::instance()->activeJoystick();
    if (joystick == _joystick) {
        return;
    }

    if (_joystick) {
        (void) disconnect(_joystick, nullptr, this, nullptr);
        // A held slew would otherwise run on after the stick it came from went away.
        SiyiCameraController::instance()->stopRotation();
    }

    _joystick = joystick;
    if (_joystick) {
        _connectJoystick(_joystick);
    }

    qCDebug(PoliceGimbalJoystickLog) << "active joystick" << (_joystick ? "connected" : "cleared");
}

void PoliceGimbalJoystick::_connectJoystick(Joystick *joystick)
{
    SiyiCameraController *const camera = SiyiCameraController::instance();

    (void) connect(joystick, &Joystick::siyiGimbalStart, this,
                   [camera](int yawDirection, int pitchDirection) {
                       camera->rotate(yawDirection * kSlewPercent, pitchDirection * kSlewPercent);
                   });
    (void) connect(joystick, &Joystick::siyiGimbalStop, camera, &SiyiCameraController::stopRotation);
    (void) connect(joystick, &Joystick::siyiGimbalCenter, camera, &SiyiCameraController::center);

    (void) connect(joystick, &Joystick::siyiZoomStart, this,
                   [camera](int direction) { camera->zoom(direction); });
    (void) connect(joystick, &Joystick::siyiZoomStop, this,
                   [camera]() { camera->zoom(0); });
    (void) connect(joystick, &Joystick::siyiZoomWide, this,
                   [camera]() { camera->setZoom(1.0); });
    (void) connect(joystick, &Joystick::siyiZoomTele, this,
                   [camera]() { camera->setZoom(20.0); });

    (void) connect(joystick, &Joystick::siyiTakePhoto, camera, &SiyiCameraController::takePhoto);
    (void) connect(joystick, &Joystick::siyiToggleRecording, camera, &SiyiCameraController::toggleRecording);

    // The pod carries one sensor per stream, so switching the EO window between the zoom and
    // wide cameras means re-routing the main stream. Thermal stays on the sub stream either
    // way, which is what keeps the IR window alive across the switch.
    (void) connect(joystick, &Joystick::siyiToggleWideAngle, this, [camera]() {
        const bool wide = camera->cameraImageType() ==
                          static_cast<int>(SiyiProtocol::CameraImageType::MainWideAngleSubThermal);
        camera->setCameraImageType(static_cast<int>(
            wide ? SiyiProtocol::CameraImageType::MainZoomSubThermal
                 : SiyiProtocol::CameraImageType::MainWideAngleSubThermal));
    });

    (void) connect(joystick, &Joystick::siyiToggleAiRecognition, this, []() {
        SiyiAiController *const ai = SiyiAiController::instance();
        ai->setRecognition(!ai->recognitionEnabled());
    });
}
