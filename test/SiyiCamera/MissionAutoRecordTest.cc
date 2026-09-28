#include "MissionAutoRecordTest.h"

#include <memory>

#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtTest/QTest>

#include "Fact.h"
#include "MissionAutoRecord.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"
#include "Vehicle.h"

namespace {

/// The pod as MissionAutoRecord sees it through SiyiCameraController: a record toggle that may be
/// lost on the way, and config replies that arrive only when the test sends one.
struct FakePod {
    bool recording = false;
    bool dropToggles = false;
    int toggles = 0;
    std::unique_ptr<MissionAutoRecord> autoRecord;

    FakePod()
        : autoRecord(std::make_unique<MissionAutoRecord>(nullptr, [this] {
              ++toggles;
              if (!dropToggles) {
                  recording = !recording;
              }
          }))
    {
        autoRecord->init();
    }

    /// A config reply
    void reply() { autoRecord->podState(recording); }
    /// The operator's record button: the app hears it, then the pod answers
    void operatorToggle()
    {
        autoRecord->operatorToggled();
        recording = !recording;
        reply();
    }
    void linkLost() { autoRecord->podState(std::nullopt); }
};

Fact *autoRecordSetting()
{
    return SettingsManager::instance()->siyiCameraSettings()->autoRecordMission();
}

} // namespace

void MissionAutoRecordTest::_takeOff()
{
    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());

    // The flying transition creates QGCPressure, which warns on hosts without a backend.
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

    // MockLink's takeoff sets the altitude to home plus param7 and reports itself in the air above home
    vehicle()->sendMavCommand(vehicle()->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false, 0, 0, 0, 0, 0, 0, 1.5F);
    QVERIFY_TRUE_WAIT(vehicle()->flying(), TestTimeout::mediumMs());
}

void MissionAutoRecordTest::_land()
{
    vehicle()->sendMavCommand(vehicle()->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false, 0, 0, 0, 0, 0, 0, 0.0F);
    QVERIFY_TRUE_WAIT(!vehicle()->flying(), TestTimeout::mediumMs());
}

void MissionAutoRecordTest::_setMode(const QString &mode)
{
    vehicle()->setFlightMode(mode);
    QVERIFY_TRUE_WAIT(vehicle()->flightMode() == mode, TestTimeout::mediumMs());
}

void MissionAutoRecordTest::_recordsTheMissionOnly_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    FakePod pod;
    pod.reply();

    _takeOff();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);

    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    QVERIFY(pod.recording);
    pod.reply();

    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 2);
    QVERIFY(!pod.recording);
    pod.reply();

    // Back up in the mission, then out of it into hold
    _takeOff();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 3);
    pod.reply();
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 4);
    QVERIFY(!pod.recording);
}

void MissionAutoRecordTest::_leavesTheOperatorsRecordingAlone_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    FakePod pod;
    pod.reply();

    // Started by hand before the mission
    pod.operatorToggle();
    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);
    QVERIFY(pod.recording);

    // Ours this time, stopped and started again by hand in the mission: the landing leaves it on
    pod.operatorToggle();
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    pod.reply();
    pod.operatorToggle();
    pod.operatorToggle();
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    QVERIFY(pod.recording);
}

void MissionAutoRecordTest::_settingSwitchesItInFlight_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(false);

    FakePod pod;
    pod.reply();

    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);

    autoRecordSetting()->setRawValue(true);
    QCOMPARE(pod.toggles, 1);
    pod.reply();

    autoRecordSetting()->setRawValue(false);
    QCOMPARE(pod.toggles, 2);
    QVERIFY(!pod.recording);
    pod.reply();

    // Disarmed in the air with it on: stopped as at a landing
    autoRecordSetting()->setRawValue(true);
    QCOMPARE(pod.toggles, 3);
    pod.reply();
    mockLink()->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle()->armed(), TestTimeout::mediumMs());
    QCOMPARE(pod.toggles, 4);
    QVERIFY(!pod.recording);
}

void MissionAutoRecordTest::_waitsForTheCameraToAnswer_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    // The operator is recording, but the pod has not answered yet
    FakePod pod;
    pod.recording = true;
    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);
    pod.reply();
    QCOMPARE(pod.toggles, 0);

    // The link drops across the next mission start and comes back on the same recording
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    pod.linkLost();
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);
    pod.reply();
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);
    QVERIFY(pod.recording);

    // Not recording this time: the start goes out on the first answer, not before it
    pod.operatorToggle();
    pod.linkLost();
    _takeOff();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 0);
    pod.reply();
    QCOMPARE(pod.toggles, 1);
    QVERIFY(pod.recording);
    pod.reply();

    // Landed while the link is down: stopped once the pod says it is still recording ours
    pod.linkLost();
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    pod.reply();
    QCOMPARE(pod.toggles, 2);
    QVERIFY(!pod.recording);
}

void MissionAutoRecordTest::_unconfirmedStartIsNotClaimed_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    FakePod pod;
    pod.reply();
    pod.dropToggles = true;

    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);

    // Every start is lost: sent again after each run of unconfirming replies, then given up
    for (int reply = 0; reply < MissionAutoRecord::kConfirmReplies * MissionAutoRecord::kStartAttempts; ++reply) {
        pod.reply();
    }
    QCOMPARE(pod.toggles, MissionAutoRecord::kStartAttempts);
    pod.reply();
    QCOMPARE(pod.toggles, MissionAutoRecord::kStartAttempts);

    // The operator records by hand later in the mission: the landing leaves it on
    pod.dropToggles = false;
    pod.operatorToggle();
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, MissionAutoRecord::kStartAttempts);
    QVERIFY(pod.recording);

    // Ours lost again, and the operator presses record before the pod has said anything
    pod.operatorToggle();
    pod.dropToggles = true;
    _takeOff();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, MissionAutoRecord::kStartAttempts + 1);
    pod.dropToggles = false;
    pod.operatorToggle();
    for (int reply = 0; reply < MissionAutoRecord::kConfirmReplies; ++reply) {
        pod.reply();
    }
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, MissionAutoRecord::kStartAttempts + 1);
    QVERIFY(pod.recording);
}

void MissionAutoRecordTest::_lateConfirmationIsStopped_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    FakePod pod;
    pod.reply();

    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);

    // Out of the mission before the pod said it started: stopped when it does
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    pod.reply();
    QCOMPARE(pod.toggles, 2);
    QVERIFY(!pod.recording);
}

void MissionAutoRecordTest::_unconfirmedStopIsSentAgain_test()
{
    const QVariant saved = autoRecordSetting()->rawValue();
    const auto restore = qScopeGuard([saved] { autoRecordSetting()->setRawValue(saved); });
    autoRecordSetting()->setRawValue(true);

    FakePod pod;
    pod.reply();

    _takeOff();
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 1);
    pod.reply();

    // Every stop is lost: sent again after each run of replies still recording, then given up
    pod.dropToggles = true;
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 2);
    for (int reply = 0; reply < MissionAutoRecord::kConfirmReplies * MissionAutoRecord::kStartAttempts; ++reply) {
        pod.reply();
    }
    QCOMPARE(pod.toggles, 1 + MissionAutoRecord::kStartAttempts);
    pod.reply();
    QCOMPARE(pod.toggles, 1 + MissionAutoRecord::kStartAttempts);
    QVERIFY(pod.recording);

    // The operator stops that one. Next mission the stop gets through: sent once
    pod.dropToggles = false;
    pod.operatorToggle();
    _takeOff();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 2 + MissionAutoRecord::kStartAttempts);
    pod.reply();
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 3 + MissionAutoRecord::kStartAttempts);
    QVERIFY(!pod.recording);
    for (int reply = 0; reply < MissionAutoRecord::kConfirmReplies * MissionAutoRecord::kStartAttempts; ++reply) {
        pod.reply();
    }
    QCOMPARE(pod.toggles, 3 + MissionAutoRecord::kStartAttempts);

    // Out and back into the mission with the stop lost: nothing more sent in the mission, and ours
    // is stopped at the landing
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 4 + MissionAutoRecord::kStartAttempts);
    pod.reply();
    pod.dropToggles = true;
    _setMode(vehicle()->pauseFlightMode());
    if (QTest::currentTestFailed()) return;
    _setMode(vehicle()->missionFlightMode());
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 5 + MissionAutoRecord::kStartAttempts);
    for (int reply = 0; reply < MissionAutoRecord::kConfirmReplies * MissionAutoRecord::kStartAttempts; ++reply) {
        pod.reply();
    }
    QCOMPARE(pod.toggles, 5 + MissionAutoRecord::kStartAttempts);
    QVERIFY(pod.recording);
    pod.dropToggles = false;
    _land();
    if (QTest::currentTestFailed()) return;
    QCOMPARE(pod.toggles, 6 + MissionAutoRecord::kStartAttempts);
    QVERIFY(!pod.recording);
}

UT_REGISTER_TEST(MissionAutoRecordTest, TestLabel::Integration, TestLabel::Vehicle)
