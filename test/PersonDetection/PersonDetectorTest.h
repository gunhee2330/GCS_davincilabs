#pragma once

#include "UnitTest.h"

class PersonDetectorTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testDetectBus();
    void _testMailboxDropsStale();
};
