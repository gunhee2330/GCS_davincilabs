#pragma once

#include "QmlUITestBase.h"

/// Manual screenshot capture for UI/palette review (standalone: never runs in a
/// ctest sweep). Boots the UI with a mock vehicle, walks the screens that are
/// most sensitive to palette changes, and writes a PNG per screen.
///
/// Run explicitly (PNG output directory comes from QGC_SCREENSHOT_DIR):
///   QGC_SCREENSHOT_DIR=/tmp/shots ./QGroundControl.app/Contents/MacOS/QGroundControl --unittest:ScreenshotTest
class ScreenshotTest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    /// The ring must stay blank with no vehicle at all, so this walks the UI
    /// before any MockLink exists. Its own slot because runWithMockLink() owns
    /// the boot/teardown of the connected walk below.
    void _captureNoVehicle();

    void _captureScreens();

    /// The indicator drop-down pages, cropped to the page itself. The same page can be reached
    /// from the police bar or from the stock toolbar, and only the crop is comparable between
    /// the two - the drawer sits at a different x over a different background in each.
    void _captureIndicatorPages();

private:
    /// Wait for bindings/animations to settle, then write the live window to
    /// <QGC_SCREENSHOT_DIR>/<name>.png. Fails the test if the grab is empty or
    /// the file cannot be written.
    void _grab(const QString &name);

    /// Click the indicator named \a indicatorObjectName, wait for its drop-down, and write the
    /// page's own pixels to <QGC_SCREENSHOT_DIR>/<name>.png. Closes the drawer again so the
    /// caller can walk several indicators. Returns false (after a qWarning) on any failure.
    bool _grabIndicatorPage(const QString &indicatorObjectName, const QString &name);
};
