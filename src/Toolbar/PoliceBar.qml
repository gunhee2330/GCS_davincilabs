pragma Singleton

import QtQuick

import QGroundControl.Controls

/// The one top bar every view wears.
///
/// Fly, Plan and the tool drawer each build their own bar out of their own controls, but an
/// operator switching between them should not see the chrome change. Height and colour live
/// here so the three stay in step.
///
/// Full toolbarHeight, not a fraction of it. The layout was drawn against a 68 px bar and
/// everything on it is a ratio of this number, so a fraction shrank every pictogram and every
/// value with it - on the desktop far enough that the ratios lost to their own floors and the
/// bar came out the size it had been before. toolbarHeight is defaultFontPixelHeight * 3, which
/// is the screen's own scale, and it is also where MainWindow anchors the indicator drawers:
/// a taller bar than this would have them opening underneath it.
QtObject {
    readonly property color color:  "#0c1218"
    readonly property real  height: Math.round(ScreenTools.toolbarHeight)

    /// Icons and text on the bar are drawn light: the palette's own button colours follow the
    /// app theme, which on the light theme would put dark glyphs on this dark ground.
    readonly property color content: "white"
}
