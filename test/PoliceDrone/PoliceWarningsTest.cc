#include "PoliceWarningsTest.h"

#include <algorithm>

#include <QtCore/QtNumeric>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "MAVLinkEventManager.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "PoliceWarnings.h"
#include "Vehicle.h"

namespace {

/// The value as the aircraft reports it: the fact changes the way a PARAM_VALUE changes it, with
/// nothing written back. MockLink's ArduPilot set arrives over FTP and rejects a PARAM_SET for a
/// FENCE_ parameter it never loaded, so a round trip would test the mock rather than the limits.
bool setParam(Vehicle* vehicle, const QString& name, double value)
{
    ParameterManager* const params = vehicle ? vehicle->parameterManager() : nullptr;
    if (!params || !params->parameterExists(ParameterManager::defaultComponentId, name)) {
        return false;
    }
    Fact* const fact = params->getParameter(ParameterManager::defaultComponentId, name);
    if (!fact) {
        return false;
    }
    fact->containerSetRawValue(value);
    return true;
}

/// A charge state for pack 3, one the mock never sends itself.
void sendBatteryState(MockLink* mockLink, Vehicle* vehicle, MAV_BATTERY_CHARGE_STATE state)
{
    uint16_t voltages[10];
    std::fill(std::begin(voltages), std::end(voltages), UINT16_MAX);
    uint16_t voltagesExt[4]{};
    mavlink_message_t msg{};
    (void) mavlink_msg_battery_status_pack_chan(
        static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, mockLink->outgoingMavlinkChannel(), &msg,
        3, MAV_BATTERY_FUNCTION_ALL, MAV_BATTERY_TYPE_LIPO, INT16_MAX, voltages, -1, -1, -1, 20, 0, state,
        voltagesExt, 0, 0);
    mockLink->respondWithMavlinkMessage(msg);
}

}  // namespace

void PoliceWarningsTest::_px4Limits_test()
{
    QVERIFY(waitForParametersReady());

    PoliceWarnings warnings;
    QCOMPARE(warnings.altitudeLimit(), PoliceWarnings::kDefaultCeilingM);
    QVERIFY(qIsNaN(warnings.radiusLimit()));
    QCOMPARE(warnings.windLimit(), PoliceWarnings::kDefaultWindWarnMps);

    // MockLink's PX4 set carries the three at their disabled values: 0, 0 and -1.
    warnings.setVehicle(vehicle());
    QCOMPARE(warnings.altitudeLimit(), PoliceWarnings::kDefaultCeilingM);
    QVERIFY(qIsNaN(warnings.radiusLimit()));
    QCOMPARE(warnings.windLimit(), PoliceWarnings::kDefaultWindWarnMps);

    QVERIFY(setParam(vehicle(), QStringLiteral("GF_MAX_VER_DIST"), 120));
    QVERIFY(setParam(vehicle(), QStringLiteral("GF_MAX_HOR_DIST"), 500));
    QVERIFY(setParam(vehicle(), QStringLiteral("COM_WIND_WARN"), 8));
    QCOMPARE(warnings.altitudeLimit(), 120.0);
    QCOMPARE(warnings.radiusLimit(), 500.0);
    QCOMPARE(warnings.windLimit(), 8.0);
}

void PoliceWarningsTest::_arduPilotFenceLimits_test()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    _disconnectMockLink();
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(waitForParametersReady());

    PoliceWarnings warnings;
    warnings.setVehicle(vehicle());

    // Copter's offline set: FENCE_ENABLE 0, FENCE_TYPE 7, FENCE_ALT_MAX 100, FENCE_RADIUS 150.
    QCOMPARE(warnings.altitudeLimit(), PoliceWarnings::kDefaultCeilingM);
    QVERIFY(qIsNaN(warnings.radiusLimit()));
    QCOMPARE(warnings.windLimit(), PoliceWarnings::kDefaultWindWarnMps);

    QVERIFY(setParam(vehicle(), QStringLiteral("FENCE_ENABLE"), 1));
    QCOMPARE(warnings.altitudeLimit(), 100.0);
    QCOMPARE(warnings.radiusLimit(), 150.0);

    QVERIFY(setParam(vehicle(), QStringLiteral("FENCE_TYPE"), 2));   // circle only
    QCOMPARE(warnings.altitudeLimit(), PoliceWarnings::kDefaultCeilingM);
    QCOMPARE(warnings.radiusLimit(), 150.0);

    QVERIFY(setParam(vehicle(), QStringLiteral("FENCE_TYPE"), 1));   // altitude only
    QCOMPARE(warnings.altitudeLimit(), 100.0);
    QVERIFY(qIsNaN(warnings.radiusLimit()));
}

void PoliceWarningsTest::_altitudeAndRadius_test()
{
    QVERIFY(waitForParametersReady());

    PoliceWarnings warnings;
    warnings.setVehicle(vehicle());
    QSignalSpy spoke(&warnings, &PoliceWarnings::spoke);
    QVERIFY(spoke.isValid());

    // On the ground nothing is over anything.
    warnings.setAltitude(200);
    QVERIFY(warnings.altitudeText().isEmpty());
    QCOMPARE(spoke.count(), 0);

    warnings.setFlying(true);
    QCOMPARE(warnings.altitudeText(), QStringLiteral("제한 고도 160 m를 넘었습니다"));
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Altitude limit exceeded. Above 160 meters."));

    // Inside the 2 m band it holds, and a reading that stays up says nothing more.
    warnings.setAltitude(159);
    warnings.setAltitude(161);
    warnings.setAltitude(158.5);
    QVERIFY(!warnings.altitudeText().isEmpty());
    warnings.setAltitude(qQNaN());
    QVERIFY2(!warnings.altitudeText().isEmpty(), "A dropout took the banner down");
    QCOMPARE(spoke.count(), 0);

    warnings.setAltitude(158);
    QVERIFY(warnings.altitudeText().isEmpty());
    warnings.setAltitude(160.5);
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // A raised limit takes it down with no telemetry change; landing takes it down as well.
    QVERIFY(setParam(vehicle(), QStringLiteral("GF_MAX_VER_DIST"), 300));
    QVERIFY(warnings.altitudeText().isEmpty());
    QVERIFY(setParam(vehicle(), QStringLiteral("GF_MAX_VER_DIST"), 120));
    QCOMPARE(warnings.altitudeText(), QStringLiteral("제한 고도 120 m를 넘었습니다"));
    QCOMPARE(spoke.count(), 1);
    warnings.setFlying(false);
    QVERIFY(warnings.altitudeText().isEmpty());
    warnings.setAltitude(10);
    warnings.setFlying(true);
    spoke.clear();

    // No radius limit, no radius banner however far out.
    warnings.setHomeDistance(5000);
    QVERIFY(warnings.radiusText().isEmpty());

    QVERIFY(setParam(vehicle(), QStringLiteral("GF_MAX_HOR_DIST"), 500));
    QCOMPARE(warnings.radiusText(), QStringLiteral("제한 반경 500 m를 넘었습니다"));
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Distance limit exceeded. Beyond 500 meters."));

    warnings.setHomeDistance(499);
    QVERIFY(!warnings.radiusText().isEmpty());
    warnings.setHomeDistance(498);
    QVERIFY(warnings.radiusText().isEmpty());
    warnings.setHomeDistance(500);
    QVERIFY2(warnings.radiusText().isEmpty(), "Exactly on the limit is not over it");
    warnings.setHomeDistance(501);
    QVERIFY(!warnings.radiusText().isEmpty());
    QCOMPARE(spoke.count(), 1);
}

void PoliceWarningsTest::_wind_test()
{
    QVERIFY(waitForParametersReady());

    PoliceWarnings warnings;
    warnings.setVehicle(vehicle());
    QSignalSpy spoke(&warnings, &PoliceWarnings::spoke);
    QVERIFY(spoke.isValid());

    warnings.setWindSpeed(12);
    QVERIFY(warnings.windText().isEmpty());
    warnings.setWindSpeed(0);

    warnings.setFlying(true);
    warnings.setWindSpeed(9.9);
    QVERIFY(warnings.windText().isEmpty());
    warnings.setWindSpeed(10);
    QCOMPARE(warnings.windText(), QStringLiteral("제자리 유지가 어렵습니다"));
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("High wind warning. Unable to hold position."));

    warnings.setWindSpeed(9.0);
    QVERIFY(!warnings.windText().isEmpty());
    warnings.setWindSpeed(8.9);
    QVERIFY(warnings.windText().isEmpty());

    QVERIFY(setParam(vehicle(), QStringLiteral("COM_WIND_WARN"), 8));
    QVERIFY(!warnings.windText().isEmpty());
    QCOMPARE(spoke.count(), 1);
}

void PoliceWarningsTest::_battery_test()
{
    PoliceWarnings warnings;
    QSignalSpy spoke(&warnings, &PoliceWarnings::spoke);
    QVERIFY(spoke.isValid());

    // Flying or not: the top bar changes colour on the ground too.
    warnings.setBatteryPercent(30);
    QVERIFY(warnings.batteryText().isEmpty());
    warnings.setBatteryLevel(1);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 30%, 복귀를 준비하십시오"));
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Low battery, 30 percent remaining. Prepare to return."));

    // The bar going white at 31 % holds the banner; its going orange again says nothing.
    warnings.setBatteryPercent(31);
    warnings.setBatteryLevel(0);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 31%, 복귀를 준비하십시오"));
    warnings.setBatteryPercent(30);
    warnings.setBatteryLevel(1);
    QCOMPARE(spoke.count(), 0);
    warnings.setBatteryPercent(32);
    warnings.setBatteryLevel(0);
    QVERIFY(warnings.batteryText().isEmpty());

    // Straight to critical, and the line follows the pack down without another word.
    warnings.setBatteryPercent(20);
    warnings.setBatteryLevel(2);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 20%, 즉시 복귀하십시오"));
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Low battery, 20 percent remaining. Return immediately."));
    warnings.setBatteryPercent(18.6);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 19%, 즉시 복귀하십시오"));

    // Critical steps down to low only 2 % above where it went critical, silently, and back up
    // to critical is a new rise.
    warnings.setBatteryPercent(21);
    warnings.setBatteryLevel(1);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 21%, 즉시 복귀하십시오"));
    warnings.setBatteryPercent(22);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 22%, 복귀를 준비하십시오"));
    QCOMPARE(spoke.count(), 0);
    warnings.setBatteryLevel(2);
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // A pack that reports no percentage: the aircraft's word alone, and the banner follows it.
    warnings.setBatteryLevel(0);
    warnings.setBatteryPercent(qQNaN());
    QVERIFY(warnings.batteryText().isEmpty());
    warnings.setBatteryLevel(1);
    QCOMPARE(warnings.batteryText(), QStringLiteral("복귀를 준비하십시오"));
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Low battery. Prepare to return."));
    warnings.setBatteryLevel(0);
    QVERIFY(warnings.batteryText().isEmpty());
}

void PoliceWarningsTest::_dismiss_test()
{
    PoliceWarnings warnings;
    QSignalSpy spoke(&warnings, &PoliceWarnings::spoke);
    QVERIFY(spoke.isValid());
    QSignalSpy texts(&warnings, &PoliceWarnings::textsChanged);
    QVERIFY(texts.isValid());

    // No vehicle: the 160 m ceiling and the 10 m/s wind.
    warnings.setFlying(true);
    warnings.setBatteryPercent(25);
    warnings.setBatteryLevel(1);
    warnings.setAltitude(170);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 25%, 복귀를 준비하십시오"));
    QVERIFY(!warnings.batteryCritical());
    QVERIFY(!warnings.altitudeText().isEmpty());
    QCOMPARE(spoke.count(), 2);
    spoke.clear();

    // One tap hides the battery alone, says nothing, and the pack draining does not undo it.
    texts.clear();
    warnings.dismiss(PoliceWarnings::Battery);
    QCOMPARE(texts.count(), 1);
    QVERIFY(warnings.batteryText().isEmpty());
    QVERIFY(!warnings.altitudeText().isEmpty());
    warnings.setBatteryPercent(22);
    QVERIFY(warnings.batteryText().isEmpty());
    QCOMPARE(spoke.count(), 0);

    // Low turning critical is an escalation: back on screen and spoken.
    warnings.setBatteryPercent(18);
    warnings.setBatteryLevel(2);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 18%, 즉시 복귀하십시오"));
    QVERIFY(warnings.batteryCritical());
    QCOMPARE(spoke.count(), 1);
    QCOMPARE(spoke.takeFirst().at(0).toString(), QStringLiteral("Low battery, 18 percent remaining. Return immediately."));

    // Critical dismissed, then easing back to low is no escalation and stays hidden; critical
    // again is one.
    warnings.dismiss(PoliceWarnings::Battery);
    warnings.setBatteryPercent(21);
    warnings.setBatteryLevel(1);
    QVERIFY(!warnings.batteryCritical());
    QVERIFY(warnings.batteryText().isEmpty());
    QCOMPARE(spoke.count(), 0);
    warnings.setBatteryLevel(2);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 21%, 즉시 복귀하십시오"));
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // Cleared and back again shows again.
    warnings.dismiss(PoliceWarnings::Battery);
    warnings.setBatteryPercent(40);
    warnings.setBatteryLevel(0);
    QVERIFY(warnings.batteryText().isEmpty());
    warnings.setBatteryPercent(28);
    warnings.setBatteryLevel(1);
    QCOMPARE(warnings.batteryText(), QStringLiteral("잔량 28%, 복귀를 준비하십시오"));
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // Altitude: hidden while it stays over, a new limit included; back once it clears and returns.
    warnings.dismiss(PoliceWarnings::Altitude);
    QVERIFY(warnings.altitudeText().isEmpty());
    QVERIFY(!warnings.batteryText().isEmpty());
    warnings.setAltitude(175);
    QVERIFY(warnings.altitudeText().isEmpty());
    warnings.setAltitude(140);
    warnings.setAltitude(161);
    QCOMPARE(warnings.altitudeText(), QStringLiteral("제한 고도 160 m를 넘었습니다"));
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // Landing clears it as well.
    warnings.dismiss(PoliceWarnings::Altitude);
    warnings.setFlying(false);
    warnings.setFlying(true);
    QVERIFY(!warnings.altitudeText().isEmpty());
    QCOMPARE(spoke.count(), 1);
    spoke.clear();

    // A tap on a warning that is down is nothing to remember.
    texts.clear();
    warnings.dismiss(PoliceWarnings::Wind);
    QCOMPARE(texts.count(), 0);
    warnings.setWindSpeed(12);
    QCOMPARE(warnings.windText(), QStringLiteral("제자리 유지가 어렵습니다"));
    warnings.dismiss(PoliceWarnings::Wind);
    QVERIFY(warnings.windText().isEmpty());
    warnings.setWindSpeed(8);
    warnings.setWindSpeed(11);
    QVERIFY(!warnings.windText().isEmpty());
}

void PoliceWarningsTest::_stockBatteryVoice_test()
{
    // ArduPilot Copter 4.5's line and the failsafe notice it sends straight after, PX4 v1.17's two.
    for (const char* const text : { "Battery 1 is low 14.20V used 1234 mAh", "Battery 2 is critical 13.10V used 2500 mAh",
                                    "Battery Failsafe", "Low battery level, return advised",
                                    "Critical battery level, land now" }) {
        QVERIFY2(PoliceWarnings::isBatteryAnnouncement(QString::fromLatin1(text)), text);
    }
    // Neighbours that are not those two events, or say more than the sentence does.
    for (const char* const text : { "Battery Failsafe: Disarming", "Battery Failsafe: Continuing Landing",
                                    "Emergency battery level, land immediately", "Vehicle 1 battery 1 is powering off",
                                    "Radio Failsafe", "Low remaining flight time, return advised" }) {
        QVERIFY2(!PoliceWarnings::isBatteryAnnouncement(QString::fromLatin1(text)), text);
    }

    // Only while one watches this very vehicle.
    Vehicle* const watched = vehicle();
    QVERIFY(watched);
    QVERIFY(!PoliceWarnings::voicesBattery(watched));
    QVERIFY(!PoliceWarnings::voicesBattery(nullptr));
    {
        PoliceWarnings unbound;
        QVERIFY(!PoliceWarnings::voicesBattery(watched));
        QVERIFY(!PoliceWarnings::voicesBattery(nullptr));

        PoliceWarnings warnings;
        warnings.setVehicle(watched);
        QVERIFY(PoliceWarnings::voicesBattery(watched));
        warnings.setVehicle(nullptr);
        QVERIFY(!PoliceWarnings::voicesBattery(watched));
        warnings.setVehicle(watched);
        QVERIFY(PoliceWarnings::voicesBattery(watched));
    }
    QVERIFY(!PoliceWarnings::voicesBattery(watched));

    // A STATUSTEXT goes through Vehicle::_textMessageReceived, the one function that speaks a
    // status text, and it announces each one it handles with textMessageReceived. PX4 sends its
    // battery warnings as events instead, and those are listed without passing through it.
    MockLink* const mockLink = this->mockLink();
    QVERIFY(mockLink);
    MAVLinkEventManager* const events = watched->findChild<MAVLinkEventManager*>();
    QVERIFY(events);
    QSignalSpy handled(watched, &Vehicle::textMessageReceived);
    QVERIFY(handled.isValid());
    QSignalSpy spoken(watched, &Vehicle::spoke);
    QVERIFY(spoken.isValid());
    const QString lowText = QStringLiteral("Battery 1 is low 14.20V used 1234 mAh");
    // What the stock voice said of the two: the status text itself, and the third pack's state in
    // whatever language the app runs in, numbered only while the vehicle lists more than one pack.
    const QString packLow = Vehicle::tr("battery %1 level low");
    const auto spokenBattery = [&spoken, &lowText, &packLow] {
        return std::count_if(spoken.cbegin(), spoken.cend(), [&](const QList<QVariant>& args) {
            const QString said = args.first().toString();
            return (said == lowText) || said.contains(packLow.arg(QString())) || said.contains(packLow.arg(3));
        });
    };

    // Nobody watching: the stock voice speaks both. A pack's first state is only noted, so it is
    // OK before it is LOW.
    mockLink->sendStatusTextMessage(MAV_SEVERITY_WARNING, lowText);
    QVERIFY_TRUE_WAIT(handled.count() == 1, TestTimeout::mediumMs());
    QCOMPARE(spokenBattery(), 1);
    sendBatteryState(mockLink, watched, MAV_BATTERY_CHARGE_STATE_OK);
    sendBatteryState(mockLink, watched, MAV_BATTERY_CHARGE_STATE_LOW);
    QVERIFY_TRUE_WAIT(spokenBattery() == 2, TestTimeout::mediumMs());

    // Watched: neither. The line after them is handled only once both have been.
    PoliceWarnings dashboard;
    dashboard.setVehicle(watched);
    handled.clear();
    mockLink->sendStatusTextMessage(MAV_SEVERITY_WARNING, lowText);
    sendBatteryState(mockLink, watched, MAV_BATTERY_CHARGE_STATE_OK);
    sendBatteryState(mockLink, watched, MAV_BATTERY_CHARGE_STATE_LOW);
    mockLink->sendStatusTextMessage(MAV_SEVERITY_INFO, QStringLiteral("after the battery"));
    QVERIFY_TRUE_WAIT(handled.count() == 2, TestTimeout::mediumMs());
    QCOMPARE(handled.first().at(3).toString(), lowText);
    QCOMPARE(spokenBattery(), 2);

    handled.clear();
    const QString px4 = QStringLiteral("Low battery level, return advised");
    emit events->statusTextMessageFromEvent(MAV_COMP_ID_AUTOPILOT1, MAV_SEVERITY_WARNING, px4, QString());
    QVERIFY2(watched->formattedMessages().contains(px4), "The event never reached the message list");
    QTest::qWait(200);
    QCOMPARE(handled.count(), 0);
}

UT_REGISTER_TEST(PoliceWarningsTest, TestLabel::Integration, TestLabel::Vehicle)
