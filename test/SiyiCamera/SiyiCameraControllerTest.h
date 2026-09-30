#pragma once

#include "UnitTest.h"

class SiyiCameraControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _aiFollowNeedsTheGimbalToConfirm_test();
    void _aiFollowReportsEveryRefusalCode_test();
    void _aiFollowAcceptsAWiderReply_test();
    void _lateFollowAcceptanceAfterStopIsIgnored_test();
    void _linkLossLeavesFollowUnconfirmedNotOff_test();
    void _podReconnectedFiresOncePerReturn_test();
    void _stopFollowFreesTheSwitchAndIsRepeated_test();
    void _followStartsUnconfirmedNotOff_test();
    void _closingTheLinkStopsFollowAndSaysWhatItCouldNotSend_test();
    void _newFollowRequestDropsTheOldRefusal_test();
    void _newFollowRequestIsUnconfirmedNotOff_test();
    void _laserSwitchIsTheOperatorsChoice_test();
    void _podImageTypeIsPulledBackToWanted_test();
    void _zoomCrossesBetweenZoomAndWideAtOneX_test();
};
