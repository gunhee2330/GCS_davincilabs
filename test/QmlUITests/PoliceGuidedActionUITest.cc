#include "PoliceGuidedActionUITest.h"

#include <algorithm>

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QMetaMethod>
#include <QtCore/QRect>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QTimer>
#include <QtCore/QtMath>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "FactMetaData.h"
#include "FlyViewSettings.h"
#include "MAVLinkLib.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
#include "SiyiAiController.h"
#include "SiyiAiProtocol.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "SiyiLongProtocol.h"
#include "SiyiProtocol.h"
#include "SpeakerSettings.h"
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
const QString kRtlAltButton       = QStringLiteral("policeToolRtlAlt");
const QString kRtlAltPanel        = QStringLiteral("policeRtlAltPanel");

/// The takeoff panel beside the strip and what it holds.
const QString kTakeoffPanel    = QStringLiteral("policeTakeoffPanel");
const QString kTakeoffSlide    = QStringLiteral("policeTakeoffSlide");
const QString kAltitudeSlider  = QStringLiteral("policeAltitudeSlider");
const QString kAltitudeBubble  = QStringLiteral("policeAltitudeBubbleText");
const QString kAltitudeMaximum = QStringLiteral("policeAltitudeMaximum");
const QString kSlideKnob       = QStringLiteral("policeSlideKnob");
/// The land panel, and 복귀, which keeps the stock hold confirm.
const QString kLandButton      = QStringLiteral("policeToolLand");
const QString kLandPanel       = QStringLiteral("policeLandPanel");
const QString kLandSlide       = QStringLiteral("policeLandSlide");
const QString kRtlButton       = QStringLiteral("policeToolRtl");

const QString kSlider    = QStringLiteral("guidedValueSlider");
const QString kDashboard = QStringLiteral("policeDroneDashboard");

/// The two proximity displays the forward lidar drives.
const QString kRing = QStringLiteral("policeDroneProximityRing");
const QString kGlow = QStringLiteral("policeDroneObstacleGlow");

/// PoliceLidarMonitor::staleTimeoutMs default, which nothing here changes.
constexpr int kStaleTimeoutMs = 5000;

/// The camera windows and the blocks that share the screen with them.
const QString kForwardWindow   = QStringLiteral("cameraWindowPrimary");
const QString kZoomWindow      = QStringLiteral("cameraWindowSecondary");
const QString kThermalWindow   = QStringLiteral("cameraWindowShared");
const QString kAiPanel         = QStringLiteral("policeAiPanel");
const QString kTelemetryBar    = QStringLiteral("policeTelemetryBar");
const QString kInstrumentPanel = QStringLiteral("policeInstrumentPanel");
const QString kInstrumentRow   = QStringLiteral("policeInstrumentRow");
const QString kCameraStrip     = QStringLiteral("policeCameraToolStrip");
const QString kGuidedStrip     = QStringLiteral("policeGuidedToolStrip");
const QString kTopBar          = QStringLiteral("policeTopBar");
/// QGC's MapScale as the dashboard hosts it, and the map it measures.
const QString kMapScale        = QStringLiteral("policeMapScale");
const QString kFlyViewMap      = QStringLiteral("flyViewMap");
/// ToolStripHoverButton takes its objectName from the action it renders.
const QString kMosaicEntry     = QStringLiteral("policeMosaicToolAction");

/// The camera grid's six entries, in the order they are laid out: two columns, three rows.
const QStringList kCameraEntries {
    QStringLiteral("카메라"), QStringLiteral("촬영"),
    QStringLiteral("AI"),     QStringLiteral("모자이크"),
    QStringLiteral("추종"),   QStringLiteral("추적해제"),
};

/// Every camera window carries the same title chip name, and the zoom panel's chips the same
/// chip names, so these are looked up inside one window's or one chip's own subtree.
const QString kTitleChip  = QStringLiteral("cameraWindowTitleChip");
const QString kZoomPanel  = QStringLiteral("policeZoomCameraPanel");
const QString kStateChips = QStringLiteral("policeCameraStateChips");
const QString kChipText   = QStringLiteral("policeCameraStateChipText");

/// Drag-to-track on the zoom panel: the VideoOutput whose contentRect the box is normalised
/// through, and the box drawn while the finger is down.
const QString kZoomStream  = QStringLiteral("videoContent");
const QString kDragBox     = QStringLiteral("policeTargetDragBox");

/// The release button on the picture. The dashboard never shows it: the rail's 추적해제 stays
/// reachable over the big picture.
const QString kTrackCancel = QStringLiteral("policeTrackCancelButton");

/// The drag is walked in this many steps, each one well past the handler's drag threshold in
/// total, so the handler activates and reports a moving centroid rather than one jump.
constexpr int kDragSteps = 8;

/// Frames land on whole device pixels and the drag points are floored to integers, so the box
/// that comes back is compared in panel pixels with a couple to spare.
constexpr qreal kDragSlack = 3.0;

/// A signal of \a obj by name, whatever its parameter spelling in the metaobject.
QMetaMethod signalByName(const QObject *obj, const char *name)
{
    const QMetaObject *const mo = obj->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod method = mo->method(i);
        if ((method.methodType() == QMetaMethod::Signal) && (method.name() == name)) {
            return method;
        }
    }
    return QMetaMethod();
}

/// Lit and unlit chip colours, as PoliceDroneCameraPanel.qml sets them.
const QColor kChipLit   = QColor(QStringLiteral("#39ff14"));
const QColor kChipUnlit = QColor(QStringLiteral("#9aa3ab"));

/// QGCDelayButton.defaultDelay is 500 ms. The press must outlast it, with room for the
/// progress animation to reach 1.0 on the software backend.
constexpr int kHoldMs = 1500;

/// Bindings and the strip animations settle well inside this.
constexpr int kSettleMs = 1500;

/// The layout assertions are about edges meeting, and the layout resolves in real numbers.
constexpr qreal kEdgeSlack = 1.0;

/// PoliceDroneDashboard._cameraStripRight: the inset the camera tool strip keeps off the right
/// screen edge while the altitude slider is not up.
constexpr qreal kRightInset = 8.0;

/// The pill's height is derived from a width derived from the two rows' implicit heights, so it
/// carries a rounding step more than a single edge comparison does.
constexpr qreal kPillSlack = 2.0;

/// PoliceDroneDashboard._instrumentHeightOfStack: how much of card top to bar bottom the pill takes.
constexpr qreal kPillOfStack = 0.85;

/// PoliceDroneDashboard._stackWindowScale / _windowScale: the zoom and thermal windows against
/// the forward one, which stays at the smaller scale.
constexpr qreal kStackOfForward = 0.7 / 0.6;

/// A centred item lands on a half pixel either way.
constexpr qreal kCentreSlack = 2.0;

/// The pill is a rounded rectangle whose height is a fixed fraction of its width; the check is
/// that it was scaled, not squashed, so the ratio only has to land on its own implicit one.
constexpr qreal kRatioSlack = 0.01;

/// The delivery tablet's proportions in logical pixels. It is 1920x1200 with a default font
/// height near 45 px and this host's offscreen default is 18, and 1200/45 equals 480/18, so a
/// 768x480 logical window puts the layout's geometry at the tablet's own scale. Run it under
/// QT_SCALE_FACTOR=2.5 and the grabs come out 1920x1200.
constexpr int kLayoutWidth  = 768;
constexpr int kLayoutHeight = 480;

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

/// The camera grid's entries. Each one is a ToolStripHoverButton, which takes its objectName
/// from its action, and only one of the camera actions carries one - so they are collected off
/// the grid itself by the property the delegate holds rather than by name.
void collectStripEntries(QQuickItem *item, QList<QQuickItem *> &out)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *const child : children) {
        if (child->isVisible() && child->property("toolStripAction").isValid()) {
            out.append(child);
        } else {
            collectStripEntries(child, out);
        }
    }
}

/// Same approach ScreenshotTest takes: the mock's own sweep drives every sector off one sine, so
/// a single close arc can only be had by sending crafted DISTANCE_SENSOR messages.
/// Min 40 cm, max 12 m, a TF Mini's own range. A NaN entry is not sent at all, which is how the
/// one forward sensor this airframe carries is injected on its own.
///
/// Onto the link rather than into the fact group: PoliceLidarMonitor listens to Vehicle's
/// mavlinkMessageReceived, and handing the fact group a decoded message skips that dispatch
/// entirely - the displays would stay dark however close the injected obstacle was.
void injectProximity(MockLink *mockLink, Vehicle *vehicle, const double (&metresPerSector)[8])
{
    for (int sector = 0; sector < 8; sector++) {
        if (qIsNaN(metresPerSector[sector])) {
            continue;
        }
        mavlink_message_t msg{};
        const float quaternion[4]{};
        (void) mavlink_msg_distance_sensor_pack_chan(
            static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1,
            mockLink->outgoingMavlinkChannel(), &msg,
            0,                                                      // time_boot_ms
            40,                                                     // min_distance cm
            1200,                                                   // max_distance cm
            static_cast<uint16_t>(metresPerSector[sector] * 100.0),  // current_distance cm
            MAV_DISTANCE_SENSOR_LASER,
            static_cast<uint8_t>(sector),                           // id
            static_cast<uint8_t>(sector),                           // orientation: NONE..YAW_315 are 0..7
            255, 0.0f, 0.0f, quaternion, 0);
        mockLink->respondWithMavlinkMessage(msg);
    }
}

/// The first visible item in \a root's subtree whose text property reads \a text. The proximity
/// displays give their labels no objectName, so the number on screen is found by what it says.
QQuickItem *findVisibleTextItem(QQuickItem *root, const QString &text)
{
    if (root->isVisible() && (root->property("text").toString() == text)) {
        return root;
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findVisibleTextItem(child, text)) {
            return found;
        }
    }
    return nullptr;
}

/// Evaluates \a expression against \a item's own QML scope, so the item's internal readings can
/// be asserted without giving them an objectName of their own.
QVariant evaluateOn(QQuickItem *item, const QString &expression)
{
    QQmlExpression qmlExpression(qmlContext(item), item, expression);
    const QVariant value = qmlExpression.evaluate();
    if (qmlExpression.hasError()) {
        QTest::qFail(qPrintable(qmlExpression.error().toString()), __FILE__, __LINE__);
        return QVariant();
    }
    return value;
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

/// The forward window's ring, windowed or full screen: the visible ring outside the instrument pill.
QQuickItem *findForwardRing(QQuickItem *item)
{
    if (!item->isVisible()) {
        return nullptr;
    }
    if (item->objectName() == kRing) {
        return hasAncestorNamed(item, kInstrumentPanel) ? nullptr : item;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *const child : children) {
        if (QQuickItem *const found = findForwardRing(child)) {
            return found;
        }
    }
    return nullptr;
}

/// The stock vertical altitude slider, which FlyView hands the guided controller. Not a QObject
/// child of anything findChild can reach, so it is taken off the dashboard's controller.
QQuickItem *stockSlider(QQuickItem *dashboard)
{
    QObject *const controller = dashboard->property("guidedController").value<QObject *>();
    return controller ? controller->property("guidedValueSlider").value<QQuickItem *>() : nullptr;
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
    // in the guided slots as well as the layout ones, none of which instantiate those files, and
    // from the mission item visuals as well as the home position one - which slot catches it
    // moves with how long the slots before it took. The sentence is localised, so the pattern
    // matches the files rather than the words.
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/[A-Za-z]+MapVisual\\.qml: ")));
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

    // The stock plan view's tree while a downloaded mission is drawn. Present at HEAD, and it
    // also arrives in the first mission run of the process once QT_SCALE_FACTOR is set, which the
    // tablet-scale captures need. Nothing in the police layout instantiates that file; the
    // pattern names it, so a warning out of the camera layout cannot be swallowed by it.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/PlanViewRightPanel\\.qml:[0-9]+:[0-9]+: "
                         "QML PlanTreeView: the delegate's implicitHeight needs to be greater than zero$")));
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

void PoliceGuidedActionUITest::_grab(const QString &name, const std::function<void()> &beforeGrab)
{
    const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
    QVERIFY2(!dir.isEmpty(), "QGC_SCREENSHOT_DIR is not set");
    QVERIFY2(QDir().mkpath(dir), qPrintable(QStringLiteral("Cannot create %1").arg(dir)));

    QTest::qWait(kSettleMs);
    if (beforeGrab) {
        beforeGrab();
    }

    const QImage image = _window->grabWindow();
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Empty grab for %1").arg(name)));

    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    QVERIFY2(image.save(path), qPrintable(QStringLiteral("Cannot write %1").arg(path)));
}

void PoliceGuidedActionUITest::_dragPointer(const QList<QPointF> &path, const QString &grabName,
                                            const std::function<void()> &midDrag)
{
    QPoint at(qFloor(path.first().x()), qFloor(path.first().y()));
    QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, at);

    for (int leg = 1; leg < path.size(); ++leg) {
        const QPoint corner(qFloor(path.at(leg).x()), qFloor(path.at(leg).y()));
        const QPoint start = at;
        for (int step = 1; step <= kDragSteps; ++step) {
            at = QPoint(start.x() + ((corner.x() - start.x()) * step / kDragSteps),
                        start.y() + ((corner.y() - start.y()) * step / kDragSteps));
            QTest::mouseMove(_window, at);
            QTest::qWait(16);
        }
    }

    // Everything below here happens with the pointer still down, which is the only time the box
    // is on screen and the only time the panel can be caught changing its mind mid-drag.
    if (midDrag) {
        midDrag();
    }

    if (!grabName.isEmpty() && !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        _grab(grabName);
    }

    QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, at);
    QTest::qWait(kSettleMs);
}

void PoliceGuidedActionUITest::_slide(const QString &barName, qreal fraction, const QString &grabName)
{
    QQuickItem *const bar = findVisibleItem(_rootItem, barName, 5000);
    QVERIFY2(bar, qPrintable(QStringLiteral("%1 is not on screen").arg(barName)));
    QVERIFY2(bar->isEnabled(), qPrintable(QStringLiteral("%1 is disabled").arg(barName)));
    QQuickItem *const knob = findVisibleItem(bar, kSlideKnob, 1000);
    QVERIFY2(knob, qPrintable(QStringLiteral("%1 has no knob").arg(barName)));

    const QPointF from = knob->mapToScene(QPointF(knob->width() / 2, knob->height() / 2));
    // The knob's centre travels the bar less one knob width; past the end is clamped by the drag.
    const qreal travel = bar->width() - knob->width();
    _dragPointer({from, from + QPointF(travel * fraction, 0)}, grabName);
}

void PoliceGuidedActionUITest::_takeOffFromPanel()
{
    QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
    QVERIFY2(findVisibleItem(_rootItem, kTakeoffPanel, 5000), "Takeoff panel never opened");
    _slide(kTakeoffSlide, 1.1);
}

void PoliceGuidedActionUITest::_testTargetDragPicksBox()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const panel = findVisibleItem(_rootItem, kZoomPanel, 5000);
        QVERIFY2(panel, "Zoom camera panel not found");

        // targetPickEnabled tracks the AI module's own connection, which no mock link can make
        // true, so it is driven on the panel - the same way the chip test drives the chips.
        QVERIFY(panel->setProperty("targetPickEnabled", true));
        QTest::qWait(kSettleMs);

        // Target picking on and nothing dragged yet: no control on the picture, the drag is the
        // whole gesture.
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("drag_0_at_rest"));
        }

        // The box is normalised through the video item's contentRect, so the expected numbers are
        // taken from the same rectangle rather than from the panel.
        QQuickItem *const video = findVisibleItem(panel, kZoomStream, 3000);
        QVERIFY2(video, "Zoom panel video item not found");
        const QRectF content = video->property("contentRect").toRectF();
        QVERIFY2(!content.isEmpty(), "Video content rect is empty - there is nothing to normalise against");

        const QMetaMethod picked = signalByName(panel, "targetBoxPicked");
        QVERIFY2(picked.isValid(), "Panel has no targetBoxPicked signal");
        QSignalSpy spy(panel, picked);
        QVERIFY(spy.isValid());

        const QRectF panelRect = sceneRect(panel);
        const QPointF from = panelRect.topLeft() + QPointF(panelRect.width() * 0.2, panelRect.height() * 0.2);
        const QPointF to   = panelRect.topLeft() + QPointF(panelRect.width() * 0.8, panelRect.height() * 0.75);

        // Mid-drag the box is on screen; the grab inside _dragPointer is taken while it is.
        _dragPointer({ from, to }, QStringLiteral("drag_1_box"), [panel] {
            QVERIFY2(findVisibleItem(panel, kDragBox, 0), "No box was drawn while the drag was under way");
        });
        if (QTest::currentTestFailed()) return;

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> box = spy.takeFirst();
        QCOMPARE(box.size(), 4);

        // Back out of frame coordinates into the panel's own, which is where the drag was aimed.
        const QPointF localFrom = panel->mapFromScene(QPointF(qFloor(from.x()), qFloor(from.y())));
        const QPointF localTo   = panel->mapFromScene(QPointF(qFloor(to.x()), qFloor(to.y())));
        const QRectF wanted = QRectF(localFrom, localTo).normalized();
        const QRectF got(content.x() + (box.at(0).toDouble() * content.width()),
                         content.y() + (box.at(1).toDouble() * content.height()),
                         (box.at(2).toDouble() - box.at(0).toDouble()) * content.width(),
                         (box.at(3).toDouble() - box.at(1).toDouble()) * content.height());
        QVERIFY2((qAbs(got.left()   - wanted.left())   <= kDragSlack) &&
                     (qAbs(got.top()    - wanted.top())    <= kDragSlack) &&
                     (qAbs(got.right()  - wanted.right())  <= kDragSlack) &&
                     (qAbs(got.bottom() - wanted.bottom()) <= kDragSlack),
                 qPrintable(QStringLiteral("Drag sent the box %1, expected %2 in panel pixels")
                                .arg(QDebug::toString(got), QDebug::toString(wanted))));

        // A drag is not a tap: the panel must not have gone full screen under it.
        QCOMPARE(dashboard->property("expandedPanel").toString(), QString());

        // Out and back: the pointer moved far enough for the handler to activate, so the box it
        // ends on being too small is the only thing that can turn this down.
        _dragPointer({ from, to, from + QPointF(2, 2) });
        if (QTest::currentTestFailed()) return;
        QVERIFY2(spy.isEmpty(), "A drag that came back to its press point still sent a box");

        // The AI link dropping, or the operator switching AI off, takes target picking away with
        // the finger still down. The half-drawn box must not go out as the handler falls over.
        _dragPointer({ from, to }, QString(), [panel] {
            QVERIFY(panel->setProperty("targetPickEnabled", false));
        });
        if (QTest::currentTestFailed()) return;
        QVERIFY2(spy.isEmpty(), "A drag whose panel stopped taking target picks still sent a box");

        // And a panel that does not take target picks - which is what the fixed forward camera is
        // left at - must ignore the gesture outright. targetPickEnabled is already false above.
        _dragPointer({ from, to });
        if (QTest::currentTestFailed()) return;
        QVERIFY2(spy.isEmpty(), "A drag on a panel with target picking off sent a box");
        QCOMPARE(dashboard->property("expandedPanel").toString(), QString());

        // The gesture the drag sits beside: a short tap on the picture still goes full screen,
        // through the window's own MouseArea, which is what stops the tap reaching the map.
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(qFloor(panelRect.center().x()), qFloor(panelRect.center().y())));
        QTest::qWait(kSettleMs);
        QCOMPARE(dashboard->property("expandedPanel").toString(), QStringLiteral("secondary"));
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
    });
}

void PoliceGuidedActionUITest::_testTrackedBoxColourByClass()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const panel = findVisibleItem(_rootItem, kZoomPanel, 5000);
        QVERIFY2(panel, "Zoom camera panel not found");

        // A fake module on loopback: the controller's first request says where to answer, and the
        // target stream is resent faster than the controller's 1.5 s target timeout.
        QUdpSocket module;
        QVERIFY(module.bind(QHostAddress::LocalHost, 0));
        SiyiAiController *const ai = SiyiAiController::instance();
        QVERIFY(ai);
        Fact *const aiAddress = SettingsManager::instance()->siyiCameraSettings()->aiIpAddress();
        Fact *const aiPort = SettingsManager::instance()->siyiCameraSettings()->aiPort();
        const QVariant savedAddress = aiAddress->rawValue();
        const QVariant savedPort = aiPort->rawValue();
        const auto restoreModule = qScopeGuard([ai, aiAddress, aiPort, savedAddress, savedPort] {
            ai->stop();
            aiAddress->setRawValue(savedAddress);
            aiPort->setRawValue(savedPort);
        });
        aiAddress->setRawValue(QStringLiteral("127.0.0.1"));
        aiPort->setRawValue(module.localPort());
        ai->start();
        // The settings writes above may restart the link on their own, so the last socket to
        // speak is the live one.
        QVERIFY(module.waitForReadyRead(5000));
        QTest::qWait(300);
        QNetworkDatagram probe;
        while (module.hasPendingDatagrams()) {
            probe = module.receiveDatagram();
        }
        QVERIFY(probe.isValid());
        const QHostAddress controllerAddress = probe.senderAddress();
        const quint16 controllerPort = static_cast<quint16>(probe.senderPort());

        QByteArray streaming;
        const auto frameOf = [](SiyiAi::TargetType type, SiyiAi::TrackingStatus status) {
            QByteArray data;
            for (const quint16 value : { quint16(520), quint16(360), quint16(120), quint16(300) }) {
                data.append(static_cast<char>(value & 0xFF));
                data.append(static_cast<char>(value >> 8));
            }
            data.append(static_cast<char>(type));
            data.append(static_cast<char>(status));
            return SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiAi::CommandId::TargetStream), data);
        };
        QTimer pusher;
        pusher.setInterval(200);
        (void) connect(&pusher, &QTimer::timeout, &module, [&] {
            (void) module.writeDatagram(streaming, controllerAddress, controllerPort);
        });
        pusher.start();

        // Full screen, as the operator sees a pick.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));

        struct Case {
            SiyiAi::TargetType type;
            SiyiAi::TrackingStatus status;
            QString word;
            QColor box;
            QColor text;
            QString grab;
        };
        const QList<Case> cases {
            { SiyiAi::TargetType::Person,    SiyiAi::TrackingStatus::Tracking,          QStringLiteral("person"),
              QColor(QStringLiteral("#e0a800")), QColor(Qt::black), QStringLiteral("box_0_person") },
            { SiyiAi::TargetType::Car,       SiyiAi::TrackingStatus::Tracking,          QStringLiteral("car"),
              QColor(QStringLiteral("#a78bfa")), QColor(Qt::black), QStringLiteral("box_1_car") },
            { SiyiAi::TargetType::Arbitrary, SiyiAi::TrackingStatus::TrackingArbitrary, QStringLiteral("object"),
              QColor(QStringLiteral("#ff9500")), QColor(Qt::white), QStringLiteral("box_2_object") },
        };
        for (const Case &c : cases) {
            streaming = frameOf(c.type, c.status);
            QTRY_COMPARE_WITH_TIMEOUT(ai->targetTypeName(), c.word, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(findVisibleTextItem(panel, c.word), 5000);
            QQuickItem *const label = findVisibleTextItem(panel, c.word);
            QVERIFY2(label, qPrintable(QStringLiteral("No %1 label on the zoom panel").arg(c.word)));
            QQuickItem *const chip = label->parentItem();
            QVERIFY(chip);
            QQuickItem *const box = chip->parentItem();
            QVERIFY(box);
            QCOMPARE(label->property("color").value<QColor>(), c.text);
            QVERIFY(label->property("font").value<QFont>().bold());
            QCOMPARE(chip->property("color").value<QColor>(), c.box);
            QCOMPARE(evaluateOn(box, QStringLiteral("border.color")).value<QColor>(), c.box);
            if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
                _grab(c.grab);
                if (QTest::currentTestFailed()) return;
            }
        }

        pusher.stop();
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
    });
}

void PoliceGuidedActionUITest::_testTrackCancelStaysEnabledWhileHeld()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const strip = findVisibleItem(_rootItem, kCameraStrip, 5000);
        QVERIFY2(strip, "Camera rail not found");
        QQuickItem *const cancel = findVisibleTextItem(strip, QStringLiteral("추적해제"));
        QVERIFY2(cancel, "The rail lost its 추적해제 button");
        QVERIFY2(!cancel->isEnabled(), "추적해제 is live with nothing tracked");

        QUdpSocket module;
        QVERIFY(module.bind(QHostAddress::LocalHost, 0));
        SiyiAiController *const ai = SiyiAiController::instance();
        QVERIFY(ai);
        Fact *const aiAddress = SettingsManager::instance()->siyiCameraSettings()->aiIpAddress();
        Fact *const aiPort = SettingsManager::instance()->siyiCameraSettings()->aiPort();
        const QVariant savedAddress = aiAddress->rawValue();
        const QVariant savedPort = aiPort->rawValue();
        const auto restoreModule = qScopeGuard([ai, aiAddress, aiPort, savedAddress, savedPort] {
            ai->stop();
            aiAddress->setRawValue(savedAddress);
            aiPort->setRawValue(savedPort);
        });
        aiAddress->setRawValue(QStringLiteral("127.0.0.1"));
        aiPort->setRawValue(module.localPort());
        ai->start();
        QVERIFY(module.waitForReadyRead(5000));
        QTest::qWait(300);
        QNetworkDatagram probe;
        while (module.hasPendingDatagrams()) {
            probe = module.receiveDatagram();
        }
        QVERIFY(probe.isValid());
        const QHostAddress controllerAddress = probe.senderAddress();
        const quint16 controllerPort = static_cast<quint16>(probe.senderPort());
        const auto reply = [&](SiyiAi::CommandId command, const QByteArray &data) {
            const QByteArray frame = SiyiProtocol::encodeRaw(static_cast<quint8>(command), data);
            QCOMPARE(module.writeDatagram(frame, controllerAddress, controllerPort), frame.size());
        };
        QByteArray target;
        for (const quint16 value : { quint16(640), quint16(360), quint16(120), quint16(300) }) {
            target.append(static_cast<char>(value & 0xFF));
            target.append(static_cast<char>(value >> 8));
        }
        target.append(static_cast<char>(SiyiAi::TargetType::Person));

        // Nothing held to begin with, whatever an earlier test left the singleton's last target at.
        reply(SiyiAi::CommandId::TargetStream, target + QByteArray(1, static_cast<char>(SiyiAi::TrackingStatus::CancelledByUser)));
        QTRY_COMPARE_WITH_TIMEOUT(ai->targetTypeName(), QStringLiteral("person"), 3000);
        QVERIFY(!ai->selectionHeld());

        // A pick the module accepts, then its target on the stream.
        ai->trackBox(0.4, 0.4, 0.6, 0.8);
        reply(SiyiAi::CommandId::SetTrackTarget, QByteArray(1, '\1'));
        QTRY_VERIFY_WITH_TIMEOUT(ai->selectionHeld(), 3000);
        QTimer pusher;
        pusher.setInterval(200);
        (void) connect(&pusher, &QTimer::timeout, &module, [&] {
            reply(SiyiAi::CommandId::TargetStream, target + QByteArray(1, static_cast<char>(SiyiAi::TrackingStatus::Tracking)));
        });
        pusher.start();
        QTRY_VERIFY_WITH_TIMEOUT(ai->hasTarget(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(cancel->isEnabled(), 3000);

        // The stream pauses for 2 s, past the 1.5 s the box stays up for; the module still holds it.
        pusher.stop();
        QTest::qWait(2000);
        QVERIFY2(!ai->hasTarget(), "The target outlived the stream pause, so the pause proved nothing");
        QVERIFY2(cancel->isEnabled(), "추적해제 greyed out while the module still held the target");

        // The module reports the target cancelled: nothing left to release.
        reply(SiyiAi::CommandId::TargetStream, target + QByteArray(1, static_cast<char>(SiyiAi::TrackingStatus::CancelledByUser)));
        QTRY_VERIFY_WITH_TIMEOUT(!ai->selectionHeld(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!cancel->isEnabled(), 3000);
    });
}

void PoliceGuidedActionUITest::_testTrackCancelButton()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const panel = findVisibleItem(_rootItem, kZoomPanel, 5000);
        QVERIFY2(panel, "Zoom camera panel not found");

        // Both properties follow the AI module's own state, which no mock link can drive, so they
        // are set on the panel - the same way the chip test drives the chips.
        QVERIFY(panel->setProperty("targetPickEnabled", true));
        QVERIFY(panel->setProperty("trackCancelEnabled", true));
        QTest::qWait(kSettleMs);

        // In the corner the rail carries the command and the window is too small to give any of
        // its picture to a button, so the one on the picture stays off.
        QVERIFY2(!findVisibleItem(panel, kTrackCancel, 0),
                 "The release button is on the small zoom window, over its picture");
        QQuickItem *const strip = findVisibleItem(_rootItem, kCameraStrip, 3000);
        QVERIFY2(strip, "Camera rail not found");
        QVERIFY2(findVisibleTextItem(strip, QStringLiteral("추적해제")), "The rail lost its 추적해제 button");
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("cancel_0_corner_no_button"));
        }

        // Big, the picture sits under the camera rail, so the rail's 추적해제 is still the one to
        // reach for and the picture carries none.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        QTest::qWait(kSettleMs);
        QVERIFY2(!findVisibleItem(panel, kTrackCancel, 0), "The release button is on the big zoom picture");
        QVERIFY2(findVisibleTextItem(strip, QStringLiteral("추적해제")), "The rail lost its 추적해제 button over the big picture");
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
    });
}

void PoliceGuidedActionUITest::_testTakeoffOpensPanel()
{
    _ignorePreexistingQmlWarnings();

    Fact *const maxFact = SettingsManager::instance()->flyViewSettings()->guidedMaximumAltitude();
    const QVariant savedMax = maxFact->rawValue();
    const auto restoreMax = qScopeGuard([maxFact, savedMax] { maxFact->setRawValue(savedMax); });

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this, maxFact](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QVERIFY2(!findVisibleItem(_rootItem, kTakeoffPanel, 0), "Takeoff panel was open before the tap");
        QVERIFY(verifyEnabled(kTakeoffButton, true, QStringLiteral("before pressing takeoff")));
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");

        QQuickItem *const panel = findVisibleItem(_rootItem, kTakeoffPanel, 5000);
        QVERIFY2(panel, "Takeoff panel never opened after pressing takeoff");
        // Neither the stock hold button nor its vertical slider comes up for takeoff any more.
        QVERIFY2(!findVisibleItem(_rootItem, kConfirmButton, 1500), "The stock hold confirm came up for takeoff");
        QVERIFY2(!findVisibleItem(_rootItem, kSlider, 0), "The stock vertical slider came up for takeoff");
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("takeoff_0_panel"));
            if (QTest::currentTestFailed()) return;
        }

        QQuickItem *const slider = findVisibleItem(panel, kAltitudeSlider, 1000);
        QVERIFY2(slider, "Takeoff panel has no altitude slider");
        QQuickItem *const bubble = findVisibleItem(panel, kAltitudeBubble, 1000);
        QVERIFY2(bubble, "Takeoff panel has no value bubble");
        QQuickItem *const maxField = findVisibleItem(panel, kAltitudeMaximum, 1000);
        QVERIFY2(maxField, "Takeoff panel has no 최대 field");

        // It starts where the stock takeoff slider started: the vehicle's minimum takeoff altitude.
        const double minTakeoff = FactMetaData::metersToAppSettingsVerticalDistanceUnits(
                                      vehicle->minimumTakeoffAltitudeMeters()).toDouble();
        QVERIFY2(qAbs(slider->property("value").toDouble() - minTakeoff) < 0.01,
                 qPrintable(QStringLiteral("Slider starts at %1, the minimum takeoff altitude is %2")
                                .arg(slider->property("value").toDouble()).arg(minTakeoff)));
        QCOMPARE(slider->property("from").toDouble(), 0.0);
        QVERIFY(qAbs(slider->property("to").toDouble() - maxFact->cookedValue().toDouble()) < 0.01);
        const QString unit = FactMetaData::appSettingsVerticalDistanceUnitsString();

        // The bubble follows the slider.
        QVERIFY(slider->setProperty("value", 30));
        QTRY_COMPARE(bubble->property("text").toString(), QStringLiteral("30 %1").arg(unit));

        // 최대 typed in writes the stock setting, and the slider's range follows it.
        QQuickItem *const fieldItem = maxField;
        const QPointF fieldCentre = fieldItem->mapToScene(QPointF(fieldItem->width() / 2, fieldItem->height() / 2));
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, fieldCentre.toPoint());
        QTRY_VERIFY(fieldItem->hasActiveFocus());
        QTest::keySequence(_window, QKeySequence::SelectAll);
        QTest::keyClick(_window, Qt::Key_8);
        QTest::keyClick(_window, Qt::Key_0);
        QTest::keyClick(_window, Qt::Key_Return);
        QTRY_COMPARE(qRound(maxFact->cookedValue().toDouble()), 80);
        QTRY_COMPARE(slider->property("to").toDouble(), 80.0);
        QCOMPARE(fieldItem->property("text").toString(), QStringLiteral("80"));

        // The X closes it with nothing sent.
        QVERIFY2(clickButton(QStringLiteral("policeDropPanelClose")), "Could not click the panel's X");
        QTRY_VERIFY2(!findVisibleItem(_rootItem, kTakeoffPanel, 0), "Takeoff panel stayed after its X");
    });
}

void PoliceGuidedActionUITest::_testTakeoffAltitudeSliderIsOnTop()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        // Takeoff no longer raises the stock slider, but pause, goto and orbit still do; it is
        // put up by hand here, the way confirmAction would.
        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 3000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const slider = stockSlider(dashboard);
        QVERIFY2(slider, "Stock altitude slider not found");
        QVERIFY(slider->setProperty("visible", true));
        QVERIFY2(findVisibleItem(_rootItem, kSlider, 3000), "Altitude slider never became visible");

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
        QVERIFY(slider->setProperty("visible", false));
    });
}

void PoliceGuidedActionUITest::_testSlideSendsTakeoff()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

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
        QQuickItem *const panel = findVisibleItem(_rootItem, kTakeoffPanel, 5000);
        QVERIFY2(panel, "Takeoff panel never opened after pressing takeoff");
        QQuickItem *const slider = findVisibleItem(panel, kAltitudeSlider, 1000);
        QVERIFY2(slider, "Takeoff panel has no altitude slider");
        QVERIFY(slider->setProperty("value", 7));
        const double sliderMeters = FactMetaData::appSettingsVerticalDistanceUnitsToMeters(7).toDouble();

        mockLink->clearReceivedMavCommandCounts();

        // Let go halfway: the knob goes back and nothing is sent. The mid-drag frame is the capture.
        _slide(kTakeoffSlide, 0.5, QStringLiteral("takeoff_1_mid_drag"));
        if (QTest::currentTestFailed()) return;
        QQuickItem *const bar = findVisibleItem(panel, kTakeoffSlide, 1000);
        QVERIFY2(bar, "The slide bar went away after an early release");
        QQuickItem *const knob = findVisibleItem(bar, kSlideKnob, 1000);
        QVERIFY2(knob, "The slide bar has no knob");
        QTRY_VERIFY2(knob->x() < knob->width() / 2, "The knob did not slide back after an early release");
        QTest::qWait(kSettleMs);
        QCOMPARE(mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF), 0);
        QVERIFY2(!vehicle->armed(), "An early release armed the vehicle");
        QVERIFY2(findVisibleItem(_rootItem, kTakeoffPanel, 0), "An early release closed the panel");

        // PX4FirmwarePlugin::guidedModeTakeoff builds param7 as this AMSL plus the slider metres.
        const double vehicleAmslAtCommand = vehicle->altitudeAMSL()->rawValue().toDouble();

        // MockLink::_handleTakeoff is the only writer of the mock's simulated AMSL, and it sets it
        // to the commanded param7 plus its own home altitude. Untouched so far, so the rise over
        // this reading is param7 itself. Reading param7 off lastReceivedMavlinkMessage() instead
        // does not work: only the newest COMMAND_LONG is kept and QGC's MAV_CMD_REQUEST_MESSAGE
        // traffic overwrites it.
        const double mockAltitudeBeforeTakeoff = mockLink->vehicleAltitudeAMSL();

        // All the way: the takeoff goes out, the vehicle arms, and the panel closes.
        _slide(kTakeoffSlide, 1.1);
        if (QTest::currentTestFailed()) return;

        QVERIFY_TRUE_WAIT(mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF) == 1, TestTimeout::longMs());
        QVERIFY_TRUE_WAIT(vehicle->armed(), TestTimeout::longMs());
        QVERIFY2(!findVisibleItem(_rootItem, kTakeoffPanel, 0), "Takeoff panel stayed after the takeoff");

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

void PoliceGuidedActionUITest::_testSlideSendsLand()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        // In the air first. PX4 sends the takeoff altitude as AMSL and refuses until it is known;
        // the flying transition creates QGCPressure, which warns on hosts without a backend.
        QVERIFY_TRUE_WAIT(!qIsNaN(vehicle->altitudeAMSL()->rawValue().toDouble()), TestTimeout::longMs());
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));
        _takeOffFromPanel();
        if (QTest::currentTestFailed()) return;
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
        QTest::qWait(kSettleMs);

        // 복귀 keeps the stock hold confirm, in the police host.
        QVERIFY2(clickButton(kRtlButton), "Could not click the 복귀 tool strip entry");
        QQuickItem *const confirmButton = findVisibleItem(_rootItem, kConfirmButton, 5000);
        QVERIFY2(confirmButton, "복귀 raised no stock hold confirm");
        QVERIFY2(hasAncestorNamed(confirmButton, kConfirmHost), "Confirm control on screen is not the police instance");

        // 착륙 opens its own panel and takes the pending stock confirm down.
        QVERIFY2(clickButton(kLandButton), "Could not click the 착륙 tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kLandPanel, 5000), "Land panel never opened");
        QTRY_VERIFY2(!findVisibleItem(_rootItem, kConfirmButton, 0), "The stock confirm stayed up under the land panel");
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("land_0_panel"));
            if (QTest::currentTestFailed()) return;
        }

        // Let go halfway and nothing changes.
        const QString landMode = vehicle->landFlightMode();
        QVERIFY2(vehicle->flightMode() != landMode, "The vehicle was already landing");
        _slide(kLandSlide, 0.5);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(vehicle->flightMode() != landMode, "An early release started a landing");
        QVERIFY2(findVisibleItem(_rootItem, kLandPanel, 0), "An early release closed the land panel");

        // All the way: PX4 lands by switching to its land mode.
        _slide(kLandSlide, 1.1);
        if (QTest::currentTestFailed()) return;
        QVERIFY_TRUE_WAIT(vehicle->flightMode() == landMode, TestTimeout::longMs());
        QVERIFY2(!findVisibleItem(_rootItem, kLandPanel, 0), "Land panel stayed after the landing was sent");
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

void PoliceGuidedActionUITest::_testRtlAltitudeFromToolStrip()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        // Stock showRTL is false on the ground, and the entry follows it.
        QVERIFY(verifyEnabled(kRtlAltButton, false, QStringLiteral("on the ground")));

        // PX4 sends the takeoff altitude as AMSL and refuses until it is known; the flying
        // transition creates QGCPressure, which warns on hosts without a pressure backend.
        QVERIFY_TRUE_WAIT(!qIsNaN(vehicle->altitudeAMSL()->rawValue().toDouble()), TestTimeout::longMs());
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));
        _takeOffFromPanel();
        if (QTest::currentTestFailed()) return;
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
        QTest::qWait(kSettleMs);

        // The regression: the entry stayed greyed in flight because the dashboard was handed
        // its own undefined controller rather than the fly view's.
        QVERIFY(verifyEnabled(kRtlAltButton, true, QStringLiteral("in flight")));
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("rtl_alt_0_strip_flying"));
            if (QTest::currentTestFailed()) return;
        }

        QVERIFY2(!findVisibleItem(_rootItem, kRtlAltPanel, 0), "Return altitude panel was open before the tap");
        QVERIFY2(clickButton(kRtlAltButton), "Could not click the 복귀고도 tool strip entry");
        QQuickItem *const panel = findVisibleItem(_rootItem, kRtlAltPanel, 3000);
        QVERIFY2(panel, "Return altitude panel never opened");

        // PX4's return altitude, which the old presets wrote.
        Fact *const rtlAlt = vehicle->parameterManager()->getParameter(ParameterManager::defaultComponentId,
                                                                       QStringLiteral("RTL_RETURN_ALT"));
        QVERIFY2(rtlAlt, "The mock carries no RTL_RETURN_ALT");

        // The slider starts at the height the vehicle would return at now, over 1 to 1000 m.
        QQuickItem *const slider = findVisibleItem(panel, kAltitudeSlider, 1000);
        QVERIFY2(slider, "Return altitude panel has no slider");
        QVERIFY2(findVisibleItem(panel, kAltitudeMaximum, 0), "Return altitude panel has no 최대 field");
        QCOMPARE(qRound(slider->property("value").toDouble()), qRound(rtlAlt->rawValue().toDouble()));
        QCOMPARE(slider->property("from").toDouble(), 1.0);
        QCOMPARE(slider->property("to").toDouble(), 1000.0);
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("rtl_alt_1_panel"));
            if (QTest::currentTestFailed()) return;
        }

        // 50 m, the middle preset, and 복귀: the parameter takes it and the stock RTL confirm comes up.
        QVERIFY(slider->setProperty("value", 50));
        QVERIFY2(clickButton(QStringLiteral("policeRtlAltReturn")), "Could not click the panel's 복귀");
        QVERIFY_TRUE_WAIT(qRound(rtlAlt->rawValue().toDouble()) == 50, TestTimeout::longMs());
        QQuickItem *const confirmButton = findVisibleItem(_rootItem, kConfirmButton, 5000);
        QVERIFY2(confirmButton, "복귀 raised no stock hold confirm");
        QVERIFY2(hasAncestorNamed(confirmButton, kConfirmHost), "Confirm control on screen is not the police instance");
    });
}

void PoliceGuidedActionUITest::_testBroadcastActionAlwaysShown()
{
    _ignorePreexistingQmlWarnings();

    // Off, to prove the switch no longer hides the entry, and back to what it was afterwards.
    Fact *const speakerEnabled = SettingsManager::instance()->speakerSettings()->enabled();
    QVERIFY2(speakerEnabled->rawValue().toBool(), "The speaker is not on by default");
    speakerEnabled->setRawValue(false);
    const auto restore = qScopeGuard([speakerEnabled] { speakerEnabled->setRawValue(true); });

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const strip = findVisibleItem(_rootItem, kCameraStrip, 5000);
        QVERIFY2(strip, "Camera tool strip is not on screen");
        QList<QQuickItem *> entries;
        collectStripEntries(strip, entries);
        QQuickItem *broadcast = nullptr;
        for (QQuickItem *const entry : entries) {
            if (entry->property("text").toString() == QStringLiteral("경고방송")) {
                broadcast = entry;
            }
        }
        QVERIFY2(broadcast, "경고방송 is not on the camera grid with the speaker switch off");
        QVERIFY2(!broadcast->isEnabled(), "경고방송 is live with no speaker answering");
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("speaker_0_right_rail"));
        }
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
        QVERIFY2(findVisibleItem(_rootItem, kTakeoffPanel, 5000), "Takeoff panel never appeared");
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
                                     kTelemetryBar, kInstrumentPanel, kInstrumentRow, kCameraStrip,
                                     kGuidedStrip, kTopBar }) {
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
        const QRectF strip   = sceneRect(items[kCameraStrip]);
        const QRectF left    = sceneRect(items[kGuidedStrip]);
        const QRectF topBar  = sceneRect(items[kTopBar]);

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

        // Two rows: the detection card directly above the telemetry bar, same left edge and the
        // same width, and the bar on the instrument row's own bottom line.
        QVERIFY2(card.bottom() <= bar.top() + kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card bottom %1 is not above the telemetry bar top %2")
                                .arg(card.bottom()).arg(bar.top())));
        QVERIFY2(qAbs(card.left() - bar.left()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card left %1 is not on the telemetry bar left %2")
                                .arg(card.left()).arg(bar.left())));
        QVERIFY2(qAbs(card.width() - bar.width()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card width %1 is not the telemetry bar width %2")
                                .arg(card.width()).arg(bar.width())));
        QVERIFY2(qAbs(bar.bottom() - row.bottom()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Telemetry bar bottom %1 is not on the instrument row bottom %2")
                                .arg(bar.bottom()).arg(row.bottom())));
        // The two rows keep their natural width and stop short of the zoom/thermal stack. Same
        // width and same left edge as the bar under it, both asserted above, so one check covers
        // the pair.
        QVERIFY2(card.right() <= zoom.left() - kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card right %1 runs into the camera stack left edge %2")
                                .arg(card.right()).arg(zoom.left())));

        // The pill stands a notch short of the two rows beside it, card top to bar bottom, on
        // the row's own bottom line, and is scaled to that height at its own proportions rather
        // than squashed into it.
        const qreal wantedPillHeight = kPillOfStack * (bar.bottom() - card.top());
        QVERIFY2(qAbs(compass.height() - wantedPillHeight) <= kPillSlack,
                 qPrintable(QStringLiteral("Instrument pill is %1 tall against a wanted %2 of a two-row block of %3")
                                .arg(compass.height()).arg(wantedPillHeight).arg(bar.bottom() - card.top())));
        QVERIFY2(qAbs(compass.bottom() - row.bottom()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Instrument pill bottom %1 is not on the instrument row bottom %2")
                                .arg(compass.bottom()).arg(row.bottom())));
        const qreal implicitRatio = items[kInstrumentPanel]->implicitHeight() /
                                    items[kInstrumentPanel]->implicitWidth();
        QVERIFY2(qAbs((compass.height() / compass.width()) - implicitRatio) <= kRatioSlack,
                 qPrintable(QStringLiteral("Instrument pill %1 sits at ratio %2 against its implicit ratio %3")
                                .arg(QDebug::toString(compass)).arg(compass.height() / compass.width())
                                .arg(implicitRatio)));


        // 줌 directly above 열상 in the bottom right corner, the two flush right, and 열상 on the
        // same baseline the forward window stands on.
        QVERIFY2(qAbs(thermal.top() - zoom.bottom()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Thermal top %1 is not on the zoom bottom %2")
                                .arg(thermal.top()).arg(zoom.bottom())));
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
        // The pair is one size, and a bigger one than the forward window: the pod's picture is
        // what gets read, the fixed forward camera is the wide shot beside it.
        QVERIFY2(qAbs(zoom.width() - thermal.width()) <= kEdgeSlack &&
                     qAbs(zoom.height() - thermal.height()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Zoom %1 and thermal %2 are not the same size")
                                .arg(QDebug::toString(zoom), QDebug::toString(thermal))));
        QVERIFY2(qAbs(zoom.width() - forward.width() * kStackOfForward) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Zoom is %1 wide against a wanted %2 off the forward window's %3")
                                .arg(zoom.width()).arg(forward.width() * kStackOfForward).arg(forward.width())));

        // The camera tool grid takes the same right edge, at the top bar's own offset, and ends
        // above the zoom window under it: it scrolls whatever does not fit in its maxHeight, so
        // the item itself is never taller than the room it was given.
        QVERIFY2(qAbs(strip.right() - (screen.right() - kRightInset)) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Camera tool strip right %1 is not at the right inset %2")
                                .arg(strip.right()).arg(screen.right() - kRightInset)));
        QVERIFY2(strip.top() >= topBar.bottom() - kEdgeSlack,
                 qPrintable(QStringLiteral("Camera tool strip top %1 runs under the top bar bottom %2")
                                .arg(strip.top()).arg(topBar.bottom())));
        QVERIFY2(strip.bottom() <= zoom.top() + kEdgeSlack,
                 qPrintable(QStringLiteral("Camera tool strip bottom %1 runs into the zoom window top %2")
                                .arg(strip.bottom()).arg(zoom.top())));
        QVERIFY2((strip.top() >= screen.top() - kEdgeSlack) && (strip.bottom() <= screen.bottom() + kEdgeSlack),
                 qPrintable(QStringLiteral("Camera tool strip %1 runs outside the screen %2")
                                .arg(QDebug::toString(strip), QDebug::toString(screen))));

        // Two columns by three rows: all six camera entries are on screen at once, each one
        // inside the grid rather than scrolled below its fold, and none of them on another.
        QList<QQuickItem *> entries;
        collectStripEntries(items[kCameraStrip], entries);
        QStringList entryLabels;
        for (int i = 0; i < entries.size(); ++i) {
            entryLabels.append(entries.at(i)->property("text").toString());
        }
        for (const QString &wanted : kCameraEntries) {
            QVERIFY2(entryLabels.contains(wanted),
                     qPrintable(QStringLiteral("Camera grid entry '%1' is not on screen - it holds %2")
                                    .arg(wanted, entryLabels.join(QStringLiteral(", ")))));
        }
        const QRectF stripBounds = strip.adjusted(-kEdgeSlack, -kEdgeSlack, kEdgeSlack, kEdgeSlack);
        for (int i = 0; i < entries.size(); ++i) {
            const QRectF a = sceneRect(entries[i]);
            QVERIFY2(stripBounds.contains(a),
                     qPrintable(QStringLiteral("Camera grid entry '%1' %2 is not inside the grid %3")
                                    .arg(entryLabels[i], QDebug::toString(a), QDebug::toString(strip))));
            for (int j = i + 1; j < entries.size(); ++j) {
                const QRectF b = sceneRect(entries[j]);
                QVERIFY2(!a.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack).intersects(
                             b.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack)),
                         qPrintable(QStringLiteral("Camera grid entry '%1' %2 overlaps '%3' %4")
                                        .arg(entryLabels[i], QDebug::toString(a),
                                             entryLabels[j], QDebug::toString(b))));
            }
        }

        // Nothing clipped by anything else. The two column windows are one block here: they are
        // stacked edge to edge, so on their own they always touch.
        const QList<QPair<QString, QRectF>> blocks {
            { QStringLiteral("forward window"),     forward },
            { QStringLiteral("detection card"),     card },
            { QStringLiteral("telemetry bar"),      bar },
            { QStringLiteral("instrument panel"),   compass },
            { QStringLiteral("camera tool strip"),  strip },
            { QStringLiteral("zoom/thermal column"), zoom.united(thermal) },
        };
        for (int i = 0; i < blocks.size(); ++i) {
            for (int j = i + 1; j < blocks.size(); ++j) {
                // Shrunk by the slack the edge checks use, so blocks that merely abut do not
                // read as overlapping.
                const QRectF a = blocks[i].second.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
                const QRectF b = blocks[j].second.adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
                QVERIFY2(!a.intersects(b),
                         qPrintable(QStringLiteral("%1 %2 overlaps %3 %4")
                                        .arg(blocks[i].first, QDebug::toString(blocks[i].second),
                                             blocks[j].first, QDebug::toString(blocks[j].second))));
            }
        }

        // The card's five counts fit: squeezed below its implicit width they start eliding.
        QQuickItem *const aiPanelItem = items[kAiPanel];
        QVERIFY2(aiPanelItem->width() >= aiPanelItem->implicitWidth() - kEdgeSlack,
                 qPrintable(QStringLiteral("Detection card is %1 wide against an implicit width of %2")
                                .arg(aiPanelItem->width()).arg(aiPanelItem->implicitWidth())));

        // The zoom window is the narrow one, and it carries both the window's name chip and the
        // panel's two state chips. On the tablet the chips ran over the name.
        QQuickItem *const titleChip = findVisibleItem(items[kZoomWindow], kTitleChip, 3000);
        QVERIFY2(titleChip, "Zoom window title chip is not on screen");
        QQuickItem *const chipRow = findVisibleItem(items[kZoomWindow], kStateChips, 3000);
        QVERIFY2(chipRow, "Zoom window state chips are not on screen");
        QVERIFY2(!sceneRect(titleChip).intersects(sceneRect(chipRow)),
                 qPrintable(QStringLiteral("Zoom title chip %1 is under the state chips %2")
                                .arg(QDebug::toString(sceneRect(titleChip)),
                                     QDebug::toString(sceneRect(chipRow)))));

        // One label in every detector state: the longer ones were clipped at both ends.
        QQuickItem *const mosaic = findVisibleItem(items[kCameraStrip], kMosaicEntry, 3000);
        QVERIFY2(mosaic, "Mosaic camera tool strip entry is not on screen");
        QCOMPARE(mosaic->property("text").toString(), QStringLiteral("모자이크"));

        // The left guided strip grows downward with the aircraft's state and the forward window
        // stands at the bottom of its column, so the strip has to stop above it.
        QVERIFY2(left.bottom() <= forward.top() + kEdgeSlack,
                 qPrintable(QStringLiteral("Left guided tool strip bottom %1 runs into the forward window top %2")
                                .arg(left.bottom()).arg(forward.top())));

        // The stock altitude slider takes the whole right screen edge while a confirmation is up,
        // which is the edge this strip stands on: it steps inboard of the slider for as long as
        // the slider is there and comes back to its own inset afterwards.
        // Takeoff no longer raises it, so it is put up by hand, the way confirmAction would.
        QQuickItem *const hiddenSlider = stockSlider(dashboard);
        QVERIFY2(hiddenSlider, "Stock altitude slider not found");
        QVERIFY(hiddenSlider->setProperty("visible", true));
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 5000);
        QVERIFY2(slider, "Altitude slider never appeared");
        QTest::qWait(kSettleMs);
        const QRectF sliderRect   = sceneRect(slider);
        const QRectF shiftedStrip = sceneRect(items[kCameraStrip]);
        QVERIFY2(!shiftedStrip.intersects(sliderRect),
                 qPrintable(QStringLiteral("Camera tool strip %1 is under the altitude slider %2")
                                .arg(QDebug::toString(shiftedStrip), QDebug::toString(sliderRect))));
        // It stepped, rather than the slider happening to land clear of where it already was -
        // which is what a dead reference to the slider would look like from the outside.
        QVERIFY2(shiftedStrip.right() < strip.right() - kEdgeSlack,
                 qPrintable(QStringLiteral("Camera tool strip right %1 did not step left of its resting %2")
                                .arg(shiftedStrip.right()).arg(strip.right())));

        // Hidden the way GuidedActionsController.closeAll() hides it - an assignment, not a
        // binding - and the strip comes back to its own inset on the next frame.
        QVERIFY(slider->setProperty("visible", false));
        QTRY_VERIFY2(qAbs(sceneRect(items[kCameraStrip]).right() - strip.right()) <= kEdgeSlack,
                     qPrintable(QStringLiteral("Camera tool strip right %1 did not return to %2 once the slider hid")
                                    .arg(sceneRect(items[kCameraStrip]).right()).arg(strip.right())));
    });
}

void PoliceGuidedActionUITest::_testMapScale()
{
    _ignorePreexistingQmlWarnings();

    const auto checkScale = [this](const QString &captureName) {
        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const scale = findVisibleItem(_rootItem, kMapScale, 5000);
        QVERIFY2(scale, "The map scale is not on the full map");
        QQuickItem *const map = findVisibleItem(_rootItem, kFlyViewMap, 5000);
        QVERIFY2(map, "The fly view map is not on screen");

        QHash<QString, QQuickItem *> items;
        for (const QString &name : { kForwardWindow, kZoomWindow, kThermalWindow, kAiPanel, kTelemetryBar,
                                     kInstrumentPanel, kCameraStrip, kGuidedStrip, kTopBar }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 3000);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on screen").arg(name)));
            items.insert(name, item);
        }

        // Stock autohides the full-map scale three seconds after the map last moved and brings
        // it back on the next change. An operator's zoom, then the grab straight after.
        QVERIFY(map->setProperty("zoomLevel", map->property("zoomLevel").toReal() - 1));
        QCOMPARE(scale->opacity(), 1.0);
        QTest::qWait(300);
        const QImage grab = _window->grabWindow();
        QVERIFY(!grab.isNull());
        const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            QVERIFY(QDir().mkpath(dir));
            QVERIFY(grab.save(QDir(dir).filePath(captureName + QStringLiteral(".png"))));
        }
        QCOMPARE(scale->opacity(), 1.0);

        const QRectF screen  = sceneRect(dashboard);
        const QRectF rect    = sceneRect(scale);
        const QRectF forward = sceneRect(items[kForwardWindow]);
        const QRectF card    = sceneRect(items[kAiPanel]);
        const QRectF left    = sceneRect(items[kGuidedStrip]);
        QVERIFY2(rect.width() > 0 && rect.height() > 0,
                 qPrintable(QStringLiteral("The map scale is %1").arg(QDebug::toString(rect))));

        // Bottom left: on the left column's inset, the one the tool strip and the forward window
        // stand on, and the stock margin above the forward window or the detection card,
        // whichever stands taller. The lidar glow's left band may light behind it.
        const qreal margin = dashboard->property("_toolsMargin").toReal();
        QVERIFY(margin > 0);
        const qreal floor = qMin(forward.top(), card.top());
        QVERIFY2(qAbs(rect.left() - left.left()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Map scale left %1 is not the tool strip's left %2")
                                .arg(rect.left()).arg(left.left())));
        QVERIFY2(qAbs(rect.left() - forward.left()) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Map scale left %1 is not the forward window's left %2")
                                .arg(rect.left()).arg(forward.left())));
        QVERIFY2(qAbs(rect.bottom() - (floor - margin)) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Map scale bottom %1 is not a %2 margin above %3")
                                .arg(rect.bottom()).arg(margin).arg(floor)));
        QVERIFY2((rect.top() >= sceneRect(items[kTopBar]).bottom()) && screen.contains(rect),
                 qPrintable(QStringLiteral("Map scale %1 is not on the map below the top bar")
                                .arg(QDebug::toString(rect))));

        const QList<QPair<QString, QRectF>> blocks {
            { QStringLiteral("forward window"),     forward },
            { QStringLiteral("zoom window"),        sceneRect(items[kZoomWindow]) },
            { QStringLiteral("thermal window"),     sceneRect(items[kThermalWindow]) },
            { QStringLiteral("detection card"),     card },
            { QStringLiteral("telemetry bar"),      sceneRect(items[kTelemetryBar]) },
            { QStringLiteral("instrument panel"),   sceneRect(items[kInstrumentPanel]) },
            { QStringLiteral("guided tool strip"),  left },
            { QStringLiteral("camera tool strip"),  sceneRect(items[kCameraStrip]) },
        };
        for (const auto &block : blocks) {
            QVERIFY2(!rect.intersects(block.second),
                     qPrintable(QStringLiteral("Map scale %1 overlaps the %2 %3")
                                    .arg(QDebug::toString(rect), block.first, QDebug::toString(block.second))));
        }

        // Drawn, not just laid out: the scale's left tick reads its own colour in the grab. The
        // software backend leaves the grab's devicePixelRatio at 1, so the scale comes from the
        // grab's size against the window's.
        QQuickItem *const tick = scale->childItems().value(1);
        QVERIFY2(tick && tick->property("color").isValid(), "The map scale has no left tick");
        const QPointF tickCentre = tick->mapToScene(QPointF(tick->width() / 2, tick->height() / 2));
        const QColor drawn = grab.pixelColor((tickCentre * grab.width() / _window->width()).toPoint());
        const QColor wanted = tick->property("color").value<QColor>();
        QVERIFY2(drawn == wanted,
                 qPrintable(QStringLiteral("The map scale tick at (%1, %2) reads %3, not its own %4")
                                .arg(tickCentre.x()).arg(tickCentre.y()).arg(drawn.name(), wanted.name())));
    };

    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }
    _window->resize(kLayoutWidth, kLayoutHeight);
    QTest::qWait(kSettleMs);
    checkScale(QStringLiteral("map_scale_0_idle"));
    if (QTest::currentTestFailed()) {
        return;
    }

    Vehicle *vehicle = nullptr;
    QPointer<MockLink> mockLink = connectMockLinkAndWaitReady([] { return MockLink::startPX4MockLink(); }, vehicle);
    QVERIFY2(mockLink, "Could not start the mock aircraft");
    const auto teardown = qScopeGuard([&] {
        disconnectMockLink(mockLink);
        closeUIWindow();
        destroyUIEngine();
    });
    QTest::qWait(kSettleMs);
    checkScale(QStringLiteral("map_scale_1_connected"));
    if (QTest::currentTestFailed()) {
        return;
    }

    // Now that the scale shares the strip's column, the strip at its tallest must still stop
    // above it: in flight 착륙, 복귀 and 일시정지 appear and 이륙 goes away. PX4 sends the takeoff
    // altitude as AMSL and refuses until it is known; the flying transition creates QGCPressure,
    // which warns on hosts without a pressure backend.
    QVERIFY_TRUE_WAIT(!qIsNaN(vehicle->altitudeAMSL()->rawValue().toDouble()), TestTimeout::longMs());
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
    ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));
    _takeOffFromPanel();
    if (QTest::currentTestFailed()) return;
    QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
    QTest::qWait(kSettleMs);
    checkScale(QStringLiteral("map_scale_2_flying"));
}

void PoliceGuidedActionUITest::_testStateChipColours()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const panel = findVisibleItem(_rootItem, kZoomPanel, 5000);
        QVERIFY2(panel, "Zoom camera panel not found");

        const QStringList expectedLabels { QStringLiteral("추종"), QStringLiteral("추적") };

        // Driven on the panel itself: the properties behind these chips are a gimbal flag and an
        // AI module flag, and neither can be made true from a mock link.
        for (const bool lit : { false, true }) {
            QVERIFY(panel->setProperty("followActive", lit));
            QVERIFY(panel->setProperty("trackingActive", lit));
            QTest::qWait(kSettleMs);

            QQuickItem *const chipRow = findVisibleItem(panel, kStateChips, 3000);
            QVERIFY2(chipRow, "State chip row is not on screen");

            const QList<QQuickItem *> chips = chipRow->childItems();
            QCOMPARE(chips.size(), expectedLabels.size());

            const QColor expectedColour = lit ? kChipLit : kChipUnlit;
            for (int i = 0; i < chips.size(); ++i) {
                QQuickItem *const label = findVisibleItem(chips.at(i), kChipText, 3000);
                QVERIFY2(label, qPrintable(QStringLiteral("Chip %1 carries no label").arg(i)));
                QVERIFY2(label->property("text").toString() == expectedLabels.at(i),
                         qPrintable(QStringLiteral("Chip %1 reads '%2', expected '%3'")
                                        .arg(i).arg(label->property("text").toString(),
                                                    expectedLabels.at(i))));
                const QColor colour = label->property("color").value<QColor>();
                QVERIFY2(colour == expectedColour,
                         qPrintable(QStringLiteral("Chip %1 is %2 with lit=%3, expected %4")
                                        .arg(i).arg(colour.name(), lit ? QStringLiteral("true")
                                                                       : QStringLiteral("false"),
                                                    expectedColour.name())));
            }
        }
    });
}

void PoliceGuidedActionUITest::_testLidarDisplaysFollowTheSensor()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");

        // Disarmed throughout: seeing the lidar work before takeoff is the point of dropping the
        // arming gate, and a display that only came up armed would pass none of this.
        QVERIFY2(!vehicle->armed(), "The mock vehicle came up armed");
        QVERIFY2(!findVisibleItem(_rootItem, kRing, 1000),
                 "A proximity ring was up before any reading had arrived");
        QVERIFY2(!findVisibleItem(_rootItem, kGlow, 1000),
                 "The obstacle glow was up before any reading had arrived");

        // The one forward sensor this airframe carries, at 3.3 m, inside the 7 m close band.
        const double rgForwardOnly[8] = { 3.3, qQNaN(), qQNaN(), qQNaN(),
                                          qQNaN(), qQNaN(), qQNaN(), qQNaN() };
        injectProximity(mockLink, vehicle, rgForwardOnly);

        QQuickItem *const ring = findVisibleItem(_rootItem, kRing, 5000);
        QVERIFY2(ring, "No proximity ring became visible while the lidar reported 3.3 m");
        QCOMPARE(evaluateOn(ring, QStringLiteral("_sectorDistance(0)")).toDouble(), 3.3);
        QVERIFY2(evaluateOn(ring, QStringLiteral("_sectorStroke(0)")).toDouble() > 0,
                 "The ring is up but its nose arc has no width, so nothing is drawn");

        QQuickItem *const glow = findVisibleItem(_rootItem, kGlow, 3000);
        QVERIFY2(glow, "The obstacle glow never became visible");
        // Sector 0 folds onto the top edge, which is where the number sits under the top bar.
        QCOMPARE(evaluateOn(glow, QStringLiteral("_edgeDistances[0]")).toDouble(), 3.3);
        QQuickItem *const forwardLabel = findVisibleTextItem(glow, QStringLiteral("전방 3.3 m"));
        QVERIFY2(forwardLabel, "The forward distance is not on screen");
        const QColor badColour = forwardLabel->property("color").value<QColor>();
        QVERIFY2(badColour == QColor(Qt::white),
                 qPrintable(QStringLiteral("The 3.3 m number is %1, not white").arg(badColour.name())));

        // The glow is aircraft-relative: turned to 90 the nose is still the top edge, and the right
        // edge, where the zoom window stands, stays dark. MockLink cannot hold a heading - its own
        // ATTITUDE_QUATERNION sweeps yaw within about 17 degrees of north at 10 Hz - so the Fact
        // is set directly and everything below is read before the event loop runs again.
        const auto bandAt = [glow](int edge) -> QQuickItem * {
            const QList<QQuickItem *> children = glow->childItems();
            for (QQuickItem *const child : children) {
                if (child->property("_distance").isValid() && (child->property("index").toInt() == edge)) {
                    return child;
                }
            }
            return nullptr;
        };
        QVERIFY(bandAt(0) && bandAt(1));
        vehicle->heading()->setRawValue(90.0);
        QCOMPARE(vehicle->heading()->rawValue().toDouble(), 90.0);
        QVERIFY2(bandAt(0)->isVisible(), "The top band went dark at heading 90");
        QVERIFY2(!bandAt(1)->isVisible(), "The right band lit at heading 90");
        QVERIFY2(forwardLabel->isVisible(), "The forward distance went away at heading 90");
        const QRectF labelRect = sceneRect(forwardLabel);
        QVERIFY2(QRectF(0, 0, _window->width(), _window->height()).contains(labelRect),
                 "The forward distance is not wholly inside the window");
        for (const QString &window : { kForwardWindow, kZoomWindow, kThermalWindow }) {
            QQuickItem *const camera = findVisibleItem(_rootItem, window, 0);
            QVERIFY2(!camera || !sceneRect(camera).intersects(labelRect),
                     qPrintable(QStringLiteral("The forward distance is under %1").arg(window)));
        }

        // The compass ring is aircraft-relative as well: one forward lidar, so its arc stays at
        // 12 o'clock at heading 90 rather than turning to 3 o'clock with the dial. Read off the
        // rendered frame at the middle of the stroke, at the rim's top and at its right side. The
        // grab renders without running the event loop, so the mock cannot move the heading first.
        QQuickItem *const instruments = findVisibleItem(_rootItem, kInstrumentPanel, 0);
        QQuickItem *const compassRing = instruments ? findVisibleItem(instruments, kRing, 0) : nullptr;
        QVERIFY2(compassRing, "The compass ring is not up while the lidar reports 3.3 m");
        const QImage frame = _window->grabWindow();
        QCOMPARE(vehicle->heading()->rawValue().toDouble(), 90.0);
        const qreal dpr = qreal(frame.width()) / _window->width();
        const QColor arcColour = evaluateOn(compassRing, QStringLiteral("_sectorColor(0)")).value<QColor>();
        const qreal arcRadius = evaluateOn(compassRing, QStringLiteral("_arcRadius")).toDouble();
        const QPointF centre = compassRing->mapToScene(QPointF(compassRing->width() / 2, compassRing->height() / 2));
        const auto arcPixel = [&](const QPointF &offset) {
            const QColor c = frame.pixelColor(qFloor((centre.x() + offset.x()) * dpr),
                                              qFloor((centre.y() + offset.y()) * dpr));
            return (qAbs(c.red() - arcColour.red()) + qAbs(c.green() - arcColour.green()) +
                    qAbs(c.blue() - arcColour.blue())) <= 60;
        };
        QVERIFY2(arcPixel(QPointF(0, -arcRadius)), "The compass arc is not at 12 o'clock at heading 90");
        QVERIFY2(!arcPixel(QPointF(arcRadius, 0)), "The compass arc turned to 3 o'clock at heading 90");

        const bool capture = !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty();
        if (capture) {
            injectProximity(mockLink, vehicle, rgForwardOnly);
            _grab(QStringLiteral("f_0_hdg000_3m3"), [vehicle] { vehicle->heading()->setRawValue(0.0); });
            if (QTest::currentTestFailed()) return;
            injectProximity(mockLink, vehicle, rgForwardOnly);
            _grab(QStringLiteral("f_1_hdg090_3m3"), [vehicle] { vehicle->heading()->setRawValue(90.0); });
            if (QTest::currentTestFailed()) return;
        }

        // Inside the warn band but past the close one: the number shows too, white on the glow's
        // orange and on the forward window's ring.
        const double rgForwardWarn[8] = { 8.5, qQNaN(), qQNaN(), qQNaN(),
                                          qQNaN(), qQNaN(), qQNaN(), qQNaN() };
        injectProximity(mockLink, vehicle, rgForwardWarn);
        QTRY_VERIFY(findVisibleTextItem(glow, QStringLiteral("전방 8.5 m")));
        const QColor warnColour =
            findVisibleTextItem(glow, QStringLiteral("전방 8.5 m"))->property("color").value<QColor>();
        QVERIFY2(warnColour == QColor(Qt::white),
                 qPrintable(QStringLiteral("The 8.5 m number is %1, not white").arg(warnColour.name())));
        QVERIFY2(findVisibleTextItem(_rootItem, QStringLiteral("8.5 m")),
                 "The forward window's ring shows no number at 8.5 m");
        if (capture) {
            injectProximity(mockLink, vehicle, rgForwardWarn);
            _grab(QStringLiteral("f_2_8m5"), [vehicle] { vehicle->heading()->setRawValue(0.0); });
            if (QTest::currentTestFailed()) return;
        }

        // Past the warn band nothing is drawn: no band, no number.
        const double rgForwardFar[8] = { 12.5, qQNaN(), qQNaN(), qQNaN(),
                                         qQNaN(), qQNaN(), qQNaN(), qQNaN() };
        injectProximity(mockLink, vehicle, rgForwardFar);
        QTRY_COMPARE(evaluateOn(glow, QStringLiteral("_edgeDistances[0]")).toDouble(), 12.5);
        QVERIFY2(!bandAt(0)->isVisible(), "The top band is lit at 12.5 m");
        QVERIFY2(!findVisibleTextItem(glow, QStringLiteral("전방 12.5 m")), "The glow shows 12.5 m");
        QVERIFY2(!findVisibleTextItem(_rootItem, QStringLiteral("12.5 m")), "The ring shows 12.5 m");

        injectProximity(mockLink, vehicle, rgForwardOnly);
        QTRY_VERIFY(findVisibleTextItem(glow, QStringLiteral("전방 3.3 m")));

        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            // Fed again right here: the assertions above take an unbudgeted while and _grab waits
            // another 1.5 s, which without a fresh frame could hand the file the cleared state
            // while every assertion had already passed. Re-fed, the grab lands 1.5 s into a 5 s
            // window.
            injectProximity(mockLink, vehicle, rgForwardOnly);
            _grab(QStringLiteral("l_0_disarmed_3m3"));
            if (QTest::currentTestFailed()) return;
        }

        // The same distance, once a second, for 6 s - longer than the 5 s timeout, so a frame
        // carrying a value already held has to be what keeps the displays up. An equal Fact value
        // signals nothing, which is what made a steady sensor indistinguishable from a dead one;
        // here the frames themselves are the receipt.
        for (int second = 0; second < 6; ++second) {
            QTest::qWait(1000);
            injectProximity(mockLink, vehicle, rgForwardOnly);
        }
        QVERIFY2(findVisibleItem(_rootItem, kRing, 1000),
                 "The ring went away while the same reading kept arriving");
        QVERIFY2(findVisibleItem(_rootItem, kGlow, 1000),
                 "The glow went away while the same reading kept arriving");
        QVERIFY2(findVisibleTextItem(glow, QStringLiteral("전방 3.3 m")),
                 "The forward distance went away while the same reading kept arriving");

        // Then the sensor stops, for the monitor's own timeout and a margin. The number is
        // PoliceLidarMonitor's default, which PoliceLidarMonitorTest pins; the displays leave it
        // alone, and its QML id is not reachable from here.
        QTest::qWait(kStaleTimeoutMs + 500);

        QVERIFY2(!findVisibleItem(_rootItem, kRing, 0),
                 "A proximity ring stayed up after the readings stopped");
        QVERIFY2(!findVisibleItem(_rootItem, kGlow, 0),
                 "The obstacle glow stayed up after the readings stopped");
        QVERIFY2(!findVisibleTextItem(glow, QStringLiteral("전방 3.3 m")),
                 "The forward distance stayed on screen after the readings stopped");

        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("l_1_after_silence"));
        }
    });
}

void PoliceGuidedActionUITest::_testForwardRingFullscreenSize()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        const bool capture = !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty();

        const double rgForwardOnly[8] = { 3.3, qQNaN(), qQNaN(), qQNaN(),
                                          qQNaN(), qQNaN(), qQNaN(), qQNaN() };
        const QString number = QStringLiteral("3.3 m");
        const auto real = [](QQuickItem *ring, const char *expression) {
            return evaluateOn(ring, QString::fromLatin1(expression)).toDouble();
        };

        // Windowed: exactly as before the cap - strokes a share of the radius, number at the
        // app's default point size.
        injectProximity(mockLink, vehicle, rgForwardOnly);
        QQuickItem *windowRing = nullptr;
        QTRY_VERIFY((windowRing = findForwardRing(_rootItem)) != nullptr);
        QVERIFY2(hasAncestorNamed(windowRing, kForwardWindow), "The ring found is not in the forward window");
        const qreal windowRadius = real(windowRing, "ringRadius");
        QVERIFY(windowRadius > 0);
        QVERIFY(qFuzzyCompare(real(windowRing, "boldStroke"), windowRadius * 0.21));
        QVERIFY(qFuzzyCompare(real(windowRing, "warnStroke"), windowRadius * 0.15));
        QVERIFY(qFuzzyCompare(real(windowRing, "_sectorStroke(0)"), windowRadius * 0.21));
        QQuickItem *windowLabel = nullptr;
        QTRY_VERIFY((windowLabel = findVisibleTextItem(windowRing, number)) != nullptr);
        const qreal defaultPointSize = real(windowRing, "ScreenTools.defaultFontPointSize");
        const qreal windowPointSize = windowLabel->property("font").value<QFont>().pointSizeF();
        QCOMPARE(windowPointSize, defaultPointSize);
        const qreal windowLabelHeight = windowLabel->implicitHeight();
        if (capture) {
            injectProximity(mockLink, vehicle, rgForwardOnly);
            _grab(QStringLiteral("q_0_window"));
            if (QTest::currentTestFailed()) return;
        }

        // Full screen: bigger ring, strokes held under the cap, number grown past windowed.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("primary")));
        injectProximity(mockLink, vehicle, rgForwardOnly);
        QQuickItem *fullRing = nullptr;
        QTRY_VERIFY((fullRing = findForwardRing(_rootItem)) && (real(fullRing, "ringRadius") > windowRadius));
        const qreal cap = real(fullRing, "ScreenTools.defaultFontPixelHeight * 0.6");
        const qreal fullBold = real(fullRing, "boldStroke");
        QVERIFY2(fullBold <= cap,
                 qPrintable(QStringLiteral("Full screen bold stroke %1 is over the cap %2").arg(fullBold).arg(cap)));
        QVERIFY(real(fullRing, "warnStroke") <= (cap * 0.7) + 1e-9);
        QVERIFY(real(fullRing, "_sectorStroke(0)") <= cap);
        QQuickItem *fullLabel = nullptr;
        QTRY_VERIFY((fullLabel = findVisibleTextItem(fullRing, number)) != nullptr);
        const qreal fullPointSize = fullLabel->property("font").value<QFont>().pointSizeF();
        QVERIFY2(fullPointSize > windowPointSize,
                 qPrintable(QStringLiteral("Full screen number is %1 pt, windowed %2 pt").arg(fullPointSize).arg(windowPointSize)));
        QVERIFY(fullLabel->implicitHeight() > windowLabelHeight);
        if (capture) {
            injectProximity(mockLink, vehicle, rgForwardOnly);
            _grab(QStringLiteral("q_1_fullscreen"));
            if (QTest::currentTestFailed()) return;
        }

        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
    });
}

void PoliceGuidedActionUITest::_captureCameraBand()
{
    if (qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
        QSKIP("QGC_SCREENSHOT_DIR is not set");
    }

    _ignorePreexistingQmlWarnings();
    _ignoreDownloadedMissionFontWarnings();

    const int requestedWidth  = qEnvironmentVariableIntValue("QGC_SCREENSHOT_WIDTH");
    const int requestedHeight = qEnvironmentVariableIntValue("QGC_SCREENSHOT_HEIGHT");
    const int width  = requestedWidth  > 0 ? requestedWidth  : kLayoutWidth;
    const int height = requestedHeight > 0 ? requestedHeight : kLayoutHeight;

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this, width, height](QPointer<MockLink> mockLink, Vehicle *vehicle) {
        _window->resize(width, height);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout to capture is not up");

        _grab(QStringLiteral("v2_0_idle"));
        if (QTest::currentTestFailed()) return;

        // Both chips lit, which is the state the narrow zoom window has least room for. Driven
        // on the panel, as in _testStateChipColours; the assignment replaces the binding, and
        // false is what the binding was reading anyway, so the later frames are unaffected.
        QQuickItem *const zoomPanel = findVisibleItem(_rootItem, kZoomPanel, 3000);
        QVERIFY2(zoomPanel, "Zoom camera panel not found");
        QVERIFY(zoomPanel->setProperty("followActive", true));
        QVERIFY(zoomPanel->setProperty("trackingActive", true));
        _grab(QStringLiteral("v2_6_chips_lit"));
        if (QTest::currentTestFailed()) return;
        QVERIFY(zoomPanel->setProperty("followActive", false));
        QVERIFY(zoomPanel->setProperty("trackingActive", false));

        // Full screen and back again, which is where the re-dock can go wrong. Set rather than
        // tapped: the property is what the tap handler assigns, and these two frames are about
        // where the windows land, not about the gesture.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        _grab(QStringLiteral("v2_7_zoom_fullscreen"));
        if (QTest::currentTestFailed()) return;
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        _grab(QStringLiteral("v2_8_after_fullscreen"));
        if (QTest::currentTestFailed()) return;

        // The ring on the forward window, the compass ring and the map edge glow all read the
        // injected frames. The mock's own sweep is off under OptionNone, so an injected reading
        // stands until the monitor's staleness drops it. Armed for the rest of what the frame
        // shows - the bar and the strip - rather than for the displays, which no longer wait
        // for it.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        const double rgForwardClose[8] = { 4, 11, 11, 11, 11, 11, 11, 11 };
        injectProximity(mockLink, vehicle, rgForwardClose);
        _grab(QStringLiteral("v2_1_lidar_front"));
        if (QTest::currentTestFailed()) return;

        // Sector 2 is YAW_90, the aircraft's right side, which is the map's right edge while the
        // aircraft heads north. That edge is the one the camera column stands on.
        const double rgRightClose[8] = { 11, 11, 4, 11, 11, 11, 11, 11 };
        injectProximity(mockLink, vehicle, rgRightClose);
        _grab(QStringLiteral("v2_2_lidar_right"));
        if (QTest::currentTestFailed()) return;

        // Takeoff wants the vehicle on the ground again.
        vehicle->setArmed(false, false);
        QTRY_VERIFY(!vehicle->armed());
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kTakeoffPanel, 5000), "Takeoff panel never appeared");
        _grab(QStringLiteral("v2_3_takeoff"));
        if (QTest::currentTestFailed()) return;

        // The same forward obstacle with the confirm control up. The glow draws at z -1, so the
        // top edge's number has to step below the control instead of sitting under it. Takeoff
        // stays offered while armed, so the control the click above raised is still the one here.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        injectProximity(mockLink, vehicle, rgForwardClose);
        _grab(QStringLiteral("v2_9_lidar_with_confirm"));
        if (QTest::currentTestFailed()) return;

        // Back to what the flying frames were captured in: no obstacle, on the ground.
        const double rgAllFar[8] = { 11, 11, 11, 11, 11, 11, 11, 11 };
        injectProximity(mockLink, vehicle, rgAllFar);
        vehicle->setArmed(false, false);
        QTRY_VERIFY(!vehicle->armed());

        // The mock stops answering while the vehicle climbs, and the flying transition creates
        // QGCPressure, which warns on hosts without a pressure backend.
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Failed to connect to pressure backend")));
        ignoreLogMessage("Utilities.QGCSensors", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Error Initializing Pressure Sensor")));

        // In flight the left strip carries the most entries the mock can put in it: 착륙, 복귀
        // and 일시정지 all appear and 이륙 goes away. That is the height T3's cap has to hold.
        _slide(kTakeoffSlide, 1.1);
        if (QTest::currentTestFailed()) return;
        QVERIFY_TRUE_WAIT(vehicle->flying(), TestTimeout::longMs());
        _grab(QStringLiteral("v2_5_flying_strip"));
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
        _grab(QStringLiteral("v2_4_route_uploaded"));
    });
}

namespace {

/// The model byte on every frame of the fake count link. The controller only asks that the class
/// list and the pushes agree on it.
constexpr quint8 kCountModel = 5;

/// One ObjectCount frame as the module writes it on its private link: mode, model, then \a body.
QByteArray objectCountFrame(SiyiAi::ObjectCountMode mode, const QByteArray &body)
{
    QByteArray data;
    data.append(static_cast<char>(mode));
    data.append(static_cast<char>(kCountModel));
    data.append(body);
    return SiyiLongProtocol::encode(static_cast<quint8>(SiyiAi::PrivateCommandId::ObjectCount), data);
}

/// The detection card's count cells, found by the property only the card's Stat carries.
void collectCountCells(QQuickItem *item, QList<QQuickItem *> &out)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *const child : children) {
        if (child->property("labelWidth").isValid()) {
            out.append(child);
        } else {
            collectCountCells(child, out);
        }
    }
}

/// A cell's dot-and-label row (the child with a spacing) or its count (the child with a contentWidth).
QQuickItem *cellChild(QQuickItem *cell, const char *property)
{
    const QList<QQuickItem *> children = cell->childItems();
    for (QQuickItem *const child : children) {
        if (child->property(property).isValid()) {
            return child;
        }
    }
    return nullptr;
}

} // namespace

void PoliceGuidedActionUITest::_testDetectionCardCells()
{
    _ignorePreexistingQmlWarnings();

    // The card's order. Each count goes out split over four classes of its kind, every tally
    // under the byte's 255 ceiling: a class tally on the ceiling is drawn "255+", not a number.
    const QStringList labels { QStringLiteral("인원"), QStringLiteral("차량"), QStringLiteral("배"),
                               QStringLiteral("연기"), QStringLiteral("화재") };
    const QStringList classNames { QStringLiteral("person"), QStringLiteral("car"), QStringLiteral("boat"),
                                   QStringLiteral("smoke"), QStringLiteral("fire") };
    const QList<int> counts { 123, 456, 789, 100, 999 };
    constexpr int kClassesPerCount = 4;

    QStringList names;
    QByteArray tallies;
    for (int cell = 0; cell < counts.size(); ++cell) {
        for (int part = 0; part < kClassesPerCount; ++part) {
            names.append(classNames[cell]);
            tallies.append(static_cast<char>((counts[cell] / kClassesPerCount) +
                                             ((part < (counts[cell] % kClassesPerCount)) ? 1 : 0)));
        }
    }
    QByteArray classList(1, static_cast<char>(names.size()));
    classList.append(QByteArray(names.size(), '\1'));     // filter mask, every class counted
    classList.append(names.join(QLatin1Char(',')).toLatin1());
    classList.append('\0');
    const QByteArray classListFrame = objectCountFrame(SiyiAi::ObjectCountMode::ClassList, classList);
    const QByteArray pushFrame = objectCountFrame(SiyiAi::ObjectCountMode::Start,
                                                  QByteArray(1, static_cast<char>(names.size())) + tallies);
    // Every kind's first class on the byte's ceiling: all five cells read "255+", the widest
    // thing the card is ever sent.
    QByteArray ceilingTallies(names.size(), '\0');
    for (int cell = 0; cell < counts.size(); ++cell) {
        ceilingTallies[cell * kClassesPerCount] = static_cast<char>(SiyiAi::kObjectCountSaturation);
    }
    const QByteArray ceilingFrame = objectCountFrame(SiyiAi::ObjectCountMode::Start,
                                                     QByteArray(1, static_cast<char>(names.size())) + ceilingTallies);

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [&, this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");

        QHash<QString, QQuickItem *> items;
        for (const QString &name : { kZoomWindow, kThermalWindow, kAiPanel, kTelemetryBar,
                                     kInstrumentPanel, kCameraStrip, kGuidedStrip }) {
            QQuickItem *const item = findVisibleItem(_rootItem, name, 3000);
            QVERIFY2(item, qPrintable(QStringLiteral("%1 is not on screen").arg(name)));
            items.insert(name, item);
        }
        QQuickItem *const card = items[kAiPanel];

        QList<QQuickItem *> cells;
        collectCountCells(card, cells);
        QCOMPARE(cells.size(), labels.size());
        std::sort(cells.begin(), cells.end(), [](QQuickItem *a, QQuickItem *b) {
            return sceneRect(a).x() < sceneRect(b).x();
        });
        for (int i = 0; i < cells.size(); ++i) {
            QCOMPARE(cells[i]->property("label").toString(), labels[i]);
            QVERIFY2(cellChild(cells[i], "spacing") && cellChild(cells[i], "contentWidth"),
                     qPrintable(QStringLiteral("Cell %1 has no label row or no count").arg(labels[i])));
        }

        // One width for all five, and everything in each cell inside it, the count centred.
        const auto checkCells = [&](const QString &state) {
            for (int i = 0; i < cells.size(); ++i) {
                const QRectF cellRect = sceneRect(cells[i]);
                QVERIFY2(qAbs(cellRect.width() - sceneRect(cells[0]).width()) <= 0.5,
                         qPrintable(QStringLiteral("%1: cell %2 is %3 wide, cell %4 is %5")
                                        .arg(state, labels[i]).arg(cellRect.width())
                                        .arg(labels[0]).arg(sceneRect(cells[0]).width())));

                // The count's ink, centred in its own box however wide that box is.
                QQuickItem *const value = cellChild(cells[i], "contentWidth");
                const qreal contentWidth = value->property("contentWidth").toReal();
                const QRectF valueRect = value->mapRectToScene(
                    QRectF((value->width() - contentWidth) / 2, 0, contentWidth, value->height()));
                const QRectF labelRect = sceneRect(cellChild(cells[i], "spacing"));
                const QRectF bounds = cellRect.adjusted(-kEdgeSlack, 0, kEdgeSlack, 0);
                QVERIFY2(contentWidth <= cellRect.width() + kEdgeSlack,
                         qPrintable(QStringLiteral("%1: cell %2 count '%3' is %4 wide in a %5 cell")
                                        .arg(state, labels[i], value->property("text").toString())
                                        .arg(contentWidth).arg(cellRect.width())));
                QVERIFY2((valueRect.left() >= bounds.left()) && (valueRect.right() <= bounds.right()),
                         qPrintable(QStringLiteral("%1: cell %2 count %3 runs outside the cell %4")
                                        .arg(state, labels[i], QDebug::toString(valueRect),
                                             QDebug::toString(cellRect))));
                QVERIFY2((labelRect.left() >= bounds.left()) && (labelRect.right() <= bounds.right()),
                         qPrintable(QStringLiteral("%1: cell %2 label %3 runs outside the cell %4")
                                        .arg(state, labels[i], QDebug::toString(labelRect),
                                             QDebug::toString(cellRect))));
                QVERIFY2(qAbs(valueRect.center().x() - cellRect.center().x()) <= kCentreSlack,
                         qPrintable(QStringLiteral("%1: cell %2 count %3 is not centred in %4")
                                        .arg(state, labels[i], QDebug::toString(valueRect),
                                             QDebug::toString(cellRect))));
                QVERIFY2(!cells[i]->clip() && !value->clip(),
                         qPrintable(QStringLiteral("%1: cell %2 clips").arg(state, labels[i])));
            }
        };

        // The card and a margin around it, cut out of the window at the grab's own pixel scale.
        const QString dir = qEnvironmentVariable("QGC_SCREENSHOT_DIR");
        const auto grabCard = [&, this](const QString &name) {
            if (dir.isEmpty()) {
                return;
            }
            QVERIFY(QDir().mkpath(dir));
            QTest::qWait(kSettleMs);
            const QImage image = _window->grabWindow();
            const qreal scale = static_cast<qreal>(image.width()) / _window->width();
            const QRectF area = sceneRect(card).adjusted(-12, -12, 12, 12);
            const QRect pixels = QRectF(area.topLeft() * scale, area.size() * scale).toAlignedRect();
            QVERIFY(image.copy(pixels).save(QDir(dir).filePath(name + QStringLiteral(".png"))));
        };

        // At rest: the module is not there, every cell a dash.
        for (QQuickItem *const cell : cells) {
            QCOMPARE(cellChild(cell, "contentWidth")->property("text").toString(), QStringLiteral("–"));
        }
        grabCard(QStringLiteral("detection_card_0_rest"));
        if (QTest::currentTestFailed()) return;
        checkCells(QStringLiteral("at rest"));
        if (QTest::currentTestFailed()) return;
        const QRectF restCard = sceneRect(card);

        // A fake module on loopback at the private port, answering nothing and pushing the same
        // tallies twice a second, well inside the controller's count timeout.
        QTcpServer server;
        QVERIFY2(server.listen(QHostAddress::LocalHost, SiyiAi::kPrivatePort),
                 qPrintable(QStringLiteral("Cannot stand in for the module's count port: %1").arg(server.errorString())));
        SiyiAiController *const ai = SiyiAiController::instance();
        Fact *const aiAddress = SettingsManager::instance()->siyiCameraSettings()->aiIpAddress();
        const QVariant savedAddress = aiAddress->rawValue();
        const auto restoreModule = qScopeGuard([ai, aiAddress, savedAddress] {
            ai->stop();
            aiAddress->setRawValue(savedAddress);
        });
        aiAddress->setRawValue(QStringLiteral("127.0.0.1"));
        ai->start();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        QTcpSocket *const module = server.nextPendingConnection();
        (void) module->write(classListFrame);
        (void) module->write(pushFrame);
        QByteArray pushing = pushFrame;
        QTimer pusher;
        pusher.setInterval(500);
        (void) connect(&pusher, &QTimer::timeout, module, [module, &pushing] { (void) module->write(pushing); });
        pusher.start();

        QTRY_VERIFY_WITH_TIMEOUT(ai->countsValid(), 5000);
        for (int i = 0; i < cells.size(); ++i) {
            QTRY_COMPARE(cellChild(cells[i], "contentWidth")->property("text").toString(),
                         QString::number(counts[i]));
        }
        grabCard(QStringLiteral("detection_card_1_counts"));
        if (QTest::currentTestFailed()) return;
        if (!dir.isEmpty()) {
            _grab(QStringLiteral("detection_card_2_screen"));
            if (QTest::currentTestFailed()) return;
        }
        QTest::qWait(kSettleMs);
        checkCells(QStringLiteral("three-digit counts"));
        if (QTest::currentTestFailed()) return;
        QVERIFY2(sceneRect(card) == restCard,
                 qPrintable(QStringLiteral("Detection card moved from %1 to %2 when the counts came in")
                                .arg(QDebug::toString(restCard), QDebug::toString(sceneRect(card)))));

        // Three-digit counts at the count face's own size. A value may shrink to fit its cell, but
        // only one wider than any count: "999", the widest, is as tall and as wide as the face
        // draws it unfitted, which is how every count was drawn before the cells could fit one.
        for (int i = 0; i < cells.size(); ++i) {
            QQuickItem *const value = cellChild(cells[i], "contentWidth");
            const QString text = value->property("text").toString();
            const QFontMetricsF metrics(value->property("font").value<QFont>());
            const qreal height = value->property("contentHeight").toReal();
            const qreal width = value->property("contentWidth").toReal();
            QVERIFY2((qAbs(height - metrics.height()) <= 0.5) && (qAbs(width - metrics.horizontalAdvance(text)) <= 0.5),
                     qPrintable(QStringLiteral("Cell %1 draws '%2' %3 x %4, the face at its own size is %5 x %6")
                                    .arg(labels[i], text).arg(width).arg(height)
                                    .arg(metrics.horizontalAdvance(text)).arg(metrics.height())));
        }

        // The byte's ceiling marker, wider than any count, still inside every cell.
        pushing = ceilingFrame;
        (void) module->write(pushing);
        for (int i = 0; i < cells.size(); ++i) {
            QTRY_COMPARE(cellChild(cells[i], "contentWidth")->property("text").toString(),
                         QStringLiteral("255+"));
        }
        grabCard(QStringLiteral("detection_card_3_ceiling"));
        if (QTest::currentTestFailed()) return;
        checkCells(QStringLiteral("ceiling marker"));
        if (QTest::currentTestFailed()) return;
        QVERIFY2(sceneRect(card) == restCard,
                 qPrintable(QStringLiteral("Detection card moved from %1 to %2 on the ceiling marker")
                                .arg(QDebug::toString(restCard), QDebug::toString(sceneRect(card)))));

        // The bar under the card shares its width; it stays off the camera column as well.
        const QRectF barRect = sceneRect(items[kTelemetryBar]).adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
        for (const QString &window : { kZoomWindow, kThermalWindow }) {
            QVERIFY2(!barRect.intersects(sceneRect(items[window])),
                     qPrintable(QStringLiteral("Telemetry bar %1 overlaps %2 %3")
                                    .arg(QDebug::toString(sceneRect(items[kTelemetryBar])), window,
                                         QDebug::toString(sceneRect(items[window])))));
        }

        // The card, at the width the cells gave it, off everything around it.
        const QRectF cardRect = sceneRect(card).adjusted(kEdgeSlack, kEdgeSlack, -kEdgeSlack, -kEdgeSlack);
        const QList<QPair<QString, QString>> neighbours {
            { QStringLiteral("zoom window"),        kZoomWindow },
            { QStringLiteral("thermal window"),     kThermalWindow },
            { QStringLiteral("telemetry bar"),      kTelemetryBar },
            { QStringLiteral("instrument panel"),   kInstrumentPanel },
            { QStringLiteral("camera tool strip"),  kCameraStrip },
            { QStringLiteral("guided tool strip"),  kGuidedStrip },
        };
        for (const auto &neighbour : neighbours) {
            const QRectF other = sceneRect(items[neighbour.second]);
            QVERIFY2(!cardRect.intersects(other),
                     qPrintable(QStringLiteral("Detection card %1 overlaps the %2 %3")
                                    .arg(QDebug::toString(sceneRect(card)), neighbour.first,
                                         QDebug::toString(other))));
        }

        // A visible gap, not just no overlap, between the card and the 줌 and 열상 column.
        for (const QString &window : { kZoomWindow, kThermalWindow }) {
            const qreal gap = sceneRect(items[window]).left() - sceneRect(card).right();
            QVERIFY2(gap >= 3.0, qPrintable(QStringLiteral("Detection card ends %1 px before %2").arg(gap).arg(window)));
        }
    });
}

void PoliceGuidedActionUITest::_testStockPhotoVideoOnlyWithPodOff()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QVERIFY(vehicle);
        QVERIFY2(vehicle->cameraManager(), "The mock vehicle has no camera manager, so the loader proves nothing");
        QQuickItem *const loader = findVisibleItem(_rootItem, QStringLiteral("photoVideoLoader"), 5000);
        QVERIFY2(loader, "photoVideoLoader not found");

        Fact *const podEnabled = SettingsManager::instance()->siyiCameraSettings()->enabled();
        QVERIFY(podEnabled);
        const QVariant saved = podEnabled->rawValue();
        const auto restore = qScopeGuard([podEnabled, saved] { podEnabled->setRawValue(saved); });

        podEnabled->setRawValue(true);
        QTRY_VERIFY_WITH_TIMEOUT(!loader->property("item").value<QObject *>(), 3000);
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            _grab(QStringLiteral("stock_record_hidden"));
            if (QTest::currentTestFailed()) return;
        }

        podEnabled->setRawValue(false);
        QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject *>(), 3000);

        podEnabled->setRawValue(true);
        QTRY_VERIFY_WITH_TIMEOUT(!loader->property("item").value<QObject *>(), 3000);
    });
}

void PoliceGuidedActionUITest::_testMapSwap()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const zoomWindow = findVisibleItem(_rootItem, kZoomWindow, 3000);
        QVERIFY2(zoomWindow, "Zoom window not found");
        QQuickItem *const forwardWindow = findVisibleItem(_rootItem, kForwardWindow, 3000);
        QVERIFY2(forwardWindow, "Forward window not found");
        QQuickItem *const thermalWindow = findVisibleItem(_rootItem, kThermalWindow, 3000);
        QVERIFY2(thermalWindow, "Thermal window not found");
        QQuickItem *const panel = findVisibleItem(_rootItem, kZoomPanel, 3000);
        QVERIFY2(panel, "Zoom camera panel not found");
        QQuickItem *const topBar = findVisibleItem(_rootItem, kTopBar, 3000);
        QVERIFY2(topBar, "Top bar not found");
        QQuickItem *const rail = findVisibleItem(_rootItem, kCameraStrip, 3000);
        QVERIFY2(rail, "Camera rail not found");
        QQuickItem *const guided = findVisibleItem(_rootItem, kGuidedStrip, 3000);
        QVERIFY2(guided, "Guided tool strip not found");
        QQuickItem *const card = findVisibleItem(_rootItem, kAiPanel, 3000);
        QVERIFY2(card, "Detection card not found");
        const QRectF cardBefore = sceneRect(card);
        const bool capture = !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty();

        const auto expanded = [dashboard] { return dashboard->property("expandedPanel").toString(); };
        const auto chipReads = [](QQuickItem *window, const QString &text) {
            QQuickItem *const chip = findVisibleItem(window, kTitleChip, 0);
            return chip && findVisibleTextItem(chip, text);
        };
        const auto clickAt = [this](const QPointF &point) {
            QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
            QTest::qWait(kSettleMs);
        };
        // QQuickItem::childAt goes by declaration order and ignores z; this goes by paint order.
        const auto topChildAt = [dashboard](const QPointF &point) {
            QQuickItem *top = nullptr;
            for (QQuickItem *const child : dashboard->childItems()) {
                if (child->isVisible() && child->contains(dashboard->mapToItem(child, point)) &&
                        (!top || child->z() >= top->z())) {
                    top = child;
                }
            }
            return top;
        };
        const auto near = [](const QRectF &a, const QRectF &b) {
            return qAbs(a.left() - b.left()) <= kEdgeSlack && qAbs(a.top() - b.top()) <= kEdgeSlack &&
                   qAbs(a.width() - b.width()) <= kEdgeSlack && qAbs(a.height() - b.height()) <= kEdgeSlack;
        };

        // 1. Windowed: the zoom window names itself, and there is no map copy anywhere.
        QVERIFY2(chipReads(zoomWindow, QStringLiteral("줌")), "The zoom window chip does not read 줌");
        QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("policeMapPip"), 0), "The map copy is up with no camera big");
        // Windowed, the state chips sit in the zoom window's top right corner.
        QQuickItem *const stateChips = findVisibleItem(panel, kStateChips, 3000);
        QVERIFY2(stateChips, "Zoom state chips are not on screen");
        {
            const QRectF chips = sceneRect(stateChips);
            const QRectF window = sceneRect(zoomWindow);
            QVERIFY2(window.contains(chips) && chips.center().x() > window.center().x() &&
                         chips.center().y() < window.center().y(),
                     qPrintable(QStringLiteral("Windowed state chips %1 are not in the top right of %2")
                                    .arg(QDebug::toString(chips), QDebug::toString(window))));
        }

        // 2. A tap on the zoom window makes it the big picture.
        clickAt(sceneRect(zoomWindow).center());
        QCOMPARE(expanded(), QStringLiteral("secondary"));

        // 3. Everything under the top bar, full width.
        const qreal barBottom = sceneRect(topBar).bottom();
        const QRectF wanted(0, barBottom, _window->width(), _window->height() - barBottom);
        QVERIFY2(near(sceneRect(panel), wanted),
                 qPrintable(QStringLiteral("Big zoom picture is at %1, wanted %2")
                                .arg(QDebug::toString(sceneRect(panel)), QDebug::toString(wanted))));

        // 4. The zoom window stays, now holding the map as 지도.
        QVERIFY2(zoomWindow->isVisible(), "The zoom window went with its picture");
        QQuickItem *const pip = findVisibleItem(_rootItem, QStringLiteral("policeMapPip"), 1000);
        QVERIFY2(pip, "The map copy is not on screen");
        QVERIFY2(hasAncestorNamed(pip, kZoomWindow), "The map copy is not in the vacated zoom window");
        QVERIFY2(chipReads(zoomWindow, QStringLiteral("지도")), "The vacated window chip does not read 지도");
        QVERIFY2(!findVisibleTextItem(zoomWindow, QStringLiteral("줌")), "줌 is still written in the vacated window");

        // 5. The chrome stays where it was, over the picture.
        QVERIFY2(topBar->isVisible(), "Top bar went under the big picture");
        QVERIFY2(guided->isVisible(), "Guided tool strip went under the big picture");
        QVERIFY2(rail->isVisible(), "Camera rail went under the big picture");
        QVERIFY2(findVisibleTextItem(rail, QStringLiteral("추적해제")), "The rail lost its 추적해제 button");
        QVERIFY2(forwardWindow->isVisible(), "Forward window went with the swap");
        QVERIFY2(thermalWindow->isVisible(), "Thermal window went with the swap");
        QVERIFY2(sceneRect(card) == cardBefore,
                 qPrintable(QStringLiteral("Detection card moved to %1 from %2")
                                .arg(QDebug::toString(sceneRect(card)), QDebug::toString(cardBefore))));

        // 6. The rail is drawn over the picture, not under it.
        const QPointF railCentre = dashboard->mapFromScene(sceneRect(rail).center());
        QQuickItem *const onRail = topChildAt(railCentre);
        QVERIFY2(onRail, "Nothing at the camera rail's centre");
        QVERIFY2(onRail->objectName() != QStringLiteral("policeBigPicture"), "The big picture is over the camera rail");

        // 7. The big picture's name chip is clear of the guided strip.
        QQuickItem *const bigName = findVisibleTextItem(panel, QStringLiteral("줌"));
        QVERIFY2(bigName, "The big zoom picture has no 줌 name chip");
        QVERIFY2(sceneRect(bigName).left() > sceneRect(guided).right(),
                 qPrintable(QStringLiteral("줌 chip %1 is under the guided strip %2")
                                .arg(QDebug::toString(sceneRect(bigName)), QDebug::toString(sceneRect(guided)))));

        // 7b. Big, the state chips sit side by side just under the camera rail's handle, right
        // edges flush, instead of on the top row beside it.
        {
            QQuickItem *const handle = findVisibleItem(_rootItem, QStringLiteral("policeCameraStripHandle"), 3000);
            QVERIFY2(handle, "Camera rail handle not found");
            const QRectF chips = sceneRect(stateChips);
            const QRectF grip = sceneRect(handle);
            QVERIFY2(qAbs(chips.right() - grip.right()) <= 2 && chips.top() >= grip.bottom(),
                     qPrintable(QStringLiteral("Big state chips %1 are not under the rail handle %2")
                                    .arg(QDebug::toString(chips), QDebug::toString(grip))));
            const QList<QQuickItem *> row = stateChips->childItems();
            QVERIFY2(row.size() == 2 && qAbs(sceneRect(row.at(0)).top() - sceneRect(row.at(1)).top()) <= 1,
                     "The big state chips are not side by side");
        }

        if (capture) {
            SiyiCameraController *const camera = SiyiCameraController::instance();
            QVERIFY(camera);
            const bool savedLaser = camera->laserEnabled();
            camera->setLaserEnabled(true);
            _grab(QStringLiteral("swap_zoom_big"));
            camera->setLaserEnabled(savedLaser);
            if (QTest::currentTestFailed()) return;
        }

        // 8. A tap on the big picture keeps it big: it is the target-pick surface.
        const QRectF bigRect = sceneRect(panel);
        const QPointF onPicture(bigRect.center().x(), bigRect.top() + bigRect.height() * 0.4);
        const QPointF onPictureLocal = dashboard->mapFromScene(onPicture);
        QQuickItem *const underTap = topChildAt(onPictureLocal);
        QVERIFY2(underTap && underTap->objectName() == QStringLiteral("policeBigPicture"),
                 "The tap point on the big picture is covered by chrome");
        clickAt(onPicture);
        QCOMPARE(expanded(), QStringLiteral("secondary"));

        // 9. A tap on 지도 swaps back.
        clickAt(sceneRect(zoomWindow).center());
        QCOMPARE(expanded(), QString());
        QVERIFY2(chipReads(zoomWindow, QStringLiteral("줌")), "The zoom window chip does not read 줌 again");
        QVERIFY2(near(sceneRect(panel), sceneRect(zoomWindow)),
                 qPrintable(QStringLiteral("Zoom panel came back at %1, not in its window %2")
                                .arg(QDebug::toString(sceneRect(panel)), QDebug::toString(sceneRect(zoomWindow)))));

        // 10. Thermal big, then the forward window straight from there.
        clickAt(sceneRect(thermalWindow).center());
        QCOMPARE(expanded(), QStringLiteral("shared"));
        if (capture) {
            _grab(QStringLiteral("swap_thermal_big"));
            if (QTest::currentTestFailed()) return;
        }
        clickAt(sceneRect(forwardWindow).center());
        QCOMPARE(expanded(), QStringLiteral("primary"));
        QVERIFY2(chipReads(thermalWindow, QStringLiteral("열상")), "The thermal window chip does not read 열상 again");
        QVERIFY2(chipReads(forwardWindow, QStringLiteral("지도")), "The vacated forward window chip does not read 지도");

        // 11. Escape swaps back.
        QTest::keyClick(_window, Qt::Key_Escape);
        QTest::qWait(kSettleMs);
        QCOMPARE(expanded(), QString());
    });
}

void PoliceGuidedActionUITest::_testThermalTemperatureReadout()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(kLayoutWidth, kLayoutHeight);
        QTest::qWait(kSettleMs);

        const bool capture = !qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty();
        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 5000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");
        QQuickItem *const window = findVisibleItem(_rootItem, kThermalWindow, 5000);
        QVERIFY2(window, "Thermal window not found");
        QQuickItem *const chip = window->findChild<QQuickItem *>(kTitleChip);
        QVERIFY2(chip, "Thermal window has no name chip");
        QQuickItem *const temps = chip->findChild<QQuickItem *>(QStringLiteral("cameraWindowExtraDetail"));
        QVERIFY2(temps, "Thermal name chip has no temperature reading");
        QVERIFY2(!temps->isVisible(), "Temperatures are up with no pod answering");

        // A fake pod on loopback, the controller pointed at it the way the controller test does.
        // Test builds never init the controller, so it is started here the way the detection card
        // test starts the AI one.
        QUdpSocket pod;
        QVERIFY(pod.bind(QHostAddress::LocalHost, 0));
        SiyiCameraController *const camera = SiyiCameraController::instance();
        SiyiCameraSettings *const settings = SettingsManager::instance()->siyiCameraSettings();
        const QVariant savedAddress = settings->ipAddress()->rawValue();
        const QVariant savedPort    = settings->port()->rawValue();
        const auto restorePod = qScopeGuard([camera, settings, savedAddress, savedPort] {
            camera->stop();
            settings->ipAddress()->setRawValue(savedAddress);
            settings->port()->setRawValue(savedPort);
        });
        settings->ipAddress()->setRawValue(QStringLiteral("127.0.0.1"));
        settings->port()->setRawValue(pod.localPort());
        camera->start();

        QHostAddress controllerAddress;
        quint16 controllerPort = 0;
        QTRY_VERIFY_WITH_TIMEOUT(pod.hasPendingDatagrams(), 5000);
        QByteArray probe(static_cast<int>(pod.pendingDatagramSize()), Qt::Uninitialized);
        QVERIFY(pod.readDatagram(probe.data(), probe.size(), &controllerAddress, &controllerPort) > 0);

        // GetTempFullImage: max and min in hundredths of a degree, then the two pixel positions.
        const auto le16 = [](QByteArray &data, int value) {
            data.append(static_cast<char>(value & 0xFF));
            data.append(static_cast<char>((value >> 8) & 0xFF));
        };
        QByteArray tempReply;
        QByteArray laserReply;
        QByteArray identityReply;
        const auto setTemps = [&](int maxCenti, int minCenti) {
            QByteArray data;
            le16(data, maxCenti);
            le16(data, minCenti);
            for (int i = 0; i < 4; ++i) {
                le16(data, 100 + i);
            }
            tempReply = SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiProtocol::CommandId::GetTempFullImage), data);
        };
        setTemps(4250, 1820);

        // Twice a second, well inside the controller's three second timeout.
        QTimer answering;
        answering.setInterval(500);
        (void) connect(&answering, &QTimer::timeout, &pod, [&] {
            while (pod.hasPendingDatagrams()) {
                (void) pod.receiveDatagram();
            }
            (void) pod.writeDatagram(tempReply, controllerAddress, controllerPort);
            if (!laserReply.isEmpty()) {
                (void) pod.writeDatagram(laserReply, controllerAddress, controllerPort);
            }
            if (!identityReply.isEmpty()) {
                (void) pod.writeDatagram(identityReply, controllerAddress, controllerPort);
            }
        });
        answering.start();

        const QString kFirst = QStringLiteral("최고 42.5 °C  최저 18.2 °C");
        QTRY_COMPARE_WITH_TIMEOUT(temps->property("text").toString(), kFirst, 5000);
        QVERIFY2(temps->isVisible(), "Temperatures are not up with the pod answering");
        const auto checkInside = [&] {
            QVERIFY2(sceneRect(chip).contains(sceneRect(temps)), "Temperatures hang out of the name chip");
            QVERIFY2(sceneRect(window).contains(sceneRect(chip)),
                     qPrintable(QStringLiteral("Name chip %1 runs past the thermal window %2")
                                    .arg(QDebug::toString(sceneRect(chip)), QDebug::toString(sceneRect(window)))));
        };
        checkInside();
        if (QTest::currentTestFailed()) return;
        if (capture) {
            _grab(QStringLiteral("thermal_temp_0_window"));
            if (QTest::currentTestFailed()) return;
        }

        // The laser reads out on neither window bar: only the big zoom picture's pill carries it.
        const std::function<QQuickItem *(QQuickItem *)> findLrfText = [&findLrfText](QQuickItem *item) -> QQuickItem * {
            if (item->isVisible() && item->property("text").toString().startsWith(QStringLiteral("LRF"))) {
                return item;
            }
            for (QQuickItem *const child : item->childItems()) {
                if (QQuickItem *const found = findLrfText(child)) {
                    return found;
                }
            }
            return nullptr;
        };
        QQuickItem *const zoomWindow = findVisibleItem(_rootItem, kZoomWindow, 5000);
        QVERIFY2(zoomWindow, "Zoom window not found");
        QQuickItem *const zoomChip = zoomWindow->findChild<QQuickItem *>(kTitleChip);
        QVERIFY2(zoomChip, "Zoom window has no name chip");
        QByteArray range;
        le16(range, 1234);
        laserReply = SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiProtocol::CommandId::ReadRangefinder), range);
        QTRY_VERIFY_WITH_TIMEOUT(camera->rangefinderAvailable(), 5000);
        QTest::qWait(kSettleMs);
        QVERIFY2(!findLrfText(chip), "The thermal window bar shows the laser range");
        QVERIFY2(!findLrfText(zoomChip), "The zoom window bar shows the laser range");
        QCOMPARE(temps->property("text").toString(), kFirst);
        QVERIFY2(temps->isVisible(), "Temperatures went with the laser reading in");
        checkInside();
        if (QTest::currentTestFailed()) return;

        // Live: a new reading replaces the old one, below zero included.
        setTemps(4300, -1050);
        QTRY_COMPARE_WITH_TIMEOUT(temps->property("text").toString(), QStringLiteral("최고 43.0 °C  최저 -10.5 °C"), 5000);
        QTest::qWait(kSettleMs);
        checkInside();
        if (QTest::currentTestFailed()) return;
        if (capture) {
            _grab(QStringLiteral("thermal_temp_1_with_lrf"));
            if (QTest::currentTestFailed()) return;
        }

        // Full screen: the window's chip goes with the window, the panel's name chip carries it.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("shared")));
        QQuickItem *const fullTemps = findVisibleItem(_rootItem, QStringLiteral("policeCameraTitleDetail"), 3000);
        QVERIFY2(fullTemps, "Temperatures are not up with the thermal window full screen");
        QCOMPARE(fullTemps->property("text").toString(), QStringLiteral("최고 43.0 °C  최저 -10.5 °C"));
        QQuickItem *const fullTitle = findVisibleTextItem(fullTemps->parentItem(), QStringLiteral("열상"));
        QVERIFY2(fullTitle, "Full screen name chip does not carry the window name next to the temperatures");
        QVERIFY2(sceneRect(fullTitle).right() < sceneRect(fullTemps).left(), "Temperatures are not after the name");
        QQuickItem *thermalPanel = fullTemps;
        while (thermalPanel && !thermalPanel->property("panelTitle").isValid()) {
            thermalPanel = thermalPanel->parentItem();
        }
        QVERIFY2(thermalPanel, "The full screen name chip is not inside a camera panel");
        QVERIFY2(!findLrfText(thermalPanel), "The big thermal picture shows the laser range");
        if (capture) {
            _grab(QStringLiteral("thermal_temp_2_fullscreen"));
            if (QTest::currentTestFailed()) return;
        }

        // The pod falls silent: the reading goes rather than sitting there stale.
        answering.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!fullTemps->isVisible(), 6000);
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
        QVERIFY2(!temps->isVisible(), "Temperatures stayed up on the window after the pod fell silent");

        // The camera panel with a ZT30 answering: the laser switch sits under the LRF readout.
        identityReply = SiyiProtocol::encodeRaw(static_cast<quint8>(SiyiProtocol::CommandId::AcquireHardwareId), QByteArray("7A"));
        answering.start();
        QTRY_VERIFY_WITH_TIMEOUT(camera->isZT30(), 5000);
        QQuickItem *const strip = findVisibleItem(_rootItem, kCameraStrip, 5000);
        QVERIFY2(strip, "Camera tool strip is not on screen");
        QList<QQuickItem *> entries;
        collectStripEntries(strip, entries);
        QQuickItem *cameraEntry = nullptr;
        for (QQuickItem *const entry : entries) {
            if (entry->property("text").toString() == QStringLiteral("카메라")) {
                cameraEntry = entry;
            }
        }
        QVERIFY2(cameraEntry, "카메라 is not on the camera grid");
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier,
                          cameraEntry->mapToScene(QPointF(cameraEntry->width() / 2, cameraEntry->height() / 2)).toPoint());
        QQuickItem *const laserSwitch = findVisibleItem(_rootItem, QStringLiteral("siyiLaserSwitch"), 5000);
        QVERIFY2(laserSwitch, "The camera panel has no laser switch with a ZT30 answering");
        QCOMPARE(laserSwitch->property("checked").toBool(), camera->laserEnabled());

        // The panel scrolls inside its drop panel; bring the switch into view and check it is whole.
        QQuickItem *flickable = laserSwitch->parentItem();
        while (flickable && !flickable->property("contentY").isValid()) {
            flickable = flickable->parentItem();
        }
        QVERIFY2(flickable, "The camera panel is not inside a flickable");
        const qreal switchY = laserSwitch->mapToItem(flickable->property("contentItem").value<QQuickItem *>(), QPointF(0, 0)).y();
        const qreal maxY = qMax(0.0, flickable->property("contentHeight").toReal() - flickable->height());
        QVERIFY(flickable->setProperty("contentY", qBound(0.0, switchY - flickable->height() / 2, maxY)));
        QTest::qWait(kSettleMs);
        QVERIFY2(sceneRect(flickable).contains(sceneRect(laserSwitch)),
                 qPrintable(QStringLiteral("Laser switch %1 is clipped by the panel %2")
                                .arg(QDebug::toString(sceneRect(laserSwitch)), QDebug::toString(sceneRect(flickable)))));
        if (capture) {
            _grab(QStringLiteral("lrf_switch_panel"));
            if (QTest::currentTestFailed()) return;
        }
        QTest::keyClick(_window, Qt::Key_Escape);
    });
}
