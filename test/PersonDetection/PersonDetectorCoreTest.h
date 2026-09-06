#pragma once

#include "UnitTest.h"

class PersonDetectorCoreTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testLetterboxKeepsAspect();
    void _testFillInputMatchesRecipe();
    void _testDecodeUndoesLetterboxAndSuppressesOverlap();
    void _testDecodeDetsLabelsMapsLabelsAndUndoesLetterbox();
    void _testDescriptorMapsClassIds();
    void _testDescriptorStatesLayoutAndPreprocess();
    void _testTileBoxesMapIntoFrame();
    void _testMergeDropsTheSamePersonSeenTwice();
};
