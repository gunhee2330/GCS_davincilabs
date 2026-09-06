#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>

Q_DECLARE_LOGGING_CATEGORY(PoliceGimbalJoystickLog)

class Joystick;

/// \brief Drives the SIYI optical pod from the handheld controller's buttons.
///
/// QGC's own gimbal actions send MAVLink gimbal commands, which the ZT30 never sees: the pod
/// hangs off its own Ethernet link and speaks SIYI's protocol. Joystick therefore emits a
/// second, pod-specific set of signals and this class is what turns them into pod commands,
/// so the joystick layer keeps no knowledge of the payload.
///
/// Slew rate is a percentage of the pod maximum. Buttons are on/off, so a fixed rate is the
/// best they can do; proportional control needs a stick axis and is a separate piece of work.
class PoliceGimbalJoystick : public QObject
{
    Q_OBJECT

public:
    /// Public because Q_APPLICATION_STATIC constructs the instance; use instance() to reach it.
    explicit PoliceGimbalJoystick(QObject *parent = nullptr);

    static PoliceGimbalJoystick *instance();

    /// Follows the active joystick from here on. Call once at startup, after
    /// JoystickManager::init() and SiyiCameraController::init().
    void init();

private slots:
    void _activeJoystickChanged();

private:
    void _connectJoystick(Joystick *joystick);

    Joystick *_joystick = nullptr;
    bool _initialized = false;
};
