#pragma once

#include "BaseClasses/VehicleTest.h"

class MockLink;
class PoliceLidarMonitor;
class Vehicle;

/// Tests PoliceLidarMonitor against a MockLink PX4 vehicle, with every DISTANCE_SENSOR frame
/// serialised onto the link and dispatched by Vehicle the way the aircraft's own frames are.
/// Handing a fact group or a slot the decoded message directly would skip exactly the dispatch
/// the monitor listens to.
class PoliceLidarMonitorTest : public VehicleTest
{
    Q_OBJECT

private slots:
    /// A forward frame arrives as raw metres on sector 0, a yaw 90 frame on sector 2, and a
    /// changed distance is visible well inside the second the fact group used to coalesce into.
    void _readingsAreRawMetresPerSector_test();

    /// The same distance repeated at the telemetry profile's rate keeps the monitor fresh and
    /// emits nothing, which is the case an equal Fact value made invisible.
    void _repeatedIdenticalFramesStayFresh_test();

    /// Silence past staleTimeoutMs takes the reading away instead of leaving a dead sensor's
    /// last number on screen.
    void _silenceGoesStale_test();

    /// A downward rangefinder is none of the eight sectors: it must not draw and must not stand
    /// in for a forward sensor that has stopped.
    void _downwardOrientationIsIgnored_test();

    /// A null vehicle, a vehicle that was destroyed under the monitor, and a frame from another
    /// system id all leave the monitor with nothing.
    void _foreignSysidAndNullVehicleLeaveNothing_test();

private:
    /// Serialises one DISTANCE_SENSOR frame onto the mock link.
    ///     @param orientation MAV_SENSOR_ORIENTATION value
    ///     @param sysid System id to send from, 0 for the connected vehicle's own
    void _sendDistanceSensor(uint8_t orientation, uint16_t centimetres, uint8_t sysid = 0);
};
