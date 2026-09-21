#include "PoliceGuidedActionUITest.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QMetaMethod>
#include <QtCore/QRect>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QtMath>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fact.h"
#include "FactGroup.h"
#include "FactMetaData.h"
#include "FlyViewSettings.h"
#include "MAVLinkLib.h"
#include "MockLink.h"
#include "SettingsManager.h"
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

const QString kSlider    = QStringLiteral("guidedValueSlider");
const QString kDashboard = QStringLiteral("policeDroneDashboard");

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
/// Full screen only: the small map in the corner and the way back out.
const QString kMapPip          = QStringLiteral("policeFullscreenMapPip");
const QString kHint            = QStringLiteral("policeFullscreenHint");
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

/// The release button on the picture, the only 추적해제 reachable while a camera is full screen.
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

/// The fraction of the screen width the full screen map copy takes.
constexpr qreal kPipOfWidth = 0.27;

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
/// a single close arc can only be had by feeding the fact group crafted DISTANCE_SENSOR messages.
/// Min 40 cm, max 12 m, a TF Mini's own range.
void injectProximity(Vehicle *vehicle, const double (&metresPerSector)[8])
{
    FactGroup *const group = vehicle->distanceSensorFactGroup();
    for (int sector = 0; sector < 8; sector++) {
        mavlink_message_t msg{};
        const float quaternion[4]{};
        (void) mavlink_msg_distance_sensor_pack_chan(
            vehicle->id(), MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0, &msg,
            0,                                                      // time_boot_ms
            40,                                                     // min_distance cm
            1200,                                                   // max_distance cm
            static_cast<uint16_t>(metresPerSector[sector] * 100.0),  // current_distance cm
            MAV_DISTANCE_SENSOR_LASER,
            static_cast<uint8_t>(sector),                           // id
            static_cast<uint8_t>(sector),                           // orientation: NONE..YAW_315 are 0..7
            255, 0.0f, 0.0f, quaternion, 0);
        group->handleMessage(vehicle, msg);
    }
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
    // in the guided slots as well as the layout ones, none of which instantiate that file. The
    // sentence is localised, so the pattern matches the file rather than the words.
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/PlanView/HomePositionMapVisual\\.qml: ")));
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

void PoliceGuidedActionUITest::_grab(const QString &name)
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

        QQuickItem *const button = findVisibleItem(panel, kTrackCancel, 3000);
        QVERIFY2(button, "The release button is not on the panel");

        // It has to be hittable with a glove, and it has to be on the picture it belongs to. The
        // floor is read out of the same singleton the panel sizes itself from rather than pinned
        // to a number here, which would only pin this host's font metrics.
        QQmlExpression touchFloor(qmlContext(panel), panel, QStringLiteral("ScreenTools.minTouchPixels"));
        const QVariant floorValue = touchFloor.evaluate();
        QVERIFY2(!touchFloor.hasError(), qPrintable(touchFloor.error().toString()));
        QVERIFY2(button->height() >= floorValue.toReal(),
                 qPrintable(QStringLiteral("The release button is %1 px high, under the %2 px touch floor")
                                .arg(button->height())
                                .arg(floorValue.toReal())));
        const QRectF panelRect  = sceneRect(panel);
        const QRectF buttonRect = sceneRect(button);
        QVERIFY2(panelRect.contains(buttonRect),
                 qPrintable(QStringLiteral("The release button %1 hangs outside its panel %2")
                                .arg(QDebug::toString(buttonRect), QDebug::toString(panelRect))));

        const QMetaMethod cancelled = signalByName(panel, "trackCancelRequested");
        QVERIFY2(cancelled.isValid(), "Panel has no trackCancelRequested signal");
        QSignalSpy cancelSpy(panel, cancelled);
        QVERIFY(cancelSpy.isValid());

        const QMetaMethod picked = signalByName(panel, "targetBoxPicked");
        QVERIFY2(picked.isValid(), "Panel has no targetBoxPicked signal");
        QSignalSpy boxSpy(panel, picked);
        QVERIFY(boxSpy.isValid());

        const QPoint on(qFloor(buttonRect.center().x()), qFloor(buttonRect.center().y()));
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, on);
        QTest::qWait(kSettleMs);
        QCOMPARE(cancelSpy.count(), 1);
        QVERIFY2(boxSpy.isEmpty(), "Clicking the release button also handed the module a box");
        // The tap that fills the screen with this camera runs on the same panel, off a passive
        // grab that the button's own grab does not take away.
        QCOMPARE(dashboard->property("expandedPanel").toString(), QString());

        // Greyed, which is what no target looks like: the click has to do nothing at all.
        QVERIFY(panel->setProperty("trackCancelEnabled", false));
        QTest::qWait(kSettleMs);
        cancelSpy.clear();
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, on);
        QTest::qWait(kSettleMs);
        QVERIFY2(cancelSpy.isEmpty(), "A click on the greyed release button still asked for a cancel");
        QVERIFY2(boxSpy.isEmpty(), "A click on the greyed release button handed the module a box");
        QCOMPARE(dashboard->property("expandedPanel").toString(), QString());

        // A drag off the button is the button being pressed, not a box being drawn. The drag
        // handler is allowed to take the grab off an item, so this is not free.
        QVERIFY(panel->setProperty("trackCancelEnabled", true));
        QTest::qWait(kSettleMs);
        cancelSpy.clear();
        const QPointF to = panelRect.topLeft() + QPointF(panelRect.width() * 0.8, panelRect.height() * 0.3);
        _dragPointer({ buttonRect.center(), to });
        if (QTest::currentTestFailed()) return;
        QVERIFY2(boxSpy.isEmpty(), "A drag that started on the release button handed the module a box");
        QCOMPARE(dashboard->property("expandedPanel").toString(), QString());

        // Full screen is the case the button exists for: the camera rail that carries the same
        // command is at z 2, under the fullscreen layer at 20. Set rather than tapped - this
        // frame is about what is reachable, not about the gesture.
        if (!qEnvironmentVariable("QGC_SCREENSHOT_DIR").isEmpty()) {
            QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
            QTest::qWait(kSettleMs);
            QQuickItem *const fullButton = findVisibleItem(panel, kTrackCancel, 3000);
            QVERIFY2(fullButton, "The release button went away full screen");
            _grab(QStringLiteral("drag_2_fullscreen_cancel"));
            if (QTest::currentTestFailed()) return;
            QVERIFY(dashboard->setProperty("expandedPanel", QString()));
            QTest::qWait(kSettleMs);
        }
    });
}

void PoliceGuidedActionUITest::_testTakeoffRaisesConfirmControl()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        // Checked first: a control that is always up would pass the positive case below for
        // the wrong reason.
        QVERIFY2(!findVisibleItem(_rootItem, kConfirmButton, 1000),
                 "Confirm control was already up before any action was requested");

        // A disabled takeoff entry would leave the control down too, and would look exactly
        // like the regression this test is for.
        QVERIFY(verifyEnabled(kTakeoffButton, true, QStringLiteral("before pressing takeoff")));
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");

        QQuickItem *const confirmButton = findVisibleItem(_rootItem, kConfirmButton, 5000);
        QVERIFY2(confirmButton, "Confirm control never appeared after pressing takeoff");

        // findVisibleItem walks isVisible(), which is false while any ancestor is hidden, so
        // reaching here already rules out the toolbar-parented control. The size check catches
        // the other way it can be on screen and unusable.
        QVERIFY2((confirmButton->width() > 0) && (confirmButton->height() > 0),
                 "Confirm button is visible but has no area to press");

        // Two instances of the stock component exist and both claim guidedController.confirmDialog
        // on completion. This says the one on screen is ours.
        QVERIFY2(hasAncestorNamed(confirmButton, kConfirmHost),
                 "Confirm control on screen is not the police instance");
    });
}

void PoliceGuidedActionUITest::_testTakeoffAltitudeSliderIsOnTop()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000),
                 "Confirm control never appeared after pressing takeoff");

        // PX4 multirotors take off to a chosen altitude, so confirmAction raises the slider
        // alongside the control.
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 3000);
        QVERIFY2(slider, "Takeoff altitude slider never became visible");

        QQuickItem *const dashboard = findVisibleItem(_rootItem, kDashboard, 3000);
        QVERIFY2(dashboard, "Police dashboard not found - the layout under test is not up");

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
    });
}

void PoliceGuidedActionUITest::_testHoldConfirmSendsTakeoff()
{
    _ignorePreexistingQmlWarnings();

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> mockLink, Vehicle *vehicle) {
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
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000),
                 "Confirm control never appeared after pressing takeoff");

        mockLink->clearReceivedMavCommandCounts();

        // GuidedActionConfirm hands executeAction the slider reading and the controller converts
        // it to metres. Read it here, before the hold takes the slider away, so param7 can be
        // checked against the altitude that was on screen.
        QQuickItem *const slider = findVisibleItem(_rootItem, kSlider, 3000);
        QVERIFY2(slider, "Takeoff altitude slider never became visible");
        QVariant sliderOutput;
        QVERIFY2(QMetaObject::invokeMethod(slider, "getOutputValue", Q_RETURN_ARG(QVariant, sliderOutput)),
                 "Could not read the slider output value");
        const double sliderMeters = FactMetaData::appSettingsVerticalDistanceUnitsToMeters(sliderOutput).toDouble();
        QVERIFY2(sliderMeters > 0, "Slider offered a takeoff altitude of zero");

        // PX4FirmwarePlugin::guidedModeTakeoff builds param7 as this AMSL plus the slider metres.
        const double vehicleAmslAtCommand = vehicle->altitudeAMSL()->rawValue().toDouble();

        // MockLink::_handleTakeoff is the only writer of the mock's simulated AMSL, and it sets it
        // to the commanded param7 plus its own home altitude. Untouched so far, so the rise over
        // this reading is param7 itself. Reading param7 off lastReceivedMavlinkMessage() instead
        // does not work: only the newest COMMAND_LONG is kept and QGC's MAV_CMD_REQUEST_MESSAGE
        // traffic overwrites it.
        const double mockAltitudeBeforeTakeoff = mockLink->vehicleAltitudeAMSL();

        // A real press-and-hold rather than emitting activated(): the whole defect was that
        // this gesture could not reach the button.
        QVERIFY(_holdButton(kConfirmButton));

        QVERIFY_TRUE_WAIT(mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF) == 1, TestTimeout::longMs());

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
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000), "Confirm control never appeared");
        QVERIFY2(findVisibleItem(_rootItem, kSlider, 3000), "Altitude slider never appeared");
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

        // Full screen: the map copy is the only map on screen, so it is taken off the screen's
        // width rather than off a camera window, and the detection card goes to the middle of the
        // bottom edge instead of into the strip of picture beside it. The hint is found straight
        // away - it fades in on entry and is gone again three seconds later, but the item stays,
        // so its rectangle is still readable afterwards.
        const QRectF dockedCard = card;
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        QQuickItem *const pip  = findVisibleItem(_rootItem, kMapPip, 1000);
        QVERIFY2(pip, "Full screen map copy is not on screen");
        QQuickItem *const hint = findVisibleItem(_rootItem, kHint, 1000);
        QVERIFY2(hint, "Full screen hint label is not on screen");
        QTest::qWait(kSettleMs);

        const QRectF pipRect  = sceneRect(pip);
        const QRectF hintRect = sceneRect(hint);
        const QRectF fullCard = sceneRect(items[kAiPanel]);
        QVERIFY2(qAbs(pipRect.width() - screen.width() * kPipOfWidth) <= kEdgeSlack,
                 qPrintable(QStringLiteral("Full screen map copy is %1 wide against a wanted %2")
                                .arg(pipRect.width()).arg(screen.width() * kPipOfWidth)));
        QVERIFY2(qAbs(fullCard.center().x() - screen.center().x()) <= kCentreSlack,
                 qPrintable(QStringLiteral("Full screen detection card %1 is not centred on the screen %2")
                                .arg(QDebug::toString(fullCard), QDebug::toString(screen))));
        QVERIFY2(!fullCard.intersects(pipRect),
                 qPrintable(QStringLiteral("Full screen detection card %1 runs into the map copy %2")
                                .arg(QDebug::toString(fullCard), QDebug::toString(pipRect))));
        QVERIFY2(!fullCard.intersects(hintRect),
                 qPrintable(QStringLiteral("Full screen detection card %1 runs into the hint label %2")
                                .arg(QDebug::toString(fullCard), QDebug::toString(hintRect))));

        // Back out: the card returns to the telemetry bar it was stacked on.
        QVERIFY(dashboard->setProperty("expandedPanel", QString()));
        QTest::qWait(kSettleMs);
        QVERIFY2(sceneRect(items[kAiPanel]) == dockedCard,
                 qPrintable(QStringLiteral("Detection card came back from full screen at %1, not at %2")
                                .arg(QDebug::toString(sceneRect(items[kAiPanel])),
                                     QDebug::toString(dockedCard))));

        // The stock altitude slider takes the whole right screen edge while a confirmation is up,
        // which is the edge this strip stands on: it steps inboard of the slider for as long as
        // the slider is there and comes back to its own inset afterwards.
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
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
                    [this, width, height](QPointer<MockLink> /*mockLink*/, Vehicle *vehicle) {
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
        // same fact group and only while armed. The mock's own sweep is off under OptionNone,
        // so the injected frame stays put.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        const double rgForwardClose[8] = { 4, 11, 11, 11, 11, 11, 11, 11 };
        injectProximity(vehicle, rgForwardClose);
        _grab(QStringLiteral("v2_1_lidar_front"));
        if (QTest::currentTestFailed()) return;

        // Sector 2 is YAW_90, the aircraft's right side, which is the map's right edge while the
        // aircraft heads north. That edge is the one the camera column stands on.
        const double rgRightClose[8] = { 11, 11, 4, 11, 11, 11, 11, 11 };
        injectProximity(vehicle, rgRightClose);
        _grab(QStringLiteral("v2_2_lidar_right"));
        if (QTest::currentTestFailed()) return;

        // Takeoff wants the vehicle on the ground again.
        vehicle->setArmed(false, false);
        QTRY_VERIFY(!vehicle->armed());
        QVERIFY2(clickButton(kTakeoffButton), "Could not click the takeoff tool strip entry");
        QVERIFY2(findVisibleItem(_rootItem, kConfirmButton, 5000), "Confirm control never appeared");
        QVERIFY2(findVisibleItem(_rootItem, kSlider, 3000), "Altitude slider never appeared");
        _grab(QStringLiteral("v2_3_takeoff"));
        if (QTest::currentTestFailed()) return;

        // The same forward obstacle with the confirm control up. The glow draws at z -1, so the
        // top edge's number has to step below the control instead of sitting under it. Takeoff
        // stays offered while armed, so the control the click above raised is still the one here.
        vehicle->setArmed(true, false);
        QTRY_VERIFY(vehicle->armed());
        injectProximity(vehicle, rgForwardClose);
        _grab(QStringLiteral("v2_9_lidar_with_confirm"));
        if (QTest::currentTestFailed()) return;

        // Back to what the flying frames were captured in: no obstacle, on the ground.
        const double rgAllFar[8] = { 11, 11, 11, 11, 11, 11, 11, 11 };
        injectProximity(vehicle, rgAllFar);
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
        QVERIFY(_holdButton(kConfirmButton));
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
