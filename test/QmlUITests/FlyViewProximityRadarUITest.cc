#include "FlyViewProximityRadarUITest.h"

#include <QtCore/QRegularExpression>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "MockLink.h"
#include "Vehicle.h"

UT_REGISTER_TEST(FlyViewProximityRadarUITest, TestLabel::Integration)

void FlyViewProximityRadarUITest::_ignorePreexistingQmlWarnings()
{
    // The fly view instantiates PhotoVideoControl with no camera manager and every binding in it
    // reports the null. Present at HEAD as well, and nothing under test here touches that file.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/FlightMap/Widgets/PhotoVideoControl\\.qml:[0-9]+: "
                         "TypeError: Cannot read property '[A-Za-z0-9_]+' of (null|undefined)$")));

    // Also present at HEAD, from a font that sets both sizes.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^Both point size and pixel size set\\. Using pixel size\\.$")));

    // The stock plan view's map visuals are sometimes torn down mid-creation when the previous
    // slot's engine goes away, and the message lands in whichever slot is running by then. The
    // sentence is localised, so the pattern matches the files rather than the words.
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/[A-Za-z]+MapVisual\\.qml: ")));
}

void FlyViewProximityRadarUITest::_testRadarVisibleWithProximity()
{
    _ignorePreexistingQmlWarnings();

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
    _ignorePreexistingQmlWarnings();

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
