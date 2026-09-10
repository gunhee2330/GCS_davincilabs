import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

RadioButton {
    id:             control
    font.family:    ScreenTools.normalFontFamily
    font.pointSize: ScreenTools.defaultFontPointSize

    property color  textColor:  qgcPal.text
    property bool   _noText:    text === ""

    QGCPalette { id:qgcPal; colorGroupEnabled: enabled }

    indicator: Rectangle {
        implicitWidth:          ScreenTools.radioButtonIndicatorSize
        implicitHeight:         width
        // The dot is buttonHighlight, so the well must stay dark in the dark
        // theme or the dot is invisible - textField is the palette's input fill
        color:                  qgcPal.textField
        border.color:           control.checked ? qgcPal.buttonHighlight : qgcPal.buttonBorder
        // The mockup rings its 18px indicator with 2px so the ring reads at
        // arm's length - same proportion, whatever the font metric gives us
        border.width:           Math.max(1, Math.round(width / 9))
        radius:                 height / 2
        x:                      control.leftPadding
        y:                      parent.height / 2 - height / 2

        Rectangle {
            anchors.fill:   parent
            color:          qgcPal.buttonHighlight
            opacity:        control.hovered ? .2 : 0
            radius:         parent.radius
        }

        Rectangle {
            anchors.centerIn:   parent
            // Mockup dot is 11 across an 18px indicator. Width should be an odd
            // number to be centralized by the parent properly
            width:              2 * Math.floor(parent.width * 0.3) + 1
            height:             width
            antialiasing:       true
            radius:             height * 0.5
            color:              qgcPal.buttonHighlight
            visible:            control.checked
        }
    }

    contentItem: Text {
        text:               control.text
        font.family:        control.font.pointSize
        font.pointSize:     control.font.pointSize
        font.bold:          control.font.bold
        color:              control.textColor
        verticalAlignment:  Text.AlignVCenter
        leftPadding:        control.indicator.width + (_noText ? 0 : ScreenTools.defaultFontPixelWidth * 0.25)
    }

}
