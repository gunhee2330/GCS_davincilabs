#pragma once

#include "UnitTest.h"

class PersonDetectorCoreTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testLetterboxKeepsAspect();
    void _testDecodeUndoesLetterboxAndSuppressesOverlap();
    void _testMergeDropsTheSamePersonSeenTwice();
};
