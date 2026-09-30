#include "PoliceRcLinkTest.h"

#include "PoliceRcLink.h"

void PoliceRcLinkTest::_mapping_test()
{
    QCOMPARE(PoliceRcLink::percentFromStrength(-1), -1);
    QCOMPARE(PoliceRcLink::percentFromStrength(0), 0);
    QCOMPARE(PoliceRcLink::percentFromStrength(1), 1);
    QCOMPARE(PoliceRcLink::percentFromStrength(50), 50);
    QCOMPARE(PoliceRcLink::percentFromStrength(100), 100);
    QCOMPARE(PoliceRcLink::percentFromStrength(150), 100);
}

void PoliceRcLinkTest::_availability_test()
{
    PoliceRcLink link(nullptr);
    QVERIFY(!link.available());
    QCOMPARE(link.percent(), -1);

    link.handleLinkInfo(60, 12, 99);
    QVERIFY(link.available());
    QCOMPARE(link.percent(), 60);
    QCOMPARE(link.quality(), 12);
    QCOMPARE(link.validPercent(), 99);

    link.handleLinkInfo(-1, 12, 99);
    QVERIFY(!link.available());
    QCOMPARE(link.percent(), -1);

    // A reading nobody refreshes goes stale.
    link.handleLinkInfo(60, 12, 99);
    QVERIFY(link.available());
    QTRY_VERIFY_WITH_TIMEOUT(!link.available(), 5000);
}

void PoliceRcLinkTest::_connected_test()
{
    PoliceRcLink link(nullptr);
    QVERIFY(link.connected());
    link.handleConnected(false);
    QVERIFY(!link.connected());
    link.handleConnected(true);
    QVERIFY(link.connected());
}

UT_REGISTER_TEST_LIGHTWEIGHT(PoliceRcLinkTest, TestLabel::Unit)
