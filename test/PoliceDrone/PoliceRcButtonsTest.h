#pragma once

#include "UnitTest.h"

class PoliceRcButtonsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void init() override;
    void cleanup() override;

    void _heldFor_test();
    void _center_test();
    void _talk_test();
    void _photo_test();
    void _recordHold_test();
    void _recordOnLateRelease_test();
    void _baselineIgnoresUnseenPress_test();
    void _gapLetsGoOfTalk_test();
    void _gapThenReleasedStaysOff_test();
    void _talkEndsOffTheHighBand_test();
    void _handsetStreamWins_test();
    void _handsetLostHandsOver_test();
    void _sharedChannelGivesWay_test();
    void _recordAtTwoSecondsWithoutReadings_test();
    void _channelOff_test();
};
