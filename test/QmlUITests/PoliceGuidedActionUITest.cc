#include "PoliceGuidedActionUITest.h"

#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "Fact.h"
#include "FactMetaData.h"
#include "FlyViewSettings.h"
#include "MAVLinkLib.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

UT_REGISTER_TEST(PoliceGuidedActionUITest, TestLabel::Integration)

namespace {

/// The hold button inside GuidedActionConfirm. Every guided command is sent from its
/// onActivated and nowhere else, so this item being reachable is what these tests are about.
const QString kConfirmButton = QStringLiteral("guidedActionConfirmButton");

/// The item that must be found above the confirm button: the police instance, not the one
/// the hidden stock toolbar holds.
const QString kConfirmHost = QStringLiteral("policeGuidedConfirmHost");

/// Tool strip entries. ToolStripHoverButton takes its objectName from the action's.
const QString kTakeoffButton      = QStringLiteral("policeToolTakeoff");
const QString kStartMissionButton = QStringLiteral("policeToolStartMission");

const QString kSlider    = QStringLiteral("guidedValueSlider");
const QString kDashboard = QStringLiteral("policeDroneDashboard");

/// QGCDelayButton.defaultDelay is 500 ms. The press must outlast it, with room for the
/// progress animation to reach 1.0 on the software backend.
constexpr int kHoldMs = 1500;

/// Bindings and the strip animations settle well inside this.
constexpr int kSettleMs = 1500;

bool hasAncestorNamed(QQuickItem *item, const QString &objectName)
{
    for (QQuickItem *ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->objectName() == objectName) {
            return true;
        }
    }
    return false;
}

}  // namespace

void PoliceGuidedActionUITest::_ignorePreexistingQmlWarnings()
{
    // The fly view instantiates PhotoVideoControl with no camera manager and every binding in
    // it reports the null. Present at HEAD as well, and nothing under test here touches that
    // file. The pattern names the file, so a warning out of the guided controls cannot be
    // swallowed by it.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/FlightMap/Widgets/PhotoVideoControl\\.qml:[0-9]+: "
                         "TypeError: Cannot read property '[A-Za-z0-9_]+' of (null|undefined)$")));

    // Also present at HEAD, from a font that sets both sizes.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^Both point size and pixel size set\\. Using pixel size\\.$")));
}

void PoliceGuidedActionUITest::_ignoreDownloadedMissionFontWarnings()
{
    // Eight of these arrive while a downloaded mission is drawn on the fly view map. Measured on
    // this machine with the police mission start entry removed from the tool strip, and in the
    // stock AppCloseWarningUITest slot that loads the same mock mission, so they are neither the
    // strip entry's nor the confirm host's.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^QFont::setPointSizeF: Point size <= 0 \\(0\\.000000\\), must be greater than 0$")));
}

bool PoliceGuidedActionUITest::_holdButton(const QString &objectName)
{
    QQuickItem *const item = findVisibleItem(_rootItem, objectName, 5000);
    if (!item) {
        QTest::qFail(qPrintable(QStringLiteral("%1 not visible, cannot hold it").arg(objectName)), __FILE__, __LINE__);
        return false;
    }

    for (QQuickItem *ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
        ancestor->ensurePolished();
    }
    const QPointF scenePos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    if ((scenePos.x() < 0) || (scenePos.x() >= _window->width()) || (scenePos.y() < 0) ||
        (scenePos.y() >= _window->height())) {
        QTest::qFail(qPrintable(QStringLiteral("%1 hold point (%2, %3) is outside the window (%4x%5)")
                                    .arg(objectName)
                                    .arg(scenePos.x())
                                    .arg(scenePos.y())
                                    .arg(_window->width())
                                    .arg(_window->height())),
                     __FILE__, __LINE__);
        return false;
    }

    const QPoint holdPoint(qFloor(scenePos.x()), qFloor(scenePos.y()));
    QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);
    QTest::qWait(kHoldMs);
    QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);
    return true;
}

void PoliceGuidedActionUITest::_grab(const QString &name)
{
    const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
    QVERIFY2(!dir.isEmpty(), "QGC_SCREENSHOT_DIR is not set");
    QVERIFY2(QDir().mkpath(dir), qPrintable(QStringLiteral("Cannot create %1").arg(dir)));

    QTest::qWait(kSettleMs);

    const QImage image = _window->grabWindow();
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Empty grab for %1").arg(name)));

    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    QVERIFY2(image.save(path), qPrintable(QStringLiteral("Cannot write %1").arg(path)));
}

void PoliceGuidedActionUITest::_testTakeoffRaisesConfirmControl()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        // Checked first: a control that is always up would pass the positive case below for
        // the wrong reason.
        QVERIFY2(!findVisibleItem(_rootItem, kConfirmButton, 1000),
                 "Confirm control was already up before any action was requested");

        // A disabled takeoff entry would leave the control down too, and would look exactly
        // like the regression this test is for.
        QVERIFY(verifyEnabled(kTakeoffButton, true, QStringLiteral("before pressing takeoff")));
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");

        QQuickItem *const confirmButton = findVisibleItem(_rootItem, kConfirmButton, 5000);
        QVERIFY2(confirmButton, "Confirm control never appeared after pressing takeoff");

        // findVisibleItem walks isVisible(), which is false while any ancestor is hidden, so
        // reaching here already rules out the toolbar-parented control. The size check catches
        // the other way it can be on screen and unusable.
        QVERIFY2((confirmButton->width() > 0) && (confirmButton->height() > 0),
                 "Confirm button is visible but has no area to press");

        // Two instances of the stock component exist and both claim guidedController.confirmDialog
        // on completion. This says the one on screen is ours.
        QVERIFY2(hasAncestorNamed(confirmButton, kConfirmHost),
                 "Confirm control on screen is not the police instance");
    });
}

void PoliceGuidedActionUITest::_testTakeoffAltitudeSliderIsOnTop()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000),
                 "Confirm control never appeared after pressing takeoff");

        // PX4 multirotors take off to a chosen altitude, so confirmAction raises the slider
        // alongside the control.
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 3000);
        QVERIFY2(slider, "Takeoff altitude slider never became visible");

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 3000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");

        // z only orders items against their own siblings, so the comparison below says nothing
        // unless these two share a parent. They do today; if that ever changes this fails
        // loudly rather than comparing two unrelated numbers.
        QQuickItem *const common = slider->parentItem();
        QVERIFY2(common && (common == dashboard->parentItem()),
                 "Slider and dashboard no longer share a parent - z cannot order them");

        // Strictly greater, not merely different: the regression was a tie at zOrderTopMost,
        // which Qt Quick breaks by declaration order, and the dashboard is declared second.
        QVERIFY2(slider->z() > dashboard->z(),
                 "Altitude slider sits at or below the dashboard and would be covered");
    });
}

void PoliceGuidedActionUITest::_testHoldConfirmSendsTakeoff()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        // PX4 sends the takeoff altitude as AMSL, so it refuses outright until the vehicle
        // altitude is known.
        QVERIFY_TRUE_WAIT(!qIsNaN(vehicle->altitudeAMSL()->rawValue().toDouble()), TestTimeout::longMs());

        // The takeoff puts the mock in the air, and the flying transition creates QGCPressure,
        // which warns on hosts without a pressure backend.
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000),
                 "Confirm control never appeared after pressing takeoff");

        mockLink->clearReceivedMavCommandCounts();

        // GuidedActionConfirm hands executeAction the slider reading and the controller converts
        // it to metres. Read it here, before the hold takes the slider away, so param7 can be
        // checked against the altitude that was on screen.
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 3000);
        QVERIFY2(slider, "Takeoff altitude slider never became visible");
        QVariant sliderOutput;
        QVERIFY2(QMetaObject::invokeMethod(slider, "getOutputValue", Q_RETURN_ARG(QVariant, sliderOutput)),
                 "Could not read the slider output value");
        const double sliderMeters = FactMetaData::appSettingsVerticalDistanceUnitsToMeters(sliderOutput).toDouble();
        QVERIFY2(sliderMeters > 0, "Slider offered a takeoff altitude of zero");

        // PX4FirmwarePlugin::guidedModeTakeoff builds param7 as this AMSL plus the slider metres.
        const double vehicleAmslAtCommand = vehicle->altitudeAMSL()->rawValue().toDouble();

        // MockLink::_handleTakeoff is the only writer of the mock's simulated AMSL, and it sets it
        // to the commanded param7 plus its own home altitude. Untouched so far, so the rise over
        // this reading is param7 itself. Reading param7 off lastReceivedMavlinkMessage() instead
        // does not work: only the newest COMMAND_LONG is kept and QGC's MAV_CMD_REQUEST_MESSAGE
        // traffic overwrites it.
        const double mockAltitudeBeforeTakeoff = mockLink->vehicleAltitudeAMSL();

        // A real press-and-hold rather than emitting activated(): the whole defect was that
        // this gesture could not reach the button.
        QVERIFY(_holdButton(kConfirmButton));

        QVERIFY_TRUE_WAIT(mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF) == 1, TestTimeout::longMs());

        // The rise is param7, and param7 carries the vehicle AMSL, so on its own this says only
        // that param7 is above zero.
        QVERIFY_TRUE_WAIT(mockLink->vehicleAltitudeAMSL() > mockAltitudeBeforeTakeoff, TestTimeout::longMs());

        // Taking the AMSL the plugin added back out leaves the slider metres, which is what says
        // the slider reading reached the wire.
        const double param7 = mockLink->vehicleAltitudeAMSL() - mockAltitudeBeforeTakeoff;
        const double sentMeters = param7 - vehicleAmslAtCommand;
        QVERIFY2(qAbs(sentMeters - sliderMeters) < 0.1,
                 qPrintable(QStringLiteral("Takeoff altitude sent was %1 m, slider showed %2 m")
                                .arg(sentMeters).arg(sliderMeters)));
    });
}

void PoliceGuidedActionUITest::_testMissionStartFromToolStrip()
{
    _ignorePreexistingQmlWarnings();
    _ignoreDownloadedMissionFontWarnings();

    // Stock raises this action by itself the moment showStartMission turns true. Left on, the
    // control would already be up and the click below would prove nothing, so the popup is
    // turned off for this test only and the strip entry is the only thing that can raise it.
    Fact *const popupsFact = SettingsManager::instance()->flyViewSettings()->enableAutomaticMissionPopups();
    const QVariant savedPopups = popupsFact->rawValue();
    const auto restorePopups = qScopeGuard([popupsFact, savedPopups] { popupsFact->setRawValue(savedPopups); });
    popupsFact->setRawValue(false);

    runWithMockLink([] { return MockLink::startPX4MockLinkWithMission(); },
                    [this](QPointer<MockLink> mockLink, Vehicle * /*vehicle*/) {
        // Appears once the route is aboard, which is what showStartMission tracks.
        QVERIFY(verifyVisibility(kStartMissionButton, true, QStringLiteral("with a mission uploaded")));
        QVERIFY2(!findVisibleItem(_rootItem, kConfirmButton, 1000),
                 "Confirm control was already up before mission start was requested");

        QVERIFY2(clickButton(kStartMissionButton), "Could not click the mission start tool strip entry");

        // actionStartMission goes through the stock 1 second visibleTimer rather than showing
        // immediately.
        QQuickItem *const confirmButton = findVisibleItem(_rootItem, kConfirmButton, 5000);
        QVERIFY2(confirmButton, "Confirm control never appeared after pressing mission start");
        QVERIFY2(hasAncestorNamed(confirmButton, kConfirmHost),
                 "Confirm control on screen is not the police instance");

        mockLink->clearReceivedMavCommandCounts();
        mockLink->clearReceivedMavlinkMessageCounts();

        // Arming and the mission mode both put the mock in the air.
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

        QVERIFY(_holdButton(kConfirmButton));

        // PX4FirmwarePlugin::startMission is a mode change followed by an arm, and it only
        // reaches the arm once the mock has reported the mission mode back. PX4 does not
        // support MAV_CMD_DO_SET_MODE, so the mode change is a SET_MODE message rather than a
        // command, and there is no MAV_CMD_MISSION_START on this firmware to look for.
        QVERIFY_TRUE_WAIT(mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) == 1,
                          TestTimeout::longMs());
        QVERIFY2(mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_MODE) > 0,
                 "Mission start armed the vehicle without asking for the mission flight mode");
    });
}

void PoliceGuidedActionUITest::_captureGuidedScreens()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();
    _ignoreDownloadedMissionFontWarnings();

    const int requestedWidth = qEnvironmentVariableIntValue("QGC_SCREENSHOT_WIDTH");
    const int width = requestedWidth > 0 ? requestedWidth : 1280;

    // No route aboard: the idle strip, the takeoff confirmation, and the confirmation under a
    // warning banner.
    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this, width](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        _window->resize(width, _window->height());
        QTest::qWait(kSettleMs);

        _grab(QStringLiteral("guided_0_idle"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000), "Confirm control never appeared");
        QVERIFY2(findVisibleItem(_rootItem, kSlider, 3000), "Altitude slider never appeared");
        _grab(QStringLiteral("guided_1_takeoff_confirm"));
        if (QTest::currentTestFailed()) return;

        // The banner is driven by the state it really reports: the mock stops answering and
        // VehicleLinkManager declares the link lost on its own timer.
        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(vehicle->vehicleLinkManager()->communicationLost(), TestTimeout::longMs());
        _grab(QStringLiteral("guided_4_banner_and_confirm"));
        if (QTest::currentTestFailed()) return;
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!vehicle->vehicleLinkManager()->communicationLost(), TestTimeout::longMs());
    });
    if (QTest::currentTestFailed()) return;

    // Route aboard: the strip entry, and the mission start confirmation.
    Fact *const popupsFact = SettingsManager::instance()->flyViewSettings()->enableAutomaticMissionPopups();
    const QVariant savedPopups = popupsFact->rawValue();
    const auto restorePopups = qScopeGuard([popupsFact, savedPopups] { popupsFact->setRawValue(savedPopups); });
    popupsFact->setRawValue(false);

    runWithMockLink([] { return MockLink::startPX4MockLinkWithMission(); },
                    [this, width](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(width, _window->height());
        QTest::qWait(kSettleMs);

        QVERIFY(verifyVisibility(kStartMissionButton, true, QStringLiteral("with a mission uploaded")));
        _grab(QStringLiteral("guided_2_route_uploaded"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickButton(kStartMissionButton), "Could not click the mission start tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000), "Confirm control never appeared");
        _grab(QStringLiteral("guided_3_mission_confirm"));
    });
}
