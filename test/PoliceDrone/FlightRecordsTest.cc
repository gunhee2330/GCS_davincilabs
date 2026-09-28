#include "FlightRecordsTest.h"

#include <algorithm>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "FlightRecords.h"
#include "MockLink.h"
#include "MockLinkFTP.h"
#include "SettingsManager.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"

namespace {

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) && (file.write(contents) == contents.size());
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

void FlightRecordsTest::init()
{
    QVERIFY(QDir(TakeoffCounter::recordsDirectory()).removeRecursively());
    const QString logs = SettingsManager::instance()->appSettings()->logSavePath();
    QVERIFY(QDir(logs).removeRecursively());
    QVERIFY(QDir().mkpath(logs));
    VehicleTest::init();
}

void FlightRecordsTest::_recordsFilterByDayAcrossAirframes_test()
{
    QVERIFY(TakeoffCounter::writeFlights(QStringLiteral("sysid-1"), {
        QVariantMap{ { "takeoff", "2026-09-28T14:30:12" }, { "seconds", 1348 }, { "metres", 3800.0 },
                     { "landing", "2026-09-28T14:52:40" }, { "vehicle", "1호기" } },
        // Kept before landings were: takeoff, seconds and metres only.
        QVariantMap{ { "takeoff", "2026-09-01T00:00:05" }, { "seconds", 65 }, { "metres", 100.0 } },
    }));
    QVERIFY(TakeoffCounter::writeFlights(QStringLiteral("uid-abc"), {
        QVariantMap{ { "takeoff", "2026-09-29T00:00:00" }, { "seconds", 60 }, { "metres", 50.0 } },
        QVariantMap{ { "takeoff", "2026-09-27T16:12:55" }, { "seconds", 60 }, { "metres", 50.0 } },
        QVariantMap{ { "takeoff", "2026-08-31T23:59:59" }, { "seconds", 60 }, { "metres", 50.0 } },
    }));

    FlightRecords records(nullptr);
    const QVariantList listed = records.records(QStringLiteral("2026-09-01"), QStringLiteral("2026-09-28"));
    QCOMPARE(listed.size(), 3);

    const QStringList takeoffs{ "2026-09-28T14:30:12", "2026-09-27T16:12:55", "2026-09-01T00:00:05" };
    const QStringList airframes{ "sysid-1", "uid-abc", "sysid-1" };
    for (int i = 0; i < listed.size(); ++i) {
        const QVariantMap flight = listed.at(i).toMap();
        QCOMPARE(flight.value("takeoff").toString(), takeoffs.at(i));
        QCOMPARE(flight.value("airframe").toString(), airframes.at(i));
    }
    QCOMPARE(listed.at(0).toMap().value("landing").toString(), QStringLiteral("2026-09-28T14:52:40"));
    QCOMPARE(listed.at(0).toMap().value("vehicle").toString(), QStringLiteral("1호기"));
    QCOMPARE(listed.at(2).toMap().value("landing").toString(), QStringLiteral("2026-09-01T00:01:10"));

    QCOMPARE(records.records(QStringLiteral("2026-09-29"), QStringLiteral("2026-09-29")).size(), 1);
    QVERIFY(records.records(QStringLiteral("2025-01-01"), QStringLiteral("2025-12-31")).isEmpty());
}

void FlightRecordsTest::_csvListsRecords_test()
{
    const QVariantList flights{
        QVariantMap{ { "vehicle", "1호기" }, { "takeoff", "2026-09-28T14:30:12" }, { "landing", "2026-09-28T14:52:40" },
                     { "seconds", 1348 }, { "metres", 3800.04 }, { "maxAltitude", 35.26 },
                     { "tlog", "20260928_143012.tlog" }, { "vehicleLog", "log_12_2026-9-28-14-30-08.bin" } },
        QVariantMap{ { "vehicle", "2호기, 예비" }, { "takeoff", "2026-09-27T16:12:55" }, { "landing", "2026-09-27T16:13:55" },
                     { "seconds", 60 }, { "metres", 50.0 } },
    };

    const QString csv = FlightRecords::csv(flights);
    QVERIFY2(csv.startsWith(QChar(0xFEFF)), "No byte order mark for the spreadsheet to read the Korean by");
    const QStringList lines = csv.mid(1).split(QStringLiteral("\r\n"));
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(0), QStringLiteral("기체,이륙 일시,착륙 일시,비행 시간,비행 거리(m),최대 고도(m),텔레메트리 로그,기체 로그"));
    QCOMPARE(lines.at(1), QStringLiteral("1호기,2026-09-28 14:30:12,2026-09-28 14:52:40,00:22:28,3800.0,35.3,"
                                         "20260928_143012.tlog,log_12_2026-9-28-14-30-08.bin"));
    QCOMPARE(lines.at(2), QStringLiteral("\"2호기, 예비\",2026-09-27 16:12:55,2026-09-27 16:13:55,00:01:00,50.0,,,"));
    QCOMPARE(lines.at(3), QString());
}

void FlightRecordsTest::_exportCopiesRecordsAndLogs_test()
{
    const QVariantMap flight{ { "airframe", "sysid-1" }, { "vehicle", "1호기" }, { "takeoff", "2026-09-28T14:30:12" },
                              { "landing", "2026-09-28T14:52:40" }, { "seconds", 1348 }, { "metres", 3800.0 },
                              { "tlog", "20260928_143012.tlog" }, { "vehicleLog", "log_12.bin" } };
    // Names a telemetry log that is not on disk.
    const QVariantMap missing{ { "airframe", "sysid-1" }, { "takeoff", "2026-09-27T16:12:55" }, { "seconds", 60 },
                               { "metres", 50.0 }, { "tlog", "20260927_161255.tlog" } };
    const QString tlogDir = SettingsManager::instance()->appSettings()->telemetrySavePath();
    (void) QFile::remove(QDir(tlogDir).filePath("20260927_161255.tlog"));
    QVERIFY(writeFile(QDir(tlogDir).filePath("20260928_143012.tlog"), "tlog bytes"));
    QVERIFY(writeFile(QDir(FlightRecords::vehicleLogDirectory(flight)).filePath("log_12.bin"), "vehicle log bytes"));

    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    FlightRecords records(nullptr);
    const QString folder = records.exportRecords({ flight, missing }, parent.path());
    QVERIFY2(!folder.isEmpty(), "The export wrote nothing");

    const QFileInfo folderInfo(folder);
    QCOMPARE(folderInfo.dir().canonicalPath(), QDir(parent.path()).canonicalPath());
    QVERIFY2(QRegularExpression(QStringLiteral("^FlightRecords_\\d{8}_\\d{6}$")).match(folderInfo.fileName()).hasMatch(),
             qPrintable(folderInfo.fileName()));

    const QDir out(folder);
    QCOMPARE(out.entryList(QDir::Files, QDir::Name),
             QStringList({ QStringLiteral("20260928_143012.tlog"), QStringLiteral("flights.csv"), QStringLiteral("log_12.bin") }));
    QCOMPARE(readFile(out.filePath("flights.csv")), FlightRecords::csv({ flight, missing }).toUtf8());
    QCOMPARE(readFile(out.filePath("20260928_143012.tlog")), QByteArray("tlog bytes"));
    QCOMPARE(readFile(out.filePath("log_12.bin")), QByteArray("vehicle log bytes"));
}

void FlightRecordsTest::_downloadsTheFlightsVehicleLog_test_data()
{
    QTest::addColumn<bool>("listWithTime");
    QTest::newRow("FTP listing") << true;
    // What PX4 up to 1.17 gets: it refuses the listing with times, and the controller falls back
    // to LOG_ENTRY.
    QTest::newRow("LOG_ENTRY") << false;
}

void FlightRecordsTest::_downloadsTheFlightsVehicleLog_test()
{
    QFETCH(bool, listWithTime);

    // A battery swap: A flies 10:00 to 10:20, B takes off 10:25 and lands 10:45. Each log carries
    // its last write, a few seconds past its landing, as PX4 and ArduPilot report it. The log
    // nearest B's takeoff is A's (5 min against 20). Z hops 10:50 to 10:51 with its log gone from
    // the aircraft, and C 10:52 to 10:54, whose log ends inside Z's margin.
    const QDateTime day(QDate(2026, 9, 28), QTime(10, 0));
    const auto at = [&](int minutes, int seconds = 0) { return day.addSecs((minutes * 60) + seconds); };
    const auto iso = [&](int minutes) { return at(minutes).toString(Qt::ISODate); };
    // An aircraft that offers MAVLink FTP, as PX4 and ArduPilot do.
    _disconnectMockLink();
    _connectMockLink(MAV_AUTOPILOT_PX4, MockConfiguration::FailNone, MockConfiguration::OptionFtpCapability);
    QVERIFY(vehicle());
    QVERIFY(vehicle()->capabilityBits() & MAV_PROTOCOL_CAPABILITY_FTP);
    QVERIFY(mockLink());
    MockLinkFTP* const ftp = mockLink()->mockLinkFTP();
    QVERIFY(ftp);
    ftp->setListDirectoryWithTimeSupported(listWithTime);
    ftp->setLogFiles({ { QStringLiteral("log_0.ulg"), 3000, static_cast<uint32_t>(at(20, 6).toSecsSinceEpoch()) },
                       { QStringLiteral("log_1.ulg"), 9000, static_cast<uint32_t>(at(45, 4).toSecsSinceEpoch()) },
                       { QStringLiteral("log_2.ulg"), 2000, static_cast<uint32_t>(at(54, 5).toSecsSinceEpoch()) } });

    const QString airframe = TakeoffCounter::airframeKey(vehicle());
    const auto flight = [&](int from, int to) {
        return QVariantMap{ { "takeoff", iso(from) }, { "landing", iso(to) }, { "seconds", (to - from) * 60 }, { "metres", 100.0 } };
    };
    QVERIFY(TakeoffCounter::writeFlights(airframe, { flight(52, 54), flight(50, 51), flight(25, 45), flight(0, 20) }));

    FlightRecords records(nullptr);
    QCOMPARE(records.records(QStringLiteral("2026-09-28"), QStringLiteral("2026-09-28")).size(), 4);
    // The page's record of the flight that took off at \a minutes, as it reads now.
    const auto record = [&](int minutes) {
        const QVariantList listed = records.records(QStringLiteral("2026-09-28"), QStringLiteral("2026-09-28"));
        for (const QVariant& entry : listed) {
            if (entry.toMap().value("takeoff").toString() == iso(minutes)) {
                return entry.toMap();
            }
        }
        return QVariantMap();
    };

    // Another airframe's flight is not this aircraft's to answer for.
    QVariantMap other = record(25);
    other.insert("airframe", QStringLiteral("sysid-250"));
    QVERIFY(!records.downloadVehicleLog(other));
    QVERIFY(!records.download().value("busy").toBool());
    QVERIFY(!records.download().value("error").toString().isEmpty());

    QList<int> percents;
    (void) connect(&records, &FlightRecords::downloadChanged, &records, [&] {
        percents.append(records.download().value("percent").toInt());
    });
    const auto fetch = [&](const QVariantMap& flight) {
        QVERIFY(!flight.isEmpty());
        percents.clear();
        QVERIFY(records.downloadVehicleLog(flight));
        QVERIFY(records.download().value("busy").toBool());
        QVERIFY(records.download().value("error").toString().isEmpty());
        QVERIFY_TRUE_WAIT(!records.download().value("busy").toBool(), TestTimeout::longMs());
    };
    // The log alone in the flight's folder, whole, and named in its record.
    const auto received = [&](int minutes, const QString& log) {
        QVERIFY2(records.download().value("error").toString().isEmpty(), qPrintable(records.download().value("error").toString()));
        QCOMPARE(records.download().value("percent").toInt(), 100);
        const QFileInfoList files = QDir(FlightRecords::vehicleLogDirectory(record(minutes))).entryInfoList(QDir::Files);
        QCOMPARE(files.size(), 1);
        QCOMPARE(readFile(files.first().filePath()), ftp->logFileContents(log));
        QCOMPARE(record(minutes).value("vehicleLog").toString(), files.first().fileName());
    };

    // B gets its own log, not A's that ended five minutes before it took off.
    fetch(record(25));
    if (QTest::currentTestFailed()) {
        return;
    }
    received(25, QStringLiteral("log_1.ulg"));
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY2(std::any_of(percents.cbegin(), percents.cend(), [](int percent) { return (percent > 0) && (percent < 100); }),
             "The fetch never reported a percent on its way");

    fetch(record(0));
    if (QTest::currentTestFailed()) {
        return;
    }
    received(0, QStringLiteral("log_0.ulg"));
    if (QTest::currentTestFailed()) {
        return;
    }

    // Z's own log is gone and C's ended after C took off: nothing is fetched rather than C's.
    fetch(record(50));
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(!records.download().value("error").toString().isEmpty());
    QVERIFY(QDir(FlightRecords::vehicleLogDirectory(record(50))).entryInfoList(QDir::Files).isEmpty());
    QVERIFY(!record(50).isEmpty());
    QVERIFY(!record(50).contains("vehicleLog"));
}

UT_REGISTER_TEST(FlightRecordsTest, TestLabel::Integration, TestLabel::Vehicle)
