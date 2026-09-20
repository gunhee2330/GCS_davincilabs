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

    /// The camera layout: 전방 first in the bottom instrument row, 줌 over 열상 down the right
    /// edge from under the top bar, the detection card stacked on the telemetry bar at one
    /// width, the instrument pill as tall as those two rows and at its own proportions, the
    /// camera tool grid two columns by three rows and ending above the card, the zoom window's
    /// title chip clear of its state chips, and the left guided strip above the forward window.
    void _testCameraBandLayout();

    /// The two state chips: 추종 and 추적 in both states, grey when not lit and neon green
    /// when lit.
    void _testStateChipColours();

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
