#pragma once

#include "UnitTest.h"

class SiyiAiControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _cancelledTargetStopsTracking_test();
    void _cancelAsksTheModuleFirst_test();
    void _cancelGoesOutWhenTheModuleNeverAnswers_test();
    void _lateTrackingStateDoesNotCancelANewTarget_test();
    void _staleTrackingStateDoesNotConsumeALaterCancel_test();
    void _modelSwapWithTheSameClassCountDropsTheMapping_test();
    void _unknownClassNamesReadAsUnknownNotZero_test();
    void _moduleCountsReachProperties_test();
    void _saturatedCountIsFlagged_test();
    void _malformedCountFramesKeepLastGoodNumbers_test();
    void _unreadableTrackingStateDoesNotSwallowTheCancel_test();
    void _countingOnAckIsNotACountOfZero_test();
    void _classCountMismatchDoesNotLoopTheLink_test();
    void _countingOffDoesNotLoopTheLink_test();
    void _countingIsRestartedWhenTheModuleDropsItsFlag_test();
    void _startAckIsToldApartByItsZeroRowNotItsOrder_test();
    void _streamTooLargeSurvivesTheStateReply_test();
    void _dragKeepsAnAcceptedPointPick_test();
    void _dragSendsTheBoxWhenThePointIsRefused_test();
    void _dragSendsTheBoxWhenThePointGetsNoReply_test();
    void _dragKeepsTheClassTheStreamReports_test();
    void _cancelDropsAPendingDragBox_test();
    void _dragCancelsAHeldTargetFirst_test();
    void _dragBoxAnswersDecideTheResult_test();
    void _dragRetriesOnceWhenTheModuleIsStillTracking_test();
    void _dragRefusalsSendNoBox_test();
    void _dragBoxRefusedAsHeldAfterATimeoutIsASuccess_test();
};
