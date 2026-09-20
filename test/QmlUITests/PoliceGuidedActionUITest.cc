#include "PoliceGuidedActionUITest.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QRect>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "Fact.h"
#include "FactGroup.h"
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

/// The camera band and the blocks that share the bottom of the screen with it.
const QString kForwardWindow   = QStringLiteral("cameraWindowPrimary");
const QString kZoomWindow      = QStringLiteral("cameraWindowSecondary");
const QString kThermalWindow   = QStringLiteral("cameraWindowShared");
const QString kAiPanel         = QStringLiteral("policeAiPanel");
const QString kTelemetryBar    = QStringLiteral("policeTelemetryBar");
const QString kInstrumentPanel = QStringLiteral("policeInstrumentPanel");
const QString kInstrumentRow   = QStringLiteral("policeInstrumentRow");
const QString kCameraStrip     = QStringLiteral("policeCameraToolStrip");

/// QGCDelayButton.defaultDelay is 500 ms. The press must outlast it, with room for the
/// progress animation to reach 1.0 on the software backend.
constexpr int kHoldMs = 1500;

/// Bindings and the strip animations settle well inside this.
constexpr int kSettleMs = 1500;

/// The layout assertions are about edges meeting, and the layout resolves in real numbers.
constexpr qreal kEdgeSlack = 1.0;

/// The camera tool strip keeps a small margin off the screen edge. Doubled here, since what this
/// tells apart is a strip at the edge from one stepped inboard of a slider tens of pixels wide.
constexpr qreal kRightEdgeMargin = 16.0;

/// The size the band was designed against, and the size its assertions are stated at.
constexpr int kLayoutWidth  = 1280;
constexpr int kLayoutHeight = 800;

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

/// Same approach ScreenshotTest takes: the mock's own sweep drives every sector off one sine, so
/// a single close arc can only be had by feeding the fact group crafted DISTANCE_SENSOR messages.
/// Min 40 cm, max 12 m, a TF Mini's own range.
void injectProximity(Vehicle *vehicle, const double (&metresPerSector)[8])
{
    FactGroup *const group = vehicle->distanceSensorFactGroup();
    for (int sector = 0; sector < 8; sector++) {
        mavlink_message_t msg{};
        const float quaternion[4]{};
        (void) mavlink_msg_distance_sensor_pack_chan(
            vehicle->id(), MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0, &msg,
            0,                                                      // time_boot_ms
            40,                                                     // min_distance cm
            1200,                                                   // max_distance cm
            static_cast<uint16_t>(metresPerSector[sector] * 100.0),  // current_distance cm
            MAV_DISTANCE_SENSOR_LASER,
            static_cast<uint8_t>(sector),                           // id
            static_cast<uint8_t>(sector),                           // orientation: NONE..YAW_315 are 0..7
            255, 0.0f, 0.0f, quaternion, 0);
        group->handleMessage(vehicle, msg);
    }
}

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

    // The stock plan view's map visuals are sometimes torn down mid-creation when the previous
    // slot's engine goes away, and the message lands in whichever slot is running by then. Seen
    // in the guided slots as well as the layout ones, none of which instantiate that file. The
    // sentence is localised, so the pattern matches the file rather than the words.
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/HomePositionMapVisual\\.qml: ")));
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

void PoliceGuidedActionUITest::_testCameraBandLayout()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");

        QHash<QString, QQuickItem *> items;
        for (const QString &name : { kForwardWindow, kZoomWindow, kThermalWindow, kAiPanel,
                                     kTelemetryBar, kInstrumentPanel, kInstrumentRow, kCameraStrip }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 3000);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on screen").arg(name)));
            items.insert(name, item);
        }

        const QRectF screen  = sceneRect(dashboard);
        const QRectF forward = sceneRect(items[kForwardWindow]);
        const QRectF zoom    = sceneRect(items[kZoomWindow]);
        const QRectF thermal = sceneRect(items[kThermalWindow]);
        const QRectF card    = sceneRect(items[kAiPanel]);
        const QRectF bar     = sceneRect(items[kTelemetryBar]);
        const QRectF compass = sceneRect(items[kInstrumentPanel]);
        const QRectF row     = sceneRect(items[kInstrumentRow]);

        // 전방 first in the row: on the row's own left inset, the compass to its right and the
        // detection card right of that again. 전방 stands on the instrument row's bottom line.
        QVERIFY2(qAbs(forward.left() - row.left()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Forward window left edge %1 is not on the instrument row left edge %2")
                                .arg(forward.left()).arg(row.left())));
        QVERIFY2(forward.right() <= compass.left() + kEdgeSlack,
                 qPrintable(QStringLiteral("Forward window right edge %1 is right of the instrument panel left edge %2")
                                .arg(forward.right()).arg(compass.left())));
        QVERIFY2(compass.right() <= card.left() + kEdgeSlack,
                 qPrintable(QStringLiteral("Instrument panel right edge %1 is right of the detection card left edge %2")
                                .arg(compass.right()).arg(card.left())));
        QVERIFY2(qAbs(forward.bottom() - row.bottom()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Forward window bottom %1 is not on the instrument row bottom %2")
                                .arg(forward.bottom()).arg(row.bottom())));

        // 줌 directly above 열상, the two flush right, 열상 on the same bottom line.
        QVERIFY2(qAbs(zoom.bottom() - thermal.top()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Zoom bottom %1 is not on the thermal top %2")
                                .arg(zoom.bottom()).arg(thermal.top())));
        QVERIFY2(qAbs(zoom.left() - thermal.left()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Zoom left %1 and thermal left %2 are not aligned")
                                .arg(zoom.left()).arg(thermal.left())));
        QVERIFY2(qAbs(zoom.right() - screen.right()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Zoom right %1 is not on the screen right edge %2")
                                .arg(zoom.right()).arg(screen.right())));
        QVERIFY2(qAbs(thermal.right() - screen.right()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Thermal right %1 is not on the screen right edge %2")
                                .arg(thermal.right()).arg(screen.right())));
        QVERIFY2(qAbs(thermal.bottom() - row.bottom()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Thermal bottom %1 is not on the instrument row bottom %2")
                                .arg(thermal.bottom()).arg(row.bottom())));

        // Nothing in the band clipped by anything else in it. The two corner windows are one
        // block here: they are stacked edge to edge, so on their own they always touch.
        const QList<QPair<QString, QRectF>> band {
            { QStringLiteral("forward window"),     forward },
            { QStringLiteral("detection card"),     card },
            { QStringLiteral("telemetry bar"),      bar },
            { QStringLiteral("zoom/thermal stack"), zoom.united(thermal) },
        };
        for (int i = 0; i < band.size(); ++i) {
            for (int j = i + 1; j < band.size(); ++j) {
                // Shrunk by the slack the edge checks use, so blocks that merely abut do not
                // read as overlapping.
                const QRectF a = band[i].second.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
                const QRectF b = band[j].second.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
                QVERIFY2(!a.intersects(b),
                         qPrintable(QStringLiteral("%1 %2 overlaps %3 %4")
                                        .arg(band[i].first, QDebug::toString(band[i].second),
                                             band[j].first, QDebug::toString(band[j].second))));
            }
        }

        // The card's five counts fit: squeezed below its implicit width they start eliding.
        QQuickItem *const aiPanelItem = items[kAiPanel];
        QVERIFY2(aiPanelItem->width() >= aiPanelItem->implicitWidth() - kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card is %1 wide against an implicit width of %2")
                                .arg(aiPanelItem->width()).arg(aiPanelItem->implicitWidth())));

        // The camera tool strip lives at the right screen edge, which is also where the stock
        // altitude slider appears while a takeoff confirmation is up.
        QQuickItem *const strip = items[kCameraStrip];
        const qreal stripRightAtEdge = sceneRect(strip).right();
        QVERIFY2((screen.right() - stripRightAtEdge) <= kRightEdgeMargin,
                 qPrintable(QStringLiteral("Camera tool strip right %1 does not start at the screen right edge %2")
                                .arg(stripRightAtEdge).arg(screen.right())));

        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000),
                 "Confirm control never appeared after pressing takeoff");
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 3000);
        QVERIFY2(slider, "Takeoff altitude slider never became visible");
        QTest::qWait(kSettleMs);

        QVERIFY2(!sceneRect(strip).intersects(sceneRect(slider)),
                 qPrintable(QStringLiteral("Camera tool strip %1 is under the altitude slider %2")
                                .arg(QDebug::toString(sceneRect(strip)),
                                     QDebug::toString(sceneRect(slider)))));

        // What GuidedActionConfirm does on cancel and on confirm alike: the slider goes away.
        slider->setProperty("visible", false);
        QTest::qWait(kSettleMs);
        QVERIFY2(qAbs(sceneRect(strip).right() - stripRightAtEdge) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Camera tool strip stayed at %1 after the slider hid, was %2 before")
                                .arg(sceneRect(strip).right()).arg(stripRightAtEdge)));
    });
}

void PoliceGuidedActionUITest::_captureCameraBand()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();
    _ignoreDownloadedMissionFontWarnings();

    // Raised by the stock plan view's tree while the second downloaded mission of the process is
    // drawn, which is this slot's mission run following _captureGuidedScreens'. Nothing in the
    // police layout instantiates that file. The pattern names it, so a warning out of the camera
    // band cannot be swallowed by it.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/PlanViewRightPanel\\.qml:[0-9]+:[0-9]+: "
                         "QML PlanTreeView: the delegate's implicitHeight needs to be greater than zero$")));

    const int requestedWidth  = qEnvironmentVariableIntValue("QGC_SCREENSHOT_WIDTH");
    const int requestedHeight = qEnvironmentVariableIntValue("QGC_SCREENSHOT_HEIGHT");
    const int width  = requestedWidth  > 0 ? requestedWidth  : kLayoutWidth;
    const int height = requestedHeight > 0 ? requestedHeight : kLayoutHeight;

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this, width, height](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(width, height);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout to capture is not up");

        _grab(QStringLiteral("final_0_idle"));
        if (QTest::currentTestFailed()) return;

        // Full screen and back again, which is where the re-dock can go wrong. Set rather than
        // tapped: the property is what the tap handler assigns, and these two frames are about
        // where the windows land, not about the gesture.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        _grab(QStringLiteral("final_4_zoom_fullscreen"));
        if (QTest::currentTestFailed()) return;
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        _grab(QStringLiteral("final_5_after_fullscreen"));
        if (QTest::currentTestFailed()) return;

        // The ring on the forward window, the compass ring and the map edge glow all read the
        // same fact group and only while armed. The mock's own sweep is off under OptionNone,
        // so the injected frame stays put.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        const double rgForwardClose[8] = { 4, 11, 11, 11, 11, 11, 11, 11 };
        injectProximity(vehicle, rgForwardClose);
        _grab(QStringLiteral("final_1_lidar"));
        if (QTest::currentTestFailed()) return;

        // Takeoff wants the vehicle on the ground again. Last, so the confirm control raised
        // here never has to be dismissed before another frame.
        vehicle->setArmed(false, false);
        QTRY_VERIFY(!vehicle->armed());
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000), "Confirm control never appeared");
        QVERIFY2(findVisibleItem(_rootItem, kSlider, 3000), "Altitude slider never appeared");
        _grab(QStringLiteral("final_2_takeoff"));
    });
    if (QTest::currentTestFailed()) return;

    // 미션시작 only appears in the left strip with a route aboard, and stock raises the action
    // by itself the moment it does; the popup is turned off so the frame shows the strip alone.
    Fact *const popupsFact = SettingsManager::instance()->flyViewSettings()->enableAutomaticMissionPopups();
    const QVariant savedPopups = popupsFact->rawValue();
    const auto restorePopups = qScopeGuard([popupsFact, savedPopups] { popupsFact->setRawValue(savedPopups); });
    popupsFact->setRawValue(false);

    runWithMockLink([] { return MockLink::startPX4MockLinkWithMission(); },
                    [this, width, height](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(width, height);
        QTest::qWait(kSettleMs);

        QVERIFY(verifyVisibility(kStartMissionButton, true, QStringLiteral("with a mission uploaded")));
        _grab(QStringLiteral("final_3_route_uploaded"));
    });
}
