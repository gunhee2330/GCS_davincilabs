#pragma once

#include "QmlUITestBase.h"

class QQuickItem;

/// The police top bar, rebuilt as PoliceTopBar.qml.
///
/// Two things about it are invisible until they break and so are held here: every item the
/// operator can tap is still on the bar under the name the layout knows it by, and every drawer
/// still opens under the item that was tapped rather than under the bar or a row - which is the
/// whole reason each tap hands showIndicatorDrawer the item itself.
class PoliceTopBarUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    /// Every item on the bar, by the objectName the layout and these tests know it by.
    void _testBarItemsExist();

    /// Each tappable item opens its drawer under itself: the drawer hangs from the bar's bottom
    /// edge and its span covers the item that was tapped.
    void _testDrawersOpenUnderTheirItem();

    /// The clock carries the date as well as the time, which procurement asks to be readable off
    /// the video screen.
    void _testDateTimeFormat();

    /// The banner opens the flight log, on the ground and in the air alike.
    void _testStatusBannerOpensLog();

    /// The pack opens the battery detail the police bar never had.
    void _testBatteryItemOpensBatteryPage();

    /// The RF group inherited the link indicator's job: with an aircraft attached it lists the
    /// links that are up.
    void _testRfItemOpensConnectedLinks();

    /// The flight mode item drops the stock mode selector, and a mode picked out of it reaches
    /// the aircraft. The tap belongs to this item alone: the item beside it still opens its own
    /// drawer. Also the two captures, when a capture directory is set.
    void _testFlightModeItemOpensStockSelector();

    /// With no aircraft there is no mode to pick, so the item is off the bar and a tap where it
    /// would have been opens nothing.
    void _testFlightModeDoesNothingWithNoVehicle();

    /// Once the aircraft is armed the banner counts the flight, beside the state and not inside
    /// a drawer.
    void _testBannerShowsFlightTimeWhenFlying();

    /// A flight flown and landed leaves its duration in the drawer and in the settings file
    /// under the airframe's own key, which is what the next start of the app reads. A second
    /// airframe gets its own log rather than this one's.
    void _testLastFlightTimeIsStoredPerAirframe();

    /// A landed state that drops out in the air is one flight, not several: the arm cycle counts
    /// one takeoff, and its duration runs from the first liftoff to the last landing.
    void _testLandedFlickerIsOneFlight();

    /// The one state with something to go and read: a refused arm paints the banner red, grows
    /// the chevron beside it and sends the operator to the drawer.
    void _testArmBlockedBanner();

    /// Capture slot, no aircraft. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureNoVehicleBar();

    /// Capture slot, aircraft attached. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureBar();

    /// Capture slot for the two bars the fly view never shows - the plan view's and the tool
    /// drawer's. They wear the same brand mark off the same PoliceBar figures, and whether the
    /// three really measure the same is only visible in a grab. Skipped unless
    /// QGC_SCREENSHOT_DIR is set.
    void _captureOtherViewBars();

    /// Capture slot for the fork palette across the screens it reaches: settings pages, the fly
    /// view with and without the mode drawer, plan, vehicle setup, and one settings page in the
    /// light scheme. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureSettingsDark();

    /// The fork palette and the dark default only exist while custom/ names PoliceCorePlugin to
    /// QGC's custom-build hook. Without it everything builds and the stock plugin runs instead.
    void _policeCorePluginIsLive();

private:
    /// Pre-existing QML warnings that the strict log check would otherwise fail on.
    void _ignorePreexistingQmlWarnings();

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png.
    void _grab(const QString &name);

    /// _grab() where the slot also asserts something: a run with no capture directory set is
    /// still a run of the assertions.
    void _grabIfCapturing(const QString &name);

    /// Put the mock aircraft in the air through the guided takeoff the tool strip offers, and
    /// wait for it to arm. Fails the test rather than returning a flag; callers check
    /// QTest::currentTestFailed().
    void _guidedTakeoff(Vehicle *vehicle);

    /// Tap \a objectName and wait for the indicator drawer's loader. Returns the loader, or
    /// null after failing the test.
    QQuickItem *_openDrawerFrom(const QString &objectName);

    /// Close the indicator drawer and wait for its loader to go.
    bool _closeDrawer();
};
