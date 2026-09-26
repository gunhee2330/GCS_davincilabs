#include "PoliceTopBarUITest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLocale>
#include <QtCore/QMetaObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QSettings>
#include <QtCore/QVariant>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "AutoPilotPlugin.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "MissionController.h"
#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "PoliceCorePlugin.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "SimpleMissionItem.h"
#include "TakeoffCounter.h"
#include "Vehicle.h"
#include "VehicleComponent.h"

UT_REGISTER_TEST(PoliceTopBarUITest, TestLabel::Integration)

namespace {

const QString kTopBar      = QStringLiteral("policeTopBar");
const QString kBanner      = QStringLiteral("policeStatusBanner");
const QString kMessageItem = QStringLiteral("policeMessageItem");
const QString kGpsItem     = QStringLiteral("policeGpsItem");
const QString kRfItem      = QStringLiteral("policeRfItem");
const QString kBatteryItem = QStringLiteral("policeBatteryItem");
const QString kDateTime    = QStringLiteral("policeDateTime");
const QString kModeItem    = QStringLiteral("policeFlightModeItem");

/// QGC's ParameterDownloadProgress as the dashboard hosts it over the bar.
const QString kParamProgress = QStringLiteral("policeParamProgress");

const QString kChevron     = QStringLiteral("policeStatusChevron");

const QString kStatusPage  = QStringLiteral("policeStatusPage");
const QString kBatteryPage = QStringLiteral("policeBatteryPage");

/// MainWindow's own drawer loader, which is what the popup puts a page into.
const QString kDrawerLoader = QStringLiteral("indicatorDrawerLoader");

/// The guided tool strip entry, reused here to put the mock aircraft in the air.
const QString kTakeoffButton = QStringLiteral("policeToolTakeoff");
const QString kConfirmButton = QStringLiteral("guidedActionConfirmButton");
const QString kConfirmHost   = QStringLiteral("policeGuidedConfirmHost");

/// The status drawer's arming hold buttons.
const QString kArmButton      = QStringLiteral("policeArmButton");
const QString kForceArmButton = QStringLiteral("policeForceArmButton");

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

/// The first visible item under \a root whose "text" reads exactly \a text. The mode buttons in
/// the stock selector carry no objectName, and the search hits the button before the label inside
/// it because the item itself is checked before its children.
QQuickItem *findVisibleItemWithExactText(QQuickItem *root, const QString &text)
{
    if (!root) {
        return nullptr;
    }
    if (root->isVisible() && (root->property("text").toString() == text)) {
        return root;
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findVisibleItemWithExactText(child, text)) {
            return found;
        }
    }
    return nullptr;
}

/// The visible text field editing vehicle parameter \a name under \a root. The generated setup
/// pages give their fields no objectName, so the fact a field edits is what names it.
QQuickItem *findFactTextField(QQuickItem *root, const QString &name)
{
    if (!root || !root->isVisible()) {
        return nullptr;
    }
    if (root->inherits("QQuickTextField")) {
        const Fact *const fact = qobject_cast<Fact *>(root->property("fact").value<QObject *>());
        if (fact && (fact->name() == name)) {
            return root;
        }
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findFactTextField(child, name)) {
            return found;
        }
    }
    return nullptr;
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

/// The counter is started by QGCApplication::_initForNormalAppBoot, which the test harness
/// does not run - startUI brings up only what the UI itself needs. Without this the counter
/// follows no aircraft at all and every drawer in this file would read an em dash whatever
/// was flown. Once per process: init() connects to MultiVehicleManager, which outlives every
/// slot, and a second call would have every takeoff counted twice.
void startTakeoffCounter()
{
    static bool started = false;
    if (!started) {
        TakeoffCounter::instance()->init();
        started = true;
    }
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

    // The Korean translation of PIDTuning.qml's "Switches to '%1' when you click Stop." writes the
    // marker as "% 1", so the PID tuning page's arg() finds none. Present at HEAD as well. Only that
    // string, as the translator in use renders it, so any other arg() slip still fails the run
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^QString::arg: Argument missing: \"") +
                                        QRegularExpression::escape(QCoreApplication::translate(
                                            "PIDTuning", "Switches to '%1' when you click Stop.")) +
                                        QStringLiteral("\", ")));

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

void PoliceTopBarUITest::_hold(const QString &objectName)
{
    QQuickItem *const item = findVisibleItem(_rootItem, objectName, 5000);
    QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on screen to hold").arg(objectName)));
    QVERIFY2(item->isEnabled(), qPrintable(QStringLiteral("%1 is disabled").arg(objectName)));

    const QPoint holdPoint = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);
    QTest::qWait(kHoldMs);
    QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, holdPoint);
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
                                     kDateTime, kGpsItem, kModeItem }) {
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

        for (const QString &name : { kMessageItem, kBanner, kModeItem, kGpsItem, kRfItem, kBatteryItem }) {
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

void PoliceTopBarUITest::_testFlightModeItemOpensStockSelector()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, kTopBar, 5000);
        QVERIFY2(topBar, "The top bar is not on screen");
        QQuickItem *const item = findVisibleItem(_rootItem, kModeItem, 5000);
        QVERIFY2(item, "The flight mode item is not on the bar");
        _grabIfCapturing(QStringLiteral("t_10_bar"));
        if (QTest::currentTestFailed()) {
            return;
        }

        // The stock indicator sizes its own hit area, so what has to be checked is that the area
        // covers this item rather than only its middle: the far corner opens the same drawer.
        QVERIFY2(clickItemFraction(kModeItem, 0.98, 0.95), "Could not tap the corner of the flight mode item");
        QVERIFY2(findVisibleItem(_rootItem, kDrawerLoader, 5000),
                 "A tap in the corner of the flight mode item opened nothing");
        QVERIFY2(_closeDrawer(), "The corner drawer would not close");

        QQuickItem *const loader = _openDrawerFrom(kModeItem);
        if (!loader) {
            return;
        }

        // The stock page, which is the mode list: every mode on it is one the aircraft offers.
        const QStringList modes = vehicle->property("flightModes").toStringList();
        QVERIFY2(!modes.isEmpty(), "The mock aircraft offers no flight modes");
        const QString current = vehicle->property("flightMode").toString();

        // A mode the aircraft is not already in, drawn inside the window so it can be pressed.
        // The list is not scrollable, so one that fell off the bottom would never be reachable.
        QQuickItem *button = nullptr;
        QString wanted;
        for (const QString &mode : modes) {
            if (mode == current) {
                continue;
            }
            QQuickItem *const candidate = findVisibleItemWithExactText(loader, mode);
            if (!candidate) {
                continue;
            }
            const QRectF rect = sceneRect(candidate);
            if ((rect.bottom() <= _window->height()) && (rect.right() <= _window->width())) {
                button = candidate;
                wanted = mode;
                break;
            }
        }
        QVERIFY2(button, qPrintable(QStringLiteral(
                     "The drawer lists no reachable mode other than %1; it is not the mode selector")
                         .arg(current)));
        _grabIfCapturing(QStringLiteral("t_11_flightmode_drawer"));
        if (QTest::currentTestFailed()) {
            return;
        }

        // The mode buttons are QGCDelayButtons and requireModeChangeConfirmation defaults on, so
        // the press has to outlast the delay the same way the takeoff confirm does.
        const QPoint press = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
        QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, press);
        QTest::qWait(kHoldMs);
        QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, press);

        QVERIFY_TRUE_WAIT(vehicle->flightMode() == wanted, TestTimeout::longMs());

        // The stock page closes its own drawer once a mode is taken.
        QVERIFY2(waitForCondition([&] { return findVisibleItem(_rootItem, kDrawerLoader, 0) == nullptr; },
                                  3000, QStringLiteral("mode drawer closed")),
                 "The mode drawer stayed open after a mode was picked");

        // The overlay takes this item's tap and no more than this item's tap. The stock
        // indicator sizes its own hit area off its own label, which comes out wider than ours,
        // so what is checked here is the ground it would otherwise have taken: the gap between
        // the two items, which belongs to neither and must open nothing at all.
        QQuickItem *const gpsItem = findVisibleItem(_rootItem, kGpsItem, 5000);
        QVERIFY2(gpsItem, "The satellites are not on the bar");
        const QRectF modeRect = sceneRect(item);
        const QRectF gpsRect  = sceneRect(gpsItem);
        QVERIFY2(gpsRect.left() > modeRect.right(), "The two items overlap, so there is no gap to tap");
        const QPoint gap(qRound((modeRect.right() + gpsRect.left()) / 2), qRound(modeRect.center().y()));
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, gap);
        QVERIFY2(!findVisibleItem(_rootItem, kDrawerLoader, 1000),
                 "A tap in the gap beside the flight mode item opened a drawer");

        // And the item beside us still answers its own left edge, the strip the stock hit area
        // reached over before it was clipped.
        QVERIFY2(clickItemFraction(kGpsItem, 0.03, 0.5), "Could not tap the left edge of the satellites");
        QQuickItem *const gpsLoader = findVisibleItem(_rootItem, kDrawerLoader, 5000);
        QVERIFY2(gpsLoader, "The left edge of the satellites opened no drawer at all");
        QTest::qWait(kSettleMs);
        QVERIFY2(!findVisibleItemWithExactText(gpsLoader, wanted),
                 "The left edge of the satellites opened the flight mode list");
        QVERIFY2(subtreeHasText(gpsLoader, QStringLiteral("HDOP")),
                 "The left edge of the satellites opened something other than the GPS page");
        QVERIFY2(_closeDrawer(), "The GPS drawer would not close");
    });
}

void PoliceTopBarUITest::_testFlightModeDoesNothingWithNoVehicle()
{
    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }

    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    QVERIFY2(findVisibleItem(_rootItem, kTopBar, 5000), "The top bar is not on screen");
    QVERIFY2(verifyVisibility(kModeItem, false, QStringLiteral("no aircraft")),
             "The flight mode item is on the bar with no aircraft attached");
    QVERIFY2(!clickButton(kModeItem), "The flight mode item took a tap with no aircraft attached");
    QVERIFY2(!findVisibleItem(_rootItem, kDrawerLoader, 1000), "A drawer opened with no aircraft attached");

    stopUI();
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

    startTakeoffCounter();

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

void PoliceTopBarUITest::_testLandedFlickerIsOneFlight()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        startTakeoffCounter();
        TakeoffCounter *const counter = TakeoffCounter::instance();

        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);
        const int countBefore = counter->takeoffCount();

        // The counter cannot lift off before the takeoff is asked for, nor later than the vehicle
        // reads flying: the two bound the duration it may report.
        QElapsedTimer sinceTakeoffAsked;
        sinceTakeoffAsked.start();
        _guidedTakeoff(vehicle);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
        QElapsedTimer sinceFlying;
        sinceFlying.start();
        QTest::qWait(kFlightMs);

        // A landed state that drops out in the air. The mock still reports itself in the air, so
        // its next EXTENDED_SYS_STATE puts the vehicle straight back up: flying, landed, flying.
        vehicle->_setFlying(false);
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
        QTest::qWait(kFlightMs);

        // The second landing of the cycle is timed from the first liftoff, not from the flicker.
        const qint64 flownSeconds = sinceFlying.elapsed() / 1000;
        vehicle->_setFlying(false);
        QVERIFY2(counter->lastFlightSeconds() >= flownSeconds,
                 qPrintable(QStringLiteral("The landing reads a %1 s flight after %2 s in the air")
                                .arg(counter->lastFlightSeconds()).arg(flownSeconds)));
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());

        // The mock never lands, so the cycle closes on a disarm in the air.
        mockLink->setArmed(false);
        QVERIFY_TRUE_WAIT(!vehicle->armed(), TestTimeout::longMs());
        const qint64 ceilingSeconds = sinceTakeoffAsked.elapsed() / 1000;

        QCOMPARE(counter->takeoffCount(), countBefore + 1);
        QVERIFY2((counter->lastFlightSeconds() >= flownSeconds) && (counter->lastFlightSeconds() <= ceilingSeconds),
                 qPrintable(QStringLiteral("The flight reads %1 s, outside the %2 to %3 s it was flown")
                                .arg(counter->lastFlightSeconds()).arg(flownSeconds).arg(ceilingSeconds)));
    });
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

void PoliceTopBarUITest::_testDrawerArmsAndDisarms()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);
        QVERIFY2(!vehicle->armed(), "The mock aircraft came up armed");

        if (!_openDrawerFrom(kBanner)) {
            return;
        }
        QQuickItem *const arm = findVisibleItem(_rootItem, kArmButton, 3000);
        QVERIFY2(arm, "The status drawer carries no arming button");
        QCOMPARE(arm->property("text").toString(), QStringLiteral("시동"));
        QVERIFY2(!findVisibleItem(_rootItem, kForceArmButton, 0),
                 "강제 시동 is offered while the aircraft will arm");
        _grabIfCapturing(QStringLiteral("arm_0_disarmed"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kArmButton);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(vehicle->armed(), TestTimeout::longMs());
        // As stock, the drawer closes once the hold lands.
        QVERIFY2(waitForCondition([&] { return findVisibleItem(_rootItem, kDrawerLoader, 0) == nullptr; },
                                  3000, QStringLiteral("drawer closed after arming")),
                 "The drawer stayed open after arming");

        if (!_openDrawerFrom(kBanner)) {
            return;
        }
        QQuickItem *const disarm = findVisibleItem(_rootItem, kArmButton, 3000);
        QVERIFY2(disarm, "The status drawer carries no arming button once armed");
        QCOMPARE(disarm->property("text").toString(), QStringLiteral("시동 끄기"));
        _grabIfCapturing(QStringLiteral("arm_1_armed"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kArmButton);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(!vehicle->armed(), TestTimeout::longMs());
    });
}

void PoliceTopBarUITest::_testDrawerEmergencyStopConfirms()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        _guidedTakeoff(vehicle);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());

        if (!_openDrawerFrom(kBanner)) {
            return;
        }
        QQuickItem *const stop = findVisibleItem(_rootItem, kArmButton, 3000);
        QVERIFY2(stop, "The status drawer carries no arming button in the air");
        QCOMPARE(stop->property("text").toString(), QStringLiteral("비상 정지"));
        QVERIFY2(!findVisibleItem(_rootItem, kForceArmButton, 0), "강제 시동 is offered in the air");
        _grabIfCapturing(QStringLiteral("arm_2_flying"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kArmButton);
        if (QTest::currentTestFailed()) {
            return;
        }

        // The police instance of the confirm control, asking for the emergency stop. Nothing has
        // reached the aircraft yet.
        QQuickItem *const host = findVisibleItem(_rootItem, kConfirmHost, 5000);
        QVERIFY2(host, "The confirm host is not on screen");
        QQuickItem *const confirm = findVisibleItem(host, kConfirmButton, 5000);
        QVERIFY2(confirm, "비상 정지 raised no confirmation");
        QCOMPARE(confirm->property("text").toString(),
                 QCoreApplication::translate("GuidedActionsController", "EMERGENCY STOP"));
        QVERIFY2(vehicle->armed(), "The motors stopped before the confirmation was held");
        _grabIfCapturing(QStringLiteral("arm_3_emergency_stop_confirm"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kConfirmButton);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(!vehicle->armed(), TestTimeout::longMs());
    });
}

void PoliceTopBarUITest::_testDrawerForceArmWhenBlocked()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const topBar = findVisibleItem(_rootItem, kTopBar, 5000);
        QVERIFY2(topBar, "The top bar is not on screen");

        // No mock refuses to arm, so the refusal is handed to the bar the way
        // _testArmBlockedBanner hands it.
        QVariantMap blocked;
        blocked[QStringLiteral("text")]    = QStringLiteral("시동 불가");
        blocked[QStringLiteral("accent")]  = QStringLiteral("#ff5b5b");
        blocked[QStringLiteral("blocked")] = true;
        topBar->setProperty("status", blocked);
        QTest::qWait(kSettleMs);

        if (!_openDrawerFrom(kBanner)) {
            return;
        }
        QQuickItem *const force = findVisibleItem(_rootItem, kForceArmButton, 3000);
        QVERIFY2(force, "A refused arm offers no 강제 시동");
        QCOMPARE(force->property("text").toString(), QStringLiteral("강제 시동"));
        QQuickItem *const arm = findVisibleItem(_rootItem, kArmButton, 0);
        QVERIFY2(arm, "시동 left the drawer when arming was refused");
        QCOMPARE(arm->property("text").toString(), QStringLiteral("시동"));
        _grabIfCapturing(QStringLiteral("arm_4_blocked"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kForceArmButton);
        if (QTest::currentTestFailed()) {
            return;
        }

        QQuickItem *const host = findVisibleItem(_rootItem, kConfirmHost, 5000);
        QVERIFY2(host, "The confirm host is not on screen");
        QQuickItem *const confirm = findVisibleItem(host, kConfirmButton, 5000);
        QVERIFY2(confirm, "강제 시동 raised no confirmation");
        QCOMPARE(confirm->property("text").toString(),
                 QCoreApplication::translate("GuidedActionsController", "Force Arm"));
        QVERIFY2(!vehicle->armed(), "The aircraft armed before the confirmation was held");
        _grabIfCapturing(QStringLiteral("arm_5_force_arm_confirm"));
        if (QTest::currentTestFailed()) {
            return;
        }

        _hold(kConfirmButton);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY_TRUE_WAIT(vehicle->armed(), TestTimeout::longMs());
    });
}

void PoliceTopBarUITest::_testParamDownloadProgress()
{
    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }

    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    QQuickItem *const bar = findVisibleItem(_rootItem, kTopBar, 5000);
    QVERIFY2(bar, "The top bar is not on screen");
    QQuickItem *const host = findVisibleItem(_rootItem, kParamProgress, 5000);
    QVERIFY2(host, "The parameter download progress is not in the fly view");
    QCOMPARE(sceneRect(host), sceneRect(bar));

    // The stock component's first child is the strip; the second is the large bar QGC shows only
    // in the light scheme.
    QQuickItem *const strip = host->childItems().value(0);
    QVERIFY2(strip, "The parameter download progress has no strip");
    QCOMPARE(strip->width(), 0.0);

    // COM_FLTMODE6 is left out of the mock's parameter stream, so QGC reaches the end one short
    // and waits before asking for it by name: the download holds still part way through for long
    // enough to be looked at, and then completes as usual.
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    QVERIFY(spyVehicle.isValid());
    QPointer<MockLink> mockLink = MockLink::startPX4MockLink(MockConfiguration::OptionNone,
                                                             MockConfiguration::FailMissingParamOnInitialRequest);
    QVERIFY2(mockLink, "Could not start the mock aircraft");
    const auto teardown = qScopeGuard([&] {
        disconnectMockLink(mockLink);
        closeUIWindow();
        destroyUIEngine();
    });
    QVERIFY(waitForSignal(spyVehicle, 10000, QStringLiteral("activeVehicleChanged")));
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);

    // The software backend leaves the grab's devicePixelRatio at 1, so the scale comes from the
    // grab's size against the window's.
    const auto pixelAt = [this](const QImage &image, const QPointF &scenePoint) {
        return image.pixelColor((scenePoint * image.width() / _window->width()).toPoint());
    };

    // Mid-download, grabbed at once: no event is processed between the check and the grab.
    QVERIFY_TRUE_WAIT(!vehicle->parameterManager()->parametersReady() &&
                      (vehicle->parameterManager()->loadProgress() > 0) && (strip->width() > 0), 10000);
    const QImage downloading = _window->grabWindow();
    QVERIFY(!downloading.isNull());
    const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(downloading.save(QDir(dir).filePath(QStringLiteral("pp_0_downloading.png"))));
    }
    QVERIFY(strip->isVisible());
    QVERIFY2(qAbs(strip->width() - vehicle->loadProgress() * host->width()) < 1.0,
             qPrintable(QStringLiteral("Strip %1 wide for %2 of a %3 wide bar")
                            .arg(strip->width()).arg(vehicle->loadProgress()).arg(host->width())));
    QVERIFY2(strip->width() < host->width(), "The strip is full width before the download is done");
    QCOMPARE(sceneRect(strip).left(), sceneRect(bar).left());
    QCOMPARE(sceneRect(strip).bottom(), sceneRect(bar).bottom());
    const QPointF stripCentre = strip->mapToScene(QPointF(strip->width() / 2, strip->height() / 2));
    const QColor stripColour = strip->property("color").value<QColor>();
    QVERIFY2(pixelAt(downloading, stripCentre) == stripColour,
             qPrintable(QStringLiteral("The strip at (%1, %2) of the grab reads %3, not its own %4")
                            .arg(stripCentre.x()).arg(stripCentre.y())
                            .arg(pixelAt(downloading, stripCentre).name(), stripColour.name())));

    // Once the aircraft is in, the strip is gone.
    QVERIFY_TRUE_WAIT(vehicle->isInitialConnectComplete(), 10000);
    QVERIFY(vehicle->parameterManager()->parametersReady());
    QVERIFY_TRUE_WAIT(strip->width() == 0, 5000);
    QTest::qWait(kSettleMs);
    const QImage done = _window->grabWindow();
    QVERIFY(!done.isNull());
    if (!dir.isEmpty()) {
        QVERIFY(done.save(QDir(dir).filePath(QStringLiteral("pp_1_done.png"))));
    }
    QVERIFY2(pixelAt(done, stripCentre) != stripColour, "The strip is still drawn after the download");
}

void PoliceTopBarUITest::_testPlanSelectedItemStats()
{
    _ignorePreexistingQmlWarnings();

    // The tree's editor for each waypoint sizes its not-ready "?" off its indicator's width, which
    // is 0 when the editor is made (MissionItemEditor.qml), so every editor the tree makes says so.
    // Present at HEAD with this same route; clamping that one binding silences all of them.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^QFont::setPointSizeF: Point size <= 0 \\(0\\.000000\\), must be greater than 0$")));

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QVERIFY2(QMetaObject::invokeMethod(_window, "showPlanView"), "showPlanView is not invokable");
        QQuickItem *const planView = findVisibleItem(_rootItem, QStringLiteral("mainView_plan"), 5000);
        QVERIFY2(planView, "The plan view did not appear");
        MissionController *const missionController =
            qobject_cast<MissionController *>(planView->property("_missionController").value<QObject *>());
        QVERIFY2(missionController, "The plan view has no mission controller");
        // The plan's home is the operator's to place, as a map tap would; here it goes where the
        // aircraft sits.
        QVERIFY_TRUE_WAIT(vehicle->coordinate().isValid(), 5000);
        missionController->setHomePosition(vehicle->coordinate());
        QVERIFY_TRUE_WAIT(missionController->plannedHomePosition().isValid(), 5000);

        // Three waypoints off the aircraft's home. The leg into the second runs 200 m on 60 degrees
        // and climbs 20 m, so every figure in the rows can be worked out here.
        const QGeoCoordinate home = missionController->plannedHomePosition();
        const QGeoCoordinate wp1 = home.atDistanceAndAzimuth(100, 0);
        const QGeoCoordinate wp2 = wp1.atDistanceAndAzimuth(200, 60);
        const QGeoCoordinate wp3 = wp2.atDistanceAndAzimuth(150, 150);
        QList<VisualMissionItem *> items;
        for (const QGeoCoordinate &coord : { wp1, wp2, wp3 }) {
            items.append(missionController->insertSimpleMissionItem(coord, missionController->visualItems()->count()));
            QVERIFY(items.last());
        }
        SimpleMissionItem *const second = qobject_cast<SimpleMissionItem *>(items.at(1));
        QVERIFY(second);
        constexpr double climb = 20.0;
        second->altitude()->setRawValue(second->altitude()->rawValue().toDouble() + climb);
        const double legMeters = wp1.distanceTo(wp2);
        QVERIFY_TRUE_WAIT((qAbs(second->distance() - legMeters) < 0.5) && (qAbs(second->altDifference() - climb) < 0.01),
                          5000);

        const QString sectionName = QStringLiteral("missionStatsSelectedItem");

        // The mission start has no leg into it, so the rows are off the panel
        missionController->setCurrentPlanViewSeqNum(0, true);
        QVERIFY(verifyVisibility(sectionName, false, QStringLiteral("mission start picked")));
        _grabIfCapturing(QStringLiteral("plan_stats_0_start"));
        if (QTest::currentTestFailed()) return;

        missionController->setCurrentPlanViewSeqNum(second->sequenceNumber(), true);
        QVERIFY(verifyVisibility(sectionName, true, QStringLiteral("second waypoint picked")));
        QQuickItem *const section = findVisibleItem(_rootItem, sectionName);
        QVERIFY(section);
        QVERIFY2(subtreeHasText(section, QCoreApplication::translate("MissionStats", "Selected Item")),
                 "The selected item rows carry no title");

        // The figures are the ones the delivery reads in Korean; elsewhere only the source strings
        // are checked, through whatever catalogue is loaded.
        const bool korean = (QLocale().language() == QLocale::Korean);
        const double azimuth = qRound(wp1.azimuthTo(wp2)) % 360;
        struct Row {
            QString     objectName;
            const char *label;
            const char *comment;
            const char *koreanLabel;
            double      expected;
            double      tolerance;  ///< the figure is printed rounded
            QString     unit;
        };
        const QList<Row> rows = {
            { QStringLiteral("missionStatsAzimuth"),  "Azimuth",      nullptr, "방위",        azimuth, 0,
              QString() },
            { QStringLiteral("missionStatsDistPrev"), "Dist prev WP", nullptr, "이전 점 거리",
              FactMetaData::metersToAppSettingsHorizontalDistanceUnits(legMeters).toDouble(), 0.51,
              FactMetaData::appSettingsHorizontalDistanceUnitsString() },
            { QStringLiteral("missionStatsGradient"), "Gradient",     nullptr, "경사",
              static_cast<double>(qRound(qRadiansToDegrees(qAtan(climb / legMeters)))), 0,
              QCoreApplication::translate("MissionStats", "deg") },
            { QStringLiteral("missionStatsAltDiff"),  "Alt diff",     nullptr, "고도 차",
              FactMetaData::metersToAppSettingsVerticalDistanceUnits(climb).toDouble(), 0.51,
              FactMetaData::appSettingsVerticalDistanceUnitsString() },
            { QStringLiteral("missionStatsHeading"),  "Heading",      nullptr, "기수 방향",   azimuth, 0,
              QString() },
            { QStringLiteral("missionStatsMaxTelemetry"), "Max Range", "Farthest distance from home along the mission",
              "반경",
              FactMetaData::metersToAppSettingsHorizontalDistanceUnits(
                  qMax(home.distanceTo(wp1), qMax(home.distanceTo(wp2), home.distanceTo(wp3)))).toDouble(), 0.51,
              FactMetaData::appSettingsHorizontalDistanceUnitsString() },
        };
        for (const Row &row : rows) {
            QQuickItem *const stat = findVisibleItem(section->parentItem(), row.objectName, 2000);
            QVERIFY2(stat, qPrintable(QStringLiteral("%1 is not on the panel").arg(row.objectName)));
            const QString label = stat->property("label").toString();
            QCOMPARE(label, QCoreApplication::translate("MissionStats", row.label, row.comment));
            if (korean) {
                QCOMPARE(label, QString::fromUtf8(row.koreanLabel));
            }
            // Once the layout has given the rows their width
            QQuickItem *const labelText = findVisibleItemWithExactText(stat, label);
            QVERIFY2(labelText, qPrintable(QStringLiteral("%1 draws no label").arg(row.objectName)));
            QVERIFY2(waitForCondition([labelText] { return !labelText->property("truncated").toBool(); }, 2000,
                                      QStringLiteral("label drawn in full")),
                     qPrintable(QStringLiteral("%1 is cut short on the panel").arg(label)));
            QCOMPARE(stat->property("unit").toString(), row.unit);
            const QString value = stat->property("value").toString();
            bool ok = false;
            const double number = value.toDouble(&ok);
            QVERIFY2(ok && (qAbs(number - row.expected) <= row.tolerance),
                     qPrintable(QStringLiteral("%1 reads \"%2\", expected %3").arg(label, value).arg(row.expected)));
        }

        _grabIfCapturing(QStringLiteral("plan_stats_1_selected"));
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
                                  std::pair<QString, QString>{ kRfItem,      QStringLiteral("t_3_rf_drawer") },
                                  std::pair<QString, QString>{ kGpsItem,     QStringLiteral("t_12_gps_drawer") } }) {
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

void PoliceTopBarUITest::_captureSettingsDark()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        _grab(QStringLiteral("u_8_fly"));
        if (QTest::currentTestFailed()) return;

        if (!_openDrawerFrom(kModeItem)) {
            return;
        }
        _grab(QStringLiteral("s_4_fly_drawer"));
        if (QTest::currentTestFailed()) return;
        QVERIFY2(_closeDrawer(), "The mode drawer would not close");

        QVERIFY2(QMetaObject::invokeMethod(_window, "showPlanView"), "showPlanView is not invokable");
        _grab(QStringLiteral("s_5_plan"));
        if (QTest::currentTestFailed()) return;

        const auto showSettings = [this](const QString &page) {
            return QMetaObject::invokeMethod(_window, "showSettingsTool", Q_ARG(QVariant, QVariant(page)));
        };
        QVERIFY2(showSettings(QStringLiteral("General")), "showSettingsTool is not invokable");
        _grab(QStringLiteral("u_0_general"));
        if (QTest::currentTestFailed()) return;
        _grab(QStringLiteral("k_0_settings_general"));
        if (QTest::currentTestFailed()) return;

        // The page's own flickable, scrolled so the Units card heads the panel
        QQuickItem *const pageFlick = findVisibleItem(_rootItem, QStringLiteral("settingsPageFlickable"), 5000);
        QVERIFY2(pageFlick, "The General page has no flickable");
        QQuickItem *const units = findVisibleItemWithExactText(pageFlick, QStringLiteral("Units"));
        QVERIFY2(units, "The General page shows no Units heading");
        QQuickItem *const content = pageFlick->property("contentItem").value<QQuickItem *>();
        const qreal maxY = qMax(0.0, pageFlick->property("contentHeight").toReal() - pageFlick->height());
        pageFlick->setProperty("contentY", qBound(0.0, units->mapToItem(content, QPointF(0, 0)).y(), maxY));
        _grab(QStringLiteral("u_1_general_scrolled"));
        if (QTest::currentTestFailed()) return;

        // A tap on a page with sections opens it and drops its sub-rows open
        QVERIFY2(clickButton(QStringLiteral("settingsButton_Fly View")), "Could not tap the Fly View row");
        _grab(QStringLiteral("u_2_flyview_sections"));
        if (QTest::currentTestFailed()) return;
        // Folded again and the pointer taken off the rail, so the grabs below show only the
        // selected row lit rather than a hover tint left on this one
        QVERIFY2(clickButton(QStringLiteral("settingsButton_Fly View")), "Could not tap the Fly View row");
        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));

        for (const auto &shot : { std::pair<QString, QString>{ QStringLiteral("Comm Links"), QStringLiteral("u_3_links") },
                                  std::pair<QString, QString>{ QStringLiteral("Video"),      QStringLiteral("u_4_video") },
                                  std::pair<QString, QString>{ QStringLiteral("Maps"),       QStringLiteral("u_5_maps") },
                                  std::pair<QString, QString>{ QStringLiteral("Developer"),  QStringLiteral("u_6_developer") } }) {
            QVERIFY2(showSettings(shot.first), "showSettingsTool is not invokable");
            // Rows below the fold stay there on their own, so bring the selected one into view
            QVERIFY2(findVisibleItemScrolled(QStringLiteral("settingsButton_") + shot.first, QStringLiteral("settings_buttonList")),
                     qPrintable(QStringLiteral("No rail row for %1").arg(shot.first)));
            _grab(shot.second);
            if (QTest::currentTestFailed()) return;
        }

        // The other pages on the rail, those this build shows
        for (const QString &page : { QStringLiteral("Plan View"), QStringLiteral("Telemetry"), QStringLiteral("NTRIP/RTK"),
                                     QStringLiteral("3D View"), QStringLiteral("Help"), QStringLiteral("App Logging"),
                                     QStringLiteral("App Log Viewer"), QStringLiteral("PX4 Log Transfer"), QStringLiteral("Remote ID") }) {
            QVERIFY2(showSettings(page), "showSettingsTool is not invokable");
            if (!findVisibleItemScrolled(QStringLiteral("settingsButton_") + page, QStringLiteral("settings_buttonList"))) {
                continue;
            }
            _grab(QStringLiteral("w_") + QString(page).remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]"))));
            if (QTest::currentTestFailed()) return;
        }

        // The rail at the very bottom of its list
        QQuickItem *const rail = findVisibleItem(_rootItem, QStringLiteral("settings_buttonList"), 5000);
        QVERIFY2(rail, "The settings rail is not visible");
        rail->setProperty("contentY", qMax(0.0, rail->property("contentHeight").toReal() - rail->height()));
        _grab(QStringLiteral("k_1_settings_rail_bottom"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(QMetaObject::invokeMethod(_window, "showVehicleConfig"), "showVehicleConfig is not invokable");
        _grab(QStringLiteral("u_7_vehicle_setup"));
        if (QTest::currentTestFailed()) return;
        _grab(QStringLiteral("k_2_vehicle_summary"));
        if (QTest::currentTestFailed()) return;

        // A second page, to show the selected row on the vehicle rail. Radio has no section
        // sub-rows, so its own row is the one that lights
        VehicleComponent *const radio =
            vehicle->autopilotPlugin()->findKnownVehicleComponent(AutoPilotPlugin::KnownRadioVehicleComponent);
        QVERIFY2(radio, "The PX4 mock vehicle has no radio component");
        QVERIFY2(clickButtonScrolled(QStringLiteral("vehicleConfig_comp_") + radio->name().remove(QLatin1Char(' ')),
                                     QStringLiteral("vehicleConfig_sidebarFlickable")),
                 "Could not tap the radio row");
        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));
        _grab(QStringLiteral("k_3_vehicle_other_page"));
        if (QTest::currentTestFailed()) return;

        // A generated vehicle page, for its ConfigSection cards
        VehicleComponent *const safety =
            vehicle->autopilotPlugin()->findKnownVehicleComponent(AutoPilotPlugin::KnownSafetyVehicleComponent);
        QVERIFY2(safety, "The PX4 mock vehicle has no safety component");
        QVERIFY2(clickButtonScrolled(QStringLiteral("vehicleConfig_comp_") + safety->name().remove(QLatin1Char(' ')),
                                     QStringLiteral("vehicleConfig_sidebarFlickable")),
                 "Could not tap the safety row");
        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));
        _grab(QStringLiteral("k_4_vehicle_config_section"));
        if (QTest::currentTestFailed()) return;

        // Every page on the vehicle rail, the hand-written ones included
        int page = 0;
        for (const QVariant &entry : vehicle->autopilotPlugin()->vehicleComponents()) {
            VehicleComponent *const comp = entry.value<VehicleComponent *>();
            if (!comp || comp->setupSource().isEmpty()) {
                continue;
            }
            QVERIFY2(clickButtonScrolled(QStringLiteral("vehicleConfig_comp_") + comp->name().remove(QLatin1Char(' ')),
                                         QStringLiteral("vehicleConfig_sidebarFlickable")),
                     qPrintable(QStringLiteral("Could not tap the %1 row").arg(comp->name())));
            QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));
            _grab(QStringLiteral("v_%1").arg(page++, 2, 10, QLatin1Char('0')));
            if (QTest::currentTestFailed()) return;
        }

        // The palette theme is process-wide, so put it back for whatever slot runs next.
        Fact *const scheme = SettingsManager::instance()->appSettings()->indoorPalette();
        scheme->setRawValue(0);
        const auto restore = qScopeGuard([scheme] { scheme->setRawValue(1); });
        QVERIFY2(showSettings(QStringLiteral("General")), "showSettingsTool is not invokable");
        _grab(QStringLiteral("u_9_general_light"));
    });
}

void PoliceTopBarUITest::_testSettingsSwitchTouchTarget()
{
    _ignorePreexistingQmlWarnings();

    startUI();
    if (QTest::currentTestFailed()) return;
    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);

    QVERIFY2(QMetaObject::invokeMethod(_window, "showSettingsTool", Q_ARG(QVariant, QVariant(QStringLiteral("Fly View")))),
             "showSettingsTool is not invokable");
    QQuickItem *const control = findVisibleItemScrolled(QStringLiteral("settingsCheckBox_keepMapCenteredOnVehicle"),
                                                        QStringLiteral("settingsPageFlickable"));
    QVERIFY2(control, "The Fly View page shows no Keep Map Centered switch");
    QQuickItem *const row = control->parentItem() ? control->parentItem()->parentItem() : nullptr;
    QVERIFY2(row, "The switch sits in no settings row");
    const qreal rowHeight = row->height();
    QQuickItem *const content = control->property("contentItem").value<QQuickItem *>();
    QVERIFY2(content, "The switch has no content item");
    QQuickItem *pill = nullptr;
    for (QQuickItem *const child : content->childItems()) {
        if (child->inherits("QQuickRectangle")) {
            pill = child;
        }
    }
    QVERIFY2(pill, "The switch draws no pill");
    const QRectF pillRect = sceneRect(pill);

    // The tablet's figure: Android lays a logical inch out at about 160 px, so ScreenTools' 5 mm
    // comes to 31 px there. This desktop screen's own figure is under the drawn switch
    QQmlExpression setMinTouch(qmlContext(control), control, QStringLiteral(
        "ScreenTools.minTouchPixels = Math.max(ScreenTools.minTouchPixels, Math.round(5 * 160 / 25.4))"));
    const qreal minTouch = setMinTouch.evaluate().toReal();
    QVERIFY2(!setMinTouch.hasError(), qPrintable(setMinTouch.error().toString()));
    QTest::qWait(100);

    QQuickItem *const target = qobject_cast<QQuickItem *>(control->property("containmentMask").value<QObject *>());
    QVERIFY2(target, "The switch has no tap target in the settings look");

    const QRectF targetRect = sceneRect(target);
    QVERIFY2(targetRect.height() >= minTouch && targetRect.width() >= minTouch,
             qPrintable(QStringLiteral("Tap target %1 x %2 is under %3").arg(targetRect.width()).arg(targetRect.height()).arg(minTouch)));
    // The pill keeps its drawn size and the row its height: only the target grew
    QCOMPARE(sceneRect(pill), pillRect);
    QCOMPARE(row->height(), rowHeight);
    const qreal reach = minTouch / 2 - 1;
    QVERIFY2(reach > control->height() / 2, "The taps off the centre would still land inside the switch's own bounds");

    // Centre, below, above, and the centre again to put the setting back
    for (const qreal dy : { 0.0, reach, -reach, 0.0 }) {
        const bool before = control->property("checked").toBool();
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, (pillRect.center() + QPointF(0, dy)).toPoint());
        QVERIFY2(waitForCondition([&] { return control->property("checked").toBool() != before; }, 2000,
                                  QStringLiteral("switch toggled")),
                 qPrintable(QStringLiteral("A tap %1 px off the pill's centre did not toggle it").arg(dy)));
    }
}

void PoliceTopBarUITest::_captureArduPilotRadio()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }

    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QVERIFY2(QMetaObject::invokeMethod(_window, "showVehicleConfig"), "showVehicleConfig is not invokable");
        VehicleComponent *const radio =
            vehicle->autopilotPlugin()->findKnownVehicleComponent(AutoPilotPlugin::KnownRadioVehicleComponent);
        QVERIFY2(radio, "The ArduPilot mock vehicle has no radio component");
        QVERIFY2(clickButtonScrolled(QStringLiteral("vehicleConfig_comp_") + radio->name().remove(QLatin1Char(' ')),
                                     QStringLiteral("vehicleConfig_sidebarFlickable")),
                 "Could not tap the radio row");
        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));
        _grab(QStringLiteral("k_5_apm_radio"));
    });
}

void PoliceTopBarUITest::_testOperatorVehicleSetupPX4()
{
    _runOperatorVehicleSetup([] { return MockLink::startPX4MockLink(); },
                             QStringLiteral("RTL_RETURN_ALT"), 45, QStringLiteral("setup_px4"));
}

void PoliceTopBarUITest::_testOperatorVehicleSetupAPM()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    _runOperatorVehicleSetup([] { return MockLink::startAPMArduCopterMockLink(); },
                             QStringLiteral("RTL_ALT_M"), 25, QStringLiteral("setup_apm"));
}

void PoliceTopBarUITest::_runOperatorVehicleSetup(const std::function<MockLink *()> &factory, const QString &rtlParam,
                                                  double newValue, const QString &capturePrefix)
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink(factory, [&](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        const QString railName = QStringLiteral("vehicleConfig_sidebarFlickable");
        const QString parametersButton = QStringLiteral("vehicleConfig_parametersButton");
        const QString firmware = QCoreApplication::translate("VehicleConfigView", "Firmware");

        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QVERIFY2(QMetaObject::invokeMethod(_window, "showVehicleConfig"), "showVehicleConfig is not invokable");
        QQuickItem *const rail = findVisibleItem(_rootItem, railName, 5000);
        QVERIFY2(rail, "The vehicle setup rail is not visible");

        // Developer mode first, so the two absences below are of buttons that do exist
        QVERIFY2(findVisibleItem(rail, parametersButton, 2000), "Developer mode shows no Parameters button");
        QVERIFY2(findVisibleItemWithExactText(rail, firmware), "Developer mode shows no Firmware button");

        QGCCorePlugin::instance()->setProperty("showAdvancedUI", false);
        const auto restore = qScopeGuard([] { QGCCorePlugin::instance()->setProperty("showAdvancedUI", true); });
        QVERIFY2(waitForCondition([&] { return !findVisibleItem(rail, parametersButton, 0); }, 2000,
                                  QStringLiteral("Parameters button hidden")),
                 "The operator sees the Parameters button");
        QVERIFY2(!findVisibleItemWithExactText(rail, firmware), "The operator sees the Firmware button");

        // Every page the firmware offers is on the operator's rail
        for (const QVariant &entry : vehicle->autopilotPlugin()->vehicleComponents()) {
            VehicleComponent *const comp = entry.value<VehicleComponent *>();
            if (!comp || comp->setupSource().isEmpty()) {
                continue;
            }
            QVERIFY2(findVisibleItemScrolled(QStringLiteral("vehicleConfig_comp_") + comp->name().remove(QLatin1Char(' ')), railName),
                     qPrintable(QStringLiteral("The operator's rail has no %1 row").arg(comp->name())));
        }

        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));
        rail->setProperty("contentY", 0);
        _grabIfCapturing(capturePrefix + QStringLiteral("_0_rail_top"));
        if (QTest::currentTestFailed()) return;
        rail->setProperty("contentY", qMax(0.0, rail->property("contentHeight").toReal() - rail->height()));
        _grabIfCapturing(capturePrefix + QStringLiteral("_1_rail_bottom"));
        if (QTest::currentTestFailed()) return;

        VehicleComponent *const safety =
            vehicle->autopilotPlugin()->findKnownVehicleComponent(AutoPilotPlugin::KnownSafetyVehicleComponent);
        QVERIFY2(safety, "The mock vehicle has no safety component");
        QVERIFY2(clickButtonScrolled(QStringLiteral("vehicleConfig_comp_") + safety->name().remove(QLatin1Char(' ')), railName),
                 "Could not tap the safety row");
        QTest::mouseMove(_window, QPoint(kLayoutWidth * 3 / 4, kLayoutHeight / 2));

        QQuickItem *const panel = findVisibleItem(_rootItem, QStringLiteral("vehicleConfig_panelLoader"), 5000);
        QVERIFY2(panel, "The safety page did not load");
        QQuickItem *field = nullptr;
        QVERIFY2(waitForCondition([&] { return (field = findFactTextField(panel, rtlParam)) != nullptr; }, 5000,
                                  QStringLiteral("return altitude field")),
                 qPrintable(QStringLiteral("The safety page shows no %1 field").arg(rtlParam)));

        // The page's own flickable, scrolled so the field sits a third of the way down
        for (QQuickItem *flick = field->parentItem(); flick; flick = flick->parentItem()) {
            if (flick->inherits("QQuickFlickable")) {
                QQuickItem *const content = flick->property("contentItem").value<QQuickItem *>();
                const qreal maxY = qMax(0.0, flick->property("contentHeight").toReal() - flick->height());
                flick->setProperty("contentY", qBound(0.0, field->mapToItem(content, QPointF(0, 0)).y() - flick->height() / 3, maxY));
                break;
            }
        }
        QTest::qWait(kSettleMs);

        // The operator's gesture: tap the field, type the new altitude, press Enter
        const QPoint tap = field->mapToScene(QPointF(field->width() / 2, field->height() / 2)).toPoint();
        QVERIFY2(QRect(QPoint(0, 0), _window->size()).contains(tap), "The return altitude field is off screen");
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, tap);
        QVERIFY2(waitForCondition([&] { return field->hasActiveFocus(); }, 2000, QStringLiteral("field focused")),
                 "A tap did not focus the return altitude field");
        QVERIFY(QMetaObject::invokeMethod(field, "selectAll"));
        for (const QChar c : QString::number(newValue)) {
            QTest::keyClick(_window, c.toLatin1());
        }
        QTest::keyClick(_window, Qt::Key_Return);

        QTRY_COMPARE_WITH_TIMEOUT(mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, rtlParam).toDouble(), newValue, 5000);
        _grabIfCapturing(capturePrefix + QStringLiteral("_2_safety"));
    });
}

void PoliceTopBarUITest::_policeCorePluginIsLive()
{
    QVERIFY(qobject_cast<PoliceCorePlugin *>(QGCCorePlugin::instance()));
    QCOMPARE(SettingsManager::instance()->appSettings()->indoorPalette()->rawDefaultValue().toInt(), 1);
}
