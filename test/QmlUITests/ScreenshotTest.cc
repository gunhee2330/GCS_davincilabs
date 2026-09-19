#include "ScreenshotTest.h"

#include <QtCore/QDir>
#include <QtCore/QMetaObject>
#include <QtCore/QRectF>
#include <QtCore/QVariant>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include <cmath>
#include <limits>

#include "AppSettings.h"
#include "FactGroup.h"
#include "MAVLinkLib.h"
#include "MockConfiguration.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST_STANDALONE(ScreenshotTest, TestLabel::Integration)

namespace {
// Bindings, drawer slide animations and Loader instantiation all settle well
// inside this; grabbing earlier catches half-painted panels
constexpr int kSettleMs = 1500;

constexpr double kNoReading = std::numeric_limits<double>::quiet_NaN();

// The mock's own sweep drives all six of its sectors off one sine, so an
// all-far frame or a single lit arc can never occur. Feed the fact group
// crafted DISTANCE_SENSOR messages instead: same path the vehicle uses, and
// it latches telemetryAvailable for free.
void injectProximity(Vehicle *vehicle, const double (&metresPerSector)[8])
{
    FactGroup *const group = vehicle->distanceSensorFactGroup();
    for (int sector = 0; sector < 8; sector++) {
        if (std::isnan(metresPerSector[sector])) {
            continue;
        }
        mavlink_message_t msg{};
        const float quaternion[4]{};
        (void) mavlink_msg_distance_sensor_pack_chan(
            vehicle->id(), MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0, &msg,
            0,                                                           // time_boot_ms
            40,                                                          // min_distance cm
            1200,                                                        // max_distance cm -> 12 m, a TF Mini's own range
            static_cast<uint16_t>(metresPerSector[sector] * 100.0),       // current_distance cm
            MAV_DISTANCE_SENSOR_LASER,
            static_cast<uint8_t>(sector),                                // id
            static_cast<uint8_t>(sector),                                // orientation: NONE..YAW_315 are 0..7
            255, 0.0f, 0.0f, quaternion, 0);
        group->handleMessage(vehicle, msg);
    }
}

}  // namespace

void ScreenshotTest::_grab(const QString &name)
{
    const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
    QVERIFY2(!dir.isEmpty(), "QGC_SCREENSHOT_DIR is not set");
    QVERIFY2(QDir().mkpath(dir), qPrintable(QStringLiteral("Cannot create %1").arg(dir)));

    QTest::qWait(kSettleMs);

    const QImage image = _window->grabWindow();
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Empty grab for %1").arg(name)));

    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    QVERIFY2(image.save(path), qPrintable(QStringLiteral("Cannot write %1").arg(path)));
    qDebug() << "screenshot" << path;
}

void ScreenshotTest::_captureNoVehicle()
{
    startUI();
    if (QTest::currentTestFailed()) return;

    _grab(QStringLiteral("ring_neg_0_no_vehicle"));
    stopUI();
}

void ScreenshotTest::_captureScreens()
{
    // The framework clears QSettings per test function, so the light theme cannot be staged
    // from outside the process. QGC_SCREENSHOT_LIGHT=1 flips the palette for this run only.
    if (qEnvironmentVariableIntValue("QGC_SCREENSHOT_LIGHT") != 0) {
        SettingsManager::instance()->appSettings()->indoorPalette()->setRawValue(0);
        QTest::qWait(kSettleMs);
    }

    // Proximity stays off in the mock: its 1 Hz sweep would overwrite the injected values.
    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(MockConfiguration::OptionNone); },
                    [this](QPointer<MockLink>, Vehicle *vehicle) {
        // A 7in controller is far narrower than the default test window, and full-width cards
        // are the layout narrowness can break. QGC_SCREENSHOT_WIDTH re-walks at that width.
        const int narrowWidth = qEnvironmentVariableIntValue("QGC_SCREENSHOT_WIDTH");
        if (narrowWidth > 0) {
            _window->resize(narrowWidth, _window->height());
            QTest::qWait(kSettleMs);
        }

        // The bands are absolute metres now: warn at 10 m, red under 7 m. "Far" is 11 m rather than
        // the sensor's own 12 m ceiling, which the overlays read as a clear path rather than as an
        // obstacle and would leave the far frame indistinguishable from a broken one.
        // Sectors 3 (YAW_135) and 5 (YAW_225) are never fed, so they stay NaN.
        const double rgFar[8]   = { 11, 11, 11, kNoReading, 11, kNoReading, 11, 11 };
        const double rgWarn[8]  = { 8.5, 11, 11, kNoReading, 11, kNoReading, 11, 11 };
        const double rgBad[8]   = {  4, 11, 11, kNoReading, 11, kNoReading, 11, 11 };
        const double rgMixed[8] = { 11, 11,  4, kNoReading, 11, kNoReading, 8.5, 11 };

        // Armed but nothing injected yet, so the fact group has never seen a
        // DISTANCE_SENSOR: telemetryAvailable is false and the ring must stay blank.
        // Grabbed before any injection because that flag latches on the first message.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        _grab(QStringLiteral("ring_neg_1_armed_no_telemetry"));
        if (QTest::currentTestFailed()) return;

        // Disarmed with the nose reading close: a parked airframe reads close on every
        // side, so nothing may draw.
        vehicle->setArmed(false, false);
        QTRY_VERIFY(!vehicle->armed());
        injectProximity(vehicle, rgBad);
        _grab(QStringLiteral("ring_0_disarmed_bad"));
        if (QTest::currentTestFailed()) return;

        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());

        injectProximity(vehicle, rgFar);
        _grab(QStringLiteral("ring_1_all_far"));
        if (QTest::currentTestFailed()) return;

        injectProximity(vehicle, rgWarn);
        _grab(QStringLiteral("ring_2_nose_warn"));
        if (QTest::currentTestFailed()) return;

        injectProximity(vehicle, rgBad);
        _grab(QStringLiteral("ring_3_nose_bad"));
        if (QTest::currentTestFailed()) return;

        injectProximity(vehicle, rgMixed);
        _grab(QStringLiteral("ring_4_right_bad_left_warn"));
        if (QTest::currentTestFailed()) return;

        // One sector close at a time, round the airframe, so each of the map glow's four edges
        // can be seen to light on its own and no other.
        const char *const rgEdgeNames[4] = { "top", "right", "bottom", "left" };
        for (int corner = 0; corner < 4; corner++) {
            double rgOneClose[8] = { 11, 11, 11, 11, 11, 11, 11, 11 };
            rgOneClose[corner * 2] = 4;
            injectProximity(vehicle, rgOneClose);
            _grab(QStringLiteral("glow_%1_%2").arg(corner).arg(QLatin1String(rgEdgeNames[corner])));
            if (QTest::currentTestFailed()) return;
        }

        // Back to far for the non-fly screens, then the original palette walk.
        injectProximity(vehicle, rgFar);
        _grab(QStringLiteral("04_flyview"));
        if (QTest::currentTestFailed()) return;

        // MainWindow.qml is an ApplicationWindow, so its functions live on _window
        QVERIFY(QMetaObject::invokeMethod(_window, "showVehicleConfig"));
        QTest::qWait(kSettleMs);

        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_summary")), "Summary button not clickable");
        _grab(QStringLiteral("01_vehicle_summary"));
        if (QTest::currentTestFailed()) return;

        // A hand-coded setup page, kept alongside the generated ones so changes to
        // SetupPage show up on both. It sits at the bottom of the rail, so grab it before
        // an entry above it expands and pushes it past the window edge
        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_comp_Tuning-Advanced")), "Tuning button not clickable");
        _grab(QStringLiteral("06_vehicle_tuning"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_comp_FlightSafety")), "Flight Safety button not clickable");
        _grab(QStringLiteral("02_vehicle_safety"));
        if (QTest::currentTestFailed()) return;

        // Failsafes carries the bitmask checkboxes and the repeated sections
        // that Flight Safety has none of
        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_comp_Failsafes")), "Failsafes button not clickable");
        _grab(QStringLiteral("05_vehicle_failsafes"));
        if (QTest::currentTestFailed()) return;

        QVERIFY(QMetaObject::invokeMethod(_window, "showSettingsTool",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Comm Links")))));
        _grab(QStringLiteral("03_settings_commlinks"));
    });
}
