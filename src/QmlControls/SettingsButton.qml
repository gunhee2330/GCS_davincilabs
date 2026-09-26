import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Button {
    id:             control
    // Mockup rail row: 1.05 cqw down, 1.6 cqw across
    padding:        ScreenTools.mockupUnit * 1.05
    leftPadding:    ScreenTools.mockupUnit * 1.6
    rightPadding:   ScreenTools.mockupUnit * 1.6
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
        // The mockup fills the selected row edge to edge in selectedRow; press and hover keep a
        // light accent tint on the others
        Rectangle {
            anchors.fill:   parent
            color:          control.checked ? qgcPal.selectedRow : qgcPal.buttonHighlight
            opacity:        control.checked ? 1 : control.pressed ? 0.25 : control.enabled && control.hovered ? 0.1 : 0
        }

        // Kept a sibling of the fill rather than a child so it is not dimmed by its opacity.
        // This stripe is the selection cue that survives glare at arm's length. Mockup .3 cqw
        Rectangle {
            anchors.left:   parent.left
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            width:          Math.round(ScreenTools.mockupUnit * 0.3)
            color:          qgcPal.buttonHighlight
            visible:        control.checked
        }
    }

    contentItem: RowLayout {
        spacing: ScreenTools.mockupUnit

        QGCColoredImage {
            source: control.icon.source
            color:  control.icon.color
            width:  ScreenTools.mockupUnit * 1.7
            height: ScreenTools.mockupUnit * 1.7
        }

        QGCLabel {
            id:                     displayText
            Layout.fillWidth:       true
            text:                   control.text
            color:                  control.textColor
            font.pointSize:         ScreenTools.mockupPointUnit * 1.35
            horizontalAlignment:    QGCLabel.AlignLeft
        }

        QGCColoredImage {
            visible:    control.expandable
            source:     "/InstrumentValueIcons/cheveron-right.svg"
            color:      control.textColor
            width:      ScreenTools.mockupUnit * 1.1
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
