#include "TakeoffCounterTest.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "MockLink.h"
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

QJsonArray storedFlights(Vehicle *vehicle)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("PoliceDrone/TakeoffCount"));
    return QJsonDocument::fromJson(settings.value(airframeKey(vehicle) + QStringLiteral("-flights")).toByteArray()).array();
}

} // namespace

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

    const QJsonArray stored = storedFlights(vehicle());
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toObject().toVariantMap(), counter.flights().first().toMap());

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

void TakeoffCounterTest::_logIsLoadedAndKeepsLatestHundred_test()
{
    // A full log, as a station that has watched this airframe fly a hundred times leaves it:
    // newest first, so the oldest is the last entry.
    QJsonArray seeded;
    for (int i = 0; i < TakeoffCounter::kMaxFlights; ++i) {
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
    QCOMPARE(counter.flights().size(), TakeoffCounter::kMaxFlights);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 0.0);

    _fly(42.0);
    if (QTest::currentTestFailed()) {
        return;
    }

    QCOMPARE(counter.flights().size(), TakeoffCounter::kMaxFlights);
    QCOMPARE(counter.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);
    QCOMPARE(counter.flights().at(1).toMap().value(QStringLiteral("metres")).toDouble(), 0.0);
    QCOMPARE(counter.flights().last().toMap().value(QStringLiteral("metres")).toDouble(),
             static_cast<double>(TakeoffCounter::kMaxFlights - 2));

    const QJsonArray stored = storedFlights(vehicle());
    QCOMPARE(stored.size(), TakeoffCounter::kMaxFlights);
    QCOMPARE(stored.first().toObject().value(QStringLiteral("metres")).toDouble(), 42.0);

    // What the next start of the app reads.
    TakeoffCounter restarted(nullptr);
    restarted.init();
    QCOMPARE(restarted.flights().size(), TakeoffCounter::kMaxFlights);
    QCOMPARE(restarted.flights().first().toMap().value(QStringLiteral("metres")).toDouble(), 42.0);
}

UT_REGISTER_TEST(TakeoffCounterTest, TestLabel::Integration, TestLabel::Vehicle)
