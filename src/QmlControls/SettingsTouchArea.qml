import QtQuick

import QGroundControl

/// A settings control's tap target, a finger's minimum wide and tall and centred on the control,
/// while the control itself keeps the mockup's drawn size and its place in the row. The control
/// takes it as its containmentMask. The HoverHandler is what makes that work beyond the control's
/// bounds: Qt Quick skips a point outside an item whose event-handling children all lie inside it,
/// and a handler here is such a child that reaches outside. It only observes, it takes nothing
Item {
    width:  Math.max(parent.width, ScreenTools.minTouchPixels)
    height: Math.max(parent.height, ScreenTools.minTouchPixels)
    x:      (parent.width - width) / 2
    y:      (parent.height - height) / 2

    HoverHandler { }
}
