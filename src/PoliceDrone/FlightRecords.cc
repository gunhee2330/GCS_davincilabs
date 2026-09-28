#include "FlightRecords.h"

#include <algorithm>

#include <QtCore/QApplicationStatic>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStorageInfo>
#include <QtQml/QJSEngine>

#include "AppSettings.h"
#include "FTPManager.h"
#include "MultiVehicleManager.h"
#include "OnboardLogController.h"
#include "OnboardLogEntry.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"

#ifdef Q_OS_ANDROID
#include "AndroidInterface.h"
#endif

QGC_LOGGING_CATEGORY(FlightRecordsLog, "PoliceDrone.FlightRecords")

namespace {

const QString kAirframe = QStringLiteral("airframe");
const QString kTakeoff  = QStringLiteral("takeoff");
const QString kLanding  = QStringLiteral("landing");

/// From a flight's recorded landing to the last write of its log on the aircraft: the disarm,
/// which a pilot may hold a while on the pad, and the seconds an autopilot keeps logging past it.
/// A tuning knob: raise it if the aircraft's own log of a flight is refused.
constexpr qint64 kLogEndMarginS = 5 * 60;

/// A record's local ISO 8601 time in seconds since the epoch.
qint64 secs(const QVariant& isoTime)
{
    return QDateTime::fromString(isoTime.toString(), Qt::ISODate).toSecsSinceEpoch();
}

/// A record's takeoff as the file names use it, yyyyMMdd_hhmmss.
QString takeoffStamp(const QVariantMap& record)
{
    return QDateTime::fromString(record.value(kTakeoff).toString(), Qt::ISODate).toString(QStringLiteral("yyyyMMdd_hhmmss"));
}

QString duration(int seconds)
{
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                                     .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'))
                                     .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString csvField(QString text)
{
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n'))) {
        return text;
    }
    return QLatin1Char('"') + text.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

}  // namespace

Q_APPLICATION_STATIC(FlightRecords, _flightRecordsInstance, nullptr);

FlightRecords::FlightRecords(QObject* parent) : QObject(parent)
{
    (void) connect(TakeoffCounter::instance(), &TakeoffCounter::flightsChanged, this, &FlightRecords::recordsChanged);
}

FlightRecords::~FlightRecords() = default;

FlightRecords* FlightRecords::instance()
{
    return _flightRecordsInstance();
}

FlightRecords* FlightRecords::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    FlightRecords* const records = instance();
    QJSEngine::setObjectOwnership(records, QJSEngine::CppOwnership);
    return records;
}

QVariantList FlightRecords::records(const QString& from, const QString& to) const
{
    QVariantList result;
    const QFileInfoList files = QDir(TakeoffCounter::recordsDirectory()).entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
    for (const QFileInfo& file : files) {
        const QString airframe = file.completeBaseName();
        const QVariantList flights = TakeoffCounter::readFlights(airframe);
        for (const QVariant& entry : flights) {
            QVariantMap flight = entry.toMap();
            // takeoff is local ISO 8601, so its first ten characters are the day, comparable as text.
            const QString day = flight.value(kTakeoff).toString().left(10);
            if ((day < from) || (day > to)) {
                continue;
            }
            flight.insert(kAirframe, airframe);
            // A record from before the landing was kept: its duration runs from the takeoff to it.
            if (!flight.contains(QStringLiteral("landing"))) {
                flight.insert(QStringLiteral("landing"), QDateTime::fromString(flight.value(kTakeoff).toString(), Qt::ISODate)
                                                             .addSecs(flight.value(QStringLiteral("seconds")).toInt()).toString(Qt::ISODate));
            }
            result.append(flight);
        }
    }
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(kTakeoff).toString() > b.toMap().value(kTakeoff).toString();
    });
    return result;
}

QString FlightRecords::csv(const QVariantList& records)
{
    QStringList lines{ QStringList{ tr("기체"), tr("이륙 일시"), tr("착륙 일시"), tr("비행 시간"), tr("비행 거리(m)"),
                                    tr("최대 고도(m)"), tr("텔레메트리 로그"), tr("기체 로그") }.join(QLatin1Char(',')) };
    for (const QVariant& entry : records) {
        const QVariantMap flight = entry.toMap();
        const QVariant altitude = flight.value(QStringLiteral("maxAltitude"));
        lines.append(QStringList{
            csvField(flight.value(QStringLiteral("vehicle")).toString()),
            flight.value(kTakeoff).toString().replace(QLatin1Char('T'), QLatin1Char(' ')),
            flight.value(QStringLiteral("landing")).toString().replace(QLatin1Char('T'), QLatin1Char(' ')),
            duration(flight.value(QStringLiteral("seconds")).toInt()),
            QString::number(flight.value(QStringLiteral("metres")).toDouble(), 'f', 1),
            altitude.isValid() ? QString::number(altitude.toDouble(), 'f', 1) : QString(),
            csvField(flight.value(QStringLiteral("tlog")).toString()),
            csvField(flight.value(QStringLiteral("vehicleLog")).toString()),
        }.join(QLatin1Char(',')));
    }
    return QChar(0xFEFF) + lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString FlightRecords::vehicleLogDirectory(const QVariantMap& record)
{
    return QDir(SettingsManager::instance()->appSettings()->logSavePath()).filePath(takeoffStamp(record));
}

QString FlightRecords::exportDirectory() const
{
    return QDir(SettingsManager::instance()->appSettings()->savePath()->rawValue().toString()).filePath(QStringLiteral("Export"));
}

QString FlightRecords::usbDirectory() const
{
#ifdef Q_OS_ANDROID
    // The stock SD card lookup: the app's own folder on the first removable volume Android reports.
    // ponytail: first removable volume, not USB in particular; with an SD card in, the card may be
    // taken over a USB stick (and the stock save folder is already on the card unless
    // androidDontSaveToSDCard is set). Not checked on the handheld; the page shows the folder it
    // wrote. Pick the volume by StorageVolume description in the Java lookup if that bites.
    return AndroidInterface::getSDCardPath();
#else
    // ponytail: removable by mount point (macOS /Volumes, Linux /media); Windows drive letters are not looked at
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo& volume : volumes) {
        const QString root = volume.rootPath();
        if (volume.isValid() && volume.isReady() && !volume.isReadOnly() &&
            (root.startsWith(QStringLiteral("/Volumes/")) || root.startsWith(QStringLiteral("/media/")) || root.startsWith(QStringLiteral("/run/media/")))) {
            return root;
        }
    }
    return QString();
#endif
}

QString FlightRecords::exportRecords(const QVariantList& records, const QString& parentDir) const
{
    if (parentDir.isEmpty()) {
        return QString();
    }
    const QDir dir(QDir(parentDir).filePath(QStringLiteral("FlightRecords_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))));
    QFile csvFile(dir.filePath(QStringLiteral("flights.csv")));
    if (!dir.mkpath(QStringLiteral(".")) || !csvFile.open(QIODevice::WriteOnly) || (csvFile.write(csv(records).toUtf8()) < 0)) {
        qCWarning(FlightRecordsLog) << "could not write" << csvFile.fileName() << csvFile.errorString();
        return QString();
    }
    csvFile.close();

    // A log that is not on disk (never written, or cleared by hand) is left out; the CSV still names it.
    const QDir tlogDir(SettingsManager::instance()->appSettings()->telemetrySavePath());
    for (const QVariant& entry : records) {
        const QVariantMap flight = entry.toMap();
        const QString tlog = flight.value(QStringLiteral("tlog")).toString();
        if (!tlog.isEmpty()) {
            (void) QFile::copy(tlogDir.filePath(tlog), dir.filePath(tlog));
        }
        const QString vehicleLog = flight.value(QStringLiteral("vehicleLog")).toString();
        if (!vehicleLog.isEmpty()) {
            (void) QFile::copy(QDir(vehicleLogDirectory(flight)).filePath(vehicleLog), dir.filePath(vehicleLog));
        }
    }
    return dir.absolutePath();
}

bool FlightRecords::downloadVehicleLog(const QVariantMap& record)
{
    if (_download.value(QStringLiteral("busy")).toBool()) {
        return false;
    }

    _download = {
        { kAirframe, record.value(kAirframe) },
        { kTakeoff, record.value(kTakeoff) },
        { kLanding, record.value(kLanding) },
        { QStringLiteral("busy"), true },
        { QStringLiteral("percent"), 0 },
        { QStringLiteral("error"), QString() },
    };
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!_vehicle || (TakeoffCounter::airframeKey(_vehicle) != record.value(kAirframe).toString())) {
        return _fail(tr("이 비행의 기체가 연결되어 있지 않습니다"));
    }

    if (!_logs) {
        // Our own instance of the stock controller, so the Onboard Logs page keeps its list and
        // selection. Queued: the controller is still inside its own bookkeeping when it signals.
        _logs = new OnboardLogController(this);
        (void) connect(_logs, &OnboardLogController::requestingListChanged, this, &FlightRecords::_listed, Qt::QueuedConnection);
        (void) connect(_logs, &OnboardLogController::downloadingLogsChanged, this, &FlightRecords::_downloaded, Qt::QueuedConnection);
    }
    _entry = nullptr;
    _fetching = false;
    emit downloadChanged();

    _logs->refresh();
    if (!_logs->property("requestingList").toBool()) {
        return _fail(tr("기체 로그 목록을 요청하지 못했습니다"));
    }
    return true;
}

void FlightRecords::_listed()
{
    if (!_download.value(QStringLiteral("busy")).toBool() || _fetching || _logs->property("requestingList").toBool()) {
        return;
    }

    // Log times are when each log was last written, not when it opened: the stock controller takes
    // LOG_ENTRY's time_utc or the FTP listing's modification time, and PX4 and ArduPilot fill both
    // from the log file's mtime. So a flight's log carries about its disarm, and the log before it
    // the previous flight's. The flight's log is the first one still being written at the takeoff:
    // the earliest time at or after it. It must also have ended by the landing plus the margin and
    // before this airframe's next recorded takeoff. Past those it is a later flight's log, or one
    // file holding several flights (ArduPilot keeps writing one file across arms unless
    // LOG_FILE_DSRMROT is set), and nothing is fetched rather than the wrong log. A log without a
    // clock time (the controller's UnknownDate) cannot be matched and is passed over.
    const qint64 takeoff = secs(_download.value(kTakeoff));
    qint64 last = secs(_download.value(kLanding)) + kLogEndMarginS;
    const QVariantList flights = TakeoffCounter::readFlights(_download.value(kAirframe).toString());
    for (const QVariant& flight : flights) {
        const qint64 other = secs(flight.toMap().value(kTakeoff));
        if (other > takeoff) {
            last = qMin(last, other - 1);
        }
    }
    const QmlObjectListModel* const model = _logs->property("model").value<QmlObjectListModel*>();
    QGCOnboardLogEntry* best = nullptr;
    for (int i = 0; model && (i < model->count()); ++i) {
        QGCOnboardLogEntry* const entry = model->value<QGCOnboardLogEntry*>(i);
        if (!entry || !entry->received() || (entry->time().date().year() < 2010)) {
            continue;
        }
        const qint64 time = entry->time().toSecsSinceEpoch();
        if ((time >= takeoff) && (time <= last) && (!best || (time < best->time().toSecsSinceEpoch()))) {
            best = entry;
        }
    }
    if (!best) {
        (void) _fail(tr("기체에 이 비행 시각의 로그가 없습니다"));
        return;
    }

    const QString dir = vehicleLogDirectory(_download);
    if (!_vehicle || !QDir().mkpath(dir)) {
        (void) _fail(tr("기체 로그를 받지 못했습니다"));
        return;
    }

    _entry = best;
    _entrySize = best->size();
    // The controller reports progress as a size and rate string; the percent is counted here off
    // the same traffic: LOG_DATA offsets, or the FTP transfer's own fraction.
    (void) connect(_vehicle, &Vehicle::logData, this, [this](uint32_t ofs, uint16_t, uint8_t count, const uint8_t*) {
        _setPercent(static_cast<int>((static_cast<quint64>(ofs) + count) * 100 / qMax(_entrySize, 1u)));
    });
    (void) connect(_vehicle->ftpManager(), &FTPManager::commandProgress, this, [this](float value) {
        _setPercent(static_cast<int>(value * 100));
    });

    qCDebug(FlightRecordsLog) << "downloading log" << best->id() << "of" << best->time() << "for the takeoff" << _download.value(kTakeoff);
    _fetching = true;
    best->setSelected(true);
    _logs->download(dir);
}

void FlightRecords::_downloaded()
{
    if (!_fetching || _logs->property("downloadingLogs").toBool()) {
        return;
    }
    _fetching = false;
    _stopWatching();

    // The controller names the file itself (log_<id>_<time>, or the FTP file's own name), into a
    // folder of this flight's alone, so the newest file there is the one it wrote.
    const QFileInfoList files = QDir(vehicleLogDirectory(_download)).entryInfoList(QDir::Files, QDir::Time);
    if (!_entry || (_entry->status() != OnboardLogController::tr("Downloaded")) || files.isEmpty()) {
        (void) _fail(tr("기체 로그를 받지 못했습니다"));
        return;
    }
    if (!TakeoffCounter::updateFlight(_download.value(kAirframe).toString(), _download.value(kTakeoff).toString(),
                                      { { QStringLiteral("vehicleLog"), files.first().fileName() } })) {
        (void) _fail(tr("받은 기체 로그를 기록에 넣지 못했습니다"));
        return;
    }

    _download.insert(QStringLiteral("busy"), false);
    _download.insert(QStringLiteral("percent"), 100);
    emit downloadChanged();
    emit recordsChanged();
}

bool FlightRecords::_fail(const QString& error)
{
    // The page shows it; a refusal for another airframe is the operator's slip, not a fault.
    qCDebug(FlightRecordsLog) << error;
    _stopWatching();
    _fetching = false;
    _download.insert(QStringLiteral("busy"), false);
    _download.insert(QStringLiteral("error"), error);
    emit downloadChanged();
    return false;
}

void FlightRecords::_setPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (percent > _download.value(QStringLiteral("percent")).toInt()) {
        _download.insert(QStringLiteral("percent"), percent);
        emit downloadChanged();
    }
}

void FlightRecords::_stopWatching()
{
    if (_vehicle) {
        (void) disconnect(_vehicle, &Vehicle::logData, this, nullptr);
        (void) disconnect(_vehicle->ftpManager(), &FTPManager::commandProgress, this, nullptr);
    }
}
