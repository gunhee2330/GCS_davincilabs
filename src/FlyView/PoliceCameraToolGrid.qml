import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

// The camera tool strip, two columns wide. The stock ToolStrip is a single Column inside a
// Flickable, and at the tablet's own font metrics the pod's six entries want about 890 px of
// height against the 514 px there are between the top bar and the zoom window, so the last of
// them sat below the fold and had to be scrolled for. Same model and the same stock
// ToolStripHoverButton delegate, laid out in a Grid instead, filled row by row. Everything
// else - cell size, margins, radius, the exclusive checked handling, and scrolling whatever
// does not fit inside maxHeight - is what ToolStrip does.
Rectangle {
    id:     _root
    color:  qgcPal.windowTransparent
    width:  (_cellSize * _columns) + (toolGrid.spacing * (_columns - 1)) + (flickable.anchors.margins * 2)
    height: Math.min(maxHeight, toolGrid.height + (flickable.anchors.margins * 2))
    radius: ScreenTools.defaultFontPixelWidth / 2

    property alias  model:              repeater.model
    property real   maxHeight           ///< Maximum height for control, what does not fit inside it scrolls
    property var    fontSize:           ScreenTools.smallFontPointSize

    // What the stock strip gives a button: its own width, less the flickable's margins, and
    // square. Keeping the cell at that size is what makes an entry here the size it has always
    // been, and it is what leaves three rows clear of the zoom window at tablet font metrics.
    readonly property int  _columns:  2
    readonly property real _cellSize: (ScreenTools.defaultFontPixelWidth * 7) - (flickable.anchors.margins * 2)

    property var _dropPanel: dropPanel

    signal dropped(int index)

    QGCPalette { id: qgcPal }

    DeadMouseArea {
        anchors.fill: parent
    }

    QGCFlickable {
        id:                 flickable
        anchors.margins:    ScreenTools.defaultFontPixelWidth * 0.4
        anchors.fill:       parent
        contentHeight:      toolGrid.height
        flickableDirection: Flickable.VerticalFlick
        clip:               true

        Grid {
            id:         toolGrid
            columns:    _root._columns
            spacing:    ScreenTools.defaultFontPixelWidth * 0.25

            Repeater {
                id: repeater

                // A Grid leaves invisible children out of the flow, so the entries that come and
                // go with a setting close the gap behind them, as they do in the stock Column.
                ToolStripHoverButton {
                    width:              _root._cellSize
                    radius:             ScreenTools.defaultFontPixelWidth / 2
                    fontPointSize:      _root.fontSize
                    toolStripAction:    modelData
                    dropPanel:          _root._dropPanel
                    onDropped:          (index) => _root.dropped(index)

                    onCheckedChanged: {
                        // Exclusive check state by hand, the way ToolStrip does it: autoExclusive
                        // caused all sorts of problems there.
                        if (checked) {
                            for (var i = 0; i < repeater.count; i++) {
                                if (i != index) {
                                    var button = repeater.itemAt(i)
                                    if (button.checked) {
                                        button.checked = false
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // No entry in the camera model carries a dropPanelComponent - the panels those entries open
    // are DropPanels the dashboard builds itself, to drop to the left. This is here anyway
    // because ToolStripHoverButton.onClicked calls dropPanel.hide() before it triggers anything,
    // so without it every press would throw instead of reaching its action.
    ToolStripDropPanel {
        id:         dropPanel
        toolStrip:  _root
    }
}
