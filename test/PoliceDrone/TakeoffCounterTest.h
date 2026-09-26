#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests TakeoffCounter's flight log against a MockLink PX4 vehicle flown through whole arm
/// cycles: armed, in the air on the aircraft's own landed state, back on the pad, disarmed.
class TakeoffCounterTest : public VehicleTest
{
    Q_OBJECT

private slots:
    /// One cycle leaves one record, newest first: when it lifted off, how long it flew and the
    /// vehicle's flightDistance at the landing. The disarm after the landing adds nothing, and the
    /// record is in the settings file under the airframe's key.
    void _flightIsRecordedAtLanding_test();

    /// A landed state that drops out in the air is one flight: the second landing of the cycle
    /// replaces the cycle's record with the longer flight and the larger distance.
    void _landedFlickerKeepsOneRecord_test();

    /// A log already at the limit is read back from the settings file, and the next flight
    /// drops the oldest record to make room rather than growing past it.
    void _logIsLoadedAndKeepsLatestHundred_test();

private:
    /// Puts the mock at \a metres above home. MockLink has no landing: its takeoff handler arms
    /// and sets the altitude to home plus param7, so 0 sets it back on the pad. It reports
    /// itself in the air whenever it is above home, which is what makes the vehicle's flying
    /// state follow.
    void _setMockAltitude(double metres);

    /// One whole cycle: arm, lift off, fly \a metres, land, disarm.
    void _fly(double metres);
};
