import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Button {
    id:             control
    padding:        ScreenTools.defaultFontPixelHeight * 0.61
    hoverEnabled:   !ScreenTools.isMobile
    autoExclusive:  true
    icon.color:     textColor

    // No bold on the checked row: a Control font never reaches QGCLabel, which is a plain Text,
    // and binding the label directly would resize the rail on every selection because the rail
    // is as wide as its widest row.
    // The selected row is a dark tint plus an accent stripe, not an accent flood, so the
    // label keeps full contrast against sunlight instead of inverting to a dark-on-blue
    property color textColor: qgcPal.buttonText
    property bool expandable: false
    property bool expanded:   false

    signal toggleExpand()

    QGCPalette {
        id:                 qgcPal
        colorGroupEnabled:  control.enabled
    }

    background: Item {
        // The tint lands on #2f3f53, within two points per channel of the mockup's #2d3a4a and
        // indistinguishable from it. Kept as a tint rather than a fixed colour so it follows the
        // rail if the rail moves, and because a fixed one would need a palette role of its own
        Rectangle {
            anchors.fill:   parent
            color:          qgcPal.buttonHighlight
            opacity:        control.checked || control.pressed ? 0.25 : control.enabled && control.hovered ? 0.1 : 0
            radius:         ScreenTools.defaultFontPixelHeight * 0.39
        }

        // Kept a sibling of the tint rather than a child so it is not dimmed by its opacity.
        // This stripe is the selection cue that survives glare at arm's length
        Rectangle {
            anchors.left:   parent.left
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            width:          Math.max(2, Math.round(ScreenTools.defaultFontPixelHeight * 0.17))
            radius:         width / 2
            color:          qgcPal.buttonHighlight
            visible:        control.checked
        }
    }

    contentItem: RowLayout {
        spacing: ScreenTools.defaultFontPixelHeight * 0.61

        QGCColoredImage {
            source: control.icon.source
            color:  control.icon.color
            width:  ScreenTools.defaultFontPixelHeight
            height: ScreenTools.defaultFontPixelHeight
        }

        QGCLabel {
            id:                     displayText
            Layout.fillWidth:       true
            text:                   control.text
            color:                  control.textColor
            horizontalAlignment:    QGCLabel.AlignLeft
        }

        QGCColoredImage {
            visible:    control.expandable
            source:     "/InstrumentValueIcons/cheveron-right.svg"
            color:      control.textColor
            width:      ScreenTools.defaultFontPixelHeight * 0.75
            height:     width
            rotation:   control.expanded ? 90 : 0

            MouseArea {
                anchors.fill: parent
                anchors.margins: -ScreenTools.defaultFontPixelWidth
                onClicked: control.toggleExpand()
            }
        }
    }
}
