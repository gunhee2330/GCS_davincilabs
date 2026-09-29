#pragma once

#include "UnitTest.h"

class PinGateTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _defaultAnswersUntilChanged_test();
    void _changeRequiresCurrentAndWellFormedNext_test();
    void _lockoutAfterRepeatedWrongGuesses_test();
    void _gatesKeepSeparatePins_test();
};
