#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests MissionAutoRecord against a MockLink PX4 vehicle and a fake pod that records whatever it
/// is toggled to and answers only when the test says it does.
class MissionAutoRecordTest : public VehicleTest
{
    Q_OBJECT

private slots:
    /// Armed and flying is not enough: the pod records from the mission mode on and stops at the
    /// landing. A mode change out of the mission stops it too.
    void _recordsTheMissionOnly_test();

    /// A recording running before the mission is not touched at either end, and neither is one the
    /// operator starts again after stopping ours.
    void _leavesTheOperatorsRecordingAlone_test();

    /// With the setting off nothing is sent; switched on in the mission it starts, switched off it stops.
    void _settingSwitchesItInFlight_test();

    /// Nothing is sent while the pod's state is unknown: before its first reply, and from a link
    /// loss until the next one. The operator's recording seen once the link is back is left alone.
    void _waitsForTheCameraToAnswer_test();

    /// A start the pod never confirms is sent again, a few times, then given up; a recording the
    /// operator starts after that, or while ours is unconfirmed, is not taken for ours.
    void _unconfirmedStartIsNotClaimed_test();

    /// A start the pod confirms only after the mission has ended is stopped on that confirmation.
    void _lateConfirmationIsStopped_test();

    /// A stop of ours the pod never confirms is sent again, as often as a start, then given up;
    /// one it confirms is sent once, and one lost as the mission resumes leaves ours running.
    void _unconfirmedStopIsSentAgain_test();

private:
    /// Arms the mock and lifts it off to 1.5 m.
    void _takeOff();
    /// Sets the mock back on the pad.
    void _land();
    void _setMode(const QString &mode);
};
