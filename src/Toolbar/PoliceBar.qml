pragma Singleton

import QtQuick

import QGroundControl.Controls

/// The one top bar every view wears.
///
/// Fly, Plan and the tool drawer each build their own bar out of their own controls, but an
/// operator switching between them should not see the chrome change. Height and colour live
/// here so the three stay in step; the Plan bar's height is the one they settled on, being the
/// shortest that still takes a gloved thumb on the 7 inch screen.
QtObject {
    readonly property color color:  "#0c1218"
    readonly property real  height: Math.round(ScreenTools.toolbarHeight * 0.8)

    /// Icons and text on the bar are drawn light: the palette's own button colours follow the
    /// app theme, which on the light theme would put dark glyphs on this dark ground.
    readonly property color content: "white"
}
