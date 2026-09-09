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

    /// Everything drawn on the bar, sized off the bar itself so the four views draw at one
    /// scale. The fly view worked these ratios out for itself and the plan view and the tool
    /// drawer never saw them: they sized their glyphs off the app font instead, which put the
    /// brand mark on the settings screen at nearly twice the size it has on the flight screen
    /// and the hamburger at three times. Floors so a small screen still leaves them hittable.
    readonly property real iconSize:   Math.max(12, height * 0.13)
    readonly property real logoHeight: Math.max(14, height * 0.19)
    readonly property real textSize:   Math.max(12, height * 0.27)

    /// The leading run every bar starts with - a margin, the menu button, then the same gap
    /// again before the brand mark. Shared because the mark was starting in three different
    /// places across the four views, and a mark that jumps as the view changes is the one thing
    /// on the bar the eye is guaranteed to be looking at while it does.
    readonly property real margin:          Math.round(ScreenTools.defaultFontPixelWidth)
    readonly property real menuButtonWidth: Math.max(iconSize + (margin * 2), ScreenTools.minTouchPixels)
}
