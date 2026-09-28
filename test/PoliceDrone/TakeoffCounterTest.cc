#include "TakeoffCounterTest.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QtEndian>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "Fact.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"

namespace {

/// Under the 2 m that TrajectoryPoints needs to count a move, so the climb and the descent add
/// nothing to flightDistance and every metre in it is one the test put there. Also under
/// TakeoffCounter's own 2 m altitude fallback, so the liftoff it counts is the flying edge.
constexpr double kHoverM = 1.5;

/// Long enough in the air that the duration rounds to whole seconds rather than to zero.
constexpr int kFlightMs = 1500;

QString airframeKey(Vehicle *vehicle)
{
    return (vehicle->vehicleUID() != 0) ? QStringLiteral("uid-%1").arg(QString::number(vehicle->vehicleUID(), 16))
                                         : QStringLiteral("sysid-%1").arg(vehicle->id());
}

QVariantList storedFlights(Vehicle *vehicle)
{
    return TakeoffCounter::readFlights(airframeKey(vehicle));
}

} // namespace

void TakeoffCounterTest::init()
{
    QVERIFY(QDir(TakeoffCounter::recordsDirectory()).removeRecursively());
    VehicleTest::init();
}

void TakeoffCounterTest::_setMockAltitude(double metres)
{
    // The flying transition creates QGCPressure, which warns on hosts without a backend.
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

    vehicle()->sendMavCommand(vehicle()->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false,
                              0, 0, 0, 0, 0, 0, static_cast<float>(metres));
}

void TakeoffCounterTest::_fly(double metres)
{
    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());

    _setMockAltitude(kHoverM);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
    vehicle()->updateFlightDistance(metres);
    QTest::qWait(kFlightMs);

    _setMockAltitude(0);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());

    mockLink()->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle()->armed(), TestTimeout::mediumMs());
}

void TakeoffCounterTest::_flightIsRecordedAtLanding_test()
{
    TakeoffCounter counter(nullptr);
    counter.init();
    QVERIFY(counter.flights().isEmpty());

    QSignalSpy flightsSpy(&counter, &TakeoffCounter::flightsChanged);
    QVERIFY(flightsSpy.isValid());

    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    _fly(850.0);
    if (QTest::currentTestFailed()) {
        return;
    }
    const QDateTime after = QDateTime::currentDateTime();

    // Once, at the landing: the disarm that followed on the pad did not add a second record.
    QCOMPARE(flightsSpy.count(), 1);
    QCOMPARE(counter.flights().size(), 1);

    const QVariantMap flight = counter.flights().first().toMap();
    const QDateTime takeoff = QDateTime::fromString(flight.value(QStringLiteral("takeoff")).toString(), Qt::ISODate);
    QVERIFY2(takeoff.isValid() && (takeoff >= before) && (takeoff <= after),
             qPrintable(QStringLiteral("takeoff reads %1, outside the %2 to %3 it was flown")
                            .arg(flight.value(QStringLiteral("takeoff")).toString(),
                                 before.toString(Qt::ISODate), after.toString(Qt::ISODate))));
    QCOMPARE(flight.value(QStringLiteral("seconds")).toInt(), counter.lastFlightSeconds());
    QVERIFY(counter.lastFlightSeconds() >= 1);
    QCOMPARE(flight.value(QStringLiteral("metres")).toDouble(), 850.0);
    QCOMPARE(vehicle()->flightDistance()->rawValue().toDouble(), 850.0);

    // The landing, to the second the duration was timed to.
    const QDateTime landing = QDateTime::fromString(flight.value(QStringLiteral("landing")).toString(), Qt::ISODate);
    QVERIFY2(landing.isValid() && (landing >= takeoff) && (landing <= after),
             qPrintable(QStringLiteral("landing reads %1").arg(flight.value(QStringLiteral("landing")).toString())));
    QVERIFY(qAbs(takeoff.secsTo(landing) - counter.lastFlightSeconds()) <= 1);
    QVERIFY2(qAbs(flight.value(QStringLiteral("maxAltitude")).toDouble() - kHoverM) < 0.2,
             qPrintable(QStringLiteral("maxAltitude reads %1 after a %2 m hover").arg(flight.value(QStringLiteral("maxAltitude")).toDouble()).arg(kHoverM)));
    QCOMPARE(flight.value(QStringLiteral("vehicle")).toString(), QStringLiteral("%1호기").arg(vehicle()->id()));

    // The flight's own telemetry log, named by the takeoff and holding stock tlog records: a
    // big-endian microsecond time inside the flight, then a MAVLink packet.
    const QString tlogName = takeoff.toString(QStringLiteral("yyyyMMdd_hhmmss")) + QStringLiteral(".tlog");
    QCOMPARE(flight.value(QStringLiteral("tlog")).toString(), tlogName);
    QFile tlog(QDir(SettingsManager::instance()->appSettings()->telemetrySavePath()).filePath(tlogName));
    QVERIFY2(tlog.open(QIODevice::ReadOnly), qPrintable(tlog.fileName() + QStringLiteral(": ") + tlog.errorString()));
    const QByteArray head = tlog.read(9);
    QCOMPARE(head.size(), 9);
    const qint64 firstMs = static_cast<qint64>(qFromBigEndian<quint64>(head.constData()) / 1000);
    QVERIFY(firstMs >= before.toMSecsSinceEpoch());
    QVERIFY(firstMs <= after.toMSecsSinceEpoch());
    QVERIFY((static_cast<quint8>(head.at(8)) == MAVLINK_STX) || (static_cast<quint8>(head.at(8)) == MAVLINK_STX_MAVLINK1));
    // Closed at the disarm: the messages that keep coming on the pad go nowhere near it.
    const qint64 closedSize = tlog.size();
    QTest::qWait(1000);
    QCOMPARE(QFileInfo(tlog.fileName()).size(), closedSize);

    const QVariantList stored = storedFlights(vehicle());
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toMap(), counter.flights().first().toMap());

    // The next cycle goes on top, and the vehicle's distance starts again from its arm.
    _fly(1234.0);
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(counter.flights().size(), 2);
    QCOMPARE(counter.flights().at(0).toMap().value(QStringLiteral("metres")).toDouble(), 1234.0);
    QCOMPARE(counter.flights().at(1).toMap().value(QStringLiteral("metres")).toDouble(), 850.0);
    QCOMPARE(storedFlights(vehicle()).size(), 2);
}

void TakeoffCounterTest::_landedFlickerKeepsOneRecord_test()
{
    TakeoffCounter counter(nullptr);
    counter.init();

    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());
    _setMockAltitude(kHoverM);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
    vehicle()->updateFlightDistance(300.0);
    QTest::qWait(kFlightMs);

    _setMockAltitude(0);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());
    QCOMPARE(counter.flights().size(), 1);
    const int firstSeconds = counter.flights().first().toMap().value(QStringLiteral("seconds")).toInt();

    // Up again without a disarm: the same cycle, flown further and longer.
    _setMockAltitude(kHoverM);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
    vehicle()->updateFlightDistance(400.0);
    QTest::qWait(kFlightMs);
    _setMockAltitude(0);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());

    mockLink()->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle()->armed(), TestTimeout::mediumMs());

    QCOMPARE(counter.flights().size(), 1);
    const QVariantMap flight = counter.flights().first().toMap();
    QCOMPARE(flight.value(QStringLiteral("metres")).toDouble(), 700.0);
    QVERIFY2(flight.value(QStringLiteral("seconds")).toInt() > firstSeconds,
             "The second landing of the cycle did not replace the first one's duration");
    QCOMPARE(storedFlights(vehicle()).size(), 1);
}

void TakeoffCounterTest::_settingsLogMovesToFileWithoutLimit_test()
{
    // A full log as the settings file held it, a hundred flights at its old limit: newest first,
    // so the oldest is the last entry.
    constexpr int kSettingsLimit = 100;
    QJsonArray seeded;
    for (int i = 0; i < kSettingsLimit; ++i) {
        seeded.append(QJsonObject{
            { QStringLiteral("takeoff"), QDateTime(QDate(2026, 1, 1), QTime(0, 0)).addDays(-i).toString(Qt::ISODate) },
            { QStringLiteral("seconds"), 60 },
            { QStringLiteral("metres"),  static_cast<double>(i) },
        });
    }
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("PoliceDrone/TakeoffCount"));
        settings.setValue(airframeKey(vehicle()) + QStringLiteral("-flights"),
                          QString::fromUtf8(QJsonDocument(seeded).toJson(QJsonDocument::Compact)));
    }

    TakeoffCounter counter(nullptr);
    counter.init();
    QCOMPARE(counter.flights().size(), kSettingsLimit);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 0.0);

    // Moved, once: into the records file, and out of the settings file.
    QCOMPARE(storedFlights(vehicle()).size(), kSettingsLimit);
    QCOMPARE(QVariant(storedFlights(vehicle())), QVariant(seeded.toVariantList()));
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("PoliceDrone/TakeoffCount"));
        QVERIFY2(!settings.contains(airframeKey(vehicle()) + QStringLiteral("-flights")),
                 "The settings file still holds the log after the move");
    }

    _fly(42.0);
    if (QTest::currentTestFailed()) {
        return;
    }

    // On top, and the oldest still there.
    QCOMPARE(counter.flights().size(), kSettingsLimit + 1);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);
    QCOMPARE(counter.flights().at(1).toMap().value(QStringLiteral("metres")).toDouble(), 0.0);
    QCOMPARE(counter.flights().last().toMap().value(QStringLiteral("metres")).toDouble(),
             static_cast<double>(kSettingsLimit - 1));

    const QVariantList stored = storedFlights(vehicle());
    QCOMPARE(stored.size(), kSettingsLimit + 1);
    QCOMPARE(stored.first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);

    // What the next start of the app reads.
    TakeoffCounter restarted(nullptr);
    restarted.init();
    QCOMPARE(restarted.flights().size(), kSettingsLimit + 1);
    QCOMPARE(restarted.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);
}

void TakeoffCounterTest::_unreadableRecordsAreNotOverwritten_test()
{
    // Cut off mid-record, as a disk fault or a hand edit might leave it.
    const QByteArray damaged("[{\"takeoff\":\"2026-09-01T10:00:00\",\"seconds\":");
    const QString path = QDir(TakeoffCounter::recordsDirectory()).filePath(airframeKey(vehicle()) + QStringLiteral(".json"));
    QVERIFY(QDir().mkpath(TakeoffCounter::recordsDirectory()));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(damaged), damaged.size());
    }

    bool read = true;
    QVERIFY(TakeoffCounter::readFlights(airframeKey(vehicle()), &read).isEmpty());
    QVERIFY2(!read, "A damaged records file read as no records");
    read = false;
    QVERIFY(TakeoffCounter::readFlights(QStringLiteral("sysid-none"), &read).isEmpty());
    QVERIFY2(read, "No records file read as a failure");

    TakeoffCounter counter(nullptr);
    counter.init();
    expectLogMessage("PoliceDrone.TakeoffCounter", QtWarningMsg, QRegularExpression(QStringLiteral("could not read the flight records")));
    _fly(42.0);
    if (QTest::currentTestFailed()) {
        return;
    }
    verifyExpectedLogMessage();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), damaged);
    QCOMPARE(counter.flights().size(), 1);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);
}

void TakeoffCounterTest::_takeoffTimeIsFirstLiftoff_test()
{
    TakeoffCounter counter(nullptr);
    counter.init();
    QVERIFY(counter.takeoffTime().isEmpty());

    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());
    QVERIFY2(counter.takeoffTime().isEmpty(), "Arming on the pad set a takeoff time");

    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    _setMockAltitude(kHoverM);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
    const QString first = counter.takeoffTime();
    const QDateTime takeoff = QDateTime::fromString(first, Qt::ISODate);
    QVERIFY2(takeoff.isValid() && (takeoff >= before) && (takeoff <= QDateTime::currentDateTime()),
             qPrintable(QStringLiteral("takeoffTime reads \"%1\" at the liftoff").arg(first)));
    QTest::qWait(kFlightMs);

    _setMockAltitude(0);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("takeoff")).toString(), first);
    QCOMPARE(counter.takeoffTime(), first);

    // Up again in the same cycle, more than a second later: still the first liftoff.
    _setMockAltitude(kHoverM);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
    QCOMPARE(counter.takeoffTime(), first);
    _setMockAltitude(0);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());

    mockLink()->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle()->armed(), TestTimeout::mediumMs());
    QCOMPARE(counter.takeoffTime(), first);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("takeoff")).toString(), first);

    QSignalSpy takeoffTimeSpy(&counter, &TakeoffCounter::takeoffTimeChanged);
    QVERIFY(takeoffTimeSpy.isValid());
    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());
    QVERIFY2(counter.takeoffTime().isEmpty(), "The next arm kept the last cycle's takeoff time");
    QCOMPARE(takeoffTimeSpy.count(), 1);

    mockLink()->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle()->armed(), TestTimeout::mediumMs());
}

UT_REGISTER_TEST(TakeoffCounterTest, TestLabel::Integration, TestLabel::Vehicle)
