#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests TakeoffCounter's flight log against a MockLink PX4 vehicle flown through whole arm
/// cycles: armed, in the air on the aircraft's own landed state, back on the pad, disarmed.
class TakeoffCounterTest : public VehicleTest
{
    Q_OBJECT

protected slots:
    /// The records live in files of their own, which the per-test settings reset does not reach.
    void init() override;

private slots:
    /// One cycle leaves one record, newest first: when it lifted off and landed, how long it flew,
    /// the vehicle's flightDistance at the landing, the highest altitude, the airframe's label and
    /// the flight's telemetry log. The disarm after the landing adds nothing, and the record is in
    /// the airframe's records file. The log is a stock-format tlog named by the takeoff, holding
    /// the flight and closed at the disarm.
    void _flightIsRecordedAtLanding_test();

    /// A landed state that drops out in the air is one flight: the second landing of the cycle
    /// replaces the cycle's record with the longer flight and the larger distance.
    void _landedFlickerKeepsOneRecord_test();

    /// A log the settings file held, at its old hundred-record limit, moves to the records file
    /// at the start and leaves the settings file; the next flight goes on top without dropping the
    /// oldest, and a restart reads all of them.
    void _settingsLogMovesToFileWithoutLimit_test();

    /// A records file that is there but will not parse reads as a failure, unlike no file. The
    /// landing then leaves it as it was rather than writing the one flight over the history, and
    /// the flight still shows in the list.
    void _unreadableRecordsAreNotOverwritten_test();

    /// takeoffTime is empty while armed on the pad, set at the first liftoff to the string the
    /// record keeps, left alone by a second liftoff and the disarm, and cleared by the next arm.
    void _takeoffTimeIsFirstLiftoff_test();

private:
    /// Puts the mock at \a metres above home. MockLink has no landing: its takeoff handler arms
    /// and sets the altitude to home plus param7, so 0 sets it back on the pad. It reports
    /// itself in the air whenever it is above home, which is what makes the vehicle's flying
    /// state follow.
    void _setMockAltitude(double metres);

    /// One whole cycle: arm, lift off, fly \a metres, land, disarm.
    void _fly(double metres);
};
