#pragma once

#include "QmlUITestBase.h"

/// The police layout hides FlyViewToolBar, which in stock QGC is the only place
/// GuidedActionConfirm is instantiated. Every guided command is sent from that control's
/// hold button, so with it unreachable takeoff, land, RTL, pause and mission start ran
/// their whole chain and put nothing on the wire.
///
/// PoliceGuidedConfirmHost puts a second instance of the stock control in the police
/// dashboard. These tests hold it reachable and hold the commands going out, since the
/// symptom of either going away again is silence rather than a failure.
class PoliceGuidedActionUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    /// Pressing 이륙 must raise the confirm control, and the control raised must be the
    /// police one rather than the hidden toolbar's.
    void _testTakeoffRaisesConfirmControl();

    /// The takeoff altitude slider must be ordered above the dashboard, which fills the
    /// window: painted under it the operator can neither read nor set the altitude.
    void _testTakeoffAltitudeSliderIsOnTop();

    /// Holding the confirm button past its delay must put MAV_CMD_NAV_TAKEOFF on the link.
    void _testHoldConfirmSendsTakeoff();

    /// With a route aboard, 미션시작 appears in the strip, raises the confirm control, and
    /// holding it starts the mission.
    void _testMissionStartFromToolStrip();

    /// Capture slot. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureGuidedScreens();

    /// The camera band along the bottom: 전방 left of the compass, 줌 over 열상 in the bottom
    /// right corner, the detection card and the telemetry bar between them without overlap.
    /// Also holds the camera tool strip clear of the altitude slider while a confirmation is up.
    void _testCameraBandLayout();

    /// Capture slot for the camera band. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureCameraBand();

private:
    /// Pre-existing QML warnings that the strict log check would otherwise fail on.
    void _ignorePreexistingQmlWarnings();

    /// Pre-existing font warnings that only a downloaded mission raises.
    void _ignoreDownloadedMissionFontWarnings();

    /// Press the item at \a objectName, hold past the confirm delay, release.
    bool _holdButton(const QString &objectName);

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png.
    void _grab(const QString &name);
};
