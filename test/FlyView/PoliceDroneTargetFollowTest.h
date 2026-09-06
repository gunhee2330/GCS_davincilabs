#pragma once

#include "UnitTest.h"

class PoliceDroneTargetFollowTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _nadirCentreOfFrameIsBelowVehicle_test();
    void _nadirOffsetInFrameMapsToGroundOffset_test();
    void _obliqueWithLaserRangeUsesMeasuredHeight_test();
    void _rayAtOrAboveHorizonIsRejected_test();
    void _degenerateInputsAreRejected_test();
    void _gateOrdersItsChecks_test();
    void _gateAbortsOnLostTargetBeforeVehicleChecks_test();
    void _enableDefaultsOffAndReportsWhy_test();
};
