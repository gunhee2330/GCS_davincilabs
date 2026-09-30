#include "PoliceRcButtonsTest.h"

#include <QtCore/QRegularExpression>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "PoliceRcButtons.h"
#include "SettingsManager.h"
#include "SiyiCameraSettings.h"

namespace {

constexpr int kCenter = 12;
constexpr int kTalk = 6;
constexpr int kCamera = 10;
constexpr int kReleased = 1050;
constexpr int kHeld = 1950;

SiyiCameraSettings* settings()
{
    return SettingsManager::instance()->siyiCameraSettings();
}

/// Sixteen channels with every button released, except \a channel (1-based) at \a us.
QVector<int> reading(int channel = 0, int us = kReleased)
{
    QVector<int> values(16, kReleased);
    if (channel > 0) {
        values[channel - 1] = us;
    }
    return values;
}

}  // namespace

void PoliceRcButtonsTest::init()
{
    UnitTest::init();
    // Talk and camera ship off; the tests use the channels S1 and S2 are moved onto in UniGCS.
    settings()->rcKeyCenterChannel()->setRawValue(kCenter);
    settings()->rcKeyTalkChannel()->setRawValue(kTalk);
    settings()->rcKeyCameraChannel()->setRawValue(kCamera);
}

void PoliceRcButtonsTest::cleanup()
{
    settings()->rcKeyCenterChannel()->setRawValue(kCenter);
    settings()->rcKeyTalkChannel()->setRawValue(0);
    settings()->rcKeyCameraChannel()->setRawValue(0);
    UnitTest::cleanup();
}

void PoliceRcButtonsTest::_heldFor_test()
{
    QCOMPARE(PoliceRcButtons::heldFor(kHeld, false), true);
    QCOMPARE(PoliceRcButtons::heldFor(kReleased, true), false);
    QCOMPARE(PoliceRcButtons::heldFor(1500, true), true);
    QCOMPARE(PoliceRcButtons::heldFor(1500, false), false);
    QCOMPARE(PoliceRcButtons::heldFor(0, true), true);
    QCOMPARE(PoliceRcButtons::heldFor(65535, false), false);
}

void PoliceRcButtonsTest::_center_test()
{
    PoliceRcButtons buttons;
    QSignalSpy spy(&buttons, &PoliceRcButtons::centerPressed);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kCenter, kHeld), 100);
    QCOMPARE(spy.count(), 1);
    buttons.handleChannelsAt(reading(kCenter, kHeld), 200);
    buttons.handleChannelsAt(reading(), 300);
    QCOMPARE(spy.count(), 1);
    buttons.handleChannelsAt(reading(kCenter, kHeld), 400);
    QCOMPARE(spy.count(), 2);
}

void PoliceRcButtonsTest::_talk_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 900);
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(released.count(), 0);
    buttons.handleChannelsAt(reading(), 1000);
    QCOMPARE(released.count(), 1);
}

void PoliceRcButtonsTest::_photo_test()
{
    PoliceRcButtons buttons;
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);
    QSignalSpy record(&buttons, &PoliceRcButtons::recordingToggleRequested);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 100);
    QCOMPARE(photo.count(), 0);
    buttons.handleChannelsAt(reading(), 600);
    QCOMPARE(photo.count(), 1);
    QCOMPARE(record.count(), 0);
}

void PoliceRcButtonsTest::_recordHold_test()
{
    PoliceRcButtons buttons;
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);
    QSignalSpy record(&buttons, &PoliceRcButtons::recordingToggleRequested);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 100);
    for (qint64 t = 200; t < 2100; t += 100) {
        buttons.handleChannelsAt(reading(kCamera, kHeld), t);
    }
    QCOMPARE(record.count(), 0);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 2100);
    QCOMPARE(record.count(), 1);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 2500);
    buttons.handleChannelsAt(reading(), 2600);
    QCOMPARE(record.count(), 1);
    QCOMPARE(photo.count(), 0);
}

void PoliceRcButtonsTest::_recordOnLateRelease_test()
{
    PoliceRcButtons buttons;
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);
    QSignalSpy record(&buttons, &PoliceRcButtons::recordingToggleRequested);

    // Readings every 700 ms, as when the rate request has not taken yet: the hold spans the two
    // second mark between readings and is judged by its length on release.
    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 700);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 1400);
    buttons.handleChannelsAt(reading(kCamera, kHeld), 2100);
    QCOMPARE(record.count(), 0);
    buttons.handleChannelsAt(reading(), 2800);
    QCOMPARE(record.count(), 1);
    QCOMPARE(photo.count(), 0);
}

void PoliceRcButtonsTest::_baselineIgnoresUnseenPress_test()
{
    PoliceRcButtons buttons;
    QSignalSpy center(&buttons, &PoliceRcButtons::centerPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);

    QVector<int> allHeld = reading();
    allHeld[kCenter - 1] = kHeld;
    allHeld[kTalk - 1] = kHeld;
    allHeld[kCamera - 1] = kHeld;
    buttons.handleChannelsAt(allHeld, 0);
    buttons.handleChannelsAt(reading(), 100);
    QCOMPARE(center.count(), 0);
    QCOMPARE(released.count(), 0);
    QCOMPARE(photo.count(), 0);
}

void PoliceRcButtonsTest::_gapLetsGoOfTalk_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    QCOMPARE(pressed.count(), 1);
    // The link was gone longer than kStaleMsecs: the talk ends, and since the key is still down
    // on the first reading back, it goes on.
    const qint64 back = 100 + PoliceRcButtons::kStaleMsecs + 500;
    buttons.handleChannelsAt(reading(kTalk, kHeld), back);
    QCOMPARE(released.count(), 1);
    QCOMPARE(pressed.count(), 2);
    buttons.handleChannelsAt(reading(), back + 100);
    QCOMPARE(released.count(), 2);
}

void PoliceRcButtonsTest::_gapThenReleasedStaysOff_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    const qint64 back = 100 + PoliceRcButtons::kStaleMsecs + 500;
    buttons.handleChannelsAt(reading(), back);
    QCOMPARE(released.count(), 1);
    // A later press is a press like any other.
    buttons.handleChannelsAt(reading(kTalk, kHeld), back + 100);
    QCOMPARE(pressed.count(), 2);
}

void PoliceRcButtonsTest::_talkEndsOffTheHighBand_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    QCOMPARE(pressed.count(), 1);
    // A centred switch or a failsafe value is not a held key.
    buttons.handleChannelsAt(reading(kTalk, 1500), 200);
    QCOMPARE(released.count(), 1);
}

void PoliceRcButtonsTest::_handsetStreamWins_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleHandsetChannelsAt(reading(), 0);
    QVERIFY(buttons.handsetLive());
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    QCOMPARE(pressed.count(), 0);
    buttons.handleHandsetChannelsAt(reading(kTalk, kHeld), 200);
    QCOMPARE(pressed.count(), 1);
    buttons.handleChannelsAt(reading(), 300);
    QCOMPARE(released.count(), 0);
    buttons.handleHandsetChannelsAt(reading(), 400);
    QCOMPARE(released.count(), 1);
}

void PoliceRcButtonsTest::_handsetLostHandsOver_test()
{
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy released(&buttons, &PoliceRcButtons::talkReleased);

    buttons.handleHandsetChannelsAt(reading(), 0);
    buttons.handleHandsetChannelsAt(reading(kTalk, kHeld), 100);
    QCOMPARE(pressed.count(), 1);
    buttons.handleHandsetLost();
    QVERIFY(!buttons.handsetLive());
    QCOMPARE(released.count(), 1);
    // RC_CHANNELS takes over and still shows the key down: the talk goes on.
    buttons.handleChannelsAt(reading(kTalk, kHeld), 500);
    QCOMPARE(pressed.count(), 2);
    buttons.handleChannelsAt(reading(), 700);
    QCOMPARE(released.count(), 2);
}

void PoliceRcButtonsTest::_sharedChannelGivesWay_test()
{
    // Talk on L1's channel, camera on a PoliceRcKeys key's channel: both give way.
    settings()->rcKeyTalkChannel()->setRawValue(kCenter);
    settings()->rcKeyCameraChannel()->setRawValue(settings()->rcKeyZoomChannel()->rawValue().toInt());
    const QRegularExpression offPattern(QStringLiteral("already a key"));
    expectLogMessage("PoliceDrone.RcButtons", QtWarningMsg, offPattern);
    expectLogMessage("PoliceDrone.RcButtons", QtWarningMsg, offPattern);
    PoliceRcButtons buttons;
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QSignalSpy center(&buttons, &PoliceRcButtons::centerPressed);
    QSignalSpy talk(&buttons, &PoliceRcButtons::talkPressed);
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);

    const int zoom = settings()->rcKeyZoomChannel()->rawValue().toInt();
    buttons.handleHandsetChannelsAt(reading(), 0);
    buttons.handleHandsetChannelsAt(reading(kCenter, kHeld), 100);
    buttons.handleHandsetChannelsAt(reading(zoom, kHeld), 200);
    buttons.handleHandsetChannelsAt(reading(), 300);
    QCOMPARE(center.count(), 1);
    QCOMPARE(talk.count(), 0);
    QCOMPARE(photo.count(), 0);
}

void PoliceRcButtonsTest::_recordAtTwoSecondsWithoutReadings_test()
{
    PoliceRcButtons buttons;
    QSignalSpy photo(&buttons, &PoliceRcButtons::photoRequested);
    QSignalSpy record(&buttons, &PoliceRcButtons::recordingToggleRequested);

    // The handset sends nothing while the button just stays down.
    buttons.handleHandsetChannelsAt(reading(), 0);
    buttons.handleHandsetChannelsAt(reading(kCamera, kHeld), 100);
    QVERIFY(record.wait(static_cast<int>(PoliceRcButtons::kRecordHoldMsecs) + 1000));
    QCOMPARE(record.count(), 1);
    buttons.handleHandsetChannelsAt(reading(), 100 + PoliceRcButtons::kRecordHoldMsecs + 500);
    QCOMPARE(record.count(), 1);
    QCOMPARE(photo.count(), 0);
}

void PoliceRcButtonsTest::_channelOff_test()
{
    settings()->rcKeyTalkChannel()->setRawValue(0);
    PoliceRcButtons buttons;
    QSignalSpy pressed(&buttons, &PoliceRcButtons::talkPressed);

    buttons.handleChannelsAt(reading(), 0);
    buttons.handleChannelsAt(reading(kTalk, kHeld), 100);
    QCOMPARE(pressed.count(), 0);
}

UT_REGISTER_TEST(PoliceRcButtonsTest, TestLabel::Unit)
