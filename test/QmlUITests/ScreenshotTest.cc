#include "ScreenshotTest.h"

#include <QtCore/QDir>
#include <QtCore/QMetaObject>
#include <QtCore/QVariant>
#include <QtGui/QImage>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "AppSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST_STANDALONE(ScreenshotTest, TestLabel::Integration)

namespace {
// Bindings, drawer slide animations and Loader instantiation all settle well
// inside this; grabbing earlier catches half-painted panels
constexpr int kSettleMs = 1500;
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

void ScreenshotTest::_captureScreens()
{
    // The framework clears QSettings per test function, so the light theme cannot be staged
    // from outside the process. QGC_SCREENSHOT_LIGHT=1 flips the palette for this run only.
    if (qEnvironmentVariableIntValue("QGC_SCREENSHOT_LIGHT") != 0) {
        SettingsManager::instance()->appSettings()->indoorPalette()->setRawValue(0);
        QTest::qWait(kSettleMs);
    }

    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(); },
                    [this](QPointer<MockLink>, Vehicle *) {
        // A 7in controller is far narrower than the default test window, and full-width cards
        // are the layout narrowness can break. QGC_SCREENSHOT_WIDTH re-walks at that width.
        const int narrowWidth = qEnvironmentVariableIntValue("QGC_SCREENSHOT_WIDTH");
        if (narrowWidth > 0) {
            _window->resize(narrowWidth, _window->height());
            QTest::qWait(kSettleMs);
        }

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
