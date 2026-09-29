#pragma once

#include "QmlUITestBase.h"

/// The four amber warning strips on the police fly view (RFP p6 아 and p8), driven end to end:
/// telemetry off a PX4 MockLink into the vehicle's facts, the dashboard's bindings into
/// PoliceWarnings, and the strips it puts up under the top bar.
class PoliceWarningsUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    /// Each strip goes up with its condition and says the live value, battery low turns into
    /// critical and says it again, altitude and wind hold inside their hysteresis band, several
    /// stack full width under the bar in a fixed order, the tool strips step down by the stack,
    /// and the red link strip comes up first in the stack.
    void _testStripsFollowTelemetry();

    /// A tap hides one strip and no other, silently; low battery stays hidden while it drains and
    /// comes back and speaks once critical; altitude comes back once it clears and returns.
    void _testTapDismisses();

    /// The red link strip: first in the stack, a tap hides it while the loss lasts, it comes back
    /// once the link returns and is lost again, and the RC receiver alone has its own words.
    void _testLinkLostStrip();

    /// The follow warning: an amber strip last in the stack, with its words, hidden by a tap.
    void _testFollowModeStrip();

    /// Captures into QGC_SCREENSHOT_DIR: the red strip alone, with two amber strips, after a tap,
    /// the follow strip, RC lost. Skipped unless QGC_SCREENSHOT_DIR is set.
    void _captureStrips();

private:
    /// Pre-existing warnings the strict log check would otherwise fail on.
    void _ignorePreexistingWarnings();

    /// Grab the window to <QGC_SCREENSHOT_DIR>/<name>.png.
    void _grab(const QString &name);
};
