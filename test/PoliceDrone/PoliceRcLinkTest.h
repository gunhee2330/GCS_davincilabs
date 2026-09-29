#pragma once

#include "UnitTest.h"

class PoliceRcLinkTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _mapping_test();
    void _availability_test();
    void _connected_test();
};
