#include "PoliceLrfOverlayUITest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "MockLink.h"
#include "SiyiCameraController.h"

UT_REGISTER_TEST(PoliceLrfOverlayUITest, TestLabel::Integration)

void PoliceLrfOverlayUITest::_testOverlayFollowsFullscreenAndLaser()
{
    // Pre-existing warnings, the same ones PoliceGuidedActionUITest ignores.
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^qrc:/qml/QGroundControl/FlightMap/Widgets/PhotoVideoControl\\.qml:[0-9]+: "
                         "TypeError: Cannot read property '[A-Za-z0-9_]+' of (null|undefined)$")));
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^Both point size and pixel size set\\. Using pixel size\\.$")));
    ignoreLogMessage("default", QtInfoMsg,
                     QRegularExpression(QStringLiteral("^qrc:/qml/QGroundControl/PlanView/[A-Za-z]+MapVisual\\.qml: ")));

    runWithMockLink([] { return MockLink::startPX4MockLink(); },
                    [this](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
        _window->resize(768, 480);
        QTest::qWait(1500);

        QQuickItem *const dashboard = findVisibleItem(_rootItem, QStringLiteral("policeDroneDashboard"), 5000);
        QVERIFY2(dashboard, "Police dashboard not found");
        QQuickItem *const panel = findVisibleItem(_rootItem, QStringLiteral("policeZoomCameraPanel"), 5000);
        QVERIFY2(panel, "Zoom camera panel not found");
        QQuickItem *const overlay = panel->findChild<QQuickItem *>(QStringLiteral("policeLrfOverlay"));
        QVERIFY2(overlay, "Zoom panel has no LRF overlay");
        QQuickItem *const text = overlay->findChild<QQuickItem *>(QStringLiteral("policeLrfOverlayText"));
        QVERIFY2(text, "LRF overlay has no text");

        SiyiCameraController *const camera = SiyiCameraController::instance();
        QVERIFY(camera);
        const bool savedLaser = camera->laserEnabled();
        const auto restore = qScopeGuard([camera, savedLaser, dashboard] {
            camera->setLaserEnabled(savedLaser);
            dashboard->setProperty("expandedPanel", QString());
        });
        camera->setLaserEnabled(true);

        // (a) Small window, laser on: nothing on the picture.
        QTest::qWait(500);
        QVERIFY2(!overlay->isVisible(), "LRF overlay is on the small zoom window");

        // (b) Full screen, laser on, no pod reading: overlay up, 범위 밖.
        QVERIFY(dashboard->setProperty("expandedPanel", QStringLiteral("secondary")));
        QTRY_VERIFY_WITH_TIMEOUT(overlay->isVisible(), 2000);
        QCOMPARE(text->property("text").toString(), QStringLiteral("범위 밖"));

        // (c) Laser switched off: overlay gone.
        camera->setLaserEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!overlay->isVisible(), 2000);
    });
}
