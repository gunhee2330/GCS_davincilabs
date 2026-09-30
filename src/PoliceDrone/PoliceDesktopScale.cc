#include "PoliceDesktopScale.h"

#include <QtCore/QByteArray>
#include <QtCore/QtEnvironmentVariables>
#include <QtCore/QtGlobal>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

void PoliceDesktopScale::apply()
{
#ifdef Q_OS_MACOS
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        return;
    }
    // Points, the units macOS lays windows out in, so a Retina panel's pixel density drops out.
    const double width = CGDisplayBounds(CGMainDisplayID()).size.width;
    if (width <= kHandsetLogicalWidth) {
        return;
    }
    (void) qputenv("QT_SCALE_FACTOR", QByteArray::number(width / kHandsetLogicalWidth, 'f', 4));
#endif
}
