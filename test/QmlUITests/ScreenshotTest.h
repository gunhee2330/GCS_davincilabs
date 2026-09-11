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
    void _captureScreens();

private:
    /// Wait for bindings/animations to settle, then write the live window to
    /// <QGC_SCREENSHOT_DIR>/<name>.png. Fails the test if the grab is empty or
    /// the file cannot be written.
    void _grab(const QString &name);
};
