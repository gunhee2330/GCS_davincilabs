#include "MissionAutoRecord.h"

#include "Fact.h"
#include "MultiVehicleManager.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"
#include "Vehicle.h"

MissionAutoRecord::MissionAutoRecord(QObject *parent, std::function<void()> toggleRecording)
    : QObject(parent)
    , _toggleRecording(std::move(toggleRecording))
{
}

void MissionAutoRecord::init()
{
    (void) connect(SettingsManager::instance()->siyiCameraSettings()->autoRecordMission(), &Fact::rawValueChanged,
                   this, &MissionAutoRecord::_update);

    MultiVehicleManager *const manager = MultiVehicleManager::instance();
    (void) connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &MissionAutoRecord::_follow);
    _follow(manager->activeVehicle());
}

void MissionAutoRecord::_follow(Vehicle *vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }

    _vehicle = vehicle;
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::armedChanged, this, &MissionAutoRecord::_update);
        (void) connect(_vehicle, &Vehicle::flyingChanged, this, &MissionAutoRecord::_update);
        (void) connect(_vehicle, &Vehicle::flightModeChanged, this, &MissionAutoRecord::_update);
    }
    _update();
}

void MissionAutoRecord::_update()
{
    // A vehicle gone from the link has not landed, so the pod is left as it is until one is back
    if (!_vehicle) {
        return;
    }

    const bool active = SettingsManager::instance()->siyiCameraSettings()->autoRecordMission()->rawValue().toBool()
                        && _vehicle->armed() && _vehicle->flying()
                        && (_vehicle->flightMode() == _vehicle->missionFlightMode());
    if (active == _active) {
        return;
    }
    _active = active;
    _startPending = active;
    _startsLeft = kStartAttempts;
    _act();
}

void MissionAutoRecord::podState(std::optional<bool> recording)
{
    _pod = recording;
    if (!recording) {
        return;
    }

    if (*recording) {
        if (_own == Own::Asked) {
            _own = Own::Recording;
        } else if ((_own == Own::Stopping) && (++_unconfirmedReplies >= kConfirmReplies)) {
            // The stop was lost or refused. Back in the mission it is simply ours again
            _unconfirmedReplies = 0;
            if (_active) {
                _own = Own::Recording;
            } else if (_stopsLeft-- > 0) {
                _toggleRecording();
            } else {
                _own = Own::None;
            }
        }
    } else if ((_own == Own::Recording) || (_own == Own::Stopping)) {
        // Stopped by us, by hand or by the pod. Whatever is started after this is the operator's
        _own = Own::None;
    } else if ((_own == Own::Asked) && (++_unconfirmedReplies >= kConfirmReplies)) {
        // The start was lost or refused. Unclaimed, so a recording started later is not taken for ours
        _own = Own::None;
        _startPending = _active && (_startsLeft > 0);
    }
    _act();
}

void MissionAutoRecord::operatorToggled()
{
    _own = Own::None;
    _startPending = false;
}

void MissionAutoRecord::_act()
{
    if (!_pod) {
        return;
    }

    if (_active) {
        if (_startPending) {
            // A recording on the pod is left alone: the operator's, or ours still running
            // ponytail: a mission left and re-entered within the second before the pod confirms
            // our stop reads as "already recording" and stays unrecorded; start again when that
            // stop is confirmed if it turns up in the field.
            _startPending = false;
            if ((_own == Own::None) && !*_pod) {
                --_startsLeft;
                _own = Own::Asked;
                _unconfirmedReplies = 0;
                _toggleRecording();
            }
        }
        return;
    }

    // A start confirmed after the mission ended is stopped here too, on that confirmation
    if ((_own == Own::Recording) && *_pod) {
        _own = Own::Stopping;
        _unconfirmedReplies = 0;
        _stopsLeft = kStartAttempts - 1;
        _toggleRecording();
    }
}
