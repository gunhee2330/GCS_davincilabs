#include "FlyViewProximityRadarUITest.h"

#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "MockLink.h"
#include "Vehicle.h"

UT_REGISTER_TEST(FlyViewProximityRadarUITest, TestLabel::Integration)

void FlyViewProximityRadarUITest::_testRadarVisibleWithProximity()
{
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(MockConfiguration::OptionEnableProximity); },
        [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
            // The map shows proximity as edge glow rather than sectors around the vehicle, so the
            // item this looks for moved. What is under test did not: telemetry arrives, the map
            // says something is close.
            //
            // The glow also waits for arming, because a vehicle on the ground is surrounded and a
            // permanently lit map teaches an operator to ignore it. So this has to arm first, and
            // wait for the heartbeat to carry the state back.
            vehicle->setArmed(true, false /*showError*/);
            QVERIFY2(QTest::qWaitFor([vehicle] { return vehicle->armed(); }, 5000),
                     "Vehicle never reported armed");

            QQuickItem *const glow = findVisibleItem(_rootItem, QStringLiteral("policeDroneObstacleGlow"), 15000);
            QVERIFY2(glow, "Obstacle glow never became visible");
        });
}

void FlyViewProximityRadarUITest::_testRadarHiddenWithoutProximity()
{
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(); },
        [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
            // Armed, so arming is not what is keeping the glow away: without
            // OptionEnableProximity no DISTANCE_SENSOR ever arrives and the map has nothing to say.
            vehicle->setArmed(true, false /*showError*/);
            QVERIFY2(QTest::qWaitFor([vehicle] { return vehicle->armed(); }, 5000),
                     "Vehicle never reported armed");

            QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("policeDroneObstacleGlow"), 3000),
                     "Obstacle glow visible without proximity telemetry");
        });
}
