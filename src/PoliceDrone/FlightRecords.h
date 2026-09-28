#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(FlightRecordsLog)

class OnboardLogController;
class QGCOnboardLogEntry;
class QQmlEngine;
class QJSEngine;
class Vehicle;

/// \brief The whole flight log as the 비행 기록 page reads it.
///
/// The procurement spec wants the flight log easy to read and handed over for any period on
/// request. TakeoffCounter keeps the records, one file per airframe; this reads all of them for a
/// span of days, writes chosen records out with their log files, and fetches the aircraft's own
/// log for one flight through the stock onboard log controller.
class FlightRecords : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The vehicle log fetch: airframe, takeoff and landing of the flight it is for, busy while it runs,
    /// percent received, and error, the reason the last one failed (empty when it did not).
    Q_PROPERTY(QVariantMap download READ download NOTIFY downloadChanged)

public:
    /// No default argument, for the reason TakeoffCounter gives.
    explicit FlightRecords(QObject* parent);
    ~FlightRecords() override;

    static FlightRecords* instance();
    static FlightRecords* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] QVariantMap download() const { return _download; }

    /// Every airframe's flights that took off from \a from to \a to (yyyy-MM-dd, both days
    /// included), newest first. Each is TakeoffCounter's record with airframe added.
    Q_INVOKABLE QVariantList records(const QString& from, const QString& to) const;

    /// Writes \a records to a new dated folder under \a parentDir: flights.csv and a copy of each
    /// record's telemetry log and vehicle log. Returns the folder, or empty when it could not be written.
    Q_INVOKABLE QString exportRecords(const QVariantList& records, const QString& parentDir) const;

    /// Export under the app's save folder.
    Q_INVOKABLE QString exportDirectory() const;

    /// A writable removable volume (a USB stick or SD card), or empty when none is mounted.
    Q_INVOKABLE QString usbDirectory() const;

    /// Asks the connected aircraft for its log list and downloads the log that was being written
    /// through the record's flight (takeoff to landing) into vehicleLogDirectory(). False, with
    /// download.error set, when it cannot start: another fetch is running, or the record's airframe
    /// is not the one connected. It ends with download.error set when no log fits the flight.
    Q_INVOKABLE bool downloadVehicleLog(const QVariantMap& record);

    /// The records as CSV, UTF-8 with a byte order mark so a spreadsheet reads the Korean.
    static QString csv(const QVariantList& records);

    /// Where the vehicle log of \a record is downloaded: the takeoff's name under the stock log folder.
    static QString vehicleLogDirectory(const QVariantMap& record);

signals:
    void downloadChanged();
    /// A record was added or changed on disk: a flight landed, or its vehicle log came in.
    void recordsChanged();

private slots:
    void _listed();
    void _downloaded();

private:
    bool _fail(const QString& error);
    void _setPercent(int percent);
    void _stopWatching();

    OnboardLogController* _logs = nullptr;
    QPointer<Vehicle> _vehicle;
    QPointer<QGCOnboardLogEntry> _entry;
    /// The download has been handed to the controller, so its next idle is the end of it.
    bool _fetching = false;
    uint _entrySize = 0;
    QVariantMap _download;
};
