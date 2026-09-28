#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests PoliceWarnings: the limits it reads off a MockLink vehicle's parameters and the
/// fallbacks when there are none, and for each warning when the strip goes up, when it comes
/// down, that the sentence is spoken once per rise, and how a dismissal lasts.
class PoliceWarningsTest : public VehicleTest
{
    Q_OBJECT

private slots:
    /// PX4: GF_MAX_VER_DIST, GF_MAX_HOR_DIST and COM_WIND_WARN when set, 150 m, no radius and
    /// 10 m/s while they are at their disabled defaults.
    void _px4Limits_test();

    /// ArduPilot: FENCE_ALT_MAX and FENCE_RADIUS only while the fence is on and FENCE_TYPE carries
    /// their bit.
    void _arduPilotFenceLimits_test();

    /// Over the limit while flying, and only while flying; back down 2 m under it; spoken once
    /// per rise; a new limit takes effect without a telemetry change.
    void _altitudeAndRadius_test();

    /// At or above the threshold while flying; back down 1 m/s under it.
    void _wind_test();

    /// Up with the top bar's verdict, down 2 % above where it went up, critical spoken again, the
    /// percentage in the line live.
    void _battery_test();

    /// A tap hides one warning and no other, silently; it stays hidden while its condition lasts
    /// and comes back when the condition clears and returns, or when the battery turns critical,
    /// which speaks again.
    void _dismiss_test();

    /// Which autopilot lines are the English low and critical battery announcements Vehicle then
    /// leaves unspoken, that it does so only while a PoliceWarnings watches that vehicle, for the
    /// status text and the pack's charge state alike, and that PX4's (events) never reach the
    /// function that speaks a status text at all.
    void _stockBatteryVoice_test();
};
