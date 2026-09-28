#include "PoliceWarningsUITest.h"

#include <algorithm>
#include <iterator>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
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
#include "Vehicle.h"
#include "VehicleLinkManager.h"

UT_REGISTER_TEST(PoliceWarningsUITest, TestLabel::Integration)

namespace {

const QString kBattery  = QStringLiteral("policeWarningBattery");
const QString kAltitude = QStringLiteral("policeWarningAltitude");
const QString kRadius   = QStringLiteral("policeWarningRadius");
const QString kWind     = QStringLiteral("policeWarningWind");

const QString kStrips      = QStringLiteral("policeWarningStrips");
const QString kLeftStrip   = QStringLiteral("policeGuidedToolStrip");
const QString kRightStrip  = QStringLiteral("policeCameraToolStrip");

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

/// The red link banner, up or not: the Rectangle two levels above its 통신 두절 title. It has no
/// objectName, so it is found by what it says and its red.
QQuickItem *findLinkBanner(QQuickItem *root)
{
    if (!root) {
        return nullptr;
    }
    if (root->property("text").toString() == QStringLiteral("통신 두절")) {
        QQuickItem *const banner = root->parentItem() ? root->parentItem()->parentItem() : nullptr;
        if (banner && (banner->property("color").value<QColor>() == QColor(QStringLiteral("#d31f1f")))) {
            return banner;
        }
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findLinkBanner(child)) {
            return found;
        }
    }
    return nullptr;
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

        const double barBottom = sceneRect(topBar).bottom();
        const double stripHeight = topBar->height() * kStripToBar;
        // Both tool strips sit 8 px under the stack, and with nothing up that is under the bar.
        const auto checkToolStrips = [&](int count, const QString &context) {
            const double expected = barBottom + (count * stripHeight) + 8;
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
        QString offset = checkToolStrips(0, QStringLiteral("nothing up"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        // Where the red link banner sits with nothing up, laid out though hidden. It stays there.
        QQuickItem *const linkBanner = findLinkBanner(_rootItem);
        QVERIFY2(linkBanner, "The red link banner is not in the dashboard");
        const QRectF bannerHome = sceneRect(linkBanner);

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
        QCOMPARE(lastSpoken(), QStringLiteral("배터리가 부족합니다. 잔량 20퍼센트. 복귀를 준비하십시오."));

        // Full width, straight under the bar, the mockup's height, and the tool strips one strip down.
        QQuickItem *const battery = findVisibleItem(_rootItem, kBattery, 0);
        QVERIFY(battery);
        QTest::qWait(300);
        const QRectF batteryRect = sceneRect(battery);
        QVERIFY2(qAbs(batteryRect.top() - barBottom) < 1.0,
                 qPrintable(QStringLiteral("The strip starts at %1, the bar ends at %2").arg(batteryRect.top()).arg(barBottom)));
        QCOMPARE(batteryRect.left(), 0.0);
        QCOMPARE(batteryRect.width(), static_cast<qreal>(_window->width()));
        QVERIFY2(qAbs(batteryRect.height() - stripHeight) < 0.5,
                 qPrintable(QStringLiteral("The strip is %1 tall, not %2").arg(batteryRect.height()).arg(stripHeight)));
        offset = checkToolStrips(1, QStringLiteral("battery"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        injectBattery(mockLink, vehicle, 18, MAV_BATTERY_CHARGE_STATE_CRITICAL);
        QVERIFY(verifyProperty(kBattery, "line", QStringLiteral("잔량 18%, 즉시 복귀하십시오"), QStringLiteral("critical")));
        QVERIFY(verifyProperty(kBattery, "title", QStringLiteral("배터리 위험"), QStringLiteral("critical")));
        QCOMPARE(lastSpoken(), QStringLiteral("배터리가 부족합니다. 잔량 18퍼센트. 즉시 복귀하십시오."));

        // Altitude: over the 150 m ceiling, held 1 m under it, down 3 m under it.
        injectAltitude(mockLink, vehicle, 152);
        QVERIFY(verifyProperty(kAltitude, "line", QStringLiteral("제한 고도 150 m를 넘었습니다"), QStringLiteral("152 m")));
        QVERIFY(verifyProperty(kAltitude, "title", QStringLiteral("고도 초과"), QStringLiteral("152 m")));
        QCOMPARE(lastSpoken(), QStringLiteral("고도 초과. 제한 고도 150미터를 넘었습니다."));
        injectAltitude(mockLink, vehicle, 149);
        QVERIFY_TRUE_WAIT(altitudeFact->rawValue().toDouble() == 149.0, TestTimeout::mediumMs());
        QVERIFY2(findVisibleItem(_rootItem, kAltitude, 0), "1 m under the ceiling took the strip down");
        injectAltitude(mockLink, vehicle, 147);
        QVERIFY(verifyVisibility(kAltitude, false, QStringLiteral("147 m")));

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
        QCOMPARE(lastSpoken(), QStringLiteral("반경 초과. 제한 반경 500미터를 넘었습니다."));

        // Wind: at 10 m/s with COM_WIND_WARN unset, held at 9.5.
        injectWind(mockLink, vehicle, 10.5);
        QVERIFY(verifyProperty(kWind, "line", QStringLiteral("제자리 유지가 어렵습니다"), QStringLiteral("10.5 m/s")));
        QVERIFY(verifyProperty(kWind, "title", QStringLiteral("강풍 경고"), QStringLiteral("10.5 m/s")));
        QCOMPARE(lastSpoken(), QStringLiteral("강풍 경고. 제자리 유지가 어렵습니다."));
        injectWind(mockLink, vehicle, 9.5);
        QVERIFY_TRUE_WAIT(windSpeed(vehicle) == 9.5, TestTimeout::mediumMs());
        QVERIFY2(findVisibleItem(_rootItem, kWind, 0), "0.5 m/s under the threshold took the strip down");

        // All four at once: battery, altitude, radius, wind, edge to edge under the bar, and the
        // tool strips four strips down.
        injectAltitude(mockLink, vehicle, 155);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("155 m")));
        QTest::qWait(kSettleMs);
        double expectedTop = barBottom;
        for (const QString &name : { kBattery, kAltitude, kRadius, kWind }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 0);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not up with the other three").arg(name)));
            const QRectF rect = sceneRect(item);
            QVERIFY2(qAbs(rect.top() - expectedTop) < 1.0,
                     qPrintable(QStringLiteral("%1 starts at %2, not %3").arg(name).arg(rect.top()).arg(expectedTop)));
            QCOMPARE(rect.width(), static_cast<qreal>(_window->width()));
            expectedTop = rect.bottom();
        }
        offset = checkToolStrips(4, QStringLiteral("all four"));
        QVERIFY2(offset.isEmpty(), qPrintable(offset));

        // With the link gone the red banner comes up in its own place and shape, drawn over the
        // strips where they meet, and the strips stay where they are.
        QVERIFY2(!linkBanner->isVisible(), "The link is up, yet the red banner shows");
        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        QVERIFY_TRUE_WAIT(linkBanner->isVisible(), TestTimeout::mediumMs());
        QTest::qWait(kSettleMs);
        const QRectF red = sceneRect(linkBanner);
        QVERIFY2(qAbs(red.top() - bannerHome.top()) < 1.0,
                 qPrintable(QStringLiteral("The red banner starts at %1, not at its own %2").arg(red.top()).arg(bannerHome.top())));
        QVERIFY(qAbs(red.center().x() - (_window->width() / 2.0)) < 1.0);
        QVERIFY2((linkBanner->parentItem() == strips->parentItem()) && (linkBanner->z() > strips->z()),
                 "The strips draw over the red banner");
        QVERIFY(qAbs(sceneRect(strips).bottom() - expectedTop) < 1.0);
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!linkManager->communicationLost(), TestTimeout::longMs());

        // Once per rise, and the rises only: the held values said nothing, and 155 m is a new
        // rise after 147 m took the altitude strip down.
        QStringList said;
        for (const QList<QVariant> &args : std::as_const(spoke)) {
            said.append(args.at(0).toString());
        }
        QCOMPARE(said, QStringList({
            QStringLiteral("배터리가 부족합니다. 잔량 20퍼센트. 복귀를 준비하십시오."),
            QStringLiteral("배터리가 부족합니다. 잔량 18퍼센트. 즉시 복귀하십시오."),
            QStringLiteral("고도 초과. 제한 고도 150미터를 넘었습니다."),
            QStringLiteral("반경 초과. 제한 반경 500미터를 넘었습니다."),
            QStringLiteral("강풍 경고. 제자리 유지가 어렵습니다."),
            QStringLiteral("고도 초과. 제한 고도 150미터를 넘었습니다."),
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
        const double stripHeight = topBar->height() * kStripToBar;

        QVERIFY2(takeOff(vehicle), "The mock never reported itself in the air");
        injectAltitude(mockLink, vehicle, 30);
        injectBattery(mockLink, vehicle, 25, MAV_BATTERY_CHARGE_STATE_LOW);
        injectAltitude(mockLink, vehicle, 160);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("low")));
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("160 m")));
        QVERIFY_TRUE_WAIT(spoke.count() == 2, TestTimeout::mediumMs());
        spoke.clear();

        // A tap on the battery strip takes that one away, silently; altitude moves up into its
        // place and the tool strip follows.
        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY(verifyVisibility(kBattery, false, QStringLiteral("battery tapped")));
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("battery tapped")));
        QTest::qWait(300);
        QQuickItem *const altitude = findVisibleItem(_rootItem, kAltitude, 0);
        QVERIFY(altitude);
        QVERIFY(qAbs(sceneRect(altitude).top() - barBottom) < 1.0);
        QVERIFY(qAbs(sceneRect(leftStrip).top() - (barBottom + stripHeight + 8)) < 1.0);

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
        QCOMPARE(spoke.first().at(0).toString(), QStringLiteral("배터리가 부족합니다. 잔량 15퍼센트. 즉시 복귀하십시오."));
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
        injectAltitude(mockLink, vehicle, 152);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("back over the ceiling")));
        QVERIFY_TRUE_WAIT(spoke.count() == 1, TestTimeout::mediumMs());
        QCOMPARE(spoke.first().at(0).toString(), QStringLiteral("고도 초과. 제한 고도 150미터를 넘었습니다."));
    });
}

void PoliceWarningsUITest::_captureStrips()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingWarnings();

    // The mockup's figures: 20 %, 150 m.
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

        injectBattery(mockLink, vehicle, 20, MAV_BATTERY_CHARGE_STATE_LOW);
        QVERIFY(verifyVisibility(kBattery, true, QStringLiteral("20 %")));
        _grab(QStringLiteral("strip_1_one"));
        if (QTest::currentTestFailed()) return;

        injectAltitude(mockLink, vehicle, 152);
        QVERIFY(verifyVisibility(kAltitude, true, QStringLiteral("152 m")));
        _grab(QStringLiteral("strip_2_two_stacked"));
        if (QTest::currentTestFailed()) return;

        mockLink->setCommLost(true);
        QVERIFY_TRUE_WAIT(linkManager->communicationLost(), TestTimeout::longMs());
        _grab(QStringLiteral("strip_3_with_link_lost"));
        if (QTest::currentTestFailed()) return;
        mockLink->setCommLost(false);
        QVERIFY_TRUE_WAIT(!linkManager->communicationLost(), TestTimeout::longMs());

        QVERIFY2(clickItemFraction(kBattery, 0.5, 0.5), "Could not tap the battery strip");
        QVERIFY(verifyVisibility(kBattery, false, QStringLiteral("battery tapped")));
        _grab(QStringLiteral("strip_4_after_dismiss"));
    });
}
