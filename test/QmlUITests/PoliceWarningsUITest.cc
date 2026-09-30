#include "PoliceWarningsUITest.h"

#include <algorithm>
#include <iterator>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "FactGroup.h"
#include "MAVLinkLib.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "PoliceWarnings.h"
#include "SiyiAiController.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

UT_REGISTER_TEST(PoliceWarningsUITest, TestLabel::Integration)

namespace {

const QString kBattery  = QStringLiteral("policeWarningBattery");
const QString kAltitude = QStringLiteral("policeWarningAltitude");
const QString kRadius   = QStringLiteral("policeWarningRadius");
const QString kWind     = QStringLiteral("policeWarningWind");
const QString kLinkLost = QStringLiteral("policeWarningLinkLost");
const QString kFollow   = QStringLiteral("policeWarningFollowMode");
const QString kPick     = QStringLiteral("policeWarningTargetPick");

const QString kStrips      = QStringLiteral("policeWarningStrips");
const QString kLeftStrip   = QStringLiteral("policeGuidedToolStrip");
const QString kRightStrip  = QStringLiteral("policeCameraToolStrip");
const QString kGridHandle  = QStringLiteral("policeCameraStripHandle");

constexpr int kSettleMs = 1500;

/// The mockup's strip against the bar it hangs under, both in 1920 px capture pixels.
constexpr double kStripToBar = 64.0 / 135.0;

/// The tablet's 1920x1200 under QT_SCALE_FACTOR=2.5, as the other police UI tests size it.
constexpr int kLayoutWidth  = 768;
constexpr int kLayoutHeight = 480;

/// A third pack beside the mock's own two. Those drain on the mock's clock and are resent every
/// second, so an injected value for them would last a second; this one stands until the next
/// injection. The dashboard reads the lowest percentage and the worst charge state across packs,
/// which is what the top bar colours by, so the third pack alone sets both.
void injectBattery(MockLink *mockLink, Vehicle *vehicle, int percent, MAV_BATTERY_CHARGE_STATE state)
{
    uint16_t voltages[10];
    std::fill(std::begin(voltages), std::end(voltages), UINT16_MAX);
    uint16_t voltagesExt[4]{};
    mavlink_message_t msg{};
    (void) mavlink_msg_battery_status_pack_chan(
        static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, mockLink->outgoingMavlinkChannel(), &msg,
        3,                          // battery id
        MAV_BATTERY_FUNCTION_ALL, MAV_BATTERY_TYPE_LIPO,
        INT16_MAX,                  // temperature unknown
        voltages,
        -1, -1, -1,                 // current, consumed, energy unknown
        static_cast<int8_t>(percent),
        0,                          // time remaining unknown
        state, voltagesExt, 0, 0);
    mockLink->respondWithMavlinkMessage(msg);
}

/// ALTITUDE outranks GLOBAL_POSITION_INT for the relative altitude from the first one received,
/// so after one of these the mock's own position stream no longer moves the figure.
void injectAltitude(MockLink *mockLink, Vehicle *vehicle, double relativeMetres)
{
    const float amsl = static_cast<float>(vehicle->homePosition().altitude() + relativeMetres);
    mavlink_message_t msg{};
    (void) mavlink_msg_altitude_pack_chan(
        static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, mockLink->outgoingMavlinkChannel(), &msg,
        0,                                   // time_usec
        amsl, amsl,                          // monotonic, amsl
        static_cast<float>(relativeMetres),  // local
        static_cast<float>(relativeMetres),  // relative
        qQNaN(), qQNaN());                   // terrain, bottom clearance
    mockLink->respondWithMavlinkMessage(msg);
}

/// The mock sends no wind at all, so an injected WIND_COV stands.
void injectWind(MockLink *mockLink, Vehicle *vehicle, double metresPerSecond)
{
    mavlink_message_t msg{};
    (void) mavlink_msg_wind_cov_pack_chan(
        static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, mockLink->outgoingMavlinkChannel(), &msg,
        0,                                     // time_usec
        static_cast<float>(metresPerSecond),   // wind_x, all of it from the north
        0.0f, 0.0f,                            // wind_y, wind_z
        0.0f, 0.0f,                            // var_horiz, var_vert
        0.0f,                                  // wind_alt
        0.0f, 0.0f);                           // horiz_accuracy, vert_accuracy
    mockLink->respondWithMavlinkMessage(msg);
}

/// MockLink holds the aircraft on its home point: its latitude and longitude are constants and it
/// resends HOME_POSITION every second, so neither end of the distance can be moved over the link.
/// The distance fact is set instead, the fact the dashboard binds; it stands because Vehicle only
/// works it out again when the position or the home point changes, and the hovering mock changes
/// neither.
bool setHomeDistance(Vehicle *vehicle, double metres)
{
    Fact *const distance = vehicle ? vehicle->distanceToHome() : nullptr;
    if (!distance) {
        return false;
    }
    distance->setRawValue(metres);
    return true;
}

/// Straight to the mock rather than through the tool strip: the mock climbs to param7 above home
/// and reports IN_AIR, which is all these tests need, and the strip's own takeoff is held by the
/// guided action tests. PX4's own takeoff would send an AMSL figure the mock adds home to again.
bool takeOff(Vehicle *vehicle)
{
    vehicle->sendMavCommand(vehicle->defaultComponentId(), MAV_CMD_NAV_TAKEOFF, false,
                            qQNaN(), qQNaN(), qQNaN(), qQNaN(), qQNaN(), qQNaN(), 30.0f);
    return UnitTest::waitForCondition([vehicle] { return vehicle->flying(); }, TestTimeout::longMs(),
                                      QStringLiteral("vehicle flying"));
}

/// NaN when the vehicle has no wind group or speed, which no comparison waits out.
double windSpeed(Vehicle *vehicle)
{
    FactGroup *const wind = vehicle ? vehicle->windFactGroup() : nullptr;
    Fact *const speed = wind ? wind->getFact(QStringLiteral("speed")) : nullptr;
    return speed ? speed->rawValue().toDouble() : qQNaN();
}

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

/// \a name anywhere under \a root, shown or not.
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == name) {
        return root;
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

/// The dashboard's PoliceWarnings, by its id in the dashboard file's own context, which is where
/// the top bar was made.
QObject *findWarnings(QQuickItem *topBar)
{
    QQmlContext *const context = topBar ? qmlContext(topBar) : nullptr;
    return context ? context->objectForName(QStringLiteral("policeWarnings")) : nullptr;
}

/// SYS_STATUS with the RC receiver present, enabled and not healthy. The mock sends its own every
/// second without the receiver, so a caller keeps resending this to hold the state.
void injectRcLost(MockLink *mockLink, Vehicle *vehicle)
{
    const uint32_t rc = MAV_SYS_STATUS_SENSOR_RC_RECEIVER;
    mavlink_message_t msg{};
    (void) mavlink_msg_sys_status_pack_chan(
        static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, mockLink->outgoingMavlinkChannel(), &msg,
        MAV_SYS_STATUS_SENSOR_GPS | rc,  // present
        rc,                              // enabled
        0,                               // health
        250, 4200 * 4, 8000, -1,
        0, 0, 0, 0, 0, 0, 0, 0, 0);
    mockLink->respondWithMavlinkMessage(msg);
}

}  // namespace

void PoliceWarningsUITest::_ignorePreexistingWarnings()
{
    // PoliceTopBarUITest's list: present at HEAD and nothing here touches them.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/FlightMap/Widgets/PhotoVideoControl\\.qml:[0-9]+: "
                         "TypeError: Cannot read property '[A-Za-z0-9_]+' of (null|undefined)$")));
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^Both point size and pixel size set\\. Using pixel size\\.$")));
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^QString::arg: Argument missing: \"") +
                                        QRegularExpression::escape(QCoreApplication::translate(
                                            "PIDTuning", "Switches to '%1' when you click Stop.")) +
                                        QStringLiteral("\", ")));
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/HomePositionMapVisual\\.qml: ")));

    // The flying transition creates QGCPressure, which warns on hosts without a backend.
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));
}

void PoliceWarningsUITest::_grab(const QString &name)
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

void PoliceWarningsUITest::_testStripsFollowTelemetry()
{
    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        Fact *const altitudeFact = vehicle->altitudeRelative();
        QVERIFY(altitudeFact);
        VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
        QVERIFY(linkManager);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        QObject *const warnings = findWarnings(topBar);
        QVERIFY2(warnings, "The dashboard has no PoliceWarnings");
        // Watching this vehicle, so Vehicle leaves the aircraft's own low and critical battery
        // announcements to its Korean sentence.
        QVERIFY(PoliceWarnings::voicesBattery(vehicle));
        QSignalSpy spoke(warnings, SIGNAL(spoke(QString)));
        QVERIFY(spoke.isValid());
        const auto lastSpoken = [&spoke] { return spoke.isEmpty() ? QString() : spoke.last().at(0).toString(); };

        QQuickItem *const strips = findItem(_rootItem, kStrips);
        QVERIFY2(strips, "The dashboard has no warning strip stack");
        QQuickItem *const leftStrip = findVisibleItem(_rootItem, kLeftStrip, 5000);
        QVERIFY2(leftStrip, "The left tool strip is not up");
        QQuickItem *const rightStrip = findItem(_rootItem, kRightStrip);
        QVERIFY2(rightStrip, "The dashboard has no right tool strip");
        QQuickItem *const gridHandle = findItem(_rootItem, kGridHandle);
        QVERIFY2(gridHandle, "The dashboard has no camera grid handle");

        const double barBottom = sceneRect(topBar).bottom();
        const double stripHeight = topBar->height() * kStripToBar;
        // A strip spans the map between the columns: 8 px right of the left tool strip, 8 px left
        // of whichever of the camera grid (while it is open) and its handle is further left.
        const auto checkSpan = [&](const QRectF &rect, const QString &context) {
            const double left = sceneRect(leftStrip).right() + 8;
            const double gridLeft = rightStrip->isVisible() ? sceneRect(rightStrip).left() : sceneRect(gridHandle).left();
            const double right = qMin(sceneRect(gridHandle).left(), gridLeft) - 8;
            if (qAbs(rect.left() - left) >= 1.0 || qAbs(rect.right() - right) >= 1.0) {
                return QStringLiteral("%1: strip runs %2 to %3, not %4 to %5").arg(context).arg(rect.left()).arg(rect.right()).arg(left).arg(right);
            }
            return QString();
        };
        // Both tool strips stay 8 px under the bar however many strips are up, and the stack is
        // drawn over them rather than pushing them down.
        QVERIFY(strips->parentItem() == leftStrip->parentItem() && strips->parentItem() == rightStrip->parentItem());
        QVERIFY2(strips->z() > leftStrip->z() && strips->z() > rightStrip->z(), "The warning strips are not above the tool strips");
        const auto checkToolStrips = [&](const QString &context) {
            const double expected = barBottom + 8;
            for (QQuickItem *const tool : { leftStrip, rightStrip }) {
                const double top = sceneRect(tool).top();
                if (qAbs(top - expected) >= 1.0) {
                    return QStringLiteral("%1: %2 starts at %3, not %4").arg(context, tool->objectName()).arg(top).arg(expected);
                }
            }
            return QString();
        };

        for (const QString &name : { kBattery, kAltitude, kRadius, kWind }) {
            QVERIFY(verifyVisibility(name, false, QStringLiteral("before anything is wrong")));
        }
        QCOMPARE(strips->height(), 0.0);
        QString offset = checkToolStrips(QStringLiteral("nothing up"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        // Altitude and wind count only in the air.
        injectAltitude(mockLink, vehicle, 200);
        injectWind(mockLink, vehicle, 12);
        QVERIFY_TRUE_WAIT(windSpeed(vehicle) == 12.0, TestTimeout::mediumMs());
        QVERIFY_TRUE_WAIT(altitudeFact->rawValue().toDouble() == 200.0, TestTimeout::mediumMs());
        QTest::qWait(300);
        QVERIFY2(!findVisibleItem(_rootItem, kAltitude, 0) && !findVisibleItem(_rootItem, kWind, 0),
                 "An in-flight warning went up on the ground");
        injectAltitude(mockLink, vehicle, 30);
        injectWind(mockLink, vehicle, 0);
        QVERIFY_TRUE_WAIT(windSpeed(vehicle) == 0.0, TestTimeout::mediumMs());

        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        QVERIFY(verifyVisibility(kAltitude, false, QStringLiteral("30 m in the air")));
        QCOMPARE(spoke.count(), 0);

        // Battery: up with the top bar's orange, then its red, speaking each time.
        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyProperty(kBattery, "line", QStringLiteral("잔량 20%, 복귀를 준비하십시오"), QStringLiteral("low")));
        QVERIFY(verifyProperty(kBattery, "title", QStringLiteral("배터리 부족"), QStringLiteral("low")));
        QCOMPARE(lastSpoken(), QStringLiteral("Low battery, 20 percent remaining. Prepare to return."));

        // Between the columns, straight under the bar, the mockup's height, and the tool strips where they were.
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QTest::qWait(300);
        const QRectF batteryRect = sceneRect(battery);
        QVERIFY2(qAbs(batteryRect.top() - barBottom) < 1.0,
                 qPrintable(QStringLiteral("The strip starts at %1, the bar ends at %2").arg(batteryRect.top()).arg(barBottom)));
        offset = checkSpan(batteryRect, QStringLiteral("battery"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));
        QVERIFY2(qAbs(batteryRect.height() - stripHeight) < 0.5,
                 qPrintable(QStringLiteral("The strip is %1 tall, not %2").arg(batteryRect.height()).arg(stripHeight)));
        offset = checkToolStrips(QStringLiteral("battery"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        // The grid folded away: the strip follows its handle to the right edge, and back.
        QQuickItem *const dashboard = strips->parentItem();
        QVERIFY(dashboard);
        QVERIFY(dashboard->setProperty("cameraStripOpen", false));
        QTest::qWait(kSettleMs);
        offset = checkSpan(sceneRect(battery), QStringLiteral("grid folded"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));
        QVERIFY2(sceneRect(battery).right() > batteryRect.right() + 1.0, "The strip did not widen with the grid folded");
        QVERIFY(dashboard->setProperty("cameraStripOpen", true));
        QTest::qWait(kSettleMs);
        offset = checkSpan(sceneRect(battery), QStringLiteral("grid open again"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        injectBattery(mockLink, vehicle, 18, MAV_BATTERY_CHARGE_STATE_CRITICAL);
        QVERIFY(verifyProperty(kBattery, "line", QStringLiteral("잔량 18%, 즉시 복귀하십시오"), QStringLiteral("critical")));
        QVERIFY(verifyProperty(kBattery, "title", QStringLiteral("배터리 위험"), QStringLiteral("critical")));
        QCOMPARE(lastSpoken(), QStringLiteral("Low battery, 18 percent remaining. Return immediately."));

        // Altitude: over the 160 m ceiling, held 1 m under it, down 3 m under it.
        injectAltitude(mockLink, vehicle, 162);
        QVERIFY(verifyProperty(kAltitude, "line", QStringLiteral("제한 고도 160 m를 넘었습니다"), QStringLiteral("162 m")));
        QVERIFY(verifyProperty(kAltitude, "title", QStringLiteral("고도 초과"), QStringLiteral("162 m")));
        QCOMPARE(lastSpoken(), QStringLiteral("Altitude limit exceeded. Above 160 meters."));
        injectAltitude(mockLink, vehicle, 159);
        QVERIFY_TRUE_WAIT(altitudeFact->rawValue().toDouble() == 159.0, TestTimeout::mediumMs());
        QVERIFY2(findVisibleItem(_rootItem, kAltitude, 0), "1 m under the ceiling took the strip down");
        injectAltitude(mockLink, vehicle, 157);
        QVERIFY(verifyVisibility(kAltitude, false, QStringLiteral("157 m")));

        // Radius: none without GF_MAX_HOR_DIST, then 500 m once it is set.
        QVERIFY(setHomeDistance(vehicle, 620));
        QTest::qWait(300);
        QVERIFY2(!findVisibleItem(_rootItem, kRadius, 0), "A radius strip with no radius limit set");
        ParameterManager *const params = vehicle->parameterManager();
        QVERIFY(params);
        Fact *const radiusParam = params->getParameter(ParameterManager::defaultComponentId, QStringLiteral("GF_MAX_HOR_DIST"));
        QVERIFY(radiusParam);
        radiusParam->setRawValue(500);
        QVERIFY_TRUE_WAIT(!params->pendingWrites(), TestTimeout::mediumMs());
        QVERIFY(verifyProperty(kRadius, "line", QStringLiteral("제한 반경 500 m를 넘었습니다"), QStringLiteral("620 m out")));
        QVERIFY(verifyProperty(kRadius, "title", QStringLiteral("반경 초과"), QStringLiteral("620 m out")));
        QCOMPARE(lastSpoken(), QStringLiteral("Distance limit exceeded. Beyond 500 meters."));

        // Wind: at 10 m/s with COM_WIND_WARN unset, held at 9.5.
        injectWind(mockLink, vehicle, 10.5);
        QVERIFY(verifyProperty(kWind, "line", QStringLiteral("제자리 유지가 어렵습니다"), QStringLiteral("10.5 m/s")));
        QVERIFY(verifyProperty(kWind, "title", QStringLiteral("강풍 경고"), QStringLiteral("10.5 m/s")));
        QCOMPARE(lastSpoken(), QStringLiteral("High wind warning. Unable to hold position."));
        injectWind(mockLink, vehicle, 9.5);
        QVERIFY_TRUE_WAIT(windSpeed(vehicle) == 9.5, TestTimeout::mediumMs());
        QVERIFY2(findVisibleItem(_rootItem, kWind, 0), "0.5 m/s under the threshold took the strip down");

        // All four at once: battery, altitude, radius, wind, between the columns under the bar, beside the
        // top of the tool strips, which have not moved.
        injectAltitude(mockLink, vehicle, 165);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("165 m")));
        QTest::qWait(kSettleMs);
        double expectedTop = barBottom;
        for (const QString &name : { kBattery, kAltitude, kRadius, kWind }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 0);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not up with the other three").arg(name)));
            const QRectF rect = sceneRect(item);
            QVERIFY2(qAbs(rect.top() - expectedTop) < 1.0,
                     qPrintable(QStringLiteral("%1 starts at %2, not %3").arg(name).arg(rect.top()).arg(expectedTop)));
            offset = checkSpan(rect, name);
            QVERIFY2(offset.isEmpty(), qPrintable(offset));
            expectedTop = rect.bottom();
        }
        offset = checkToolStrips(QStringLiteral("all four"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));
        QVERIFY2(sceneRect(strips).bottom() > sceneRect(leftStrip).top() + 1.0, "Four strips do not reach over the tool strip");

        // With the link gone the red strip comes up first in the stack, the four step down one
        // strip under it, and the tool strips still stay put.
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("link up")));
        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("comm lost")));
        QTest::qWait(kSettleMs);
        QQuickItem *const red = findVisibleItem(_rootItem, kLinkLost, 0);
        QVERIFY(red);
        QCOMPARE(red->property("color").value<QColor>(), QColor(QStringLiteral("#e5484d")));
        QCOMPARE(red->property("title").toString(), QStringLiteral("통신 두절"));
        QCOMPARE(red->property("line").toString(), QStringLiteral("기체와의 통신이 끊겼습니다"));
        const QRectF redRect = sceneRect(red);
        QVERIFY(qAbs(redRect.top() - barBottom) < 1.0);
        QVERIFY(qAbs(redRect.height() - stripHeight) < 0.5);
        offset = checkSpan(redRect, QStringLiteral("comm lost"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));
        QQuickItem *const batteryUnder = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(batteryUnder);
        QVERIFY2(qAbs(sceneRect(batteryUnder).top() - redRect.bottom()) < 1.0, "The battery strip is not right under the red one");
        QVERIFY(qAbs(sceneRect(strips).bottom() - (expectedTop + stripHeight)) < 1.0);
        offset = checkToolStrips(QStringLiteral("comm lost over all four"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("link back")));

        // Once per rise, and the rises only: the held values said nothing, and 165 m is a new
        // rise after 157 m took the altitude strip down.
        QStringList said;
        for (const QList<QVariant> &args : std::as_const(spoke)) {
            said.append(args.at(0).toString());
        }
        QCOMPARE(said, QStringList({
            QStringLiteral("Low battery, 20 percent remaining. Prepare to return."),
            QStringLiteral("Low battery, 18 percent remaining. Return immediately."),
            QStringLiteral("Altitude limit exceeded. Above 160 meters."),
            QStringLiteral("Distance limit exceeded. Beyond 500 meters."),
            QStringLiteral("High wind warning. Unable to hold position."),
            QStringLiteral("Altitude limit exceeded. Above 160 meters."),
        }));
    });
}

void PoliceWarningsUITest::_testTapDismisses()
{
    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        Fact *const altitudeFact = vehicle->altitudeRelative();
        QVERIFY(altitudeFact);
        VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
        QVERIFY(linkManager);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        QObject *const warnings = findWarnings(topBar);
        QVERIFY2(warnings, "The dashboard has no PoliceWarnings");
        QSignalSpy spoke(warnings, SIGNAL(spoke(QString)));
        QVERIFY(spoke.isValid());
        QQuickItem *const leftStrip = findVisibleItem(_rootItem, kLeftStrip, 5000);
        QVERIFY2(leftStrip, "The left tool strip is not up");
        const double barBottom = sceneRect(topBar).bottom();

        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        injectAltitude(mockLink, vehicle, 30);
        injectBattery(mockLink, vehicle, 25, MAV_BATTERY_CHARGE_STATE_LOW);
        injectAltitude(mockLink, vehicle, 170);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("170 m")));
        QVERIFY_TRUE_WAIT(spoke.count() == 2, TestTimeout::mediumMs());
        spoke.clear();

        // A tap on the battery strip takes that one away, silently; altitude moves up into its
        // place and the tool strip stays where it was.
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY(verifyVisibility(kBattery, false, QStringLiteral("battery tapped")));
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("battery tapped")));
        QTest::qWait(300);
        QQuickItem *const altitude = findVisibleItem(_rootItem, kAltitude, 0);
        QVERIFY(altitude);
        QVERIFY(qAbs(sceneRect(altitude).top() - barBottom) < 1.0);
        QVERIFY(qAbs(sceneRect(leftStrip).top() - (barBottom + 8)) < 1.0);

        // The pack draining further is no reason to bring it back.
        injectBattery(mockLink, vehicle, 23, MAV_BATTERY_CHARGE_STATE_LOW);
        QTest::qWait(kSettleMs);
        QVERIFY2(!findVisibleItem(_rootItem, kBattery, 0), "A dismissed low battery came back while still low");
        QCOMPARE(spoke.count(), 0);

        // Critical is: back on top, in its critical words, and spoken again.
        injectBattery(mockLink, vehicle, 15, MAV_BATTERY_CHARGE_STATE_CRITICAL);
        QVERIFY(verifyProperty(kBattery, "title", QStringLiteral("배터리 위험"), QStringLiteral("critical after dismiss")));
        QVERIFY(verifyProperty(kBattery, "line", QStringLiteral("잔량 15%, 즉시 복귀하십시오"), QStringLiteral("critical after dismiss")));
        QVERIFY_TRUE_WAIT(spoke.count() == 1, TestTimeout::mediumMs());
        QCOMPARE(spoke.first().at(0).toString(), QStringLiteral("Low battery, 15 percent remaining. Return immediately."));
        QTest::qWait(300);
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QVERIFY(qAbs(sceneRect(battery).top() - barBottom) < 1.0);
        QVERIFY(sceneRect(altitude).top() >= sceneRect(battery).bottom() - 0.5);
        spoke.clear();

        // Dismissing altitude leaves the battery; once the aircraft is back under and over the
        // ceiling again it shows again.
        QVERIFY2(clickItemFraction(kAltitude, 0.8, 0.5), "Could not tap the altitude strip");
        QVERIFY(verifyVisibility(kAltitude, false, QStringLiteral("altitude tapped")));
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("altitude tapped")));
        injectAltitude(mockLink, vehicle, 140);
        QVERIFY_TRUE_WAIT(altitudeFact->rawValue().toDouble() == 140.0, TestTimeout::mediumMs());
        QTest::qWait(300);
        QVERIFY(!findVisibleItem(_rootItem, kAltitude, 0));
        injectAltitude(mockLink, vehicle, 162);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("back over the ceiling")));
        QVERIFY_TRUE_WAIT(spoke.count() == 1, TestTimeout::mediumMs());
        QCOMPARE(spoke.first().at(0).toString(), QStringLiteral("Altitude limit exceeded. Above 160 meters."));
    });
}

void PoliceWarningsUITest::_testLinkLostStrip()
{
    _ignorePreexistingWarnings();
    // The link goes early here, before the vehicle's start-up requests have all been answered.
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries")));

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
        QVERIFY(linkManager);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        const double barBottom = sceneRect(topBar).bottom();

        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));

        const auto loseLink = [&](bool lost) {
            mockLink->setCommLost(lost);
            return UnitTest::waitForCondition([&] { return linkManager->communicationLost() == lost; },
                                              TestTimeout::longMs(), QStringLiteral("comm lost %1").arg(lost));
        };

        // Up, first, above the battery strip.
        QVERIFY(loseLink(true));
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("comm lost")));
        QTest::qWait(300);
        QQuickItem *const red = findVisibleItem(_rootItem, kLinkLost, 0);
        QVERIFY(red);
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QVERIFY(qAbs(sceneRect(red).top() - barBottom) < 1.0);
        QVERIFY(qAbs(sceneRect(battery).top() - sceneRect(red).bottom()) < 1.0);

        // A tap takes it away and it stays away while the link stays lost; the battery moves up.
        QVERIFY2(clickItemFraction(kLinkLost, 0.5, 0.5), "Could not tap the red strip");
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("red tapped")));
        QTest::qWait(kSettleMs);
        QVERIFY2(!findVisibleItem(_rootItem, kLinkLost, 0), "A dismissed link loss came back while still lost");
        QVERIFY(linkManager->communicationLost());
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("red tapped")));
        QVERIFY(qAbs(sceneRect(battery).top() - barBottom) < 1.0);

        // Back and lost again: up again.
        QVERIFY(loseLink(false));
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("link back")));
        QVERIFY(loseLink(true));
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("lost again")));
        QCOMPARE(red->property("title").toString(), QStringLiteral("통신 두절"));
        QVERIFY(loseLink(false));

        // The RC receiver alone: its own words. The mock's own SYS_STATUS says otherwise once a
        // second, so the lost state is resent faster than that.
        QTimer rcLost;
        rcLost.setInterval(20);
        (void) connect(&rcLost, &QTimer::timeout, mockLink.data(), [&] { injectRcLost(mockLink, vehicle); });
        rcLost.start();
        QVERIFY(verifyProperty(kLinkLost, "title", QStringLiteral("RC 링크 끊김"), QStringLiteral("rc lost")));
        QVERIFY(verifyProperty(kLinkLost, "line",
                               QStringLiteral("조종기 신호가 수신되지 않습니다"), QStringLiteral("rc lost")));
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("rc lost")));
        rcLost.stop();
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("rc back")));
    });
}

void PoliceWarningsUITest::_testFollowModeStrip()
{
    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, QStringLiteral("policeDroneDashboard"), 5000);
        QVERIFY2(dashboard, "The police dashboard is not up");
        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        const double stripHeight = topBar->height() * kStripToBar;

        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));
        QVERIFY(verifyVisibility(kFollow, false, QStringLiteral("nothing following")));

        // In the air and in PX4's Hold, a guided mode. Follow slid on and then stopped, with the
        // aircraft left in it: the dashboard's own two latches, set as the follow panel sets them.
        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        vehicle->setFlightMode(vehicle->pauseFlightMode());
        QVERIFY_TRUE_WAIT(vehicle->guidedMode(), TestTimeout::mediumMs());
        QVERIFY(dashboard->setProperty("_followArmed", true));
        QVERIFY(dashboard->setProperty("_followEngaged", true));

        const QString words = QStringLiteral("추종이 꺼졌는데 기체가 GUIDED 입니다, 조종간이 듣지 않습니다. 비행모드를 바꾸십시오");
        QVERIFY(verifyProperty(kFollow, "line", words, QStringLiteral("follow off in GUIDED")));
        QVERIFY(verifyProperty(kFollow, "title", QStringLiteral("추종 모드"), QStringLiteral("follow off in GUIDED")));
        QVERIFY(verifyVisibility(kFollow, true, QStringLiteral("follow off in GUIDED")));
        QTest::qWait(300);
        QQuickItem *const follow = findVisibleItem(_rootItem, kFollow, 0);
        QVERIFY(follow);
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QCOMPARE(follow->property("color").value<QColor>(), QColor(QStringLiteral("#b35c00")));
        QVERIFY2(sceneRect(follow).top() >= sceneRect(battery).bottom() - 0.5, "The follow strip is not under the battery strip");
        QQuickItem *const strips = findItem(_rootItem, kStrips);
        QVERIFY(strips);
        QVERIFY2(qAbs(sceneRect(follow).bottom() - sceneRect(strips).bottom()) < 1.0, "The follow strip is not last");
        // One line at the tablet's width: the strip keeps its height.
        QVERIFY2(qAbs(follow->height() - stripHeight) < 0.5,
                 qPrintable(QStringLiteral("The follow strip is %1 tall, not %2").arg(follow->height()).arg(stripHeight)));

        QVERIFY2(clickItemFraction(kFollow, 0.5, 0.5), "Could not tap the follow strip");
        QVERIFY(verifyVisibility(kFollow, false, QStringLiteral("follow tapped")));
        QTest::qWait(kSettleMs);
        QVERIFY2(!findVisibleItem(_rootItem, kFollow, 0), "A dismissed follow warning came back while unchanged");
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("follow tapped")));

        // Cleared and back: up again.
        QVERIFY(dashboard->setProperty("_followEngaged", false));
        QTest::qWait(300);
        QVERIFY(dashboard->setProperty("_followEngaged", true));
        QVERIFY(verifyVisibility(kFollow, true, QStringLiteral("follow warning back")));
    });
}

void PoliceWarningsUITest::_testTargetPickRefusalStrip()
{
    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, QStringLiteral("policeDroneDashboard"), 5000);
        QVERIFY2(dashboard, "The police dashboard is not up");
        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        SiyiAiController *const ai = SiyiAiController::instance();
        QVERIFY(ai);
        const bool capture = !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty();
        const QString noTarget = QStringLiteral("지정한 곳에 대상이 없습니다");
        const QString stillTracking = QStringLiteral("추적 중입니다. 먼저 추적을 해제하십시오");

        QVERIFY(verifyVisibility(kPick, false, QStringLiteral("no refusal yet")));

        // Alone, as the mockup draws it: under the bar, between the two columns.
        emit ai->trackRequestFailed(0, noTarget);
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("refusal 0")));
        if (capture) {
            _grab(QStringLiteral("pick_strip_normal"));
            if (QTest::currentTestFailed()) return;
        }
        QQuickItem *const pick = findVisibleItem(_rootItem, kPick, 0);
        QVERIFY(pick);
        QCOMPARE(pick->property("title").toString(), QStringLiteral("표적 지정 실패"));
        QCOMPARE(pick->property("line").toString(), noTarget);
        QCOMPARE(pick->property("color").value<QColor>(), QColor(QStringLiteral("#b35c00")));
        QVERIFY(qAbs(sceneRect(pick).top() - sceneRect(topBar).bottom()) < 1.0);
        QVERIFY(qAbs(pick->height() - topBar->height() * kStripToBar) < 0.5);
        QTRY_VERIFY_WITH_TIMEOUT(!findVisibleItem(_rootItem, kPick, 0), 5000);

        // First in the stack, over a warning that is already up.
        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));
        QElapsedTimer shown;
        emit ai->trackRequestFailed(6, stillTracking);
        shown.start();
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("refusal 6")));
        QTest::qWait(300);
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QCOMPARE(pick->property("line").toString(), stillTracking);
        QVERIFY(qAbs(sceneRect(pick).top() - sceneRect(topBar).bottom()) < 1.0);
        QVERIFY(qAbs(sceneRect(battery).top() - sceneRect(pick).bottom()) < 1.0);

        // Gone by itself after 3 s, and not before.
        QTRY_VERIFY_WITH_TIMEOUT(!findVisibleItem(_rootItem, kPick, 0), 5000);
        QVERIFY2(shown.elapsed() >= 2900,
                 qPrintable(QStringLiteral("The strip went after %1 ms").arg(shown.elapsed())));
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("pick strip gone")));

        // A new refusal 2 s in starts the 3 s over.
        emit ai->trackRequestFailed(0, noTarget);
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("first refusal")));
        QTest::qWait(2000);
        emit ai->trackRequestFailed(7, QStringLiteral("모델이 초기화되지 않았습니다"));
        QTest::qWait(2000);
        QVERIFY2(findVisibleItem(_rootItem, kPick, 0), "A second refusal did not restart the strip's 3 s");
        QCOMPARE(pick->property("line").toString(), QStringLiteral("모델이 초기화되지 않았습니다"));
        QVERIFY(verifyVisibility(kPick, false, QStringLiteral("second refusal after 3 s")));

        // A tap hides it at once, and only it.
        emit ai->trackRequestFailed(0, noTarget);
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("refusal before tap")));
        const QRectF windowedPick = sceneRect(pick);
        QVERIFY2(clickItemFraction(kPick, 0.5, 0.5), "Could not tap the pick strip");
        QTRY_VERIFY_WITH_TIMEOUT(!findVisibleItem(_rootItem, kPick, 0), 500);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("pick strip tapped")));

        // Swapped: the strips keep their windowed band.
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY(verifyVisibility(kBattery, false, QStringLiteral("battery tapped")));
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        QTest::qWait(kSettleMs);
        emit ai->trackRequestFailed(6, stillTracking);
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("refusal full screen")));
        if (capture) {
            _grab(QStringLiteral("pick_strip_fullscreen"));
            if (QTest::currentTestFailed()) return;
        }
        QVERIFY(qAbs(sceneRect(pick).left() - windowedPick.left()) < 1.0);
        QVERIFY(qAbs(sceneRect(pick).width() - windowedPick.width()) < 1.0);
        // The big picture's chips drop below the strip rather than sit under it.
        QQuickItem *const zoomPanel = findVisibleItem(_rootItem, QStringLiteral("policeZoomCameraPanel"), 5000);
        QVERIFY(zoomPanel);
        QQuickItem *const nameChip = findVisibleItem(zoomPanel, QStringLiteral("policeCameraNameChip"), 5000);
        QVERIFY2(nameChip, "The zoom name chip is not up full screen");
        QQuickItem *const stateChips = findVisibleItem(zoomPanel, QStringLiteral("policeCameraStateChips"), 5000);
        QVERIFY2(stateChips, "The state chips are not up full screen");
        QTRY_VERIFY_WITH_TIMEOUT(!sceneRect(nameChip).intersects(sceneRect(pick)), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!sceneRect(stateChips).intersects(sceneRect(pick)), 2000);

        // A cancel the module did not act on: its reason is the title.
        emit ai->trackRequestFailed(-1, QStringLiteral("추적 해제 실패"));
        QVERIFY(verifyProperty(kPick, "title", QStringLiteral("추적 해제 실패"), QStringLiteral("cancel failed")));
        QCOMPARE(pick->property("line").toString(), QString());
        QVERIFY(verifyVisibility(kPick, true, QStringLiteral("cancel failed")));
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
    });
}

bool PoliceWarningsUITest::_tapBelowStrips(const QString &name)
{
    QQuickItem *const item = findVisibleItem(_rootItem, name, 5000);
    QQuickItem *const strips = findItem(_rootItem, kStrips);
    if (!item || !strips) {
        return false;
    }
    const QRectF rect = sceneRect(item);
    const double covered = sceneRect(strips).bottom();
    if (covered >= rect.bottom() - 2.0) {
        return false;
    }
    const double visibleTop = qMax(rect.top(), covered);
    return clickItemFraction(name, 0.5, ((visibleTop + rect.bottom()) / 2.0 - rect.top()) / rect.height());
}

void PoliceWarningsUITest::_testPanelOverStrips()
{
    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, QStringLiteral("policeTopBar"), 5000);
        QVERIFY2(topBar, "The police top bar is not up");
        QQuickItem *const strips = findItem(_rootItem, kStrips);
        QVERIFY2(strips, "The dashboard has no warning strip stack");
        QQuickItem *const leftStrip = findVisibleItem(_rootItem, kLeftStrip, 5000);
        QVERIFY2(leftStrip, "The left tool strip is not up");
        const double leftTop = sceneRect(leftStrip).top();

        // Low battery on the ground: one strip, over the top of the tool strip, which stays put.
        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));
        QTest::qWait(300);
        QVERIFY2(sceneRect(strips).bottom() > leftTop + 1.0, "The strip does not reach over the tool strip");
        QCOMPARE(sceneRect(leftStrip).top(), leftTop);

        // Takeoff tapped where the strip leaves it showing, then the red RC strip on top of that:
        // the panel stays up and over both.
        QVERIFY2(_tapBelowStrips(QStringLiteral("policeToolTakeoff")), "The takeoff entry is covered or could not be tapped");
        QQuickItem *const panel = findVisibleItem(_rootItem, QStringLiteral("policeTakeoffPanel"), 5000);
        QVERIFY2(panel, "Takeoff panel never opened under the strip");
        QTimer rcLost;
        rcLost.setInterval(20);
        (void) connect(&rcLost, &QTimer::timeout, mockLink.data(), [&] { injectRcLost(mockLink, vehicle); });
        rcLost.start();
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("rc lost")));
        QTest::qWait(300);
        QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("policeTakeoffPanel"), 0), "The takeoff panel closed when the RC strip came up");
        QCOMPARE(sceneRect(leftStrip).top(), leftTop);
        QVERIFY2(sceneRect(panel).intersects(sceneRect(strips)), "The takeoff panel does not reach under the strips");
        QVERIFY2(leftStrip->z() > strips->z(), "The takeoff panel's strip is not above the warning strips");

        // A tap on a strip while the panel is up closes the panel and leaves the strip.
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QTRY_VERIFY2(!findVisibleItem(_rootItem, QStringLiteral("policeTakeoffPanel"), 0), "The takeoff panel stayed after a tap on a strip");
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("panel closed")));
        QVERIFY2(leftStrip->z() < strips->z(), "The tool strip stayed above the warning strips with its panel closed");

        // The strip's own tap, at its end beside the tool strip, still takes it away; the tool
        // strip is not under it.
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        const QRectF leftRect = sceneRect(leftStrip);
        const QRectF batteryRect = sceneRect(battery);
        QVERIFY(!batteryRect.intersects(leftRect));
        QVERIFY2(clickItemFraction(kBattery, 0.05, 0.5), "Could not tap the battery strip beside the tool strip");
        QVERIFY(verifyVisibility(kBattery, false, QStringLiteral("battery tapped beside the tool strip")));
        rcLost.stop();
    });
}

void PoliceWarningsUITest::_captureOverlay()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
        QVERIFY(linkManager);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("20 %")));
        _grab(QStringLiteral("strips_1_one_strip"));
        if (QTest::currentTestFailed()) return;

        // Two strips cover the takeoff entry whole, so the panel is opened under one and the red
        // RC strip comes up while it is open.
        QVERIFY2(_tapBelowStrips(QStringLiteral("policeToolTakeoff")), "The takeoff entry is covered or could not be tapped");
        QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("policeTakeoffPanel"), 5000), "Takeoff panel never opened");
        QTimer rcLost;
        rcLost.setInterval(20);
        (void) connect(&rcLost, &QTimer::timeout, mockLink.data(), [&] { injectRcLost(mockLink, vehicle); });
        rcLost.start();
        QVERIFY(verifyProperty(kLinkLost, "title", QStringLiteral("RC 링크 끊김"), QStringLiteral("rc lost")));
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("rc lost")));
        _grab(QStringLiteral("strips_4_takeoff_panel"));
        if (QTest::currentTestFailed()) return;

        // A tap on the battery strip closes the panel and leaves the strips: the RC words.
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY(verifyVisibility(QStringLiteral("policeTakeoffPanel"), false, QStringLiteral("panel closed")));
        _grab(QStringLiteral("strips_5_rc_lost"));
        if (QTest::currentTestFailed()) return;
        rcLost.stop();
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("rc back")));

        // In the air: battery, altitude and wind, then the link.
        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        injectAltitude(mockLink, vehicle, 162);
        injectWind(mockLink, vehicle, 12);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("162 m")));
        QVERIFY(verifyVisibility(kWind, true, QStringLiteral("12 m/s")));
        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("comm lost")));
        _grab(QStringLiteral("strips_2_four_strips"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickItemFraction(kLinkLost, 0.5, 0.5), "Could not tap the red strip");
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("red tapped")));
        _grab(QStringLiteral("strips_3_after_red_tap"));
        mockLink->setCommLost(false);
    });
}

void PoliceWarningsUITest::_captureStrips()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingWarnings();

    // The mockup's figures: 20 %, 160 m.
    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        QVERIFY(mockLink);
        QVERIFY(vehicle);
        VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
        QVERIFY(linkManager);
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);
        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        injectAltitude(mockLink, vehicle, 30);

        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("comm lost")));
        _grab(QStringLiteral("banner_F_red_strip"));
        if (QTest::currentTestFailed()) return;

        // Nothing reaches the vehicle while the link is lost, so the two amber strips go up first.
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!linkManager->communicationLost(), TestTimeout::longMs());
        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        injectAltitude(mockLink, vehicle, 162);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("20 %")));
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("162 m")));
        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("comm lost again")));
        _grab(QStringLiteral("banner_G_red_plus_amber"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickItemFraction(kLinkLost, 0.5, 0.5), "Could not tap the red strip");
        QVERIFY(verifyVisibility(kLinkLost, false, QStringLiteral("red tapped")));
        _grab(QStringLiteral("banner_H_after_tap"));
        if (QTest::currentTestFailed()) return;
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY2(clickItemFraction(kAltitude, 0.5, 0.5), "Could not tap the altitude strip");
        QVERIFY(verifyVisibility(kAltitude, false, QStringLiteral("altitude tapped")));

        QQuickItem *const dashboard = findVisibleItem(_rootItem, QStringLiteral("policeDroneDashboard"), 5000);
        QVERIFY(dashboard);
        vehicle->setFlightMode(vehicle->pauseFlightMode());
        QVERIFY_TRUE_WAIT(vehicle->guidedMode(), TestTimeout::mediumMs());
        QVERIFY(dashboard->setProperty("_followArmed", true));
        QVERIFY(dashboard->setProperty("_followEngaged", true));
        QVERIFY(verifyVisibility(kFollow, true, QStringLiteral("follow off in GUIDED")));
        _grab(QStringLiteral("banner_I_follow_strip"));
        if (QTest::currentTestFailed()) return;
        QVERIFY2(clickItemFraction(kFollow, 0.5, 0.5), "Could not tap the follow strip");
        QVERIFY(verifyVisibility(kFollow, false, QStringLiteral("follow tapped")));

        QTimer rcLost;
        rcLost.setInterval(20);
        (void) connect(&rcLost, &QTimer::timeout, mockLink.data(), [&] { injectRcLost(mockLink, vehicle); });
        rcLost.start();
        QVERIFY(verifyProperty(kLinkLost, "title", QStringLiteral("RC 링크 끊김"), QStringLiteral("rc lost")));
        QVERIFY(verifyVisibility(kLinkLost, true, QStringLiteral("rc lost")));
        _grab(QStringLiteral("banner_J_rc_lost"));
        rcLost.stop();
    });
}
