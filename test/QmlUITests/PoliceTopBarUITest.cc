#include "PoliceTopBarUITest.h"

#include <QtCore/QDir>
#include <QtCore/QMetaObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QSettings>
#include <QtCore/QVariant>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "MockLink.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"

UT_REGISTER_TEST(PoliceTopBarUITest, TestLabel::Integration)

namespace {

const QString kTopBar      = QStringLiteral("policeTopBar");
const QString kBanner      = QStringLiteral("policeStatusBanner");
const QString kMessageItem = QStringLiteral("policeMessageItem");
const QString kGpsItem     = QStringLiteral("policeGpsItem");
const QString kRfItem      = QStringLiteral("policeRfItem");
const QString kBatteryItem = QStringLiteral("policeBatteryItem");
const QString kDateTime    = QStringLiteral("policeDateTime");

const QString kChevron     = QStringLiteral("policeStatusChevron");

const QString kStatusPage  = QStringLiteral("policeStatusPage");
const QString kBatteryPage = QStringLiteral("policeBatteryPage");

/// MainWindow's own drawer loader, which is what the popup puts a page into.
const QString kDrawerLoader = QStringLiteral("indicatorDrawerLoader");

/// The guided tool strip entry, reused here to put the mock aircraft in the air.
const QString kTakeoffButton = QStringLiteral("policeToolTakeoff");
const QString kConfirmButton = QStringLiteral("guidedActionConfirmButton");

/// QGCDelayButton.defaultDelay is 500 ms; the press must outlast it with room for the progress
/// animation to reach 1.0 on the software backend.
constexpr int kHoldMs = 1500;

/// Bindings and the drawer's own slide settle well inside this.
constexpr int kSettleMs = 1500;

/// How long the mock aircraft is left in the air before the pad disarms it. Long enough that the
/// duration rounds to whole seconds rather than to zero, which is what a flight never timed at
/// all would also read.
constexpr int kFlightMs = 2500;

/// The delivery tablet's proportions in logical pixels: 768x480 under QT_SCALE_FACTOR=2.5 puts
/// the layout's geometry at the tablet's 1920x1200. Same figures the camera band test uses.
constexpr int kLayoutWidth  = 768;
constexpr int kLayoutHeight = 480;

/// The drawer is a Popup with its own padding and the bar's bottom edge carries a margin, so the
/// top comparison is not to the pixel.
constexpr qreal kDrawerSlack = 12.0;

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

/// Every string an item's subtree draws. Labels are QGCLabel/Text instances with no objectName of
/// their own, so their contents are the only way to name them.
void collectTexts(QQuickItem *item, QStringList &out)
{
    if (!item) {
        return;
    }
    const QVariant text = item->property("text");
    if (text.isValid() && !text.toString().isEmpty()) {
        out.append(text.toString());
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *const child : children) {
        collectTexts(child, out);
    }
}

bool subtreeHasText(QQuickItem *item, const QString &needle)
{
    QStringList texts;
    collectTexts(item, texts);
    for (const QString &text : texts) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

/// A clock face somewhere under \a item: hh:mm:ss, which is what the drawer prints for a flight
/// it has a duration for and an em dash for one it does not.
bool subtreeHasDuration(QQuickItem *item)
{
    const QRegularExpression pattern(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d$"));
    QStringList texts;
    collectTexts(item, texts);
    for (const QString &text : texts) {
        if (pattern.match(text).hasMatch()) {
            return true;
        }
    }
    return false;
}

}  // namespace

void PoliceTopBarUITest::_ignorePreexistingQmlWarnings()
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
    // slot's engine goes away, and the message lands in whichever slot is running by then.
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/HomePositionMapVisual\\.qml: ")));
}

void PoliceTopBarUITest::_grab(const QString &name)
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

void PoliceTopBarUITest::_grabIfCapturing(const QString &name)
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        return;
    }
    _grab(name);
}

void PoliceTopBarUITest::_guidedTakeoff(Vehicle *vehicle)
{
    // PX4 sends the takeoff altitude as AMSL, so it refuses outright until the vehicle
    // altitude is known.
    QVERIFY_TRUE_WAIT(!qIsNaN(vehicle->altitudeAMSL()->rawValue().toDouble()), TestTimeout::longMs());

    // The flying transition creates QGCPressure, which warns on hosts without a backend.
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

    QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
    QQuickItem *const confirm = findVisibleItem(_rootItem, kConfirmButton, 5000);
    QVERIFY2(confirm, "Confirm control never appeared after pressing takeoff");

    const QPointF scenePos = confirm->mapToScene(QPointF(confirm->width() / 2, confirm->height() / 2));
    const QPoint holdPoint = scenePos.toPoint();
    QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);
    QTest::qWait(kHoldMs);
    QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);

    QVERIFY_TRUE_WAIT(vehicle->armed(), TestTimeout::longMs());
}

QQuickItem *PoliceTopBarUITest::_openDrawerFrom(const QString &objectName)
{
    if (!clickButton(objectName)) {
        QTest::qFail(qPrintable(QStringLiteral("Could not tap %1").arg(objectName)), __FILE__, __LINE__);
        return nullptr;
    }

    QQuickItem *const loader = findVisibleItem(_rootItem, kDrawerLoader, 5000);
    if (!loader) {
        QTest::qFail(qPrintable(QStringLiteral("No drawer opened after tapping %1").arg(objectName)),
                     __FILE__, __LINE__);
        return nullptr;
    }
    QTest::qWait(kSettleMs);
    return loader;
}

bool PoliceTopBarUITest::_closeDrawer()
{
    // The same gesture ToolbarIndicatorUITest closes these with: the popup's closePolicy carries
    // CloseOnEscape.
    QTest::keyClick(_window, Qt::Key_Escape);
    return waitForCondition([&] { return findVisibleItem(_rootItem, kDrawerLoader, 0) == nullptr; },
                            3000, QStringLiteral("indicator drawer closed"));
}

void PoliceTopBarUITest::_testBarItemsExist()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        for (const QString &name : { kTopBar, kBanner, kMessageItem, kRfItem, kBatteryItem,
                                     kDateTime, kGpsItem }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 5000);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on the bar").arg(name)));
            QVERIFY2((item->width() > 0) && (item->height() > 0),
                     qPrintable(QStringLiteral("%1 is on the bar with no area").arg(name)));
        }
    });
}

void PoliceTopBarUITest::_testDrawersOpenUnderTheirItem()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, kTopBar, 5000);
        QVERIFY2(topBar, "The top bar is not on screen");
        const QRectF barRect = sceneRect(topBar);

        for (const QString &name : { kMessageItem, kBanner, kGpsItem, kRfItem, kBatteryItem }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 5000);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on the bar").arg(name)));
            const QRectF itemRect = sceneRect(item);

            QQuickItem *const loader = _openDrawerFrom(name);
            if (!loader) {
                return;
            }
            const QRectF drawerRect = sceneRect(loader);

            // Hangs from the bar rather than floating over the map or over the bar itself.
            QVERIFY2(drawerRect.top() >= barRect.bottom() - kDrawerSlack,
                     qPrintable(QStringLiteral("%1: drawer top %2 is above the bar bottom %3")
                                    .arg(name).arg(drawerRect.top()).arg(barRect.bottom())));

            // Under the item that was tapped. showIndicatorDrawer centres the drawer on the item
            // it is handed and then clamps it inside the window, so what the item's identity buys
            // is a drawer whose span covers it - which is how an operator reads "this panel
            // belongs to that pictogram". A drawer opened off the bar or off a row would sit on
            // the bar's centre instead and fail this for every item but the middle one.
            const qreal itemCentre = itemRect.center().x();
            QVERIFY2((drawerRect.left() <= itemCentre) && (itemCentre <= drawerRect.right()),
                     qPrintable(QStringLiteral("%1: drawer spans %2..%3, which does not cover the item at %4..%5")
                                    .arg(name).arg(drawerRect.left()).arg(drawerRect.right())
                                    .arg(itemRect.left()).arg(itemRect.right())));

            QVERIFY2(_closeDrawer(), qPrintable(QStringLiteral("%1: drawer would not close").arg(name)));
        }
    });
}

void PoliceTopBarUITest::_testDateTimeFormat()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const clock = findVisibleItem(_rootItem, kDateTime, 5000);
        QVERIFY2(clock, "The bar clock is not on screen");

        const QString text = clock->property("text").toString();
        const QRegularExpression pattern(QStringLiteral("^\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d$"));
        QVERIFY2(pattern.match(text).hasMatch(),
                 qPrintable(QStringLiteral("Bar clock reads \"%1\", not MM-dd HH:mm:ss").arg(text)));
    });
}

void PoliceTopBarUITest::_testStatusBannerOpensLog()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const loader = _openDrawerFrom(kBanner);
        if (!loader) {
            return;
        }

        QQuickItem *const page = findVisibleItem(_rootItem, kStatusPage, 3000);
        QVERIFY2(page, "The status banner opened something other than the status page");
        QVERIFY2(subtreeHasText(page, QStringLiteral("이륙 횟수")),
                 "The status page does not carry the takeoff count");

        QVERIFY2(_closeDrawer(), "The status drawer would not close");
    });
}

void PoliceTopBarUITest::_testBatteryItemOpensBatteryPage()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const loader = _openDrawerFrom(kBatteryItem);
        if (!loader) {
            return;
        }

        QQuickItem *const page = findVisibleItem(_rootItem, kBatteryPage, 3000);
        QVERIFY2(page, "The pack opened something other than the battery page");
        QVERIFY2(subtreeHasText(page, QStringLiteral("잔량")) ||
                     subtreeHasText(page, QStringLiteral("전압")),
                 "The battery page carries neither the charge left nor the voltage");

        QVERIFY2(_closeDrawer(), "The battery drawer would not close");
    });
}

void PoliceTopBarUITest::_testRfItemOpensConnectedLinks()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const loader = _openDrawerFrom(kRfItem);
        if (!loader) {
            return;
        }

        QVERIFY2(subtreeHasText(loader, QStringLiteral("연결된 링크")),
                 "The RF group did not open the connected link list");

        QVERIFY2(_closeDrawer(), "The link drawer would not close");
    });
}

void PoliceTopBarUITest::_testBannerShowsFlightTimeWhenFlying()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const banner = findVisibleItem(_rootItem, kBanner, 5000);
        QVERIFY2(banner, "The status banner is not on the bar");

        _guidedTakeoff(vehicle);
        if (QTest::currentTestFailed()) {
            return;
        }

        // The banner's own text lives on a label inside it; the banner is the tap target.
        QVERIFY_TRUE_WAIT(([this] {
            QQuickItem *const item = findVisibleItem(_rootItem, kBanner, 0);
            return item && subtreeHasText(item, QStringLiteral("비행 중"));
        })(), TestTimeout::longMs());
    });
}

void PoliceTopBarUITest::_testLastFlightTimeIsStoredPerAirframe()
{
    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }

    // The counter is started by QGCApplication::_initForNormalAppBoot, which the test harness
    // does not run - startUI brings up only what the UI itself needs. Without this the counter
    // follows no aircraft at all and every drawer in this file would read an em dash whatever
    // was flown. Once, in this slot only: init() connects to MultiVehicleManager, and a second
    // call would have every takeoff counted twice.
    TakeoffCounter::instance()->init();

    Vehicle *vehicle = nullptr;
    QPointer<MockLink> mockLink =
        connectMockLinkAndWaitReady([] { return MockLink::startPX4MockLink(); }, vehicle);
    if (!mockLink) {
        return;
    }

    // Two connections in one slot, so the teardown is the caller's rather than
    // runWithMockLink's: the link is dropped and remade in the middle of the test.
    const auto teardown = qScopeGuard([&] {
        disconnectMockLink(mockLink);
        closeUIWindow();
        destroyUIEngine();
    });

    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    _guidedTakeoff(vehicle);
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());

    QTest::qWait(kFlightMs);

    // The landing edge, as the pad gives it: the mock stays at takeoff altitude, so what closes
    // the flight is the disarm - which is the case TakeoffCounter has to cover for an airframe
    // that never sends a landed state of its own.
    mockLink->setArmed(false);
    QVERIFY_TRUE_WAIT(!vehicle->armed(), TestTimeout::longMs());

    QQuickItem *loader = _openDrawerFrom(kBanner);
    if (!loader) {
        return;
    }
    QQuickItem *page = findVisibleItem(_rootItem, kStatusPage, 3000);
    QVERIFY2(page, "The banner opened something other than the status page");
    QVERIFY2(subtreeHasDuration(page),
             "The drawer still shows a dash where the flight that was just flown should be");
    _grabIfCapturing(QStringLiteral("t_6_last_flight"));
    QVERIFY2(_closeDrawer(), "The status drawer would not close");

    // What a restart would read: the same key the counter writes under, in the settings file
    // this run owns. The reload itself cannot be staged in one process - the mock takes a new
    // system id on every connect, so an aircraft that attaches again is a different airframe to
    // this counter, which is the next thing checked.
    const QString airframeKey = (vehicle->vehicleUID() != 0)
                                    ? QStringLiteral("uid-%1").arg(QString::number(vehicle->vehicleUID(), 16))
                                    : QStringLiteral("sysid-%1").arg(vehicle->id());
    int storedSeconds = -1;
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("PoliceDrone/TakeoffCount"));
        storedSeconds = settings.value(airframeKey + QStringLiteral("-lastFlightSeconds"), -1).toInt();
    }
    QVERIFY2(storedSeconds >= 1, qPrintable(QStringLiteral(
                 "%1-lastFlightSeconds reads %2, so no restart would find the flight")
                     .arg(airframeKey).arg(storedSeconds)));

    // A different airframe gets its own log, not this one's.
    disconnectMockLink(mockLink);
    Vehicle *rejoined = nullptr;
    mockLink = connectMockLinkAndWaitReady([] { return MockLink::startPX4MockLink(); }, rejoined);
    if (!mockLink) {
        return;
    }
    QTest::qWait(kSettleMs);

    loader = _openDrawerFrom(kBanner);
    if (!loader) {
        return;
    }
    page = findVisibleItem(_rootItem, kStatusPage, 3000);
    QVERIFY2(page, "The banner opened something other than the status page after reconnecting");
    QVERIFY2(!subtreeHasDuration(page),
             "A second airframe was handed the first one's flight time");
    QVERIFY2(_closeDrawer(), "The status drawer would not close");
}

void PoliceTopBarUITest::_testArmBlockedBanner()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, kTopBar, 5000);
        QVERIFY2(topBar, "The top bar is not on screen");
        QVERIFY2(!findVisibleItem(_rootItem, kChevron, 0),
                 "The chevron is on the banner while the aircraft will arm");

        // No mock refuses to arm. MockLink sends SYS_STATUS with no enabled sensor bits, which
        // makes Vehicle::allSensorsHealthy true, and it sends no health-and-arming report at
        // all, so every rung of the dashboard's _status lands on 시동 가능. The state object is
        // therefore handed to the bar directly. That covers what the bar and the drawer draw for
        // a refused arm - red banner, chevron, and the drawer's line sending the operator to the
        // aircraft messages - and not the dashboard logic that decides it, nor the PX4 reasons
        // list, which needs a libevents report the mock never sends.
        QVariantMap blocked;
        blocked[QStringLiteral("text")]    = QStringLiteral("시동 불가");
        blocked[QStringLiteral("accent")]  = QStringLiteral("#ff5b5b");
        blocked[QStringLiteral("blocked")] = true;
        topBar->setProperty("status", blocked);
        QTest::qWait(kSettleMs);

        QQuickItem *const banner = findVisibleItem(_rootItem, kBanner, 5000);
        QVERIFY2(banner, "The status banner is not on the bar");
        QVERIFY2(subtreeHasText(banner, QStringLiteral("시동 불가")),
                 "The banner does not carry the refusal");
        QVERIFY2(findVisibleItem(_rootItem, kChevron, 3000),
                 "The banner grew no chevron for a refused arm");
        _grabIfCapturing(QStringLiteral("t_7_arm_blocked"));

        QQuickItem *const loader = _openDrawerFrom(kBanner);
        if (!loader) {
            return;
        }
        QQuickItem *const page = findVisibleItem(_rootItem, kStatusPage, 3000);
        QVERIFY2(page, "The banner opened something other than the status page");
        QVERIFY2(subtreeHasText(page, QStringLiteral("기체 메시지")),
                 "The drawer does not say where the refusal is written out");
        _grabIfCapturing(QStringLiteral("t_8_arm_blocked_drawer"));
        QVERIFY2(_closeDrawer(), "The status drawer would not close");
    });
}

void PoliceTopBarUITest::_captureNoVehicleBar()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) return;

    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    _grab(QStringLiteral("t_4_novehicle"));
    stopUI();
}

void PoliceTopBarUITest::_captureBar()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        _grab(QStringLiteral("t_0_idle"));
        if (QTest::currentTestFailed()) return;

        for (const auto &shot : { std::pair<QString, QString>{ kBanner,      QStringLiteral("t_1_status_drawer") },
                                  std::pair<QString, QString>{ kBatteryItem, QStringLiteral("t_2_battery_drawer") },
                                  std::pair<QString, QString>{ kRfItem,      QStringLiteral("t_3_rf_drawer") } }) {
            if (!_openDrawerFrom(shot.first)) {
                return;
            }
            _grab(shot.second);
            if (QTest::currentTestFailed()) return;
            QVERIFY2(_closeDrawer(), qPrintable(QStringLiteral("%1: drawer would not close").arg(shot.first)));
        }

        _guidedTakeoff(vehicle);
        if (QTest::currentTestFailed()) {
            return;
        }
        _grab(QStringLiteral("t_5_flying"));
    });
}

void PoliceTopBarUITest::_captureOtherViewBars()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) return;

    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    // The view-switch helper the stock tests use clicks the dropdown hanging off the stock bar's
    // brand mark, and the police fly bar carries no such button. MainWindow is an
    // ApplicationWindow, so the functions that dropdown would have called sit on the window
    // itself - the route ScreenshotTest already takes for the same reason.
    QVERIFY2(QMetaObject::invokeMethod(_window, "showPlanView"), "showPlanView is not invokable");
    _grab(QStringLiteral("t_9_plan"));
    if (QTest::currentTestFailed()) return;

    QVERIFY2(QMetaObject::invokeMethod(_window, "showSettingsTool", Q_ARG(QVariant, QVariant(QString()))),
             "showSettingsTool is not invokable");
    QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("mainView_toolDrawer"), 5000),
             "The tool drawer never opened");
    _grab(QStringLiteral("t_10_settings"));

    stopUI();
}
