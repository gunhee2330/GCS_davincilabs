#pragma once

#include <functional>
#include <optional>

#include <QtCore/QObject>
#include <QtCore/QPointer>

class Vehicle;

/// \brief The 임무 중 자동 녹화 setting: records on the pod while the active vehicle flies its mission.
///
/// Recording starts when the setting is on and the vehicle is armed, flying and in its mission
/// flight mode (PX4 Mission, ArduPilot Auto), unless the pod is already recording. It stops when
/// any of that ends, but only a recording this class started and the pod confirmed: one the
/// operator started, or started again after stopping ours, is left running. Done on the ground
/// station because the pod is driven by this app's SDK client, not by the flight controller.
///
/// The SDK has a toggle only, so nothing is sent while the pod's state is unknown (before its
/// first config reply, and from a link loss until the next one): a toggle sent on a guess stops
/// the operator's recording as readily as it starts ours.
class MissionAutoRecord : public QObject
{
    Q_OBJECT

public:
    /// Config replies still saying "not recording" after a start before it counts as lost or
    /// refused. The pod is asked right after the toggle and then once a second
    static constexpr int kConfirmReplies = 5;
    /// Starts sent per mission, the first included, while the pod keeps saying "not recording"
    static constexpr int kStartAttempts = 3;

    /// \a toggleRecording is the pod's record toggle: SiyiCameraController in the app, a fake in the tests.
    MissionAutoRecord(QObject *parent, std::function<void()> toggleRecording);

    /// Starts following the active vehicle and the setting. Call once, after MultiVehicleManager::init().
    void init();

    /// The pod's recording state from each config reply, or nullopt when the link is lost.
    void podState(std::optional<bool> recording);

    /// The operator pressed record (screen, panel or joystick): whatever is on the pod is theirs
    /// from here on, and nothing more is started for them in this mission.
    void operatorToggled();

private:
    void _follow(Vehicle *vehicle);
    void _update();
    /// Sends what the vehicle state asks for, once the pod's state is known.
    void _act();

    /// Whose recording is on the pod: none of ours, one asked for, one the pod confirmed.
    enum class Own { None, Asked, Recording };

    std::function<void()> _toggleRecording;
    QPointer<Vehicle> _vehicle;
    std::optional<bool> _pod;
    bool _active = false;
    /// In the mission and not yet started, waiting for a known "not recording"
    bool _startPending = false;
    Own _own = Own::None;
    int _unconfirmedReplies = 0;
    int _startsLeft = 0;
};
