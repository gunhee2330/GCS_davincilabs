#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests FlightRecords, what the 비행 기록 page reads: records of every airframe for a span of
/// days, the CSV and folder an export writes, and the fetch of the aircraft's own log for one
/// flight against a MockLink PX4 vehicle serving logs over LOG_ENTRY and LOG_DATA.
class FlightRecordsTest : public VehicleTest
{
    Q_OBJECT

protected slots:
    /// Starts from no records and no downloaded logs: both live outside the per-test settings reset.
    void init() override;

private slots:
    /// Every airframe's file is read, only takeoffs on the days asked for are kept (both ends
    /// included), newest first, each with its airframe. A record from before landings were kept
    /// gets its landing from its takeoff and duration.
    void _recordsFilterByDayAcrossAirframes_test();

    /// One header line and one line a record: the airframe label, takeoff and landing with a space
    /// for the T, the duration as hh:mm:ss, metres and altitude to a decimal, and the two log names.
    /// A field holding a comma is quoted.
    void _csvListsRecords_test();

    /// An export writes a dated folder holding flights.csv and a copy of each record's telemetry
    /// log and vehicle log; a log missing from disk is left out rather than failing the export.
    void _exportCopiesRecordsAndLogs_test();

    /// The fetch lists the aircraft's logs, whose times are their last writes, and takes the one
    /// written through the flight: after a battery swap each of two flights close together gets
    /// its own log, not the one that ended nearest its takeoff. A flight whose log is gone gets
    /// none rather than a later flight's. The percent counts up to 100 and the file name goes into
    /// the record. A record of another airframe is refused with a reason. Over the FTP listing
    /// and over LOG_ENTRY both.
    void _downloadsTheFlightsVehicleLog_test_data();
    void _downloadsTheFlightsVehicleLog_test();
};
