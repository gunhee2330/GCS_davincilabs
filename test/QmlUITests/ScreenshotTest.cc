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
        _grab(QStringLiteral("04_flyview"));
        if (QTest::currentTestFailed()) return;

        // MainWindow.qml is an ApplicationWindow, so its functions live on _window
        QVERIFY(QMetaObject::invokeMethod(_window, "showVehicleConfig"));
        QTest::qWait(kSettleMs);

        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_summary")), "Summary button not clickable");
        _grab(QStringLiteral("01_vehicle_summary"));
        if (QTest::currentTestFailed()) return;

        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_comp_FlightSafety")), "Flight Safety button not clickable");
        _grab(QStringLiteral("02_vehicle_safety"));
        if (QTest::currentTestFailed()) return;

        QVERIFY(QMetaObject::invokeMethod(_window, "showSettingsTool",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Comm Links")))));
        _grab(QStringLiteral("03_settings_commlinks"));
    });
}
