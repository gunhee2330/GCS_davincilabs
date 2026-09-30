#pragma once

#include "UnitTest.h"

class PoliceRcKeysTest : public UnitTest
{
    Q_OBJECT

private slots:
    void cleanup() override;

    void _positionFor_test();
    void _imageTypeFor_test();
    void _baseline_test();
    void _keys_test();
    void _wideWithAiOn_test();
    void _ignoredReadings_test();
    void _settingChangeRebaselines_test();
};
