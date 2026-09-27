#pragma once

#include <QtCore/QList>
#include <QtCore/QPointF>

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

    /// 복귀고도 follows the guided controller's showRTL: greyed on the ground, live in flight,
    /// and a tap in flight opens the return altitude panel. Grabs rtl_alt_0_strip_flying and
    /// rtl_alt_1_panel when QGC_SCREENSHOT_DIR is set.
    void _testRtlAltitudeFromToolStrip();

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

    /// The forward lidar drives the compass ring, the map edge glow and the forward number on
    /// the ground as well as in the air: a frame at 3.3 m must raise all three while disarmed,
    /// the same distance repeated must keep them up, and silence past the monitor's timeout must
    /// take all three away rather than leave a dead sensor's last reading on screen. The glow and
    /// the compass ring are aircraft-relative: at heading 90 the top edge is still the one lit, the
    /// compass arc is still at 12 o'clock, and the "전방" number stays clear of the camera
    /// windows. 8.5 m shows the number in orange, 12.5 m nothing.
    void _testLidarDisplaysFollowTheSensor();

    /// The forward window's ring full screen: its strokes stay under a cap and its number is
    /// bigger than windowed. Windowed the ring is as it was: strokes a share of the radius, the
    /// number at the app's default size. Grabs q_0_window and q_1_fullscreen when
    /// QGC_SCREENSHOT_DIR is set.
    void _testForwardRingFullscreenSize();

    /// Capture slot for the camera band. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureCameraBand();

    /// QGC's map scale is on the full map at tablet scale, idle, with a PX4 aircraft and with it
    /// in flight: bottom left on the tool strip's and the forward window's left edge, a stock
    /// margin above the forward window and the detection card, drawn, and clear of the camera
    /// windows, both tool strips, the instruments and the detection card. Grabs
    /// map_scale_0_idle, map_scale_1_connected and map_scale_2_flying when QGC_SCREENSHOT_DIR
    /// is set.
    void _testMapScale();

    /// Dragging across the zoom panel must hand the AI module exactly one box, in the frame
    /// coordinates the drag covered, without also toggling fullscreen. A drag that ends where it
    /// started, one whose panel stops taking picks with the finger still down, and any drag on a
    /// panel that does not take target picks must hand it nothing. A short tap still goes
    /// full screen.
    void _testTargetDragPicksBox();

    /// The release button on the picture: a click must ask for exactly one cancel, must not send
    /// a box and must not take the panel full screen; greyed it must do nothing at all; and a
    /// drag that starts on it must not send a box either.
    void _testTrackCancelButton();

    /// The detection card's five cells are one width, the widest of a dot with its label and a
    /// three-digit count, so 배 is no narrower than the rest. Counts of 123, 456, 789, 100 and 999
    /// sent down the AI module's count link each land inside their own cell, centred, at the count
    /// face's own unfitted size; the byte-ceiling marker "255+" in all five fits inside its cell
    /// too. The card at that width, and the telemetry bar sharing it, stay clear of the 줌 and 열상
    /// windows, the card by at least 3 px; the card also clears the telemetry bar, the instrument
    /// panel and both tool strips. Grabs detection_card_0_rest, detection_card_1_counts,
    /// detection_card_2_screen and detection_card_3_ceiling when QGC_SCREENSHOT_DIR is set.
    void _testDetectionCardCells();

    /// 경고방송 is on the camera grid whatever the speaker switch says, and greyed while no
    /// speaker answers. The switch is on by default. Grabs speaker_0_right_rail when
    /// QGC_SCREENSHOT_DIR is set.
    void _testBroadcastActionAlwaysShown();

private:
    /// Pre-existing QML warnings that the strict log check would otherwise fail on.
    void _ignorePreexistingQmlWarnings();

    /// Pre-existing font warnings that only a downloaded mission raises.
    void _ignoreDownloadedMissionFontWarnings();

    /// Press the item at \a objectName, hold past the confirm delay, release.
    bool _holdButton(const QString &objectName);

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png. \a beforeGrab runs after the settle
    /// wait and just before the grab, with no event loop turn in between.
    void _grab(const QString &name, const std::function<void()> &beforeGrab = {});

    /// Press at the first point of \a path, walk the pointer through the rest of it in steps,
    /// release at the last one. \a midDrag runs at the last point with the pointer still down,
    /// and with \a grabName set and QGC_SCREENSHOT_DIR pointing somewhere the window is grabbed
    /// under that name there too.
    void _dragPointer(const QList<QPointF> &path, const QString &grabName = QString(),
                      const std::function<void()> &midDrag = {});
};
